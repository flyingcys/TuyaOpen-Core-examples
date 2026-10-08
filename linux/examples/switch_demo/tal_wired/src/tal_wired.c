/**
 * @file tal_wired.c
 * @brief 有线网卡应用层包装：tkl_wired（port）→ tal_wired（应用）
 *
 * 只保留实际被使用的两个接口：get_ip（上层取有线 IP）与 set_status_cb
 * （注册链路状态回调）。原文件的 get_status / set_ip / get_mac / set_mac
 * 零调用，已随 port 侧接口裁剪一并删除。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_wired.h"
#include "tkl_wired.h"

/**
 * @brief  set the status change callback
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tal_wired_set_status_cb(TAL_WIRED_STATUS_CHANGE_CB cb)
{
    return tkl_wired_set_status_cb(cb);
}

/**
 * @brief  get the ip address of the wired link
 *
 * @param[in]   ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tal_wired_get_ip(NW_IP_S *ip)
{
    return tkl_wired_get_ip(ip);
}
