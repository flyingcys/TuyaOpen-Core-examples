/**
 * @file tal_kv_ops.h
 * @brief tal_kv 的平台 ops 契约：KV 四接口（init / set / get / del）
 *
 * 定义 TAL_KV_OPS_T：port 在 port_kv.c 组装 tal_kv_ops_platform（成员绑定本平台
 * port_kv_* 实现），core 的 tal_kv.c 经 getter 完成 tal_kv_* 分发，对上签名不变。
 *
 * 存储介质、记录格式、加密、序列化全部是 port 内部细节：换平台 = 换一个
 * port_kv.c（Linux = littlefs + AES；NVS 类平台直接映射芯片 KV），core 零改动。
 *
 * key 是平台相关资源：长度上限与字符集由平台决定（见 tal_kv.h 的 key 契约），
 * port 可对 core 传入的 key 做映射（如超长 key 哈希后再落盘）。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TAL_KV_OPS_H__
#define __TAL_KV_OPS_H__

#include "tuya_cloud_types.h"

#include "tal_kv.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief tal_kv 平台 ops：成员与 port 的 port_kv 实现一一对应
 */
typedef struct {
    /* 挂载介质 / 打开芯片 KV；见 tal_kv.h 的 tal_kv_init */
    OPERATE_RET (*init)(tal_kv_cfg_t *kv_cfg);
    /* 写入键值对；见 tal_kv.h 的 tal_kv_set */
    OPERATE_RET (*set)(const char *key, const uint8_t *value, size_t length);
    /* 读取键值对：成功时 *value 为堆内存（调用者 tal_free 释放）；见 tal_kv.h 的 tal_kv_get */
    OPERATE_RET (*get)(const char *key, uint8_t **value, size_t *length);
    /* 删除键；见 tal_kv.h 的 tal_kv_del */
    OPERATE_RET (*del)(const char *key);
} TAL_KV_OPS_T;

/**
 * @brief Get the platform ops of tal_kv
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_KV_OPS_T *tal_kv_get_ops(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
