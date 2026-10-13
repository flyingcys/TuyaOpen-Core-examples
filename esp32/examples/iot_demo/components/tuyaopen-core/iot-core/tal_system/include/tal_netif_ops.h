/**
 * @file tal_netif_ops.h
 * @brief tal_netif 的平台 ops 契约：本机网络接口两接口（get_ip / set_link_cb）
 *
 * port 在 port_netif.c 组装 tal_netif_ops_platform（成员绑定本平台 port_netif_*），
 * core 的 tal_netif.c 经 getter 分发，对上 tal_netif_* 签名不变。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TAL_NETIF_OPS_H__
#define __TAL_NETIF_OPS_H__

#include "tuya_cloud_types.h"

#include "tal_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief tal_netif 平台 ops：成员与 port 的 port_netif 实现一一对应
 */
typedef struct {
    /* 取本机网络接口 IP；见 tal_netif.h 的 tal_netif_get_ip */
    OPERATE_RET (*get_ip)(NW_IP_S *ip);
    /* 注册链路状态变化回调；见 tal_netif.h 的 tal_netif_set_link_cb */
    OPERATE_RET (*set_link_cb)(TAL_NETIF_LINK_CHANGE_CB cb);
} TAL_NETIF_OPS_T;

/**
 * @brief Get the platform ops of tal_netif
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_NETIF_OPS_T *tal_netif_get_ops(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
