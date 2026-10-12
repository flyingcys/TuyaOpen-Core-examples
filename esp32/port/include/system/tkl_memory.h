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

/* 实现收编于 port/src/tkl_runtime.c：malloc/free 是主卡 TAL_API_OPS_T 的
 * .malloc/.free 实现本体（core 经 tal_malloc/tal_free 分发），port 内部亦直接使用。
 * 2026-10 解耦后 core 不再直调本符号（原 tuya_transport 的 mbedtls_* 直调已改 tal_*）。
 * 原无调用方的 tkl_system_memset/memcpy、
 * tkl_system_realloc、tkl_system_get_free_heap_size、tkl_system_calloc
 * （calloc 改由 core 的 tal_calloc 用 tal_malloc+memset 实现）已删除；
 * 外部 RAM 分配家族亦已删除（p2p-core 的 tuya_ipc_skill.c 改用
 * tal_malloc/tal_free）。 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
