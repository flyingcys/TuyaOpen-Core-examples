/**
 * @file tkl_kv.c
 * @brief tal_kv 平台 ops 卡：Linux 用 littlefs + AES-128-CBC 做加密键值存储
 *
 * 由旧 third_party/tal_kv/src/tal_kv.c（tuyaopen-core 8fa1d6a^）改造：删掉
 * ENABLE_FILE_SYSTEM 分支（只留 littlefs）、serialize（kv_serialize.c）、
 * CLI（tal_kv_cmd）与 tal_lfs_get；kv_storage_* 从 tal_fs 门面改为直调 lfs API。
 *
 * 介质：tkl_flash 的 TUYA_FLASH_TYPE_UF 分区（Linux 上由 tkl_flash.c 映射到
 * ./tuyadb/tuyadb 文件）挂 littlefs；每条记录 = 明文经 AES-128-CBC 加密后的密文，
 * IV/密钥在 init 时由 seed/key 各经 SHA256 派生 16 字节，读回时逐字节校验 PKCS7
 * 填充。写/读/删全程持 s_kv_mutex（tal_mutex），加解密在锁外做。
 *
 * 平台 ops 卡：tal_kv_ops_platform 由本文件组装，core 的 tal_kv.c 经 extern 引用。
 * 移植其它平台时替换本文件（NVS 类平台直接映射芯片 KV 即可）。
 *
 * 注意：lfs_util.h 经 -DLFS_CONFIG=lfs_config.h（CMake 注入）取 lfs_config.h，
 * 内存/日志走 tal_malloc / PR_ERR。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_kv_ops.h"

#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_security.h"

#include "lfs.h"
#include "tkl_flash.h"

static lfs_t s_lfs;
static lfs_size_t s_lfs_flash_addr;

static tal_kv_cfg_t s_lfs_kv_cfg;
static MUTEX_HANDLE s_lfs_mutex;
static BOOL_T s_lfs_mounted = FALSE;

static int user_provided_block_device_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
                                           lfs_size_t size)
{
    OPERATE_RET ret = tkl_flash_read(s_lfs_flash_addr + c->block_size * block + off, buffer, size);
    return (OPRT_OK == ret) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int user_provided_block_device_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                                           const void *buffer, lfs_size_t size)
{
    OPERATE_RET ret = tkl_flash_write(s_lfs_flash_addr + c->block_size * block + off, buffer, size);
    return (OPRT_OK == ret) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int user_provided_block_device_erase(const struct lfs_config *c, lfs_block_t block)
{
    OPERATE_RET ret = tkl_flash_erase(s_lfs_flash_addr + c->block_size * block, c->block_size);
    return (OPRT_OK == ret) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int user_provided_block_device_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

/* Storage helpers: callers hold s_lfs_mutex for read, write, and remove. */
static OPERATE_RET kv_storage_init(void)
{
    TUYA_FLASH_BASE_INFO_T info;
    /* lfs 内部持有 cfg 指针（lfs->cfg），配置结构须比挂载活得久 */
    static struct lfs_config lfs_cfg;
    int result;

    if (OPRT_OK != tkl_flash_get_one_type_info(TUYA_FLASH_TYPE_UF, &info) || 0 == info.partition[0].block_size) {
        return OPRT_COM_ERROR;
    }

    s_lfs_flash_addr = info.partition[0].start_addr;
    memset(&lfs_cfg, 0, sizeof(lfs_cfg));
    lfs_cfg.read = user_provided_block_device_read;
    lfs_cfg.prog = user_provided_block_device_prog;
    lfs_cfg.erase = user_provided_block_device_erase;
    lfs_cfg.sync = user_provided_block_device_sync;
    lfs_cfg.read_size = info.partition[0].block_size;
    lfs_cfg.prog_size = info.partition[0].block_size;
    lfs_cfg.block_size = info.partition[0].block_size;
    lfs_cfg.block_count = info.partition[0].size / info.partition[0].block_size;
    lfs_cfg.cache_size = info.partition[0].block_size;
    lfs_cfg.lookahead_size = lfs_cfg.block_count / 8 + (8 - (lfs_cfg.block_count / 8));
    lfs_cfg.block_cycles = 500;

    result = lfs_mount(&s_lfs, &lfs_cfg);
    if (LFS_ERR_OK != result) {
        result = lfs_format(&s_lfs, &lfs_cfg);
        if (LFS_ERR_OK == result) {
            result = lfs_mount(&s_lfs, &lfs_cfg);
        }
    }

    return (LFS_ERR_OK == result) ? OPRT_OK : OPRT_COM_ERROR;
}

