/**
* @file tkl_system.h
* @brief Common process - adpater some api which provide system
* @version 0.1
* @date 2020-11-09
*
* @copyright Copyright 2021-2030 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TKL_SYSTEM_H__
#define __TKL_SYSTEM_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* reset / get_millisecond / get_random / sleep 收编于 port/src/tkl_runtime.c
 * （ops 卡私有，经 TAL_API_OPS_T 分发），声明已移除。无调用方的
 * get_tick_count / get_reset_reason / get_cpu_info / tkl_cpu_sleep_mode_set
 * （tkl_sleep.h 整份）与无实现的 tkl_system_delay 悬空声明已删除。
 * enter/exit_critical（含 TKL_ENTER/EXIT_CRITICAL 宏）已删除：仅裸机
 * （SYSTEM_NON_OS）下 tuya_queue.c 的分支引用，Linux 走 mutex 分支、零调用。 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
