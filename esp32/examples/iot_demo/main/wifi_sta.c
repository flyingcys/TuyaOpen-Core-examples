/**
 * @file wifi_sta.c
 * @brief demo 的 WiFi station 连接（esp-idf 原生 API）
 *
 * core 已无 wifi 抽象（tal_wifi/tkl_wifi 随 netmgr 整体移除，见
 * esp32/README.md 的迁移记录），"把 WiFi 连上"是 demo 层的职责：
 * 本模块用 esp-idf 原生 API 完成 station 初始化与连接（事件序列参考上游
 * tuyaopen ESP32 适配 tkl_wifi.c），连上后网络栈就绪，后续流程与 linux
 * 完全一致——port 的 tkl_netif 监听 IP 事件把链路状态报给 core，socket
 * 走 port 的 tkl_network（lwIP）。
 *
 * SSID/密码来自 menuconfig（iot_demo 菜单 → CONFIG_DEMO_WIFI_SSID /
 * CONFIG_DEMO_WIFI_PASSWORD，见 main/Kconfig.projbuild）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "wifi_sta.h"

#include "sdkconfig.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"

#include <string.h>

#define WIFI_TAG "wifi_sta"

/* 断线重连退避（ms）：连上后重置；一次连接周期内逐级递增，封顶 20s */
static const uint32_t s_reconnect_delay_ms[] = {1000, 3000, 5000, 10000, 15000, 20000};
#define WIFI_RECONNECT_DELAY_NUM (sizeof(s_reconnect_delay_ms) / sizeof(s_reconnect_delay_ms[0]))

static int s_reconnect_idx = 0;
static esp_timer_handle_t s_reconnect_timer = NULL;
static BOOL_T s_wifi_started = FALSE;

static void __wifi_reconnect_timer_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void __wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* 事件任务上下文：不阻塞等待，用 one-shot 定时器做退避重连 */
        uint32_t delay_ms = s_reconnect_delay_ms[s_reconnect_idx];
        if (s_reconnect_idx < (int)WIFI_RECONNECT_DELAY_NUM - 1) {
            s_reconnect_idx++;
        }
        ESP_LOGW(WIFI_TAG, "disconnected, reconnect in %ums", (unsigned)delay_ms);
        esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_reconnect_idx = 0;
    }
}

OPERATE_RET wifi_sta_init(const char *ssid, const char *password)
{
    if (NULL == ssid || '\0' == ssid[0]) {
        ESP_LOGE(WIFI_TAG, "ssid empty, set it in menuconfig (iot_demo -> WiFi SSID)");
        return OPRT_INVALID_PARM;
    }

    if (TRUE == s_wifi_started) {
        return OPRT_OK; /* 幂等 */
    }

    /* 退避重连定时器（one-shot，每次断线重启） */
    esp_timer_create_args_t timer_args = {
        .callback = __wifi_reconnect_timer_cb,
        .name = "wifi_reconn",
    };
    if (ESP_OK != esp_timer_create(&timer_args, &s_reconnect_timer)) {
        ESP_LOGE(WIFI_TAG, "reconnect timer create failed");
        return OPRT_COM_ERROR;
    }

    /* 网络接口与事件循环（port 的 tkl_netif 之后在其上注册链路监听）。
     * 两者非幂等：已初始化（其他组件先建）返回 ESP_ERR_INVALID_STATE，
     * 视为成功继续 */
    esp_err_t err = esp_netif_init();
    if (ESP_OK != err && ESP_ERR_INVALID_STATE != err) {
        ESP_LOGE(WIFI_TAG, "esp_netif_init failed: %d", err);
        return OPRT_COM_ERROR;
    }
    err = esp_event_loop_create_default();
    if (ESP_OK != err && ESP_ERR_INVALID_STATE != err) {
        ESP_LOGE(WIFI_TAG, "event loop create failed: %d", err);
        return OPRT_COM_ERROR;
    }
    if (NULL == esp_netif_create_default_wifi_sta()) {
        ESP_LOGE(WIFI_TAG, "create default wifi sta failed");
        return OPRT_COM_ERROR;
    }

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (ESP_OK != esp_wifi_init(&wifi_init_cfg)) {
        ESP_LOGE(WIFI_TAG, "esp_wifi_init failed");
        return OPRT_COM_ERROR;
    }

    if (ESP_OK != esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, __wifi_event_handler, NULL) ||
        ESP_OK != esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, __wifi_event_handler, NULL)) {
        ESP_LOGE(WIFI_TAG, "event handler register failed");
        return OPRT_COM_ERROR;
    }

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
    if (password) {
        snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);
    }

    if (ESP_OK != esp_wifi_set_mode(WIFI_MODE_STA) ||
        ESP_OK != esp_wifi_set_config(WIFI_IF_STA, &wifi_config)) {
        ESP_LOGE(WIFI_TAG, "esp_wifi set mode/config failed");
        return OPRT_COM_ERROR;
    }

    /* 启动后 WIFI_EVENT_STA_START → esp_wifi_connect()（见事件处理器） */
    if (ESP_OK != esp_wifi_start()) {
        ESP_LOGE(WIFI_TAG, "esp_wifi_start failed");
        return OPRT_COM_ERROR;
    }

    s_wifi_started = TRUE;
    ESP_LOGI(WIFI_TAG, "station started, connecting to \"%s\"...", ssid);

    return OPRT_OK;
}
