/**
 * @file port_runtime.c
 * @brief tal_api 平台 ops 卡组装 + OS 原语实现单文件收编（esp32 port）
 *
 * 与 linux port 的 port_runtime.c 同构：log output / memory / system /
 * semaphore / mutex / thread / queue 七组实现收编于本文件，只保留实际被
 * 分发/调用的接口（OTA 为 core 回调注入，实现由应用层提供）：
 * - log output / semaphore / system / thread / queue 实现为 ops 卡私有
 *   （static），适配面收敛到一张卡；
 * - memory 只保留 malloc/free 对外符号（主卡 .malloc/.free 的实现本体 + port
 *   内部使用；2026-10 解耦后 core 不再直调，声明保留在 port_memory.h）；
 *   calloc 由 core 的 tal_calloc 用 tal_malloc+memset 实现，卡不再要求适配；
 * - mutex 保持对外符号（主卡 .mutex_* 的实现本体 + port 内部（mutex 管理器）
 *   使用；2026-10 解耦后 core 不再直调，声明随头删除内联）；
 * - port_thread_get_id 为 ops 卡成员（无条件提供；p2p-core 的 pjproject 经
 *   tal_thread_get_id 分发使用）；原外部 RAM 分配家族已删除
 *   （p2p-core 的 tuya_ipc_skill.c 改用 tal_malloc/tal_free）。
 *
 * 实现平台差异（对照 linux 版）：
 * - memory：直接映射 esp-idf 系统堆（多区域堆，不用 linux 的
 *   tuya_mem_heap 自管堆）；
 * - sleep/reset/get_millisecond/get_random：FreeRTOS / esp-idf 原语，
 *   毫秒时钟取 esp_timer（微秒精度），不用 tick（100Hz 下仅 10ms 精度）；
 * - semaphore/mutex/queue：FreeRTOS 原生对象，句柄即对象指针（linux 版
 *   额外包了一层管理结构）；
 * - thread：xTaskCreate 字节计栈，句柄即 TaskHandle_t。
 *
 * 2026-10 port 去 tkl 化：tkl_runtime.c → port_runtime.c，tkl_* 函数去
 * tkl_ 前缀（static 者改 __ 前缀，对外符号改 port_ 前缀）；原
 * tkl_semaphore/mutex/thread/queue/system 五个头文件删除（只有本文件一个
 * includer），其有效声明（类型/超时宏）随之内联到下面"原 tkl_*.h 内联区"；
 * tkl_memory.h 另改名保留为 port_memory.h（port_kv.c 也要 include 它）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_error_code.h"
#include "tuya_cloud_types.h"

#include "tal_api_ops.h"

/* ---------------------------------------------------------------------------
 * 原 tkl_{semaphore,mutex,thread,queue,memory,system}.h 内联区
 *
 * 那六个头文件只有本文件一个 includer（2026-10 去 tkl 化时删除），此处保留其
 * 全部有效内容：句柄类型 + 两个超时宏 + 两个仍需对外可见的符号声明
 * （malloc/free，esp32 侧由 port_kv.c 跨文件调用）。
 * 各头的函数声明（mutex 四件 + thread_get_id）随实现一并在本文件下方给出
 * （static 者本就不需前置声明）。
 * --------------------------------------------------------------------------- */

/* 原 tkl_memory.h：malloc/free 是主卡 TAL_API_OPS_T 的 .malloc/.free 实现
 * 本体，保持对外符号——core 已不直调（改经 tal_malloc/tal_free 分发），但
 * esp32 侧 port 的 kv 实现（port_kv.c）跨文件调用同族符号，故声明保留在
 * port_memory.h（单一来源，本文件与 port_kv.c 都经 include 获取——避免
 * "内联声明 + 头"双来源漂移）。 */
#include "port_memory.h"

/* 原 tkl_semaphore.h */
typedef void *TKL_SEM_HANDLE;
#define TKL_SEM_WAIT_FOREVER 0xFFFFffff

/* 原 tkl_mutex.h */
typedef void *TKL_MUTEX_HANDLE;

/* 原 tkl_queue.h */
#define TKL_QUEUE_WAIT_FROEVER 0xFFFFFFFF
typedef void *TKL_QUEUE_HANDLE;

