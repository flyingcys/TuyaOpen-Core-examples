/**
 * @file ota_app.h
 * @brief 应用层 OTA 实现（esp_ota）注册接口
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __OTA_APP_H__
#define __OTA_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief ota start notify（注册回调：TAL_OTA_CBS_T.start_notify）
*
* @param[in] image_size:  image size
* @param[in] type:        ota type
* @param[in] path:        ota path
*
* @note This API is used for ota start notify
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path);

/**
* @brief ota data process（注册回调：TAL_OTA_CBS_T.data_process）
*
* @param[in] pack:       point to ota pack
* @param[out] remain_len: ota pack remain len
*
* @note This API is used for ota data process
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len);

/**
* @brief ota end notify（注册回调：TAL_OTA_CBS_T.end_notify）
*
* @param[in] reset:  ota reset
*
* @note This API is used for ota end notify
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET ota_end_notify(BOOL_T reset);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_APP_H__ */
