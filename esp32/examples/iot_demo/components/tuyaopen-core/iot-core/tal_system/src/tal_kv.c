/**
 * @file tal_kv.c
 * @brief tal_kv 平台分发：四接口透传到 port 的 port_kv（tal_kv_ops_platform）
 *
 * iot-core 不持有任何 KV 逻辑——存储介质、记录格式、加密、序列化都在 port
 * （见 tal_kv_ops.h 契约）。本文件只做 ops 卡分发，与 tal_api.c 的
 * TAL_API_EXEC_OP / TAL_OTA_EXEC_OP 同构。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "tal_kv.h"
#include "tal_kv_ops.h"
#include "tal_log.h"

/* 平台 ops 卡（port 的 port_kv.c 定义），core 侧经 getter 分发 */
extern TAL_KV_OPS_T tal_kv_ops_platform;

TAL_KV_OPS_T *tal_kv_get_ops(void)
{
    return &tal_kv_ops_platform;
}

#define TAL_KV_EXEC_OP(op_name, default_ret, ...)                                                                      \
    do {                                                                                                               \
        TAL_KV_OPS_T *ops = tal_kv_get_ops();                                                                          \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("TAL KV operation %s not available", #op_name);                                                         \
        return default_ret;                                                                                            \
    } while (0)

int tal_kv_init(tal_kv_cfg_t *kv_cfg)
{
    TAL_KV_EXEC_OP(init, OPRT_COM_ERROR, kv_cfg);
}

int tal_kv_set(const char *key, const uint8_t *value, size_t length)
{
    TAL_KV_EXEC_OP(set, OPRT_COM_ERROR, key, value, length);
}

int tal_kv_get(const char *key, uint8_t **value, size_t *length)
{
    /* 契约：失败时 *value 为 NULL、*length 为 0（含 ops 不可用） */
    if (NULL != value) {
        *value = NULL;
    }
    if (NULL != length) {
        *length = 0;
    }
    TAL_KV_EXEC_OP(get, OPRT_COM_ERROR, key, value, length);
}

int tal_kv_del(const char *key)
{
    TAL_KV_EXEC_OP(del, OPRT_COM_ERROR, key);
}