/* 原 tkl_thread.h */
typedef void *TKL_THREAD_HANDLE;
typedef void (*THREAD_FUNC_T)(void *);

/* 原 tkl_system.h 无有效声明（删前已是纯注释头：reset/sleep/get_millisecond/
 * get_random 的声明早已移除，enter/exit_critical 宏亦已删），故无需内联内容。 */

/* esp-idf 不自动 -include sdkconfig.h（其路径在组件 include 中），
 * CONFIG_IDF_TARGET_* 等宏需显式引入 */
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * log output 实现：置于卡首位，启动早期即被 tal_log 回调与 QR 输出使用。
 * esp-idf 的 stdout 经 VFS 直写 UART（console），printf 即可。
 * --------------------------------------------------------------------------- */
/**
* @brief Output log information
*
* @param[in] str: log information
*
* @note This API is used for outputing log information
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static void __log_output(const char *str, ...)
{
    printf("%s", str);
    return;
}

/* ---------------------------------------------------------------------------
 * memory 实现：只保留 malloc/free 对外符号（core 的 tuya_transport mbedtls_*
 * 与 utilities 直调，声明见 port_memory.h）。直接映射 esp-idf 系统堆
 * （malloc/free 线程安全；esp32 的堆为多区域分块管理，不适用 linux 的
 * tuya_mem_heap 大块自管堆方案）。
 * --------------------------------------------------------------------------- */
/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return the memory address malloced
*/
void *port_system_malloc(const SIZE_T size)
{
    return malloc(size);
}

/**
* @brief Free memory of system
*
* @param[in] ptr: memory point
*
* @note This API is used to free memory of system.
*
* @return void
*/
void port_system_free(void *ptr)
{
    if (ptr) {
        free(ptr);
    }
}

/* ---------------------------------------------------------------------------
 * system 实现：sleep/reset/get_millisecond/get_random 仅 tal 层经卡分发，
 * 转 static。毫秒时钟用 esp_timer（微秒计数）而非 FreeRTOS tick
 * （CONFIG_FREERTOS_HZ=100 时 tick 仅 10ms 精度，超时/时间戳计算会失真）。
 * --------------------------------------------------------------------------- */
/**
* @brief System sleep
*
* @param[in] msTime: time in MS
*
* @note This API is used for system sleep.
*
* @return void
*/
static void __system_sleep(const uint32_t num_ms)
{
    uint32_t delay_ms = num_ms;

    /* 1 tick = 10ms（CONFIG_FREERTOS_HZ=100）：不足 1 tick 的延时会被
     * pdMS_TO_TICKS 截为 0 而立即返回，补齐到 10ms（与上游适配一致） */
    if (delay_ms < 10) {
        delay_ms = 10;
    }

    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    return;
}

/**
* @brief System reset
*
* @param void
*
* @note This API is used for system reset.
*
* @return void
*/
static void __system_reset(void)
{
    printf("Rev Restart Req\r\n");
    esp_restart();
    return;
}

/**
* @brief Get system millisecond
*
* @param none
*
* @return system millisecond
*/
static SYS_TIME_T __system_get_millisecond(void)
{
    return (SYS_TIME_T)(esp_timer_get_time() / 1000);
}

/**
* @brief Get a random number in the specified range
*
* @param[in] range: range
*
* @note This API is used for getting a random number in the specified range
*
* @return a random number in the specified range
*/
static int __system_get_random(const uint32_t range)
{
    /* esp_random 为硬件 RNG；range=0 返回 0-255（对齐 linux 版契约） */
    uint32_t value = esp_random() & 0x7FFFFFFF;

    return (0 == range) ? (int)(value & 0xFF) : (int)(value % range);
}

/* ---------------------------------------------------------------------------
 * semaphore 实现（ops 卡私有）：FreeRTOS 计数信号量，句柄即对象指针
 * --------------------------------------------------------------------------- */