static OPERATE_RET kv_storage_write(const char *key, const uint8_t *data, uint32_t length)
{
    lfs_file_t file;
    int result;
    int sync_result;
    int close_result;

    result = lfs_file_open(&s_lfs, &file, key, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (LFS_ERR_OK != result) {
        PR_ERR("lfs open %s failed: %d", key, result);
        return OPRT_FILE_OPEN_FAILED;
    }

    result = lfs_file_write(&s_lfs, &file, data, length);
    sync_result = lfs_file_sync(&s_lfs, &file);
    close_result = lfs_file_close(&s_lfs, &file);
    if (result != (int)length || LFS_ERR_OK != sync_result || LFS_ERR_OK != close_result) {
        PR_ERR("kv write failed write:%d sync:%d close:%d", result, sync_result, close_result);
        return OPRT_KVS_WR_FAIL;
    }

    return OPRT_OK;
}

static OPERATE_RET kv_storage_read(const char *key, uint8_t **data, uint32_t *length)
{
    lfs_file_t file;
    lfs_soff_t stored_length;
    int result;
    int close_result;

    result = lfs_file_open(&s_lfs, &file, key, LFS_O_RDONLY);
    if (LFS_ERR_OK != result) {
        return (LFS_ERR_NOENT == result) ? OPRT_NOT_FOUND : OPRT_FILE_OPEN_FAILED;
    }

    stored_length = lfs_file_size(&s_lfs, &file);
    if (stored_length < 0) {
        lfs_file_close(&s_lfs, &file);
        return OPRT_KVS_RD_FAIL;
    }
    if (0 == stored_length) {
        lfs_file_close(&s_lfs, &file);
        return OPRT_NOT_FOUND;
    }
    if (stored_length % 16 != 0) {
        PR_ERR("invalid encrypted KV length %d for %s", (int)stored_length, key);
        lfs_file_close(&s_lfs, &file);
        return OPRT_KVS_RD_FAIL;
    }

    *data = tal_malloc((size_t)stored_length + 1U);
    if (NULL == *data) {
        lfs_file_close(&s_lfs, &file);
        return OPRT_MALLOC_FAILED;
    }

    result = lfs_file_read(&s_lfs, &file, *data, (lfs_size_t)stored_length);
    close_result = lfs_file_close(&s_lfs, &file);
    if (result != (int)stored_length || LFS_ERR_OK != close_result) {
        PR_ERR("kv read failed read:%d close:%d", result, close_result);
        tal_free(*data);
        *data = NULL;
        return OPRT_KVS_RD_FAIL;
    }

    *length = (uint32_t)stored_length;
    return OPRT_OK;
}

static OPERATE_RET kv_storage_remove(const char *key)
{
    int result = lfs_remove(&s_lfs, key);

    /* 契约（tal_kv.h）：键不存在视为成功 */
    return (LFS_ERR_OK == result || LFS_ERR_NOENT == result) ? OPRT_OK : OPRT_COM_ERROR;
}

static OPERATE_RET kv_encrypt(const uint8_t *value, size_t length, uint8_t **encrypted, uint32_t *encrypted_length)
{
    uint8_t iv[16];

    if (length > UINT32_MAX - 16U) {
        return OPRT_INVALID_PARM;
    }

    memcpy(iv, s_lfs_kv_cfg.seed, sizeof(iv));
    return tal_aes128_cbc_encode((uint8_t *)value, (uint32_t)length, (uint8_t *)s_lfs_kv_cfg.key, iv, encrypted,
                                 encrypted_length);
}

static OPERATE_RET kv_decrypt(uint8_t *encrypted, uint32_t encrypted_length, uint8_t **value, size_t *value_length)
{
    uint8_t iv[16];
    uint8_t *decrypted = NULL;
    uint32_t decrypted_length = 0;
    uint8_t padding_length;
    uint32_t i;
    OPERATE_RET result;

    if (0 == encrypted_length || encrypted_length % 16U != 0U) {
        return OPRT_KVS_RD_FAIL;
    }

    memcpy(iv, s_lfs_kv_cfg.seed, sizeof(iv));
    result = tal_aes128_cbc_decode(encrypted, encrypted_length, (uint8_t *)s_lfs_kv_cfg.key, iv, &decrypted,
                                   &decrypted_length);
    if (OPRT_OK != result) {
        return result;
    }

    if (0 == decrypted_length || decrypted_length % 16U != 0U) {
        tal_aes_free_data(decrypted);
        return OPRT_KVS_RD_FAIL;
    }

    padding_length = decrypted[decrypted_length - 1U];
    if (0 == padding_length || padding_length > 16U || padding_length >= decrypted_length) {
        tal_aes_free_data(decrypted);
        return OPRT_KVS_RD_FAIL;
    }
    for (i = 0; i < padding_length; ++i) {
        if (decrypted[decrypted_length - 1U - i] != padding_length) {
            tal_aes_free_data(decrypted);
            return OPRT_KVS_RD_FAIL;
        }
    }

    *value_length = decrypted_length - padding_length;
    decrypted[*value_length] = 0;
    *value = decrypted;
    return OPRT_OK;
}

static OPERATE_RET tkl_kv_init(tal_kv_cfg_t *kv_cfg)
{
    uint8_t sha256_ret[32];
    OPERATE_RET result;

    if (NULL == kv_cfg) {
        return OPRT_INVALID_PARM;
    }

    /* 密钥派生：每次 init 都按传入配置刷新（旧实现语义） */
    memset(&s_lfs_kv_cfg, 0, sizeof(s_lfs_kv_cfg));
    tal_sha256_ret((const uint8_t *)kv_cfg->seed, TAL_LV_KEY_LEN, sha256_ret, 0);
    memcpy(s_lfs_kv_cfg.seed, sha256_ret, TAL_LV_KEY_LEN);
    tal_sha256_ret((const uint8_t *)kv_cfg->key, TAL_LV_KEY_LEN, sha256_ret, 0);
    memcpy(s_lfs_kv_cfg.key, sha256_ret, TAL_LV_KEY_LEN);

    if (NULL == s_lfs_mutex) {
        result = tal_mutex_create_init(&s_lfs_mutex);
        if (OPRT_OK != result) {
            return result;
        }
    }

    /* 重复 init 幂等：介质已挂载就不再 mount（旧实现重复 mount 会泄漏 lfs 缓冲，
     * 且 mount 失败路径上的 format 会把已有数据抹掉） */
    if (TRUE == s_lfs_mounted) {
        return OPRT_OK;
    }

    result = kv_storage_init();
    if (OPRT_OK != result) {
        return result;
    }
    s_lfs_mounted = TRUE;

    return OPRT_OK;
}

static OPERATE_RET tkl_kv_set(const char *key, const uint8_t *value, size_t length)
{
    uint8_t *encrypted = NULL;
    uint32_t encrypted_length = 0;
    OPERATE_RET result;

    if (NULL == key || NULL == value || 0 == length) {
        return OPRT_INVALID_PARM;
    }

    PR_DEBUG("key:%s, len %u", key, (unsigned int)length);
    result = kv_encrypt(value, length, &encrypted, &encrypted_length);
    if (OPRT_OK != result) {
        PR_ERR("key %s encrypt failed: %d", key, result);
        return result;
    }

    tal_mutex_lock(s_lfs_mutex);
    result = kv_storage_write(key, encrypted, encrypted_length);
    tal_mutex_unlock(s_lfs_mutex);
    tal_aes_free_data(encrypted);
    return result;
}

static OPERATE_RET tkl_kv_get(const char *key, uint8_t **value, size_t *length)
{
    uint8_t *encrypted = NULL;
    uint32_t encrypted_length = 0;
    OPERATE_RET result;

    if (NULL == key || NULL == value || NULL == length) {
        return OPRT_INVALID_PARM;
    }
    /* 契约（tal_kv.h）：失败时 *value 置 NULL、*length 置 0 */
    *value = NULL;
    *length = 0;

    tal_mutex_lock(s_lfs_mutex);
    result = kv_storage_read(key, &encrypted, &encrypted_length);
    tal_mutex_unlock(s_lfs_mutex);
    if (OPRT_OK != result) {
        return result;
    }

    /* kv_decrypt 内部 tal_malloc 的明文缓冲（末尾已补 '\0'）直接交给调用者，
     * 由调用者 tal_free 释放（tal_aes_free_data 即 tal_free） */
    result = kv_decrypt(encrypted, encrypted_length, value, length);
    tal_free(encrypted);
    if (OPRT_OK != result) {
        PR_ERR("key %s decrypt failed: %d", key, result);
    }

    return result;
}

static OPERATE_RET tkl_kv_del(const char *key)
{
    OPERATE_RET result;

    if (NULL == key) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(s_lfs_mutex);
    result = kv_storage_remove(key);
    tal_mutex_unlock(s_lfs_mutex);
    return result;
}

/* ---------------------------------------------------------------------------
 * 平台 ops 卡：由本文件组装，core 的 tal_kv.c 经 extern 引用完成分发。
 * 移植新平台时提供同名卡与 4 个 tkl_kv_* 实现即可。
 * --------------------------------------------------------------------------- */
TAL_KV_OPS_T tal_kv_ops_platform = {
    .init = tkl_kv_init,
    .set = tkl_kv_set,
    .get = tkl_kv_get,
    .del = tkl_kv_del,
};
