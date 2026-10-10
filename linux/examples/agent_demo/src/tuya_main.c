/**
 * @file tuya_main.c
 * @brief agent_demo 主流程
 *
 * 链路：IoT 激活/连云 →（MQTT 连上后）初始化 AI agent → 等客户端就绪 →
 *       自动上传一段预置语音（触发）→ 下行只打印文字（ASR/NLG）。
 *
 * 无 UI；下行语音不处理（AI 音频模块整体关闭，媒体包在 core 内丢弃）；
 * 上行音频经 port 的 tkl_ai_demo_* 契约取得（Linux：PCM 文件回放）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "cJSON.h"
#include "mqtt_bind.h"
#include "tal_api.h"
#include "tal_netif.h"
#include "tal_api_ops.h"
#include "tuya_config.h"
#include "tuya_iot.h"
#include "tuya_iot_dp.h"

#include "ai_agent.h"
#include "tuya_ai_agent.h"
#include "agent_audio.h"
#include "agent_downlink.h"

#include <assert.h>

#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
#include "qrencode_print.h"
#endif

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

/* 上行语音素材：CMake 注入路径（Kconfig AGENT_DEMO_AUDIO_FILE）；
 * 为空时用构建目录的内置素材（assets/voice_16k.pcm 复制而来） */
#ifndef AGENT_DEMO_AUDIO_FILE
#define AGENT_DEMO_AUDIO_FILE ""
#endif
#define AGENT_AUDIO_SRC_PATH ((AGENT_DEMO_AUDIO_FILE)[0] ? (AGENT_DEMO_AUDIO_FILE) : "voice_16k.pcm")

/* 触发时序：等客户端就绪上限 / 就绪后等会话建立（crt_session 一次取 token 往返） */
#define AGENT_TRIGGER_WAIT_READY_MS (30 * 1000)
#define AGENT_TRIGGER_SESSION_SETTLE_MS (2 * 1000)

/* Tuya device handle */
tuya_iot_client_t client;

/* Tuya license information (uuid authkey) */
tuya_iot_license_t license;

/**
 * @brief user defined log output api, in this demo, it will use uart0 as log-tx
 *
 * @param str log string
 * @return void
 */
void user_log_output_cb(const char *str)
{
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->log_output) {
        ops->log_output(str);
    }
}

/**
 * @brief user defined upgrade notify callback, it will notify device a OTA
 * request received
 *
 * @param client device info
 * @param upgrade the upgrade request info
 * @return void
 */
void user_upgrade_notify_on(tuya_iot_client_t *client, cJSON *upgrade)
{
    PR_INFO("----- Upgrade information -----");
    if (!upgrade) {
        PR_WARN("upgrade JSON is NULL");
        return;
    }

    cJSON *type_item    = cJSON_GetObjectItem(upgrade, "type");
    cJSON *version_item = cJSON_GetObjectItem(upgrade, "version");
    cJSON *size_item    = cJSON_GetObjectItem(upgrade, "size");
    cJSON *url_item     = cJSON_GetObjectItem(upgrade, "url");

    PR_INFO("OTA Channel: %d", cJSON_IsNumber(type_item) ? type_item->valueint : -1);
    PR_INFO("Version: %s", cJSON_IsString(version_item) ? version_item->valuestring : "N/A");
    PR_INFO("Size: %s", cJSON_IsString(size_item) ? size_item->valuestring : "N/A");
    PR_INFO("URL: %s", cJSON_IsString(url_item) ? url_item->valuestring : "N/A");
}

/* ---------------------------------------------------------------------------
 * AI 链路：MQTT 连上后初始化 AI agent，并自动触发一次语音上传
 * --------------------------------------------------------------------------- */

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
    rt = agent_audio_upload_once(AGENT_AUDIO_SRC_PATH, &stat);
    PR_NOTICE("[AGENT] upload %s: frames=%u, bytes=%u, last_frame_bytes=%u", (rt == OPRT_OK) ? "done" : "FAILED",
              stat.frames, stat.bytes, stat.last_frame_bytes);

__exit:
    if (s_agent_trigger_thread) {
        THREAD_HANDLE h        = s_agent_trigger_thread;
        s_agent_trigger_thread = NULL;
        tal_thread_delete(h);
    }
}

/**
 * @brief 启动 AI 链路（幂等）：初始化 agent + 注册下行打印 + 起触发线程
 */
static void __agent_start(void)
{
    if (s_agent_started) {
        return;
    }
    s_agent_started = true;

    OPERATE_RET rt = ai_agent_init();
    if (rt != OPRT_OK) {
        PR_ERR("[AGENT] ai_agent_init failed: %d", rt);
        return;
    }
    PR_NOTICE("[AGENT] ai agent initialized");

    agent_downlink_init();

    THREAD_CFG_T thrd = {0};
    thrd.stackDepth   = 4608;
    thrd.priority     = THREAD_PRIO_2;
    thrd.thrdname     = "agent_trigger";
    if (tal_thread_create_and_start(&s_agent_trigger_thread, NULL, NULL, __agent_trigger_task, NULL, &thrd) != OPRT_OK) {
        PR_ERR("[AGENT] trigger thread create failed");
    }
}