/**
* @brief Create semaphore
*
* @param[out] handle: semaphore handle
* @param[in] sem_cnt: semaphore init count
* @param[in] sem_max: semaphore max count
*
* @note This API is used for creating semaphore.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __semaphore_create_init(TKL_SEM_HANDLE *handle, const uint32_t sem_cnt, const uint32_t sem_max)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    *handle = (TKL_SEM_HANDLE)xSemaphoreCreateCounting(sem_max, sem_cnt);
    if (NULL == *handle) {
        return OPRT_OS_ADAPTER_SEM_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Wait semaphore
*
* @param[in] handle: semaphore handle
* @param[in] timeout: wait timeout, TKL_SEM_WAIT_FOREVER means wait until get semaphore
*
* @note This API is used for waiting semaphore.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __semaphore_wait(const TKL_SEM_HANDLE handle, const uint32_t timeout)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret;
    TickType_t xTicksToWait = portMAX_DELAY;

    if (pdTRUE == xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xSemaphoreTakeFromISR((SemaphoreHandle_t)handle, &xHigherPriorityTaskWoken);
        if (pdTRUE == ret && pdTRUE == xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
        if (timeout < TKL_SEM_WAIT_FOREVER) {
            xTicksToWait = pdMS_TO_TICKS(timeout);
        }
        ret = xSemaphoreTake((SemaphoreHandle_t)handle, xTicksToWait);
    }

    if (pdTRUE != ret) {
        return OPRT_OS_ADAPTER_SEM_WAIT_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Post semaphore
*
* @param[in] handle: semaphore handle
*
* @note This API is used for posting semaphore.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __semaphore_post(const TKL_SEM_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret;

    if (pdTRUE == xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xSemaphoreGiveFromISR((SemaphoreHandle_t)handle, &xHigherPriorityTaskWoken);
        if (pdTRUE == ret && pdTRUE == xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
        ret = xSemaphoreGive((SemaphoreHandle_t)handle);
    }

    if (pdTRUE != ret) {
        return OPRT_OS_ADAPTER_SEM_POST_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Release semaphore
*
* @param[in] handle: semaphore handle
*
* @note This API is used for releasing semaphore.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __semaphore_release(const TKL_SEM_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    vSemaphoreDelete((SemaphoreHandle_t)handle);

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * mutex 实现：保持对外符号（core 的 tuya_queue.c 经宏直调，原 tkl_mutex.h 的
 * 声明已删除、函数体随实现保留在本文件）。递归互斥（对齐 linux 版的
 * PTHREAD_MUTEX_RECURSIVE）。
 * --------------------------------------------------------------------------- */
/**
* @brief Create mutex
*
* @param[out] pMutexHandle: mutex handle
*
* @note This API is used to create mutex.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_mutex_create_init(TKL_MUTEX_HANDLE *handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

#if configUSE_RECURSIVE_MUTEXES
    *handle = (TKL_MUTEX_HANDLE)xSemaphoreCreateRecursiveMutex();
#else
    *handle = (TKL_MUTEX_HANDLE)xSemaphoreCreateMutex();
#endif
    if (NULL == *handle) {
        return OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Lock mutex
*
* @param[in] mutexHandle: mutex handle
*
* @note This API is used to lock mutex.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_mutex_lock(const TKL_MUTEX_HANDLE handle)
{
    BaseType_t ret;

    if (!handle) {
        return OPRT_INVALID_PARM;
    }

#if configUSE_RECURSIVE_MUTEXES
    ret = xSemaphoreTakeRecursive((SemaphoreHandle_t)handle, portMAX_DELAY);
#else
    ret = xSemaphoreTake((SemaphoreHandle_t)handle, portMAX_DELAY);
#endif
    if (pdTRUE != ret) {
        return OPRT_OS_ADAPTER_MUTEX_LOCK_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Unlock mutex
*
* @param[in] mutexHandle: mutex handle
*
* @note This API is used to unlock mutex.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_mutex_unlock(const TKL_MUTEX_HANDLE handle)
{
    BaseType_t ret;

    if (!handle) {
        return OPRT_INVALID_PARM;
    }

#if configUSE_RECURSIVE_MUTEXES
    ret = xSemaphoreGiveRecursive((SemaphoreHandle_t)handle);
#else
    ret = xSemaphoreGive((SemaphoreHandle_t)handle);
#endif
    if (pdTRUE != ret) {
        return OPRT_OS_ADAPTER_MUTEX_UNLOCK_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Release mutex
*
* @param[in] mutexHandle: mutex handle
*
* @note This API is used to release mutex.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_mutex_release(const TKL_MUTEX_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    vSemaphoreDelete((SemaphoreHandle_t)handle);

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * thread 实现（ops 卡私有）：xTaskCreate 字节计栈（esp-idf 宏内部换算），
 * 句柄即 TaskHandle_t。
 * --------------------------------------------------------------------------- */
