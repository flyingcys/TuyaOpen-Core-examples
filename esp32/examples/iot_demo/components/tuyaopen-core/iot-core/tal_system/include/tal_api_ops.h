/**
 * @file tal_api_ops.h
 * @brief tal_api 的平台 ops 契约：log output / memory / system / semaphore /
 *        mutex / thread / queue 七组（OTA 见 tal_ota_ops.h，独立卡）
 *
 * 定义 TAL_API_OPS_T：port 据此组装平台 ops 实例 tal_api_ops_platform
 * （成员绑定到 port 的 tkl_output、tkl_memory、tkl_system、tkl_semaphore、
 * tkl_mutex、tkl_thread、tkl_queue 实现），core 侧经 extern 引用完成各族
 * tal_* 分发，对上 tal_* API 签名不变。移植新平台时在各自 port 中提供同名
 * 实例即可。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_API_OPS_H__
#define __TAL_API_OPS_H__

#include "tuya_cloud_types.h"

#include "tal_memory.h"
#include "tal_semaphore.h"
#include "tal_mutex.h"
#include "tal_thread.h"
#include "tal_queue.h"
#include "tal_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief tal_api 平台 ops：成员与 port 的 tkl_output、tkl_memory、
 *        tkl_system、tkl_semaphore、tkl_mutex、tkl_thread、tkl_queue
 *        实现一一对应，只收编实际被 tal 层分发的接口（OTA 见 tal_ota_ops.h）
 *
 * @note 句柄/回调类型沿用 tal 层定义（SEM_HANDLE/MUTEX_HANDLE/
 *       THREAD_HANDLE/QUEUE_HANDLE 与 TKL 层的 TKL_SEM_HANDLE 等同为
 *       void* 别名；THREAD_FUNC_CB 与 TKL 层 THREAD_FUNC_T 同为
 *       void (*)(void *)），可直接绑定 port 的 tkl_* 实现。
 */
typedef struct {
    //! log output（启动早期即被 tal_log 回调与 QR 输出使用，置卡首位）
    void (*log_output)(const char *format, ...);
    //! memory
    void *(*malloc)(size_t size);
    void (*free)(void *ptr);
    //! system
    void (*sleep)(uint32_t num_ms);
    void (*reset)(void);
    SYS_TIME_T (*get_millisecond)(void);
    int (*get_random)(uint32_t range);
    //! semaphore
    OPERATE_RET (*semaphore_create_init)(SEM_HANDLE *handle, uint32_t sem_cnt, uint32_t sem_max);
    OPERATE_RET (*semaphore_wait)(const SEM_HANDLE handle, uint32_t timeout);
    OPERATE_RET (*semaphore_post)(const SEM_HANDLE handle);
    OPERATE_RET (*semaphore_release)(const SEM_HANDLE handle);
    //! mutex
    OPERATE_RET (*mutex_create_init)(MUTEX_HANDLE *handle);
    OPERATE_RET (*mutex_lock)(const MUTEX_HANDLE handle);
    OPERATE_RET (*mutex_unlock)(const MUTEX_HANDLE handle);
    OPERATE_RET (*mutex_release)(const MUTEX_HANDLE handle);
    //! thread
    OPERATE_RET (*thread_create)(THREAD_HANDLE *thread, const char *name, uint32_t stack_size, uint32_t priority,
                                 THREAD_FUNC_CB func, void *const arg);
    OPERATE_RET (*thread_release)(const THREAD_HANDLE thread);
    OPERATE_RET (*thread_set_self_name)(const char *name);
    OPERATE_RET (*thread_is_self)(const THREAD_HANDLE thread, BOOL_T *is_self);
    OPERATE_RET (*thread_get_id)(THREAD_HANDLE *thread);
    //! queue
    OPERATE_RET (*queue_create_init)(QUEUE_HANDLE *queue, int msgsize, int msgcount);
    OPERATE_RET (*queue_post)(const QUEUE_HANDLE queue, void *data, uint32_t timeout);
    OPERATE_RET (*queue_fetch)(const QUEUE_HANDLE queue, void *msg, uint32_t timeout);
    void (*queue_free)(const QUEUE_HANDLE queue);
} TAL_API_OPS_T;

/**
 * @brief Get the platform ops of tal_api
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_API_OPS_T *tal_api_get_ops(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
