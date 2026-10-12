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
 * TAL_API_OPS_T 分发），本文件仅保留类型定义与 get_id 声明。
 * 原无实现亦无调用方的 tkl_thread_get_priority/tkl_thread_set_priority、
 * ENABLE_EXT_RAM 未定义的 tkl_thread_create_in_psram 已一并删除。 */

/**
* @brief Get the handle of the calling thread
*
* @param[out] thread: thread handle
*
* @note 卡成员（TAL_API_OPS_T.thread_get_id）的实现，无条件提供：p2p-core 的
*       pjproject（os_core_tuyaos.c）经 tal_thread_get_id 用它做 TLS 行匹配的
*       不透明线程身份。返回平台线程身份原语（linux: pthread_self()；
*       esp32: xTaskGetCurrentTaskHandle()），与 tkl_thread_create 的 handle
*       不同类；同一线程恒得同一值。
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tkl_thread_get_id(TKL_THREAD_HANDLE *thread);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif
