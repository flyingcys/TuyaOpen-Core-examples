/**
* @file tkl_thread.h
* @brief Common process - adapter the thread api provide by OS
* @version 0.1
* @date 2020-11-09
*
* @copyright Copyright 2021-2030 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TKL_THREAD_H__
#define __TKL_THREAD_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* TKL_THREAD_HANDLE;
typedef void (*THREAD_FUNC_T)(void*);

/* thread 接口实现收编于 port/src/tkl_runtime.c（ops 卡私有，经
 * TAL_API_OPS_T 分发），本文件仅保留类型定义与 p2p 门控的 get_id。
 * 原无实现亦无调用方的 tkl_thread_get_priority/tkl_thread_set_priority、
 * ENABLE_EXT_RAM 未定义的 tkl_thread_create_in_psram 已一并删除。 */

#if defined(ENABLE_TUYA_P2P)
/**
* @brief Get the handle of the calling thread
*
* @param[out] thread: thread handle
*
* @note p2p-core 的 pjproject 专用（TLS 行匹配的不透明线程身份）；
*       实现返回 pthread_self()，与 tkl_thread_create 的 handle 不同类。
*       不用 p2p 的平台无需适配（门控与 tkl_fs.c 一致）。
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tkl_thread_get_id(TKL_THREAD_HANDLE *thread);
#endif /* ENABLE_TUYA_P2P */

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif
