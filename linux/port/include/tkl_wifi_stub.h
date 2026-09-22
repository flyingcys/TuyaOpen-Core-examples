/**
 * @file tkl_wifi_stub.h
 * @brief Stub implementation for WiFi functions on Ubuntu
 *
 * This is a stub/dummy implementation for Ubuntu platform.
 * WiFi functions are not available on Ubuntu in this context - use wired networking.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __TKL_WIFI_STUB_H__
#define __TKL_WIFI_STUB_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/

/**
 * @brief Set WiFi low power mode (stub - not implemented on Ubuntu)
 * @param[in] enable Enable/disable low power mode
 * @param[in] dtim DTIM value (ignored on Ubuntu)
 * @return OPERATE_RET
 */
static inline OPERATE_RET tkl_wifi_set_lp_mode(BOOL_T enable, uint8_t dtim)
{
    // Stub: WiFi low power mode not available on Ubuntu
    (void)enable;
    (void)dtim;
    return OPRT_OK;
}

/**
 * @brief Get connected AP RSSI (stub - returns dummy value)
 * @param[out] rssi RSSI value
 * @return OPERATE_RET
 */
static inline OPERATE_RET tkl_wifi_station_get_conn_ap_rssi(int8_t *rssi)
{
    // Stub: Return a dummy RSSI value for Ubuntu
    if (rssi) {
        *rssi = -50; // Dummy "good" RSSI value
    }
    return OPRT_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* __TKL_WIFI_STUB_H__ */

