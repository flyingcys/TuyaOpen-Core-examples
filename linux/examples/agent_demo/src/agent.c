/**
 * @file agent.c
 * @brief AI 链路启动编排（自 tuya_main.c 剥离，2026-10-14）
 *
 * MQTT 连上后：初始化 AI agent → 注册下行打印（agent_downlink）→ 起触发
 * 线程（等客户端就绪 → 上传一段预置语音 → 以 ASR 响应为终判据）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"
#include "ai_agent.h"
#include "tuya_ai_agent.h"
#include "agent_audio.h"
#include "agent.h"

/* 触发时序：等客户端就绪上限 / 就绪后等会话建立（crt_session 一次取 token 往返） */
#define AGENT_TRIGGER_WAIT_READY_MS (30 * 1000)
#define AGENT_TRIGGER_SESSION_SETTLE_MS (2 * 1000)

static THREAD_HANDLE s_agent_trigger_thread = NULL;
static bool          s_agent_started        = false;

/**
 * @brief 触发线程：等 AI 客户端就绪 → 上传一段预置语音（一次性）
 *
 * 收尾必须自删：TAL 契约（tal_thread.c __WrapRunFunc）在用户回调返回后仍每
 * 10ms 轮询 thrdRunSta，直到 tal_thread_delete 置 STOP 才退出并释放；不自删
 * 则线程与句柄常驻。惯例同 iot_demo tuya_app_thread / p2p_demo 回放线程。
 */
static void __agent_trigger_task(void *arg)
{
    (void)arg;
    uint32_t           waited_ms = 0;
    OPERATE_RET        rt        = OPRT_OK;
    agent_audio_stat_t stat      = {0};

    while (!tuya_ai_agent_is_ready()) {
        if (waited_ms >= AGENT_TRIGGER_WAIT_READY_MS) {
            /* 超时放弃（一次性触发口径）：30s 上限远大于实测就绪时间(~1s)，
             * 超时说明链路异常；重试无意义，打日志供排查。 */
            PR_ERR("[AGENT] ai client not ready in %d ms, trigger aborted", AGENT_TRIGGER_WAIT_READY_MS);
            goto __exit;
        }
        tal_system_sleep(500);
        waited_ms += 500;
    }
    PR_NOTICE("[AGENT] ai client ready (waited %u ms)", waited_ms);

    /* 客户端就绪 ≠ 会话建立完成：给 crt_session（MQTT 取 token 往返）留出时间 */
    tal_system_sleep(AGENT_TRIGGER_SESSION_SETTLE_MS);

    PR_NOTICE("[AGENT] trigger: upload one utterance from %s", AGENT_AUDIO_SRC_PATH);

    /* 上行校验（1c 评审 finding）：断线时 ringbuf 写入仍成功、"upload done" 会假绿
     * （该行只表示本地喂帧完成）；连接状态查询滞后 ≥18s 抓不到上传窗口。故以
     * "云端 ASR 响应"为终判据——上传后等待，超时即如实报 FAILED。 */
    agent_audio_uplink_verify_init();
    rt = agent_audio_upload_once(AGENT_AUDIO_SRC_PATH, &stat);
    PR_NOTICE("[AGENT] upload %s: frames=%u, bytes=%u, last_frame_bytes=%u", (rt == OPRT_OK) ? "done" : "FAILED",
              stat.frames, stat.bytes, stat.last_frame_bytes);

    if (rt == OPRT_OK) {
        if (agent_audio_uplink_verify_wait(AGENT_UPLINK_VERIFY_TIMEOUT_MS) == OPRT_OK) {
            PR_NOTICE("[AGENT] uplink verified (ASR received)");
        } else {
            PR_ERR("[AGENT] upload FAILED: no ASR response in %d ms (uplink broken or cloud unreachable)",
                   AGENT_UPLINK_VERIFY_TIMEOUT_MS);
        }
    }

__exit:
    if (s_agent_trigger_thread) {
        THREAD_HANDLE h        = s_agent_trigger_thread;
        s_agent_trigger_thread = NULL;
        tal_thread_delete(h);
    }
}

/**
 * @brief 启动 AI 链路（幂等）：初始化 agent + 注册下行打印 + 起触发线程
 *
 * guard（s_agent_started）只在**全部成功后**置位：ai_agent_init 失败时不置，
 * 下次 MQTT_CONNECTED 自然重试（tuya_ai_agent_init 失败路径自清理，重试安全；
 * monitor 已降级为非阻断，不会制造"半成功"状态）。线程创建失败时 AI 已初始化、
 * 重复初始化不安全，故置位不再重试（打 ERR 供排查）。
 */
void agent_start(void)
{
    if (s_agent_started) {
        return;
    }

    OPERATE_RET rt = ai_agent_init();
    if (rt != OPRT_OK) {
        PR_ERR("[AGENT] ai_agent_init failed: %d (will retry on next mqtt connect)", rt);
        return;
    }
    PR_NOTICE("[AGENT] ai agent initialized");

    agent_downlink_init();

    THREAD_CFG_T thrd = {0};
    /* 注：stackDepth 在 Linux port 被忽略（port_thread 未用 pthread_attr_setstacksize，
     * 线程实得系统默认栈 8MB）；真机移植时此值才会生效，勿据此认为栈已按 4.6KB 调过。 */
    thrd.stackDepth   = 4608;
    thrd.priority     = THREAD_PRIO_2;
    thrd.thrdname     = "agent_trigger";
    if (tal_thread_create_and_start(&s_agent_trigger_thread, NULL, NULL, __agent_trigger_task, NULL, &thrd) != OPRT_OK) {
        PR_ERR("[AGENT] trigger thread create failed (ai agent stays initialized, no retry)");
    }

    s_agent_started = true;
}
