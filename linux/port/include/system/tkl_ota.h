/**
 * @file tkl_ota.h
 * @brief Common process - ota
 * @version 1.0.0
 * @date 2021-04-21
 *
 * @copyright Copyright (C) 2021-2022 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TKL_OTA_H__
#define __TKL_OTA_H__

#ifdef __cplusplus
	extern "C" {
#endif

#include "tuya_cloud_types.h"

/* ota 接口实现独立收编于 port/src/tkl_ota.c（平台可选单元，ops 卡私有，
 * 经 TAL_OTA_OPS_T 分发），本文件仅保留类型依赖的 include。
 * 原无实现亦无调用方的 tkl_ota_get_old_firmware_info 悬空声明已删。 */

#ifdef __cplusplus
}
#endif

#endif
