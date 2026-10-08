/**
* @file tkl_semaphore.h
* @brief Common process - adapter the semaphore api provide by OS
* @version 0.1
* @date 2020-11-09
*
* @copyright Copyright 2021-2030 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TKL_SEMAPHORE_H__
#define __TKL_SEMAPHORE_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* TKL_SEM_HANDLE;
#define TKL_SEM_WAIT_FOREVER 0xFFFFffff

/* semaphore 接口实现收编于 port/src/tkl_runtime.c（ops 卡私有，经
 * TAL_API_OPS_T 分发），本文件仅保留类型定义。 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
