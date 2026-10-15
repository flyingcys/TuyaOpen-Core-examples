/**
 * @file port_kv.c
 * @brief tal_kv 平台 ops 卡：esp32 用 NVS（nvs_flash）做键值存储
 *
 * 与 linux 版（littlefs + AES-128-CBC）对应：NVS 类平台直接映射芯片 KV，
 * 存储介质、记录格式全部是 port 内部细节（见 tal_kv_ops.h 说明），core 零改动。
 *
 * key 映射：NVS key 上限 15 字符，而 core 的 key 长度不受限（实测
 * "uuid3dfa17df5ff96fc6.devid" 26 字符、"device_timer_tasks" 18 字符、
 * 云端下发 schemaId 长度不受控）。超长 key 折叠为 "h" + 14 个 hex 字符
 * （FNV-1a 64 取低 56 bit）——用途仅为 NVS key 的命名空间映射（非安全
 * 用途），单设备 key 量级（<100）下碰撞概率可忽略。长度 ≤15 的 key 原样
 * 使用（保留可读性，便于调试）。
 *
 * 约定：nvs_flash_init() 由应用（main.c）负责，本卡只开 namespace；
 * 未 init 时 set/get/del 拒绝（不触碰 NVS 句柄）。NVS 自身线程安全，
 * 不再加 port 层互斥。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_kv_ops.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"

#define KV_NVS_NAMESPACE "tuya_kv"
/* NVS key 上限 15 字符（不含结尾 '\0'，见 nvs_flash 文档） */
#define KV_NVS_KEY_MAX 15

static nvs_handle_t s_kv_handle = 0;
static BOOL_T s_kv_opened = FALSE;

/* FNV-1a 64-bit（offset basis / prime 为算法标准常量） */
#define KV_FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define KV_FNV_PRIME        0x100000001b3ULL

/**
 * @brief 把 core 的 key 映射为合法的 NVS key（≤15 字符）
 *
 * @param[in] key: core 传入的 key（非 NULL）
 * @param[out] out: 输出缓冲（至少 KV_NVS_KEY_MAX + 1 字节）
 */
static void __kv_map_key(const char *key, char out[KV_NVS_KEY_MAX + 1])
{
    if (strlen(key) <= KV_NVS_KEY_MAX) {
        snprintf(out, KV_NVS_KEY_MAX + 1, "%s", key);
        return;
    }

    uint64_t hash = KV_FNV_OFFSET_BASIS;
    const uint8_t *p = (const uint8_t *)key;

    while (*p) {
        hash ^= (uint64_t)(*p++);
        hash *= KV_FNV_PRIME;
    }

    /* 'h' 前缀区分映射 key 与原始 key；取低 56 bit → 14 个 hex 字符 */
    snprintf(out, KV_NVS_KEY_MAX + 1, "h%014llx", (unsigned long long)(hash & 0x00FFFFFFFFFFFFFFULL));
}

/**
 * @brief 初始化 KV 存储（打开 NVS namespace）
 *
 * @note seed/key 为 linux 版（littlefs+AES）的加密参数；NVS 平台不做应用层
 *       加密，忽略。重复 init 幂等。
 *
 * @param kv_cfg 配置（不可为 NULL）
 * @return OPRT_OK 成功，负值为错误码
 */
static OPERATE_RET __kv_init(tal_kv_cfg_t *kv_cfg)
{
    if (NULL == kv_cfg) {
        return OPRT_INVALID_PARM;
    }

    if (TRUE == s_kv_opened) {
        return OPRT_OK;
    }

    esp_err_t err = nvs_open(KV_NVS_NAMESPACE, NVS_READWRITE, &s_kv_handle);
    if (ESP_OK != err) {
        PR_ERR("kv nvs_open failed: %d (nvs_flash_init called first?)", err);
        return OPRT_COM_ERROR;
    }

    s_kv_opened = TRUE;
    return OPRT_OK;
}

static OPERATE_RET __kv_set(const char *key, const uint8_t *value, size_t length)
{
    char nvs_key[KV_NVS_KEY_MAX + 1];

    if (NULL == key || NULL == value || 0 == length) {
        return OPRT_INVALID_PARM;
    }
    if (FALSE == s_kv_opened) {
        PR_ERR("kv not init, set %s rejected", key);
        return OPRT_COM_ERROR;
    }

    __kv_map_key(key, nvs_key);

    esp_err_t err = nvs_set_blob(s_kv_handle, nvs_key, value, length);
    if (ESP_OK != err) {
        PR_ERR("kv set %s failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    err = nvs_commit(s_kv_handle);
    if (ESP_OK != err) {
        PR_ERR("kv set %s commit failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

static OPERATE_RET __kv_get(const char *key, uint8_t **value, size_t *length)
{
    char nvs_key[KV_NVS_KEY_MAX + 1];
    size_t len = 0;

    /* 契约（tal_kv.h）：失败时 *value 置 NULL、*length 置 0。先于参数校验
     * 归零，保证 port 被直接调用（不经济传的 tal_kv.c）也守住承诺 */
    if (NULL != value) {
        *value = NULL;
    }
    if (NULL != length) {
        *length = 0;
    }

    if (NULL == key || NULL == value || NULL == length) {
        return OPRT_INVALID_PARM;
    }
    if (FALSE == s_kv_opened) {
        PR_ERR("kv not init, get %s rejected", key);
        return OPRT_COM_ERROR;
    }

    __kv_map_key(key, nvs_key);

    esp_err_t err = nvs_get_blob(s_kv_handle, nvs_key, NULL, &len);
    if (ESP_ERR_NVS_NOT_FOUND == err) {
        return OPRT_NOT_FOUND;
    }
    if (ESP_OK != err) {
        PR_ERR("kv get %s failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    /* 契约：返回缓冲含结尾 '\0'（长度不含 '\0'），调用者 tal_free 释放
     * （tal_malloc/tal_free 同源：都经 ops 卡分发到本 port 的 memory 实现） */
    uint8_t *buf = (uint8_t *)tal_malloc(len + 1);
    if (NULL == buf) {
        return OPRT_MALLOC_FAILED;
    }

    err = nvs_get_blob(s_kv_handle, nvs_key, buf, &len);
    if (ESP_OK != err) {
        PR_ERR("kv get %s read failed: %d", key, err);
        tal_free(buf);
        return OPRT_COM_ERROR;
    }
    buf[len] = '\0';

    *value = buf;
    *length = len;
    return OPRT_OK;
}

static OPERATE_RET __kv_del(const char *key)
{
    char nvs_key[KV_NVS_KEY_MAX + 1];

    if (NULL == key) {
        return OPRT_INVALID_PARM;
    }
    if (FALSE == s_kv_opened) {
        PR_ERR("kv not init, del %s rejected", key);
        return OPRT_COM_ERROR;
    }

    __kv_map_key(key, nvs_key);

    esp_err_t err = nvs_erase_key(s_kv_handle, nvs_key);
    if (ESP_ERR_NVS_NOT_FOUND == err) {
        return OPRT_OK; /* 契约：键不存在视为成功 */
    }
    if (ESP_OK != err) {
        PR_ERR("kv del %s failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    err = nvs_commit(s_kv_handle);
    if (ESP_OK != err) {
        PR_ERR("kv del %s commit failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * 平台 ops 卡：由本文件组装，core 的 tal_kv.c 经 extern 引用完成分发。
 * --------------------------------------------------------------------------- */
TAL_KV_OPS_T tal_kv_ops_platform = {
    .init = __kv_init,
    .set = __kv_set,
    .get = __kv_get,
    .del = __kv_del,
};
