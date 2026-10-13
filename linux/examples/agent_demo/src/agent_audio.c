/**
 * @file agent_audio.c
 * @brief agent_demo 上行音频实现：读素材 → 按帧喂 tuya_ai_audio_input → 结束本轮
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"

#include "tuya_ai_input.h"
#include "port_ai_demo.h"

#include "agent_audio.h"

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

    rt = port_ai_demo_open(src_path);
    if (rt != OPRT_OK) {
        PR_ERR("[AGENT] audio source open failed: %s, rt:%d", src_path, rt);
        return rt;
    }

    /* 开始一轮输入（force=true：若有残留会话先中断） */
    tuya_ai_input_start(true);

    for (;;) {
        INT_T n = port_ai_demo_read(frame, sizeof(frame));
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
    port_ai_demo_close();

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