/**
 * @brief user defined event handler
 *
 * @param client device info
 * @param event the event info
 * @return void
 */
void user_event_handler_on(tuya_iot_client_t *client, tuya_event_msg_t *event)
{
    PR_DEBUG("Tuya Event ID:%d(%s)", event->id, EVENT_ID2STR(event->id));
    switch (event->id) {
    case TUYA_EVENT_BIND_START:
        PR_INFO("Device Bind Start!");
        break;

    /* Print the QRCode for Tuya APP bind */
    case TUYA_EVENT_DIRECT_MQTT_CONNECTED: {
#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
        char buffer[255];
        sprintf(buffer, "https://smartapp.tuya.com/s/p?p=%s&uuid=%s&v=2.0", TUYA_PRODUCT_ID, license.uuid);
        qrcode_string_output(buffer, user_log_output_cb, 0);
#endif
    } break;

    /* 已激活设备连上云端 MQTT：启动 AI 链路（agent 会话 + 语音触发） */
    case TUYA_EVENT_MQTT_CONNECTED:
        PR_NOTICE("[AGENT] mqtt connected, start ai chain");
        __agent_start();
        break;

    /* RECV upgrade request */
    case TUYA_EVENT_UPGRADE_NOTIFY:
        user_upgrade_notify_on(client, event->value.asJSON);
        break;

    case TUYA_EVENT_RESET: {
        tuya_reset_type_t reset_type = (tuya_reset_type_t)event->value.asInteger;
        PR_INFO("Device Reset:%d", reset_type);
    } break;

    case TUYA_EVENT_RESET_COMPLETE:
        PR_INFO("Device Reset Complete!");
        tal_system_reset();
        break;

    default:
        break;
    }
}

/* 链路状态：由 port 层（tkl_netif）状态回调维护，core 经 user_network_check 拉取 */
static volatile bool s_link_up = false;

/**
 * @brief port 链路状态回调：tkl_netif 注册后立即上报当前状态，
 * 之后周期轮询、状态变化时再次回调
 *
 * @param stat 当前链路状态
 * @return void
 */
static void __user_link_status_cb(TAL_NETIF_LINK_STAT_E stat)
{
    s_link_up = (TAL_NETIF_LINK_UP == stat);

    /* 同步活动 IP：link up ≠ IP 就绪（tkl_netif 仅查 IFF_UP，不代表已获地址）。
     * 出站 socket 的源地址绑定与 LAN 广播包里的本机 IP 均取自此值；取不到置 0 表示不绑定 */
    TUYA_IP_ADDR_T addr = 0;
    if (s_link_up) {
        NW_IP_S ip = {0};
        if (OPRT_OK == tal_netif_get_ip(&ip) && ip.ip[0] != '\0') {
            addr = tal_net_str2addr(ip.ip);
        }
    }
    tal_network_card_set_active_ip(addr);

    PR_NOTICE("link status changed to %d, link %s", stat, s_link_up ? "UP" : "DOWN");
}

/**
 * @brief user defined network check callback, it will check the network every
 * 1sec by reading the link status maintained by the port layer
 *
 * @return true
 * @return false
 */
bool user_network_check(void)
{
    return s_link_up;
}

void user_main(void)
{
    int rt = OPRT_OK;

    //! open iot development kit runtim init
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = tal_malloc, .free_fn = tal_free});
    /* Before the log: every log line timestamps itself through the time
     * service, whose mutex this creates. */
    tal_time_service_init();
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, user_log_output_cb);

    PR_NOTICE("Application information:");
    PR_NOTICE("App version:         %s", PROJECT_VERSION);
    PR_NOTICE("Compile time:        %s", __DATE__);
    PR_NOTICE("Audio source:        %s", AGENT_AUDIO_SRC_PATH);

    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key  = "dflfuap134ddlduq",
    });
    tal_sw_timer_init();
    tal_workq_init();

    /* 直接使用编译期凭据（tuya_config.h 或 -D 注入） */
    license.uuid    = TUYA_OPENSDK_UUID;
    license.authkey = TUYA_OPENSDK_AUTHKEY;
    /* Initialize Tuya device configuration */
    rt = tuya_iot_init(&client, &(const tuya_iot_config_t){
                                    .software_ver  = PROJECT_VERSION,
                                    .productkey    = TUYA_PRODUCT_ID,
                                    .uuid          = license.uuid,
                                    .authkey       = license.authkey,
                                    .event_handler = user_event_handler_on,
                                    .network_check = user_network_check,
                                });
    assert(rt == OPRT_OK);

    /* 链路状态接入：port（tkl_netif）立即上报当前状态并轮询变化 */
    tal_netif_set_link_cb(__user_link_status_cb);

    /* token 获取 port：mqtt_bind 直连绑定（未激活时出二维码） */
    tuya_iot_token_get_port_register(&client, mqtt_bind_token_get);

    PR_DEBUG("tuya_iot_init success");
    /* Start tuya iot task */
    tuya_iot_start(&client);

    for (;;) {
        /* Loop to receive packets, and handles client keepalive */
        tuya_iot_yield(&client);
    }
}

/**
 * @brief main
 *
 * @param argc
 * @param argv
 * @return void
 */
#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    user_main();
}
#endif
