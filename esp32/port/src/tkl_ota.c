/**
 * @file tkl_ota.c
 * @brief tal_ota 平台 ops 卡组装 + OTA 实现（esp32 port，esp_ota）
 *
 * OTA 为平台可选能力：需要 OTA 的 port 实现本文件（组装 tal_ota_ops_platform），
 * 与主卡 tkl_runtime.c 解耦；不需要 OTA 的平台提供空实现桩即可（core 的
 * tal_api.c 对 tal_ota_ops_platform 为强引用，省略本文件会链接失败）。
 * 实现为文件内 static，经 TAL_OTA_OPS_T 分发。
 *
 * 升级包经 esp_ota 写入下一个更新分区（ota_0/ota_1 轮换），end_notify 时
 * 切换启动分区并按 reset 参数复位。注意：分区表必须包含 otadata 与
 * ota_0/ota_1 分区，否则 start_notify 取分区失败（返回错误码，不崩）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tkl_ota.h"
#include "tuya_error_code.h"
#include "tuya_cloud_types.h"

#include "tal_ota_ops.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

typedef enum {
    TKL_OTA_INIT,
    TKL_OTA_BEGIN,
    TKL_OTA_IN_PROGRESS,
    TKL_OTA_SUCCESS,
} TKL_OTA_STATE_T;

typedef struct {
    TKL_OTA_STATE_T  state;
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    unsigned int     image_size;
    unsigned int     offset_image_size;
} TKL_OTA_HANDLE_S;

#define DBG_TAG "TKL_OTA"

#define TKL_OTA_UPGRADE_HANDLE_INIT(h) do { \
     (h).state = TKL_OTA_INIT; \
     (h).handle = 0; \
     (h).partition = NULL; \
     (h).image_size = 0; \
     (h).offset_image_size = 0; \
} while (0)

static TKL_OTA_HANDLE_S s_tkl_ota_handle = {
    .state = TKL_OTA_INIT,
    .handle = 0,
    .partition = NULL,
    .image_size = 0,
    .offset_image_size = 0
};

/**
* @brief ota start notify
*
* @param[in] image_size:  image size
* @param[in] type:        ota type
* @param[in] path:        ota path
*
* @note This API is used for ota start notify
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET tkl_ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path)
{
    (void)type;
    (void)path;

    if (0 == image_size) {
        ESP_LOGE(DBG_TAG, "%s: invalid ota image size %u", __func__, (unsigned)image_size);
        return OPRT_INVALID_PARM;
    }

    if (TKL_OTA_INIT != s_tkl_ota_handle.state) {
        ESP_LOGE(DBG_TAG, "%s: ota upgrade in process", __func__);
        return OPRT_COM_ERROR;
    }

    TKL_OTA_UPGRADE_HANDLE_INIT(s_tkl_ota_handle);

    s_tkl_ota_handle.partition = esp_ota_get_next_update_partition(NULL);
    if (NULL == s_tkl_ota_handle.partition) {
        ESP_LOGE(DBG_TAG, "%s: get next update partition failed (partitions.csv needs ota_0/ota_1?)", __func__);
        return OPRT_COM_ERROR;
    }

    if (ESP_OK != esp_ota_begin(s_tkl_ota_handle.partition, image_size, &s_tkl_ota_handle.handle)) {
        s_tkl_ota_handle.handle = 0;
        s_tkl_ota_handle.partition = NULL;
        ESP_LOGE(DBG_TAG, "%s: call esp_ota_begin failed", __func__);
        return OPRT_COM_ERROR;
    }

    s_tkl_ota_handle.image_size = image_size;
    s_tkl_ota_handle.state = TKL_OTA_BEGIN;

    return OPRT_OK;
}

/**
* @brief ota data process
*
* @param[in] pack:       point to ota pack
* @param[out] remain_len: ota pack remain len
*
* @note This API is used for ota data process
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET tkl_ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len)
{
    if (NULL == pack || NULL == remain_len) {
        ESP_LOGE(DBG_TAG, "%s: input invalid params", __func__);
        return OPRT_INVALID_PARM;
    }

    if (TKL_OTA_INIT == s_tkl_ota_handle.state) {
        ESP_LOGE(DBG_TAG, "%s: ota upgrade is not init", __func__);
        return OPRT_COM_ERROR;
    }

    if (TKL_OTA_SUCCESS == s_tkl_ota_handle.state) {
        *remain_len = 0;
        ESP_LOGI(DBG_TAG, "%s: ota process is successful", __func__);
        return OPRT_OK;
    }

    if (ESP_OK != esp_ota_write(s_tkl_ota_handle.handle, (const void *)pack->data, pack->len)) {
        ESP_LOGE(DBG_TAG, "%s: call esp_ota_write failed", __func__);
        return OPRT_COM_ERROR;
    }

    s_tkl_ota_handle.offset_image_size += pack->len;
    if (TKL_OTA_BEGIN == s_tkl_ota_handle.state) {
        s_tkl_ota_handle.state = TKL_OTA_IN_PROGRESS;
    }

    *remain_len = 0;
    if ((TKL_OTA_IN_PROGRESS == s_tkl_ota_handle.state) &&
        (s_tkl_ota_handle.offset_image_size >= s_tkl_ota_handle.image_size)) {
        s_tkl_ota_handle.state = TKL_OTA_SUCCESS;
    }

    return OPRT_OK;
}

/**
* @brief ota end notify
*
* @param[in] reset:  ota reset
*
* @note This API is used for ota end notify
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET tkl_ota_end_notify(BOOL_T reset)
{
    if (TKL_OTA_SUCCESS != s_tkl_ota_handle.state) {
        ESP_LOGE(DBG_TAG, "%s: ota upgrade state is not success", __func__);
        goto err_exit;
    }

    if (ESP_OK != esp_ota_end(s_tkl_ota_handle.handle)) {
        ESP_LOGE(DBG_TAG, "%s: call esp_ota_end failed", __func__);
        goto err_exit;
    }

    if (ESP_OK != esp_ota_set_boot_partition(s_tkl_ota_handle.partition)) {
        ESP_LOGE(DBG_TAG, "%s: call esp_ota_set_boot_partition failed", __func__);
        goto err_exit;
    }

    TKL_OTA_UPGRADE_HANDLE_INIT(s_tkl_ota_handle);

    if (reset) {
        ESP_LOGI(DBG_TAG, "%s: ota upgrade successful, system reboot now...", __func__);
        esp_restart();
    }
    return OPRT_OK;

err_exit:
    TKL_OTA_UPGRADE_HANDLE_INIT(s_tkl_ota_handle);
    return OPRT_COM_ERROR;
}

/* ---------------------------------------------------------------------------
 * OTA 平台 ops 卡：由本文件组装，core 的 tal_api.c 经 extern 引用并完成
 * tal_ota_* 分发。OTA 为平台可选能力，与主卡（tkl_runtime.c）解耦。
 * --------------------------------------------------------------------------- */
TAL_OTA_OPS_T tal_ota_ops_platform = {
    .start_notify = tkl_ota_start_notify,
    .data_process = tkl_ota_data_process,
    .end_notify = tkl_ota_end_notify,
};
