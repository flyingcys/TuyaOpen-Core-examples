/**
 * @file tal_netif.c
 * @brief 本机网络接口应用层包装：tkl_netif（port）→ tal_netif（应用）
 *
 * 只保留实际被使用的两个接口：get_ip（上层取本机 IP）与 set_link_cb
 * （注册链路状态回调）。由原 tal_wired.c 改名——实现不限有线（遍历所有
 * 非回环接口），弱化 wired 命名。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_netif.h"
#include "tkl_netif.h"

/**
 * @brief  set the link status change callback
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tal_netif_set_link_cb(TAL_NETIF_LINK_CHANGE_CB cb)
{
    return tkl_netif_set_link_cb(cb);
}

/**
 * @brief  get the ip address of the local network interface
 *
 * @param[in]   ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tal_netif_get_ip(NW_IP_S *ip)
{
    return tkl_netif_get_ip(ip);
}
