/**
 * @file tal_netif.h
 * @brief 本机网络接口应用层接口（tkl_netif → tal_netif）
 *
 * 只声明实际被使用的两个接口（get_ip / set_link_cb）。由原 tal_wired.h 改名——
 * 实现不限有线（遍历所有非回环接口），弱化 wired 命名。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_NETIF_H__
#define __TAL_NETIF_H__

#include "tuya_cloud_types.h"
#include "tkl_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETIF_LINK_STAT_E        TKL_NETIF_LINK_STAT_E
#define TAL_NETIF_LINK_CHANGE_CB TKL_NETIF_LINK_CHANGE_CB

/**
 * @brief  set the link status change callback
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tal_netif_set_link_cb(TAL_NETIF_LINK_CHANGE_CB cb);

/**
 * @brief  get the ip address of the local network interface
 *
 * @param[in]   ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tal_netif_get_ip(NW_IP_S *ip);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __TAL_NETIF_H__
