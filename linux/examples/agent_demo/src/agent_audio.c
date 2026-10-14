/**
 * @file agent_audio.c
 * @brief agent_demo 音频链路对接：上行喂送（读素材按帧喂云端）+ 下行打印（ASR/NLG）
 *
 * 上行：play_local_in_* 契约取数据 → tuya_ai_audio_input 按帧喂（agent_audio_upload_once）。
 * 下行：注册 ai_user_event 回调，打印 ASR 识别结果与 NLG 回复文字；ASR 事件同时
 * 作为上行校验信号（notify）唤醒触发线程。
 *
 * （2026-10-14 由 agent_audio.c + agent_downlink.c 合并：两者是同一链路的上行/
 * 下行两侧，且下行回调直接驱动上行校验，合并在同一文件内聚。）
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"

#include "tuya_ai_input.h"
#include "play_local_in.h"
#include "ai_user_event.h"

#include "agent_audio.h"

#include <stdio.h>

/* ============================ 上行：喂送与校验 ============================ */

/* 上行校验信号量：ASR 事件（下行回调）post，触发线程 wait。
 * 按"单次触发"设计（一次性 demo 口径）；多轮扩展时每轮需重置语义。 */
static SEM_HANDLE s_uplink_verify_sem = NULL;

OPERATE_RET agent_audio_uplink_verify_init(void)
{
    if (s_uplink_verify_sem) {
        return OPRT_OK; /* 已创建（幂等） */
    }

    return tal_semaphore_create_init(&s_uplink_verify_sem, 0, 1);
}

OPERATE_RET agent_audio_uplink_verify_wait(uint32_t timeout_ms)
{
    if (!s_uplink_verify_sem) {
        return OPRT_INVALID_PARM;
    }

    return tal_semaphore_wait(s_uplink_verify_sem, timeout_ms);
}

void agent_audio_uplink_verify_notify(void)
{
    if (s_uplink_verify_sem) {
        tal_semaphore_post(s_uplink_verify_sem);
    }
}

OPERATE_RET agent_audio_upload_once(const char *src_path, agent_audio_stat_t *stat)
{
    OPERATE_RET rt = OPRT_OK;
    uint8_t     frame[AGENT_AUDIO_FRAME_BYTES];
    agent_audio_stat_t local_stat = {0};

    if (!src_path) {
        return OPRT_INVALID_PARM;
    }

    rt = play_local_in_open(src_path);
    if (rt != OPRT_OK) {
        PR_ERR("[AGENT] audio source open failed: %s, rt:%d", src_path, rt);
        return rt;
    }

    /* 开始一轮输入（force=true：若有残留会话先中断） */
    tuya_ai_input_start(true);

    for (;;) {
        INT_T n = play_local_in_read(frame, sizeof(frame));
        if (n < 0) {
            PR_ERR("[AGENT] audio source read error");
            rt = OPRT_COM_ERROR;
            break;
        }
        if (n == 0) {
            /* EOF：一段语音读完 */
            break;
        }

        /* timestamp/pts 语义与上游一致：都取当前毫秒 */
        uint64_t ts = tal_system_get_millisecond();
        rt = tuya_ai_audio_input(ts, ts, frame, (UINT_T)n, (UINT_T)n);
        if (rt != OPRT_OK) {
            PR_ERR("[AGENT] audio upload failed at frame %u, rt:%d", local_stat.frames, rt);
            break;
        }

        local_stat.frames++;
        local_stat.bytes += (uint32_t)n;
        local_stat.last_frame_bytes = (uint32_t)n;
    }

    /* 结束本轮输入：云端据此开始 ASR 与 agent 处理 */
    tuya_ai_input_stop();
    play_local_in_close();

    /* 防御：0 字节上传无意义（空文件/异常源），按失败处理，避免"upload done"假绿 */
    if ((rt == OPRT_OK) && (0 == local_stat.bytes)) {
        PR_ERR("[AGENT] audio source empty (0 bytes), treat as failure");
        rt = OPRT_COM_ERROR;
    }

    if (stat) {
        *stat = local_stat;
    }

    return rt;
}

/* ============================ 下行：事件回调 ============================ */

static void __agent_downlink_event(AI_NOTIFY_EVENT_T *event)
{
    /* 载荷生命周期：ai_user_event_notify（utility/src/ai_user_event.c）是**同步直呼**
     * 回调，payload 指向 producer（ai_skill.c __ai_nlg_process）栈上 AI_NOTIFY_TEXT_T
     * ，其 .data 指向该帧 cJSON 字符串（root 在 tuya_ai_output_text 返回后才 Delete）。
     * 故本回调内读取必然有效；**若将来改成异步/延迟消费，必须先拷贝**——探针实测：
     * 回调返回后解引用得 datalen=65535/data=0x44（栈已销毁），cJSON_Delete 后内容被覆写。 */
    if (!event) {
        return;
    }

    switch (event->type) {
    case AI_USER_EVT_ASR_OK: {
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        if (text && text->data) {
            PR_NOTICE("[AGENT][ASR] %.*s", text->datalen, text->data);
        }
        /* 上行校验：云端 ASR 已回（链路通的硬证据），唤醒触发线程 */
        agent_audio_uplink_verify_notify();
    } break;

    case AI_USER_EVT_ASR_EMPTY:
        PR_WARN("[AGENT][ASR] <empty>");
        /* 空识别 = 链路通了但没内容，不是上传失败——同样算 verified */
        agent_audio_uplink_verify_notify();
        break;

    case AI_USER_EVT_TEXT_STREAM_START: {
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        PR_NOTICE("[AGENT][NLG] >> %.*s", text ? text->datalen : 0, (text && text->data) ? text->data : "");
    } break;

    case AI_USER_EVT_TEXT_STREAM_DATA: {
        /* 流式片段：原样续打（不加前缀、不换行） */
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        if (text && text->data && text->datalen > 0) {
            printf("%.*s", text->datalen, text->data);
            fflush(stdout);
        }
    } break;

    case AI_USER_EVT_TEXT_STREAM_STOP: {
        /* 多段流的末段（eof=1）经 STOP 携带该包 content（ai_skill.c:173-175，
         * 注释原文 "append final content (may be empty)"）——非空时必须续打，
         * 否则终端缺最后一段文字（单帧场景 STOP 为显式空包，不受影响）。
         * 先续打内容、再打 stream end（内容接在上一行末尾）。 */
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        if (text && text->data && text->datalen > 0) {
            printf("%.*s", text->datalen, text->data);
            fflush(stdout);
        }
        PR_NOTICE("[AGENT][NLG] << (stream end)");
    } break;

    case AI_USER_EVT_CHAT_BREAK:
        PR_NOTICE("[AGENT] chat break (cloud interrupted)");
        break;

    case AI_USER_EVT_EXIT:
        PR_NOTICE("[AGENT] chat exit");
        break;

    default:
        /* 其余事件（TTS 播放控制、表情、技能等）本 demo 不处理 */
        break;
    }
}

OPERATE_RET agent_downlink_init(void)
{
    ai_user_event_notify_register(__agent_downlink_event);
    PR_NOTICE("[AGENT] downlink handler registered (text events; TTS/music audio via ai_audio_player)");

    return OPRT_OK;
}
