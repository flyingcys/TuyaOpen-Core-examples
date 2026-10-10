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

/* 实现收编于 port/src/tkl_runtime.c：malloc/free 保持对外符号（tuya_transport
 * 的 mbedtls_* 与 port 的 utilities 直调）。
 * 原无调用方的 tkl_system_memset/memcpy、
 * tkl_system_realloc、tkl_system_get_free_heap_size、tkl_system_calloc
 * （calloc 改由 core 的 tal_calloc 用 tal_malloc+memset 实现）已删除；
 * psram 家族在 ENABLE_TUYA_P2P 下保留（p2p-core 的 tuya_ipc_skill.c 引用）。 */

#if defined(ENABLE_TUYA_P2P)
/**
* @brief Alloc memory of psram
*
* @param[in] size: memory size
*
* @note p2p-core（tuya_ipc_skill.c）专用：Linux 无 PSRAM，实现别名到系统堆。
*       不用 p2p 的平台无需适配（门控与 tkl_fs.c 一致）。
*
* @return the memory address malloced
*/
void *tkl_system_psram_malloc(const size_t size);

/**
* @brief Free memory of psram
*
* @param[in] ptr: memory point
*
* @note 与 tkl_system_psram_malloc 配套。
*
* @return void
*/
void tkl_system_psram_free(void *ptr);
#endif /* ENABLE_TUYA_P2P */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
