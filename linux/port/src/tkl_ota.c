/**
 * @file tkl_ota.c
 * @brief tal_ota 平台 ops 卡组装 + OTA 实现（linux port）
 *
 * OTA 为平台可选能力：需要 OTA 的 port 实现本文件（组装 tal_ota_ops_platform），
 * 与主卡 tkl_runtime.c 解耦；不需要 OTA 的平台提供空实现桩即可（core 的
 * tal_api.c 对 tal_ota_ops_platform 为强引用，省略本文件会链接失败）。
 * 实现为文件内 static，经 TAL_OTA_OPS_T 分发。本实现把升级包写入
 * ./tuyadb/ota，end_notify 按 reset 复位进程。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tkl_ota.h"
#include "tuya_error_code.h"
#include "tuya_cloud_types.h"

#include "tal_ota_ops.h"

#include <stdio.h>
#include <stdlib.h>

static FILE *s_upgrade_fd = NULL;

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
    (void)image_size;
    (void)type;
    (void)path;

    FILE *p_upgrade_fd = (FILE *)s_upgrade_fd;

    if (p_upgrade_fd) {
        fclose(p_upgrade_fd);
    }

    char ota_path[255];

    sprintf(ota_path, "./tuyadb/ota");
    s_upgrade_fd = fopen(ota_path, "w+b");

    if (NULL == s_upgrade_fd) {
        printf("open upgrade file fail. upgrade fail %s\r\n", ota_path);
        return OPRT_COM_ERROR;
    }

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
    printf("Rev File Data Total_len %d, Offset:%u Len:%u\r\n", pack->total_len, pack->offset, pack->len);
    FILE *p_upgrade_fd = (FILE *)s_upgrade_fd;
    if (pack->offset + pack->len != pack->total_len) {
        fwrite(pack->data, 1, pack->len - 100, p_upgrade_fd);
        *remain_len = 100;
    } else {
        fwrite(pack->data, 1, pack->len, p_upgrade_fd);
        *remain_len = 0;
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
    FILE *p_upgrade_fd = (FILE *)s_upgrade_fd;
    fclose(p_upgrade_fd);

    if (reset) {
        printf("SOC Upgrade File Download Success\r\n");
        // UserTODO
        exit(1);
    } else {
        printf("SOC Upgrade File Download Fail.ret = %d\r\n", reset);
    }

    return OPRT_OK;
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
