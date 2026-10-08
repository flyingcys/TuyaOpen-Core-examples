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
 * TAL_API_OPS_T 分发），本文件仅保留类型定义。
 * 原无实现亦无调用方的 tkl_thread_get_priority/tkl_thread_set_priority、
 * ENABLE_EXT_RAM 未定义的 tkl_thread_create_in_psram 已一并删除。 */

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif
