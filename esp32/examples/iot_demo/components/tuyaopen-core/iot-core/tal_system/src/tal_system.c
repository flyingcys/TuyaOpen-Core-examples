/**
 * @file tal_system.c
 * @brief Implements system-level functionalities for Tuya IoT applications.
 *
 * This source file provides the implementation of system-level functionalities
 * for Tuya IoT applications, including memory allocation and deallocation. It
 * serves as a wrapper around the Tuya Kernel Layer (TKL) memory management
 * functions, offering a simplified interface for dynamic memory management
 * within the Tuya Abstract Layer (TAL). Additionally, it integrates logging
 * functionalities to report memory allocation failures, aiding in the debugging
 * and monitoring of memory usage.
 *
 * Key functionalities include:
 * - Dynamic memory allocation and deallocation.
 * - Logging of memory allocation failures with current free heap size.
 * - Integration with Tuya's IoT SDK for system-level operations.
 *
 * The implementation aims to provide robust and efficient memory management
 * practices, essential for the reliable operation of IoT devices in
 * resource-constrained environments.
 *
 * @note This file is part of the Tuya IoT Development Platform and is intended
 * for use in Tuya-based applications. It is subject to the platform's license
 * and copyright terms.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_system.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_api_ops.h"
#include "tal_api.h"   /* tal_api_get_ops 声明（随契约迁出后回归 tal_api.h） */

/**
 * @brief Allocates a block of memory of the specified size.
 *
 * This function is used to dynamically allocate memory of the specified size.
 *
 * @param size The size of the memory block to allocate.
 * @return A pointer to the allocated memory block, or NULL if the allocation
 * fails.
 */
void *tal_malloc(size_t size)
{
    if (0 == size) {
        return NULL;
    }

    void *ptr = NULL;
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->malloc) {
        ptr = ops->malloc(size);
    }
    if (NULL == ptr) {
        PR_ERR("0x%x malloc failed:0x%x", __builtin_return_address(0), size);
    }

    return ptr;
}

/**
 * @brief Frees the memory pointed to by the given pointer.
 *
 * This function is used to deallocate memory that was previously allocated
 * using the `malloc` or `calloc` functions. It takes a pointer to the memory
 * block that needs to be freed and releases the memory back to the system.
 *
 * @param ptr Pointer to the memory block to be freed.
 */
void tal_free(void *ptr)
{
    if (NULL == ptr) {
        return;
    }

    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->free) {
        ops->free(ptr);
    }
}

/**
 * Allocates memory for an array of elements, initialized to zero.
 *
 * This function allocates memory for an array of elements, where each element
 * is of size 'size'. The memory is initialized to zero.
 *
 * @param nitems The number of elements to allocate memory for.
 * @param size The size of each element in bytes.
 * @return A pointer to the allocated memory, or NULL if the allocation fails
 *         (including a zero nitems/size or a multiplication overflow).
 */
void *tal_calloc(size_t nitems, size_t size)
{
    /* Overflow guard, matching libc/mbedtls calloc semantics (NULL on
     * overflow). A zero nitems/size makes total 0, and tal_malloc(0) already
     * returns NULL, so the zero-size case needs no separate branch. */
    if (size != 0 && nitems > SIZE_MAX / size) {
        return NULL;
    }

    size_t total = nitems * size;
    void *ptr = tal_malloc(total);
    if (NULL != ptr) {
        memset(ptr, 0, total);
    }

    return ptr;
}

/**
 * @brief Sleeps for the specified amount of time in milliseconds.
 *
 * This function is used to put the system to sleep for the specified amount of
 * time.
 *
 * @param time_ms The time to sleep in milliseconds.
 */
void tal_system_sleep(uint32_t time_ms)
{
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->sleep) {
        ops->sleep(time_ms);
    }
}

/**
 * @brief Resets the TAL system.
 *
 * This function calls the `port_system_reset()` function to reset the TAL
 * system.
 *
 * @note This function should be called when a system reset is required.
 */
void tal_system_reset(void)
{
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->reset) {
        ops->reset();
    }
}

SYS_TIME_T g_sys_time_offset = 0; // used for debug
/**
 * @brief Get the current system time in milliseconds.
 *
 * This function returns the current system time in milliseconds by calling the
 * `port_system_get_millisecond()` function and adding the system time offset.
 *
 * @return The current system time in milliseconds.
 */
SYS_TIME_T tal_system_get_millisecond(void)
{
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->get_millisecond) {
        return ops->get_millisecond() + g_sys_time_offset;
    }

    return g_sys_time_offset;
}

/**
 * @brief Get a random number within the specified range.
 *
 * This function returns a random number within the specified range.
 *
 * @param range The range within which the random number should be generated.
 * @return The generated random number.
 */
int tal_system_get_random(uint32_t range)
{
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->get_random) {
        return ops->get_random(range);
    }

    return 0;
}

