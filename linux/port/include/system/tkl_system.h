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

/**
 * @brief system enter critical
 *
 * @param[in]   none
 * @return  irq mask
 */
uint32_t tkl_system_enter_critical(void);

/**
 * @brief system exit critical
 *
 * @param[in]   irq_mask: irq mask
 * @return  none
 */
void tkl_system_exit_critical(uint32_t irq_mask);

/**
 * @brief enter critical macro
 */
#define TKL_ENTER_CRITICAL()        \
    uint32_t __irq_mask;              \
    __irq_mask = tkl_system_enter_critical()

/**
 * @brief exit critical macro
 */
#define TKL_EXIT_CRITICAL()         \
    tkl_system_exit_critical(__irq_mask)

/* reset / get_millisecond / get_random / sleep 收编于 port/src/tkl_runtime.c
 * （ops 卡私有，经 TAL_API_OPS_T 分发），声明已移除。无调用方的
 * get_tick_count / get_reset_reason / get_cpu_info / tkl_cpu_sleep_mode_set
 * （tkl_sleep.h 整份）与无实现的 tkl_system_delay 悬空声明已删除。
 * enter/exit_critical 保持对外符号，供上方宏 API 使用。 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
