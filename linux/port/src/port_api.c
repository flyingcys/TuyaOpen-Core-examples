/**
 * @file port_api.c
 * @brief tal_api 平台 ops 卡组装 + OS 原语实现单文件收编（linux port）
 *
 * 原分散于 tkl_semaphore.c / tkl_mutex.c / tkl_thread.c / tkl_queue.c /
 * tkl_memory.c / tkl_output.c / tkl_system.c 的实现收编到本文件，只保留实际
 * 被分发/调用的接口（OTA 为 core 回调注入，不经 port）：
 * - log output / semaphore / thread / queue 实现为 ops 卡私有（static），
 *   适配面收敛到一张卡；
 * - memory 的 malloc/free 为**port 内部接口**（主卡 .malloc/.free/.ex_ram_* 的实现
 *   本体 + port_kv.c 跨文件使用；声明见 include/system/port_memory.h）
 *   ——core 侧只经卡分发，不直调本层符号；
 *   calloc 由 core 的 tal_calloc 用 tal_malloc+memset 实现，卡不再要求适配；
 * - system 的 sleep/reset/get_millisecond/get_random 仅 tal 层经卡分发，转 static；
 *   enter/exit_critical 与 TKL_ENTER/EXIT_CRITICAL 宏均已删（tal 层无消费者）；
 * - mutex 的 create_init/lock/unlock/release 为**port 内部接口**（主卡 .mutex_* 的
 *   实现本体 + port_kv.c 的存储读写锁；声明见 include/system/port_mutex.h）
 *   ——core 侧只经卡分发，不直调本层符号；
 * - 无调用方的接口已删除（tkl_mutex_trylock、
 *   tkl_system_memset/memcpy、tkl_log_close/open、
 *   tkl_cpu_sleep_mode_set、tkl_system_realloc、tkl_system_get_tick_count、
 *   tkl_system_get_free_heap_size、tkl_system_calloc 等）。
 * - thread_get_id 为 ops 卡成员（无条件提供；p2p-core 的 pjproject 经
 *   tal_thread_get_id 分发使用），收口后与 thread 家族一致转 static；
 *   原外部 RAM 分配家族已删除（p2p-core 的 tuya_ipc_skill.c 改用
 *   tal_malloc/tal_free）。
 * 移植新平台时在各自 port 中提供同名 ops 卡与实现即可。
 *
 * 2026-10 port 去 tkl 化：tkl_runtime.c → port_runtime.c，tkl_* 函数去前缀；
 * 原 tkl_semaphore/mutex/thread/queue/memory/system 六个头文件删除——它们只有
 * 本文件一个 includer，其类型/宏/声明随之内联到下面"原 tkl_*.h 内联区"。
 *
 * 2026-10 后续（C2）：port_runtime.c → port_api.c——本文件是 core 的 tal_api
 * 模块（契约 tal_api_ops.h）的平台侧，改名与 port_kv.c / port_netif.c /
 * port_network.c 的 port_<模块>.c 命名线对齐。
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
 * 全部有效内容：句柄类型 + 超时宏；memory 的 malloc/free 前置声明见下
 * （static，供本文件内先于定义的使用点引用），其余实现随定义就近给出。
 * --------------------------------------------------------------------------- */

/* 原 tkl_memory.h：malloc/free 是主卡 TAL_API_OPS_T 的 .malloc/.free/.ex_ram_*
 * 实现本体。**port 内部接口**（port_kv.c 跨文件用，声明见
 * src/port_memory.h）——core 侧只经卡分发，不直调本层符号。 */
#include "port_memory.h"
#include "port_mutex.h"

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

#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#if defined(__linux__) && defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
#include <sys/random.h>
#define TKL_HAS_GETRANDOM 1
#endif

