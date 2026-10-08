/**
 * @file tal_wired.h
 * @brief 有线网卡应用层接口（tkl_wired → tal_wired）
 *
 * 只声明实际被使用的两个接口（get_ip / set_status_cb）；原 get_status /
 * set_ip / get_mac / set_mac 零调用，已随 port 侧接口裁剪一并删除。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_WIRED_H__
#define __TAL_WIRED_H__

#include "tuya_cloud_types.h"
#include "tkl_wired.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIRED_STAT_E               TKL_WIRED_STAT_E
#define TAL_WIRED_STATUS_CHANGE_CB TKL_WIRED_STATUS_CHANGE_CB

/**
 * @brief  set the status change callback
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tal_wired_set_status_cb(TAL_WIRED_STATUS_CHANGE_CB cb);

/**
 * @brief  get the ip address of the wired link
 *
 * @param[in]   ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tal_wired_get_ip(NW_IP_S *ip);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __TAL_WIRED_H__
