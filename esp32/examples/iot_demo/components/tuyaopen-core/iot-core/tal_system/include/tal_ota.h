/**
 * @file tal_ota.h
 * @brief Provides OTA (Over-The-Air) update functionalities for Tuya IoT
 * applications.
 *
 * This header file defines the interface for OTA update operations within Tuya
 * IoT applications, including functions for querying OTA capabilities,
 * initiating OTA updates, processing OTA data, and finalizing the OTA process.
 * It supports different types of OTA updates such as full package updates and
 * differential updates, catering to various application and device
 * requirements.
 *
 * The OTA functionalities are essential for enabling remote firmware updates,
 * ensuring that IoT devices can receive the latest features, improvements, and
 * security patches without requiring physical access to the device. This file
 * is part of the Tuya IoT Development Platform and is intended for use in
 * Tuya-based applications.
 *
 * @note This file is subject to the platform's license and copyright terms.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_OTA_H__
#define __TAL_OTA_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ********************* constant ( macro and enum ) *********************
 **********************************************************************/

/***********************************************************************
 ********************* struct ******************************************
 **********************************************************************/

/**
 * @brief OTA 平台回调集合（回调注入制，OTA 为平台可选能力）
 *
 * 需要 OTA 的平台/应用在初始化时经 tal_ota_register_cbs 注册本结构；
 * 不注册则 tal_ota_* 返回 OPRT_NOT_SUPPORTED。
 *
 * @note 注册仅保存指针、不做拷贝：结构体须为静态/全局对象，生命周期
 *       覆盖整个 OTA 流程。
 */
typedef struct {
    /* OTA 开始通知；见 tal_ota_start_notify */
    OPERATE_RET (*start_notify)(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path);
    /* OTA 数据写入；见 tal_ota_data_process */
    OPERATE_RET (*data_process)(TUYA_OTA_DATA_T *pack, uint32_t *remain_len);
    /* OTA 结束通知；见 tal_ota_end_notify */
    OPERATE_RET (*end_notify)(BOOL_T reset);
} TAL_OTA_CBS_T;

/***********************************************************************
 ********************* variable ****************************************
 **********************************************************************/

/***********************************************************************
 ********************* function ****************************************
 **********************************************************************/

/**
 * @brief Register the platform callbacks of OTA (optional capability)
 *
 * 注册 OTA 平台回调（注入制）：需要 OTA 的平台/应用调用本接口完成注入，
 * 未注册时 tal_ota_* 分发返回 OPRT_NOT_SUPPORTED。典型调用点：应用
 * 初始化（云连接建立前）；重复调用以最后一次为准，传 NULL 可注销。
 *
 * @param[in] cbs: pointer to the callback set (static lifetime), may be NULL
 *
 * @note 未注册即视为不支持 OTA：tal_ota_* 返回 OPRT_NOT_SUPPORTED。注意
 *       core 的云端 OTA 流程（tuya_ota.c）不检查这些返回值——云端仍会
 *       走完下载/校验并上报"升级完成"，而设备实际未应用升级包；不打算
 *       支持 OTA 的平台应确保云端不下发升级任务。
 *
 * @return none
 */
void tal_ota_register_cbs(const TAL_OTA_CBS_T *cbs);

/**
 * @brief This API is used for ota start notify
 *
 * @param[in] image_size: image size
 * @param[in] type: ota type
 * @param[in] path: path
 *
 * @return OPRT_OK on success；OPRT_NOT_SUPPORTED：未注册 OTA 回调。Others
 * on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path);

/**
 * @brief This API is used for ota data process
 *
 * @param[in] *pack: point to ota pack
 * @param[in] remain_len: ota pack remain len
 *
 * @return OPRT_OK on success；OPRT_NOT_SUPPORTED：未注册 OTA 回调
 * （此时不写 *remain_len，保持调用方传入值）。Others on error, please
 * refer to tuya_error_code.h
 */
OPERATE_RET tal_ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len);

/**
 * @brief This API is used for ota end notify
 *
 * @param[in] reset: ota reset
 *
 * @return OPRT_OK on success；OPRT_NOT_SUPPORTED：未注册 OTA 回调。Others
 * on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_ota_end_notify(BOOL_T reset);

#ifdef __cplusplus
}
#endif

#endif /* __TAL_OTA_H__ */
