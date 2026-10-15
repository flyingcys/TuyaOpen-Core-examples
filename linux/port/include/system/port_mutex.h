/**
 * @file port_mutex.h
 * @brief port 侧互斥量（port 内部接口）——卡 .mutex_* 的实现本体。
 *
 * 用途：port 内部跨文件使用（port_kv.c 的存储读写锁）。
 * **只准 port 内部调用**：core 侧一律经卡分发（tal_mutex_* → 卡成员），不直调本层
 * 符号（分层约束：port 不向上调用 core，core 只向下经卡调用 port）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __PORT_MUTEX_H__
#define __PORT_MUTEX_H__

#include "tuya_cloud_types.h"
#include "tuya_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *PORT_MUTEX_HANDLE;

OPERATE_RET port_mutex_create_init(PORT_MUTEX_HANDLE *handle);
OPERATE_RET port_mutex_lock(const PORT_MUTEX_HANDLE handle);
OPERATE_RET port_mutex_unlock(const PORT_MUTEX_HANDLE handle);
OPERATE_RET port_mutex_release(const PORT_MUTEX_HANDLE handle);

#ifdef __cplusplus
}
#endif

#endif /* __PORT_MUTEX_H__ */