/**
* @brief Create thread
*
* @param[out] thread: thread handle
* @param[in] name: thread name
* @param[in] stack_size: stack size of thread
* @param[in] priority: priority of thread,please ref to tkl thread priority define in tuya_cloud_types.h
* @param[in] func: the main thread process function
* @param[in] arg: the args of the func, can be null
*
* @note This API is used for creating thread.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_create(TKL_THREAD_HANDLE *thread, const char *name, uint32_t stack_size,
                                     uint32_t priority, THREAD_FUNC_T func, void *const arg)
{
    if (!thread || !func) {
        return OPRT_INVALID_PARM;
    }

    /* 上游 tuyaopen ESP32 适配对 s3 固定补 1KB 栈余量（其 core 任务的
     * 调用链更深）；沿用该经验值 */
#if CONFIG_IDF_TARGET_ESP32S3
    stack_size += 1024;
#endif

    BaseType_t ret = xTaskCreate(func, name, stack_size, arg, priority, (TaskHandle_t *)thread);
    if (ret != pdPASS) {
        return OPRT_OS_ADAPTER_THRD_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Terminal thread and release thread resources
*
* @param[in] thread: thread handle
*
* @note This API is used to terminal thread and release thread resources.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_release(const TKL_THREAD_HANDLE thread)
{
    if (!thread) {
        return OPRT_INVALID_PARM;
    }

    /* 支持任务自删（vTaskDelete 传入自身句柄）与外部删除 */
    vTaskDelete((TaskHandle_t)thread);

    return OPRT_OK;
}

/**
* @brief Set name of self thread
*
* @param[in] name: thread name
*
* @note This API is used to set name of self thread.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_set_self_name(const char *name)
{
    if (!name) {
        return OPRT_INVALID_PARM;
    }

    /* FreeRTOS 任务名在创建时给定（xTaskCreate 的 name），无公开的运行时
     * 改名 API；保持空实现（与上游适配一致，core 仅调试友好用途） */
    return OPRT_OK;
}

/**
* @brief Check thread is self thread
*
* @param[in] thread: thread handle
* @param[out] is_self: is self thread or not
*
* @note This API is used to check thread is self thread.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_is_self(const TKL_THREAD_HANDLE thread, BOOL_T *is_self)
{
    if (NULL == thread || NULL == is_self) {
        return OPRT_INVALID_PARM;
    }

    *is_self = ((TaskHandle_t)thread == xTaskGetCurrentTaskHandle());
    return OPRT_OK;
}

/**
* @brief Get the handle of the calling thread
*
* @param[out] thread: thread handle
*
* @note p2p-core 的 pjproject（os_core_tuyaos.c）经 tal_thread_get_id 用它做
*       TLS 行匹配，只要求 "同一线程恒得同一值"的不透明身份。FreeRTOS 下
*       返回当前任务句柄。卡成员，无条件提供。
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_thread_get_id(TKL_THREAD_HANDLE *thread)
{
    if (NULL == thread) {
        return OPRT_INVALID_PARM;
    }

    *thread = (TKL_THREAD_HANDLE)xTaskGetCurrentTaskHandle();
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * queue 实现（ops 卡私有）：FreeRTOS 消息队列按值拷贝 msgsize 字节/条，
 * 与 linux 版语义一致（linux 版手工拷贝到堆缓冲后存指针）。
 * --------------------------------------------------------------------------- */