/* ---------------------------------------------------------------------------
 * log output 实现（原 tkl_output.c）：置于卡首位，启动早期即被
 * tal_log 回调与 QR 输出使用。tkl_log_close/tkl_log_open 无调用方，已删。
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
    fflush(stdout);
    return;
}

/* ---------------------------------------------------------------------------
 * memory 实现（原 tkl_memory.c）：malloc/free 为卡 .malloc/.free/.ex_ram_* 的
 * 实现本体，**port 内部接口**（port_kv.c 跨文件用，声明见 include/system/port_memory.h）。
 * 直接映射平台堆（Linux 即 libc malloc/free）——不再使用 core 的 tuya_mem_heap
 * 自管大堆（分层约束：port 不向上依赖 core；代价是失去原 512MB 上限与堆统计）。
 * calloc 不再走卡：core 的 tal_calloc 用 tal_malloc+memset 实现（含乘法溢出检查）。
 * memset/memcpy、无调用方的 realloc/get_free_heap_size 已删。
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
 * system 实现（原 tkl_system.c）：sleep/reset/get_millisecond/get_random 仅
 * tal 层经卡分发，转 static；enter/exit_critical 与 TKL_ENTER/EXIT_CRITICAL
 * 宏均已删（tal 层无消费者，与 esp32 侧一致）。
 * tkl_cpu_sleep_mode_set、tkl_system_get_tick_count、get_reset_reason、
 * get_cpu_info 无调用方，已删。
 * --------------------------------------------------------------------------- */
// Kernel CSPRNG, so the values are not a fixed sequence like unseeded rand().
static int __sys_random_bytes(uint8_t *buf, size_t len)
{
#if defined(TKL_HAS_GETRANDOM)
    size_t done = 0;

    while (done < len) {
        ssize_t got = getrandom(buf + done, len - done, 0);
        if (got <= 0) {
            if (EINTR == errno) {
                continue;
            }
            break;
        }
        done += (size_t)got;
    }

    if (done == len) {
        return 0;
    }
#endif

    FILE *fp = fopen("/dev/urandom", "rb");
    if (NULL == fp) {
        return -1;
    }

    size_t rd = fread(buf, 1, len, fp);
    fclose(fp);

    return (rd == len) ? 0 : -1;
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
    struct timespec time1 = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time1);
    return 1000*((uint64_t)time1.tv_sec) + ((uint64_t)time1.tv_nsec)/1000000;
}

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
    TIME_S sTmpTime;
    TIME_MS msTmpTime;

    sTmpTime = num_ms / 1000;
    msTmpTime = num_ms % 1000;

    if(sTmpTime)
        sleep(sTmpTime);

    if(msTmpTime)
        usleep(msTmpTime*1000);

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
    exit(0);
    return;
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
    uint32_t value = 0;

    if (0 != __sys_random_bytes((uint8_t *)&value, sizeof(value))) {
        // Both kernel sources are gone; seed rand() once so it is at least not a constant.
        static int s_seeded = 0;
        if (!s_seeded) {
            struct timespec ts = {0, 0};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            srand((unsigned int)ts.tv_nsec ^ (unsigned int)getpid());
            s_seeded = 1;
        }
        value = (uint32_t)rand();
    }

    value &= 0x7FFFFFFF; // keep the documented non-negative int contract

    return (0 == range) ? (int)(value & 0xFF) : (int)(value % range);
}

/* ---------------------------------------------------------------------------
 * semaphore 实现（原 tkl_semaphore.c，ops 卡私有）
 * --------------------------------------------------------------------------- */
