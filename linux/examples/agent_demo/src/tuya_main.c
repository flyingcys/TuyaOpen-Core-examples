/**
 * @file tuya_main.c
 * @brief agent_demo 主流程
 *
 * 链路：IoT 激活/连云 →（MQTT 连上后）初始化 AI agent → 等客户端就绪 →
 *       自动上传一段预置语音（触发）→ 下行打印文字（ASR/NLG）。
 *
 * 无 UI；下行语音在 CONFIG_ENABLE_COMP_AI_AUDIO 开启时经 port 注入的 ALSA
 * 消费端播放（port_ai_player_consumer），关闭时媒体包在 core 内丢弃；
 * 上行音频经 port 的 port_ai_demo_* 契约取得（Linux：PCM 文件回放）。
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
#include "reset_netcfg.h"

#include "ai_agent.h"
#include "tuya_ai_agent.h"
#include "agent_audio.h"
#include "agent_downlink.h"

/* MCP 头随 ai_mcp 目标（CONFIG_ENABLE_COMP_AI_MCP）提供：关开关时该目标不存在，
 * include 路径也随之消失，故 include 与调用一并由宏守卫（同下方 ENABLE_QRCODE 写法） */
#if defined(ENABLE_COMP_AI_MCP) && (ENABLE_COMP_AI_MCP == 1)
#include "ai_mcp.h"
#endif

/* 下行语音播放（agent-core/ai_audio，B 波次恢复）：ai_audio_player_init 建立
 * 播放服务（内部 tuya_ai_player_service_init），port 的 ALSA 消费端随后注入。
 * 同 MCP：关开关时头与目标都不存在，include 与调用一并由宏守卫 */
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
#include "ai_audio_player.h"
#include "port_ai_player_consumer.h"
#endif

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
static void __agent_start(void)
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

/* 链路状态：由 port 层（port_netif）状态回调维护，core 经 user_network_check 拉取 */
static volatile bool s_link_up = false;

/**
 * @brief port 链路状态回调：port_netif 注册后立即上报当前状态，
 * 之后周期轮询、状态变化时再次回调
 *
 * @param stat 当前链路状态
 * @return void
 */
static void __user_link_status_cb(TAL_NETIF_LINK_STAT_E stat)
{
    s_link_up = (TAL_NETIF_LINK_UP == stat);

    /* 同步活动 IP：link up ≠ IP 就绪（port_netif 仅查 IFF_UP，不代表已获地址）。
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

#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
    /* 下行语音播放初始化（放 main 初始化段、MQTT 之前的理由）：
     * - ai_audio_player_init 内部走 tuya_ai_player_service_init（建线程/队列，
     *   纯本地、不依赖网络），只需 TAL 运行时先就绪（故在 tal_workq_init 之后）；
     * - 下行 TTS 在 MQTT_CONNECTED 之后才可能到达，播放服务须先就绪，
     *   否则首个音频包落在默认空消费端上被丢弃；
     * - install 必须在 init 之后：service_init 会装载默认空消费端，
     *   set_consumer 才是最终替换点（时序见 port_ai_player_consumer.h）；
     * - 失败不阻断主流程：打印错误继续，下行降级为无声（默认空消费端行为） */
    rt = ai_audio_player_init();
    if (rt == OPRT_OK) {
        PR_NOTICE("[AGENT] ai_audio_player initialized, ALSA consumer install rt=%d",
                  port_ai_player_consumer_install());
    } else {
        PR_ERR("[AGENT] ai_audio_player_init failed: %d (downlink audio disabled)", rt);
    }
#endif

    /* 连续复位 3 次清配网（同 iot_demo/p2p_demo 的 reset_netcfg） */
    reset_netconfig_start();

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

    /* 链路状态接入：port（port_netif）立即上报当前状态并轮询变化 */
    tal_netif_set_link_cb(__user_link_status_cb);

    /* token 获取 port：mqtt_bind 直连绑定（未激活时出二维码） */
    tuya_iot_token_get_port_register(&client, mqtt_bind_token_get);

#if defined(ENABLE_COMP_AI_MCP) && (ENABLE_COMP_AI_MCP == 1)
    /* MCP 工具服务：ai_mcp_init 只订阅 EVENT_MQTT_CONNECTED（一次性），真正的初始化
     * （注册工具 + tuya_ai_agent_mcp_set_cb）在 MQTT 连上后的回调里执行。
     * 放这里（tuya_iot_start 之前）：事件由 iot 任务在 MQTT 连上时发布，订阅必须先于
     * 首次发布；放在 tuya_iot_init 之后则是为了保持"运行时服务（KV/定时器/工作队列）
     * 先就绪、再接云"的既有顺序——tal_event_subscribe 会惰性初始化事件管理器，
     * 故此处无硬性依赖，只为可读性。 */
    rt = ai_mcp_init();
    if (rt != OPRT_OK) {
        PR_ERR("[MCP] ai_mcp_init failed: %d", rt);
    }
#endif

    PR_DEBUG("tuya_iot_init success");
    /* Start tuya iot task */
    tuya_iot_start(&client);

    /* 连续复位计数达阈值则清配网（见 reset_netcfg.h） */
    reset_netconfig_check();

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
