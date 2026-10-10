/**
 * @file tal_ota_ops.h
 * @brief tal_ota 的平台 ops 契约：OTA 三接口（start_notify /
 *        data_process / end_notify）
 *
 * 定义 TAL_OTA_OPS_T：OTA 为平台可选能力，port 在需要 OTA 时以独立文件
 * port/src/tkl_ota.c 组装 tal_ota_ops_platform（成员绑定到本平台 tkl_ota_*
 * 实现），core 的 tal_api.c 经 extern 引用完成 tal_ota_* 分发，对上
 * tal_ota_* API 签名不变。与主卡 TAL_API_OPS_T（tal_api_ops.h）解耦，
 * 不混入 tkl_runtime.c。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_OTA_OPS_H__
#define __TAL_OTA_OPS_H__

#include "tuya_cloud_types.h"

#include "tal_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief tal_ota 平台 ops：成员与 port 的 tkl_ota 实现一一对应
 */
typedef struct {
    OPERATE_RET (*start_notify)(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path);
    OPERATE_RET (*data_process)(TUYA_OTA_DATA_T *pack, uint32_t *remain_len);
    OPERATE_RET (*end_notify)(BOOL_T reset);
} TAL_OTA_OPS_T;

/**
 * @brief Get the platform ops of tal_ota
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_OTA_OPS_T *tal_ota_get_ops(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