typedef struct
{
    sem_t sem;
} TKL_SEM_MANAGE, *P_TKL_SEM_MANAGE;

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

    P_TKL_SEM_MANAGE sem_manage;
    sem_manage = (P_TKL_SEM_MANAGE)port_system_malloc(sizeof(TKL_SEM_MANAGE));
    if (sem_manage == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    int ret;
    ret = sem_init(&sem_manage->sem, 0, sem_cnt);
    if (ret != 0) {
        port_system_free(sem_manage);
        *handle = NULL;
        return ret;
    }

    *handle = (TKL_SEM_HANDLE)sem_manage;
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

    P_TKL_SEM_MANAGE sem_manage;
    sem_manage = (P_TKL_SEM_MANAGE)handle;

    int ret;
    if (timeout == TKL_SEM_WAIT_FOREVER) {
        ret = sem_wait(&sem_manage->sem);
    } else {
        struct timespec ts = {0, 0};

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (timeout / 1000);
        ts.tv_nsec += ((timeout % 1000) * 1000000);
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec = ts.tv_nsec % 1000000000;
        }

        while ((ret = sem_timedwait(&sem_manage->sem, &ts)) == -1 && errno == EINTR) {
            continue; /* Restart if interrupted by handler */
        }
    }

    if (0 != ret) {
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

    P_TKL_SEM_MANAGE sem_manage;
    sem_manage = (P_TKL_SEM_MANAGE)handle;

    int ret;
    ret = sem_post(&(sem_manage->sem));
    if (ret != 0) {
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

    P_TKL_SEM_MANAGE sem_manage;
    sem_manage = (P_TKL_SEM_MANAGE)handle;

    int ret;
    ret = sem_destroy(&(sem_manage->sem));
    port_system_free(handle); // 释放信号量管理结构
    if (ret != 0) {
        return OPRT_OS_ADAPTER_SEM_RELEASE_FAILED;
    }

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * mutex 实现（原 tkl_mutex.c）：**port 内部接口**（卡 .mutex_* 的实现本体 +
 * port_kv.c 的存储读写锁，声明见 include/system/port_mutex.h）。core 侧
 * （tuya_queue 等）经 tal_mutex_* 走主卡分发，不直调本层符号
 * --------------------------------------------------------------------------- */
typedef pthread_mutex_t TKL_THRD_MUTEX;
typedef struct
{
    TKL_THRD_MUTEX mutex;
} TKL_MUTEX_MANAGE, *P_TKL_MUTEX_MANAGE;

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
    if (!handle)
        return OPRT_INVALID_PARM;

    P_TKL_MUTEX_MANAGE mutex_manage;
    mutex_manage = (P_TKL_MUTEX_MANAGE)port_system_malloc(sizeof(TKL_MUTEX_MANAGE));
    if (!(mutex_manage))
        return OPRT_MALLOC_FAILED;

    int ret;
    pthread_mutexattr_t attr;

    ret = pthread_mutexattr_init(&attr);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED;
    }

    ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED;
    }

    ret = pthread_mutex_init(&(mutex_manage->mutex), &attr);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED;
    }

    ret = pthread_mutexattr_destroy(&attr);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_MUTEX_RELEASE_FAILED;
    }

    *handle = (TKL_MUTEX_HANDLE)mutex_manage;

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
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    P_TKL_MUTEX_MANAGE mutex_manage;
    mutex_manage = (P_TKL_MUTEX_MANAGE)handle;

    int ret;
    ret = pthread_mutex_lock(&(mutex_manage->mutex));
    if (ret != 0) {
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
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    P_TKL_MUTEX_MANAGE mutex_manage;
    mutex_manage = (P_TKL_MUTEX_MANAGE)handle;

    int ret;
    ret = pthread_mutex_unlock(&(mutex_manage->mutex));
    if (ret != 0) {
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

    P_TKL_MUTEX_MANAGE mutex_manage;
    mutex_manage = (P_TKL_MUTEX_MANAGE)handle;

    int ret;
    ret = pthread_mutex_destroy(&(mutex_manage->mutex));
    if (ret != 0) {
        return OPRT_OS_ADAPTER_MUTEX_RELEASE_FAILED;
    }

    port_system_free(handle);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * thread 实现（原 tkl_thread.c，ops 卡私有）
 * --------------------------------------------------------------------------- */
typedef struct {
    pthread_t id;
    THREAD_FUNC_T func;
    void *arg;
} THREAD_DATA;

static void *__thread_wrap_func(void *arg)
{
    THREAD_DATA *thread_data = (THREAD_DATA *)arg;
    if (thread_data && thread_data->func) {
        thread_data->func(thread_data->arg);
    }

    return NULL;
}

/**
* @brief Create thread
*
* @param[out] thread: thread handle
* @param[in] name: thread name
* @param[in] stack_size: stack size of thread
* @param[in] priority: priority of thread
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
    if (!thread) {
        return OPRT_INVALID_PARM;
    }

    int ret = 0;
    THREAD_DATA *thread_data = (THREAD_DATA *)port_system_malloc(sizeof(THREAD_DATA));
    if (thread_data == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    memset(thread_data, 0, sizeof(THREAD_DATA));
    thread_data->id = 0;
    thread_data->func = func;
    thread_data->arg = arg;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    ret = pthread_create(&(thread_data->id), &attr, __thread_wrap_func, thread_data);
    pthread_attr_destroy(&attr);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_THRD_CREAT_FAILED;
    }

    *thread = (TKL_THREAD_HANDLE)thread_data;
    return ret;
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

    THREAD_DATA *thread_data = (THREAD_DATA *)thread;
    port_system_free(thread_data);

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

    prctl(PR_SET_NAME, name);
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

    THREAD_DATA *thread_data = (THREAD_DATA *)thread;

    *is_self = pthread_equal(thread_data->id, pthread_self());
    return OPRT_OK;
}

/**
* @brief Get the handle of the calling thread
*
* @param[out] thread: thread handle
*
* @note p2p-core 的 pjproject（os_core_tuyaos.c）经 tal_thread_get_id 用它做
*       TLS 行匹配，只要求 "同一线程恒得同一值"的不透明身份，不要求与
*       __thread_create 的 handle（本 port 内为 THREAD_DATA*，仅 is_self
*       消费）同类。Linux 下直接返回 pthread_self()。卡成员，无条件提供。
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_get_id(TKL_THREAD_HANDLE *thread)
{
    if (NULL == thread) {
        return OPRT_INVALID_PARM;
    }

    *thread = (TKL_THREAD_HANDLE)pthread_self();
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * queue 实现（原 tkl_queue.c，ops 卡私有）
 * --------------------------------------------------------------------------- */
typedef struct rpa_queue_t {
    void **data;
    volatile uint32_t nelts; /**< # elements */
    uint32_t in;             /**< next empty location */
    uint32_t out;            /**< next filled location */
    uint32_t bounds;         /**< max size of queue */
    uint32_t full_waiters;
    uint32_t empty_waiters;
    pthread_mutex_t *one_big_mutex;
    pthread_cond_t *not_empty;
    pthread_cond_t *not_full;
    int terminated;
} rpa_queue_t;

#define RPA_WAIT_NONE 0
#define RPA_WAIT_FOREVER -1

#define rpa_queue_full(queue) ((queue)->nelts == (queue)->bounds)
#define rpa_queue_empty(queue) ((queue)->nelts == 0)

static void set_timeout(struct timespec *abstime, int wait_ms)
{
    clock_gettime(CLOCK_REALTIME, abstime);
    /* add seconds */
    abstime->tv_sec += (wait_ms / 1000);
    /* add and carry microseconds */
    long ms = abstime->tv_nsec / 1000000L;
    ms += wait_ms % 1000;
    while (ms > 1000) {
        ms -= 1000;
        abstime->tv_sec += 1;
    }
    abstime->tv_nsec = ms * 1000000L;
}

static void rpa_queue_destroy(rpa_queue_t *queue)
{
    /* Ignore errors here, we can't do anything about them anyway. */
    pthread_cond_destroy(queue->not_empty);
    pthread_cond_destroy(queue->not_full);
    pthread_mutex_destroy(queue->one_big_mutex);
}

static BOOL_T rpa_queue_create(rpa_queue_t **q, uint32_t queue_capacity)
{
    rpa_queue_t *queue;
    queue = malloc(sizeof(rpa_queue_t));
    if (!queue) {
        return false;
    }
    *q = queue;
    memset(queue, 0, sizeof(rpa_queue_t));

    if (!(queue->one_big_mutex = malloc(sizeof(pthread_mutex_t))))
        return false;
    if (!(queue->not_empty = malloc(sizeof(pthread_cond_t))))
        return false;
    if (!(queue->not_full = malloc(sizeof(pthread_cond_t))))
        return false;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int rv = pthread_mutex_init(queue->one_big_mutex, &attr);
    if (rv != 0) {
        goto error;
    }

    rv = pthread_cond_init(queue->not_empty, NULL);
    if (rv != 0) {
        goto error;
    }

    rv = pthread_cond_init(queue->not_full, NULL);
    if (rv != 0) {
        goto error;
    }

    /* Set all the data in the queue to NULL */
    queue->data = malloc(queue_capacity * sizeof(void *));
    queue->bounds = queue_capacity;
    queue->nelts = 0;
    queue->in = 0;
    queue->out = 0;
    queue->terminated = 0;
    queue->full_waiters = 0;
    queue->empty_waiters = 0;

    return true;

error:
    free(queue);
    return false;
}

static BOOL_T rpa_queue_trypush(rpa_queue_t *queue, void *data)
{
    BOOL_T rv;

    if (queue->terminated) {
        return false; /* no more elements ever again */
    }

    rv = pthread_mutex_lock(queue->one_big_mutex);
    if (rv != 0) {
        return false;
    }

    if (rpa_queue_full(queue)) {
        rv = pthread_mutex_unlock(queue->one_big_mutex);
        return false; // EAGAIN;
    }

    queue->data[queue->in] = data;
    queue->in++;
    if (queue->in >= queue->bounds) {
        queue->in -= queue->bounds;
    }
    queue->nelts++;

    if (queue->empty_waiters) {
        rv = pthread_cond_signal(queue->not_empty);
        if (rv != 0) {
            pthread_mutex_unlock(queue->one_big_mutex);
            return false;
        }
    }

    pthread_mutex_unlock(queue->one_big_mutex);
    return true;
}

static BOOL_T rpa_queue_timedpush(rpa_queue_t *queue, void *data, int wait_ms)
{
    BOOL_T rv;

    if (wait_ms == RPA_WAIT_NONE)
        return rpa_queue_trypush(queue, data);

    if (queue->terminated) {
        return false; /* no more elements ever again */
    }

    rv = pthread_mutex_lock(queue->one_big_mutex);
    if (rv != 0) {
        return false;
    }

    if (rpa_queue_full(queue)) {
        if (!queue->terminated) {
            queue->full_waiters++;
            if (wait_ms == RPA_WAIT_FOREVER) {
                rv = pthread_cond_wait(queue->not_full, queue->one_big_mutex);
            } else {
                struct timespec abstime;
                set_timeout(&abstime, wait_ms);
                rv = pthread_cond_timedwait(queue->not_full, queue->one_big_mutex, &abstime);
            }
            queue->full_waiters--;
            if (rv != 0) {
                pthread_mutex_unlock(queue->one_big_mutex);
                return false;
            }
        }
        /* If we wake up and it's still full, then we were interrupted */
        if (rpa_queue_full(queue)) {
            rv = pthread_mutex_unlock(queue->one_big_mutex);
            if (rv != 0) {
                return false;
            }
            if (queue->terminated) {
                return false; /* no more elements ever again */
            } else {
                return false; // EINTR;
            }
        }
    }

    queue->data[queue->in] = data;
    queue->in++;
    if (queue->in >= queue->bounds) {
        queue->in -= queue->bounds;
    }
    queue->nelts++;

    if (queue->empty_waiters) {
        rv = pthread_cond_signal(queue->not_empty);
        if (rv != 0) {
            pthread_mutex_unlock(queue->one_big_mutex);
            return false;
        }
    }

    pthread_mutex_unlock(queue->one_big_mutex);
    return true;
}

static BOOL_T rpa_queue_trypop(rpa_queue_t *queue, void **data)
{
    BOOL_T rv;

    if (queue->terminated) {
        return false; /* no more elements ever again */
    }

    rv = pthread_mutex_lock(queue->one_big_mutex);
    if (rv != 0) {
        return false;
    }

    if (rpa_queue_empty(queue)) {
        rv = pthread_mutex_unlock(queue->one_big_mutex);
        return false; // EAGAIN;
    }

    *data = queue->data[queue->out];
    queue->nelts--;

    queue->out++;
    if (queue->out >= queue->bounds) {
        queue->out -= queue->bounds;
    }
    if (queue->full_waiters) {
        rv = pthread_cond_signal(queue->not_full);
        if (rv != 0) {
            pthread_mutex_unlock(queue->one_big_mutex);
            return false;
        }
    }

    pthread_mutex_unlock(queue->one_big_mutex);
    return true;
}

static BOOL_T rpa_queue_timedpop(rpa_queue_t *queue, void **data, int wait_ms)
{
    BOOL_T rv;

    if (wait_ms == RPA_WAIT_NONE)
        return rpa_queue_trypop(queue, data);

    if (queue->terminated) {
        return false; /* no more elements ever again */
    }

    rv = pthread_mutex_lock(queue->one_big_mutex);
    if (rv != 0) {
        return false;
    }

    /* Keep waiting until we wake up and find that the queue is not empty. */
    if (rpa_queue_empty(queue)) {
        if (!queue->terminated) {
            queue->empty_waiters++;
            if (wait_ms == RPA_WAIT_FOREVER) {
                rv = pthread_cond_wait(queue->not_empty, queue->one_big_mutex);
            } else {
                struct timespec abstime;
                set_timeout(&abstime, wait_ms);
                rv = pthread_cond_timedwait(queue->not_empty, queue->one_big_mutex, &abstime);
            }
            queue->empty_waiters--;
            if (rv != 0) {
                pthread_mutex_unlock(queue->one_big_mutex);
                return false;
            }
        }
        /* If we wake up and it's still empty, then we were interrupted */
        if (rpa_queue_empty(queue)) {
            rv = pthread_mutex_unlock(queue->one_big_mutex);
            if (rv != 0) {
                return false;
            }
            if (queue->terminated) {
                return false; /* no more elements ever again */
            } else {
                return false; // EINTR;
            }
        }
    }

    *data = queue->data[queue->out];
    queue->nelts--;

    queue->out++;
    if (queue->out >= queue->bounds) {
        queue->out -= queue->bounds;
    }
    if (queue->full_waiters) {
        rv = pthread_cond_signal(queue->not_full);
        if (rv != 0) {
            pthread_mutex_unlock(queue->one_big_mutex);
            return false;
        }
    }

    pthread_mutex_unlock(queue->one_big_mutex);
    return true;
}

typedef struct {
    rpa_queue_t *queue;
    int msgsize;
} TKL_QUEUE_T;

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
    TKL_QUEUE_T *queue = NULL;

    if ((NULL == handle) || (0 == msgsize) || (0 == msgcount)) {
        return OPRT_INVALID_PARM;
    }

    queue = (TKL_QUEUE_T *)malloc(sizeof(TKL_QUEUE_T));
    if (!queue) {
        return OPRT_MALLOC_FAILED;
    }

    if (!rpa_queue_create(&queue->queue, msgcount)) {
        return OPRT_OS_ADAPTER_QUEUE_CREAT_FAILED;
    }
    queue->msgsize = msgsize;

    *handle = (TKL_QUEUE_HANDLE)queue;

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

    TKL_QUEUE_T *queue = (TKL_QUEUE_T *)handle;
    int wait_ms = 0;

    void *buf = (void *)malloc(queue->msgsize);
    if (NULL == buf) {
        return OPRT_MALLOC_FAILED;
    }

    memcpy(buf, (void *)data, queue->msgsize);

    if (timeout == TKL_QUEUE_WAIT_FROEVER) {
        wait_ms = RPA_WAIT_FOREVER;
    } else {
        wait_ms = timeout;
    }

    if (!rpa_queue_timedpush(queue->queue, buf, wait_ms)) {
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

    TKL_QUEUE_T *queue = (TKL_QUEUE_T *)handle;
    void *buf = NULL;
    int wait_ms;

    if (timeout == TKL_QUEUE_WAIT_FROEVER) {
        wait_ms = RPA_WAIT_FOREVER;
    } else {
        wait_ms = timeout;
    }

    if (!rpa_queue_timedpop(queue->queue, (void **)&buf, wait_ms)) {
        return OPRT_OS_ADAPTER_QUEUE_RECV_FAIL;
    }

    if (buf) {
        memcpy((void *)msg, buf, queue->msgsize);
        free(buf);
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

    TKL_QUEUE_T *queue = (TKL_QUEUE_T *)handle;
    rpa_queue_destroy(queue->queue);
    free(queue);
}

/* ---------------------------------------------------------------------------
 * 标准平台 ops 卡：由本文件收编的 TKL 实现组装，core 的 tal_api.c /
 * tal_thread.c / tal_system.c 经 extern 引用并完成 memory/system/semaphore/
 * mutex/thread/queue 六组 tal_* 分发（log output 由应用回调经卡直取；
 * OTA 为回调注入，不经本卡）。
 * 移植新平台时在各自 port 中提供同名卡即可。
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
    .thread_get_id = __thread_get_id,
    //! queue
    .queue_create_init = __queue_create_init,
    .queue_post = __queue_post,
    .queue_fetch = __queue_fetch,
    .queue_free = __queue_free,
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    //! ex ram（Linux 无独立外部 RAM：退化为普通内存）
    .ex_ram_malloc = port_system_malloc,
    .ex_ram_free = port_system_free,
#endif
};
