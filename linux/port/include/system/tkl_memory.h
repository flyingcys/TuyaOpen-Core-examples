/**
* @file tkl_memory.h
* @brief Common process - adapter the semaphore api provide by OS
* @version 0.1
* @date 2020-11-09
*
* @copyright Copyright 2021-2030 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TKL_MEMORY_H__
#define __TKL_MEMORY_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return the memory address malloced
*/
void *tkl_system_malloc(SIZE_T size);

/**
* @brief Free memory of system
*
* @param[in] ptr: memory point
*
* @note This API is used to free memory of system.
*
* @return void
*/
void tkl_system_free(void* ptr);

/* 实现收编于 port/src/tkl_api_ops.c：malloc/free 保持对外符号（tuya_transport
 * 的 mbedtls_* 与 port 的 utilities 直调）；calloc/realloc/get_free_heap_size
 * 仅 tal 层经 TAL_API_OPS_T 分发，为卡私有（static），声明已移除。
 * 原无调用方的 tkl_system_memset/memcpy、tkl_system_psram_* 已一并删除。 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
