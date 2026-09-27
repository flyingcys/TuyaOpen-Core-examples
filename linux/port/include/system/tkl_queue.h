/**
 * @file tkl_queue.h
 * @brief  Common process - Initialization
 * @version 0.1
 * @date 2021-01-04
 *
 * @copyright Copyright 2021-2030 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TKL_QUEUE_H__
#define __TKL_QUEUE_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TKL_QUEUE_WAIT_FROEVER 0xFFFFFFFF
typedef void* TKL_QUEUE_HANDLE;

/* queue 接口实现收编于 port/src/tkl_api_ops.c（ops 卡私有，经
 * TAL_API_OPS_T 分发），本文件仅保留类型定义。 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
