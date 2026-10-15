/**
 * @file port_memory.h
 * @brief port 侧内存分配器（port 内部接口）——卡 .malloc/.free 的实现本体。
 *
 * 用途：port 内部跨文件使用（port_kv.c 的加解密缓冲、port_crypto.c 的分配）。
 * **只准 port 内部调用**：core 侧一律经卡分发（tal_malloc/tal_free → 卡成员），
 * 不直调本层符号（分层约束：port 不向上调用 core，core 只向下经卡调用 port）。
 * 分配与释放必须同源：本分配器即卡 .free 的实现，故调用方用 tal_free 释放本层
 * 分配的缓冲是配对的（二者最终都落到本文件实现）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __PORT_MEMORY_H__
#define __PORT_MEMORY_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void *port_system_malloc(SIZE_T size);
void  port_system_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* __PORT_MEMORY_H__ */
