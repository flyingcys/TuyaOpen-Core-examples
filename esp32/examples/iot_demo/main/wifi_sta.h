/**
 * @file wifi_sta.h
 * @brief demo 的 WiFi station 连接（esp-idf 原生 API）
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __WIFI_STA_H__
#define __WIFI_STA_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化并启动 WiFi station 连接（esp-idf 原生 API）
 *
 * 建立默认事件循环与 station netif、注册断线重连（退避），随后发起连接。
 * 本函数返回不代表已连上（连接异步完成）；IP 就绪后由 port 的 tkl_netif
 * 经 IP_EVENT_STA_GOT_IP 向 core 上报链路 up（"连上 WiFi 即当有线网络"）。
 *
 * @param ssid     WiFi 名称（非空）
 * @param password WiFi 密码（开放网络传空字符串）
 * @return OPRT_OK on success. Others on error
 */
OPERATE_RET wifi_sta_init(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_STA_H__ */
