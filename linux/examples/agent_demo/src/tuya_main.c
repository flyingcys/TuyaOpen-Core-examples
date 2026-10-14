/**
 * @file tuya_main.c
 * @brief agent_demo 主流程
 *
 * 链路：IoT 激活/连云 →（MQTT 连上后）初始化 AI agent → 等客户端就绪 →
 *       自动上传一段预置语音（触发）→ 下行打印文字（ASR/NLG）。
 *
 * 无 UI；下行语音在 CONFIG_ENABLE_COMP_AI_AUDIO 开启时经应用侧 ALSA
 * 消费端（play_local_out）播放，关闭时媒体包在 core 内丢弃；
 * 上行音频经应用侧 play_local_in_* 契约取得（Linux：PCM 文件回放）。
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

#include "agent.h"

/* MCP 头随 ai_mcp 目标（CONFIG_ENABLE_COMP_AI_MCP）提供：关开关时该目标不存在，
 * include 路径也随之消失，故 include 与调用一并由宏守卫（同下方 ENABLE_QRCODE 写法） */
#if defined(ENABLE_COMP_AI_MCP) && (ENABLE_COMP_AI_MCP == 1)
#include "ai_mcp.h"
#endif

/* 下行语音播放（src/components/ai_audio，B 波次恢复）：ai_audio_player_init 建立
 * 播放服务（内部 tuya_ai_player_service_init），port 的 ALSA 消费端随后注入。
 * 同 MCP：关开关时头与目标都不存在，include 与调用一并由宏守卫 */
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
#include "ai_audio_player.h"
#include "play_local_out.h"
#endif

#include <assert.h>

#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
#include "qrencode_print.h"
#endif

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

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

    /* 已激活设备连上云端 MQTT：启动 AI 链路（agent 会话 + 语音触发，见 agent.c） */
    case TUYA_EVENT_MQTT_CONNECTED:
        PR_NOTICE("[AGENT] mqtt connected, start ai chain");
        agent_start();
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
     *   set_consumer 才是最终替换点（时序见 play_local_out.h）；
     * - 失败不阻断主流程：打印错误继续，下行降级为无声（默认空消费端行为） */
    rt = ai_audio_player_init();
    if (rt == OPRT_OK) {
        PR_NOTICE("[AGENT] ai_audio_player initialized, ALSA consumer install rt=%d",
                  play_local_out_install());
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
