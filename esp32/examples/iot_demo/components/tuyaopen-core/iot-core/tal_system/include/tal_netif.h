/**
 * @file tal_netif.h
 * @brief 本机网络接口 TAL 层：公开 API（get_ip / set_link_cb）
 *
 * 由原 tal_wired.h 改名——实现不限有线（遍历所有非回环接口），弱化 wired 命名。
 * 与 tal_network.h 同域（网络 TAL 层），随 tal_system 一起被消费者依赖。
 * 实现经 ops 卡分发到 port（见 tal_netif_ops.h）。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TAL_NETIF_H__
#define __TAL_NETIF_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 网络接口链路状态 */
typedef enum {
    TAL_NETIF_LINK_DOWN = 0,    ///< 链路 down（无非回环接口 up）
    TAL_NETIF_LINK_UP,          ///< 链路 up（至少一个非回环接口 up）
} TAL_NETIF_LINK_STAT_E;

/**
 * @brief 链路状态变化回调
 *
 * @param[out] status 当前链路状态
 */
typedef void (*TAL_NETIF_LINK_CHANGE_CB)(TAL_NETIF_LINK_STAT_E status);

/**
 * @brief  取本机网络接口 IP（第一个非回环且 up 的接口）
 *
 * @param[out]  ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_netif_get_ip(NW_IP_S *ip);

/**
 * @brief  注册链路状态变化回调（内部起 3s 轮询线程，重复调用幂等）
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_netif_set_link_cb(TAL_NETIF_LINK_CHANGE_CB cb);

#ifdef __cplusplus
}
#endif

#endif // __TAL_NETIF_H__
