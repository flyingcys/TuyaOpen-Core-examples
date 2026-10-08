/**
 * @file tkl_netif.h
 * @brief 本机网络接口信息：取 IP / 链路状态变化回调
 *
 * 与 tkl_network.h（socket 通信）区分：本文件只提供"本机接口"信息——遍历所有
 * 非回环接口，取第一个 up 的接口的 IP；链路状态回调内部起轮询线程。
 * 由原 tkl_wired.h 改造：实现不限有线（wlan0/eth0 皆可），故弱化 wired 命名。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __TKL_NETIF_H__
#define __TKL_NETIF_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 网络接口链路状态 */
typedef enum {
    TKL_NETIF_LINK_DOWN = 0,    ///< 链路 down（无非回环接口 up）
    TKL_NETIF_LINK_UP,          ///< 链路 up（至少一个非回环接口 up）
}TKL_NETIF_LINK_STAT_E;

/**
 * @brief 链路状态变化回调
 *
 * @param[out] status 当前链路状态
 */
typedef void (*TKL_NETIF_LINK_CHANGE_CB)(TKL_NETIF_LINK_STAT_E status);

/**
 * @brief  取本机网络接口 IP（第一个非回环且 up 的接口）
 *
 * @param[out]  ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_netif_get_ip(NW_IP_S *ip);

/**
 * @brief  注册链路状态变化回调（内部起 3s 轮询线程，重复调用幂等）
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_netif_set_link_cb(TKL_NETIF_LINK_CHANGE_CB cb);

#ifdef __cplusplus
}
#endif

#endif // __TKL_NETIF_H__
