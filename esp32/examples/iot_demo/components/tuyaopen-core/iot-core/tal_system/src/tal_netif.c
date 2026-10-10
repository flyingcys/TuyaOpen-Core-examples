/**
 * @file tal_netif.c
 * @brief tal_netif 平台分发：两接口透传到 port 的 tkl_netif（tal_netif_ops_platform）
 *
 * core 不持有网络接口实现——遍历接口/ioctl 取值/轮询线程都在 port（见
 * tal_netif_ops.h 契约）。本文件只做 ops 卡分发，与 tal_kv.c / tal_ota 同构。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_netif.h"
#include "tal_netif_ops.h"
#include "tal_log.h"

/* 平台 ops 卡（port 的 tkl_netif.c 定义），core 侧经 getter 分发 */
extern TAL_NETIF_OPS_T tal_netif_ops_platform;

TAL_NETIF_OPS_T *tal_netif_get_ops(void)
{
    return &tal_netif_ops_platform;
}

#define TAL_NETIF_EXEC_OP(op_name, default_ret, ...)                                                                   \
    do {                                                                                                               \
        TAL_NETIF_OPS_T *ops = tal_netif_get_ops();                                                                    \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("TAL NETIF operation %s not available", #op_name);                                                      \
        return default_ret;                                                                                            \
    } while (0)

OPERATE_RET tal_netif_get_ip(NW_IP_S *ip)
{
    TAL_NETIF_EXEC_OP(get_ip, OPRT_COM_ERROR, ip);
}

OPERATE_RET tal_netif_set_link_cb(TAL_NETIF_LINK_CHANGE_CB cb)
{
    TAL_NETIF_EXEC_OP(set_link_cb, OPRT_COM_ERROR, cb);
}
