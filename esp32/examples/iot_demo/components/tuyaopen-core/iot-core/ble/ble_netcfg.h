/**
 * @file ble_netcfg.h
 * @brief 蓝牙配网（自包含模块，原 netcfg 框架的 BLE 前端）
 *
 * 提供 APP 经 BLE 下发 ssid/passwd/token 的配网通道。应用层直接启动：
 *   tuya_ble_init → ble_netcfg_start(finish_cb)
 * 完成回调里应用自行联网并发布 EVENT_LINK_ACTIVATE 完成绑定激活。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef _BLE_NETCFG_H_
#define _BLE_NETCFG_H_

#include "tuya_cloud_types.h"

/* 配网下发缓冲区定长：原先取自 tkl_wifi.h，但 WIFI TKL 层已随 netmgr 移除
 * （linux port 不再提供该头），此处自持。取值与 port HEAD 的 tkl_wifi.h 一致；
 * #ifndef 守卫兼容将来 WIFI TKL 回归时的同名定义。 */
#ifndef WIFI_SSID_LEN
#define WIFI_SSID_LEN 32
#endif
#ifndef WIFI_PASSWD_LEN
#define WIFI_PASSWD_LEN 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** APP 经 BLE 下发的 token 长度（原 netcfg.h WL_TOKEN_LEN） */
#define BLE_NETCFG_TOKEN_LEN 16

/**
 * @brief 蓝牙配网结果
 *
 * APP 绑定流程中经 BLE 下发：ssid/passwd 供联网，token 供绑定激活
 */
typedef struct {
    /** ssid array */
    uint8_t ssid[WIFI_SSID_LEN + 1];
    /** length of ssid array */
    uint8_t s_len;
    /** password array */
    uint8_t passwd[WIFI_PASSWD_LEN + 1];
    /** length of password array */
    uint8_t p_len;
    /** token array */
    uint8_t token[BLE_NETCFG_TOKEN_LEN + 1];
    /** length of token array */
    uint8_t t_len;
} ble_netcfg_info_t;

/**
 * @brief 蓝牙配网完成回调
 *
 * @param[in] info 配网结果（ssid/passwd/token）
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
typedef int (*ble_netcfg_finish_cb_t)(ble_netcfg_info_t *info);

/**
 * @brief 启动蓝牙配网：注册配网通道并刷新广播
 *
 * @param[in] cb 配网完成回调（不可为 NULL）
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
int ble_netcfg_start(ble_netcfg_finish_cb_t cb);

/**
 * @brief 停止蓝牙配网：移除配网通道（未启动时调用安全）
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
int ble_netcfg_stop(void);

#ifdef __cplusplus
}
#endif
#endif // _BLE_NETCFG_H_
