/**
* @file tkl_wired.h
* @brief Common process - adapter the wired api
* @version 0.1
* @date 2020-11-09
*
* @copyright Copyright 2020-2021 Tuya Inc. All Rights Reserved.
*
* @note 本 port 只声明实际被使用的两个接口（get_ip / set_status_cb），实现内联在
*       port/src/tkl_network.c；原 TKL 头的 get_status / set_ip / get_mac / set_mac
*       在 Linux 实现中无调用方，已裁剪（get_status 降为实现内部 static）。
*
*/

#ifndef __TKL_WIRED_H__
#define __TKL_WIRED_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* tuyaos definition of wired network status */
typedef enum {
    TKL_WIRED_LINK_DOWN = 0,    ///< the network cable is unplugged
    TKL_WIRED_LINK_UP,          ///< the network cable is plugged and IP is got
}TKL_WIRED_STAT_E;

/**
 * @brief callback function: WIRED_STATUS_CHANGE_CB
 *        when wired connect status changed, notify tuyaos
 *        with this callback.
 *
 * @param[out]       is_up         the wired link status is up or not
 */
typedef void (*TKL_WIRED_STATUS_CHANGE_CB)(TKL_WIRED_STAT_E status);

/**
 * @brief  set the status change callback
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wired_set_status_cb(TKL_WIRED_STATUS_CHANGE_CB cb);

/**
 * @brief  get the ip address of the wired link
 *
 * @param[in]   ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wired_get_ip(NW_IP_S *ip);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __TKL_WIRED_H__
