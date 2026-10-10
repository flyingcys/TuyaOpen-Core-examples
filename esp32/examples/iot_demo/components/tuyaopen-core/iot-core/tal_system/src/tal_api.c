/**
 * @file tal_api.c
 * @brief Provides Tuya Abstract Layer (TAL) API implementations for semaphores,
 * mutexes, OTA, and queues.
 *
 * This source file implements the Tuya Abstract Layer (TAL) APIs for managing
 * semaphores, mutexes, Over-The-Air (OTA) updates, and message queues. It
 * serves as an abstraction layer that interfaces with the lower-level Tuya
 * Kernel Layer (TKL) to provide simplified and unified API calls for
 * application development. The TAL APIs facilitate the development of Tuya IoT
 * applications by offering standardized operations for synchronization
 * primitives (semaphores and mutexes), handling OTA updates, and managing
 * inter-thread communication through queues.
 *
 * The underlying TKL calls are dispatched through the platform ops card
 * (TAL_API_OPS_T, see tal_api_ops.h) assembled in the port, instead of
 * linking against tkl_* symbols directly. The tal_* API signatures are
 * unchanged for callers.
 *
 * @note This file is part of the Tuya IoT Development Platform and is intended
 * for use in Tuya-based applications. It is subject to the platform's license
 * and copyright terms.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_semaphore.h"
#include "tal_mutex.h"
#include "tal_queue.h"
#include "tal_ota.h"
#include "tal_log.h"
#include "tal_api_ops.h"
#include "tal_ota_ops.h"

/* 标准平台 ops 卡（port 的 tkl_runtime.c 定义，成员指向其 tkl_semaphore、
 * tkl_mutex、tkl_queue 等实现） */
extern TAL_API_OPS_T tal_api_ops_platform;

TAL_API_OPS_T *tal_api_get_ops(void)
{
    return &tal_api_ops_platform;
}

/* OTA 平台 ops 卡（port 的 tkl_ota.c 定义；OTA 为平台可选能力，独立于主卡） */
extern TAL_OTA_OPS_T tal_ota_ops_platform;

TAL_OTA_OPS_T *tal_ota_get_ops(void)
{
    return &tal_ota_ops_platform;
}

/***********************************************************
************************macro define************************
***********************************************************/

/**
 * @brief Macro to execute a TAL operation on a given ops card with error handling
 *
 * This macro simplifies the repetitive pattern of getting the ops card,
 * checking if the operation exists, and calling it with parameters.
 *
 * @param ops_ptr: pointer to the ops card (main card or a subsystem card)
 * @param op_name: The operation name in the ops struct
 * @param default_ret: Default return value if operation is not available
 * @param ...: Parameters to pass to the operation function
 */
#define TAL_EXEC_OP(ops_ptr, op_name, default_ret, ...)                                                                \
    do {                                                                                                               \
        if (NULL != (ops_ptr) && (ops_ptr)->op_name) {                                                                 \
            return (ops_ptr)->op_name(__VA_ARGS__);                                                                    \
        }                                                                                                              \
        PR_ERR("TAL API operation %s not available", #op_name);                                                        \
        return default_ret;                                                                                            \
    } while (0)

/**
 * @brief Macro to execute a TAL operation on a given ops card that returns void
 *
 * @param ops_ptr: pointer to the ops card (main card or a subsystem card)
 * @param op_name: The operation name in the ops struct
 * @param ...: Parameters to pass to the operation function
 */
#define TAL_EXEC_OP_VOID(ops_ptr, op_name, ...)                                                                        \
    do {                                                                                                               \
        if (NULL != (ops_ptr) && (ops_ptr)->op_name) {                                                                 \
            (ops_ptr)->op_name(__VA_ARGS__);                                                                           \
        }                                                                                                              \
    } while (0)

/* 主卡（tal_api_ops_platform）便捷宏 */
#define TAL_API_EXEC_OP(op_name, default_ret, ...) TAL_EXEC_OP(tal_api_get_ops(), op_name, default_ret, __VA_ARGS__)
#define TAL_API_EXEC_OP_VOID(op_name, ...)         TAL_EXEC_OP_VOID(tal_api_get_ops(), op_name, __VA_ARGS__)

/* OTA 卡（tal_ota_ops_platform，平台可选）便捷宏 */
#define TAL_OTA_EXEC_OP(op_name, default_ret, ...) TAL_EXEC_OP(tal_ota_get_ops(), op_name, default_ret, __VA_ARGS__)

//! sem
OPERATE_RET tal_semaphore_create_init(SEM_HANDLE *handle, uint32_t sem_cnt, uint32_t sem_max)
{
    TAL_API_EXEC_OP(semaphore_create_init, OPRT_COM_ERROR, handle, sem_cnt, sem_max);
}

OPERATE_RET tal_semaphore_wait(SEM_HANDLE handle, uint32_t timeout)
{
    TAL_API_EXEC_OP(semaphore_wait, OPRT_COM_ERROR, handle, timeout);
}

OPERATE_RET tal_semaphore_post(SEM_HANDLE handle)
{
    TAL_API_EXEC_OP(semaphore_post, OPRT_COM_ERROR, handle);
}

OPERATE_RET tal_semaphore_release(SEM_HANDLE handle)
{
    TAL_API_EXEC_OP(semaphore_release, OPRT_COM_ERROR, handle);
}

//! mutex
OPERATE_RET tal_mutex_create_init(MUTEX_HANDLE *handle)
{
    TAL_API_EXEC_OP(mutex_create_init, OPRT_COM_ERROR, handle);
}

OPERATE_RET tal_mutex_lock(const MUTEX_HANDLE handle)
{
    TAL_API_EXEC_OP(mutex_lock, OPRT_COM_ERROR, handle);
}

OPERATE_RET tal_mutex_unlock(const MUTEX_HANDLE handle)
{
    TAL_API_EXEC_OP(mutex_unlock, OPRT_COM_ERROR, handle);
}

OPERATE_RET tal_mutex_release(const MUTEX_HANDLE handle)
{
    TAL_API_EXEC_OP(mutex_release, OPRT_COM_ERROR, handle);
}

//! ota

OPERATE_RET tal_ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path)
{
    TAL_OTA_EXEC_OP(start_notify, OPRT_COM_ERROR, image_size, type, path);
}

OPERATE_RET tal_ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len)
{
    TAL_OTA_EXEC_OP(data_process, OPRT_COM_ERROR, pack, remain_len);
}

OPERATE_RET tal_ota_end_notify(BOOL_T reset)
{
    TAL_OTA_EXEC_OP(end_notify, OPRT_COM_ERROR, reset);
}

//! queue
OPERATE_RET tal_queue_create_init(QUEUE_HANDLE *queue, int msgsize, int msgcount)
{
    TAL_API_EXEC_OP(queue_create_init, OPRT_COM_ERROR, queue, msgsize, msgcount);
}

OPERATE_RET tal_queue_post(QUEUE_HANDLE queue, void *data, uint32_t timeout)
{
    TAL_API_EXEC_OP(queue_post, OPRT_COM_ERROR, queue, data, timeout);
}

OPERATE_RET tal_queue_fetch(QUEUE_HANDLE queue, void *msg, uint32_t timeout)
{
    TAL_API_EXEC_OP(queue_fetch, OPRT_COM_ERROR, queue, msg, timeout);
}

void tal_queue_free(QUEUE_HANDLE queue)
{
    TAL_API_EXEC_OP_VOID(queue_free, queue);
}