/**
 * @brief Create message queue
 *
 * @param[in] msgsize message size
 * @param[in] msgcount message number
 * @param[out] queue the queue handle created
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
static OPERATE_RET __queue_create_init(TKL_QUEUE_HANDLE *handle, int msgsize, int msgcount)
{
    if ((NULL == handle) || (msgsize <= 0) || (msgcount <= 0)) {
        return OPRT_INVALID_PARM;
    }

    *handle = (TKL_QUEUE_HANDLE)xQueueCreate(msgcount, msgsize);
    if (NULL == *handle) {
        return OPRT_OS_ADAPTER_QUEUE_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief post a message to the message queue
 *
 * @param[in] queue the handle of the queue
 * @param[in] data the data of the message
 * @param[in] timeout timeout time
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
static OPERATE_RET __queue_post(const TKL_QUEUE_HANDLE handle, void *data, uint32_t timeout)
{
    if (NULL == handle || NULL == data) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret;
    TickType_t xTicksToWait = portMAX_DELAY;

    if (pdTRUE == xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xQueueSendFromISR((QueueHandle_t)handle, data, &xHigherPriorityTaskWoken);
        if (pdPASS == ret && pdTRUE == xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
        if (timeout < TKL_QUEUE_WAIT_FROEVER) {
            xTicksToWait = pdMS_TO_TICKS(timeout);
        }
        ret = xQueueSend((QueueHandle_t)handle, data, xTicksToWait);
    }

    if (pdPASS != ret) {
        return OPRT_OS_ADAPTER_QUEUE_SEND_FAIL;
    }

    return OPRT_OK;
}

/**
 * @brief fetch message from the message queue
 *
 * @param[in] queue the message queue handle
 * @param[inout] msg the message fetch form the message queue
 * @param[in] timeout timeout time
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
static OPERATE_RET __queue_fetch(const TKL_QUEUE_HANDLE handle, void *msg, uint32_t timeout)
{
    if (NULL == handle) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret;
    TickType_t xTicksToWait = portMAX_DELAY;

    if (pdTRUE == xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xQueueReceiveFromISR((QueueHandle_t)handle, msg, &xHigherPriorityTaskWoken);
        if (pdPASS == ret && pdTRUE == xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
        if (timeout < TKL_QUEUE_WAIT_FROEVER) {
            xTicksToWait = pdMS_TO_TICKS(timeout);
        }
        ret = xQueueReceive((QueueHandle_t)handle, msg, xTicksToWait);
    }

    if (pdPASS != ret) {
        return OPRT_OS_ADAPTER_QUEUE_RECV_FAIL;
    }

    return OPRT_OK;
}

/**
 * @brief free the message queue
 *
 * @param[in] queue the message queue handle
 *
 * @return void
 */
static void __queue_free(const TKL_QUEUE_HANDLE handle)
{
    if (NULL == handle) {
        return;
    }

    vQueueDelete((QueueHandle_t)handle);
}

/* ---------------------------------------------------------------------------
 * 标准平台 ops 卡：由本文件收编的 TKL 实现组装，core 的 tal_api.c /
 * tal_thread.c / tal_system.c 经 extern 引用并完成 memory/system/semaphore/
 * mutex/thread/queue 六组 tal_* 分发（log output 由应用回调经卡直取；
 * OTA 为回调注入，不经本卡）。
 * --------------------------------------------------------------------------- */
TAL_API_OPS_T tal_api_ops_platform = {
    //! log output
    .log_output = __log_output,
    //! memory
    .malloc = port_system_malloc,
    .free = port_system_free,
    //! system
    .sleep = __system_sleep,
    .reset = __system_reset,
    .get_millisecond = __system_get_millisecond,
    .get_random = __system_get_random,
    //! semaphore
    .semaphore_create_init = __semaphore_create_init,
    .semaphore_wait = __semaphore_wait,
    .semaphore_post = __semaphore_post,
    .semaphore_release = __semaphore_release,
    //! mutex
    .mutex_create_init = port_mutex_create_init,
    .mutex_lock = port_mutex_lock,
    .mutex_unlock = port_mutex_unlock,
    .mutex_release = port_mutex_release,
    //! thread
    .thread_create = __thread_create,
    .thread_release = __thread_release,
    .thread_set_self_name = __thread_set_self_name,
    .thread_is_self = __thread_is_self,
    .thread_get_id = port_thread_get_id,
    //! queue
    .queue_create_init = __queue_create_init,
    .queue_post = __queue_post,
    .queue_fetch = __queue_fetch,
    .queue_free = __queue_free,
};
