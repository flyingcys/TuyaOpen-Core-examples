/**
 * @file tal_kv.h
 * @brief TAL 键值存储接口：init / set / get / del 四接口
 *
 * 只定义存储语义，不涉及实现：key 为 C 字符串，value 为任意二进制数据。
 * 存储介质、记录格式、加密、序列化全部由平台 port 的 port_kv 决定
 * （Linux = littlefs + AES；NVS 类平台直接映射芯片 KV），iot-core 不持有
 * 任何 KV 逻辑，仅经 tal_kv_ops.h 的 ops 卡透传。
 *
 * 内存约定：tal_kv_get 成功时返回堆内存（*value），调用者用 tal_free 释放；
 * 失败时 *value 置 NULL、*length 置 0。
 *
 * key 契约：key 为 C 字符串（非 NULL）。长度上限与字符集由平台决定——
 * Linux/littlefs：≤255 且不含 '/'；NVS 类平台：≤15 字符，此类平台必须由 port
 * 做 key 映射（如 SHA-256 取前 16 字节 hex），否则 core 现用 key 会写入失败
 * （实测超限样例："<namespace>.devid" 26 字符、云端下发的 schemaId 长度不受控）。
 *
 * init 约定：tal_kv_init 必须先于其它接口调用、单线程、只调一次（用不同 cfg
 * 重复 init 会使已写数据无法解密）；init 失败后其它接口不可用。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TAL_KV_H__
#define __TAL_KV_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

#define TAL_LV_KEY_LEN 16

/**
 * @brief KV 初始化配置：seed/key 各 16 字符，port 实现决定用途
 *        （Linux port 经 SHA256 派生 AES-128 的 IV 与密钥；NVS 类平台可忽略）
 */
typedef struct {
    char seed[TAL_LV_KEY_LEN + 1];
    char key[TAL_LV_KEY_LEN + 1];
} tal_kv_cfg_t;

/**
 * @brief 初始化 KV 存储（挂载介质 / 打开芯片 KV）
 *
 * @note 必须先于其它接口调用、单线程、只调一次；init 失败后其它接口不可用
 *       （详见文件头的 init 约定）
 *
 * @param kv_cfg 配置（不可为 NULL）
 * @return OPRT_OK 成功，负值为错误码
 */
int tal_kv_init(tal_kv_cfg_t *kv_cfg);

/**
 * @brief 写入一个键值对（key 已存在则覆盖）
 *
 * @param key 键（C 字符串，非 NULL）
 * @param value 值（任意二进制，非 NULL）
 * @param length 值长度（字节），**必须 > 0**：为 0 视为非法参数返回
 *               OPRT_INVALID_PARM（空值无意义——tal_kv_get 读回的值恒含
 *               结尾 '\0'，长度至少 1）
 * @return OPRT_OK 成功；参数非法返回 OPRT_INVALID_PARM；其它负值为错误码
 */
int tal_kv_set(const char *key, const uint8_t *value, size_t length);

/**
 * @brief 读取一个键值对
 *
 * 成功时 *value 为堆内存（含结尾 '\0'，调用者用 tal_free 释放），*length 为
 * 数据长度（不含 '\0'）；失败时 *value 置 NULL、*length 置 0。
 *
 * @param key 键（C 字符串，非 NULL）
 * @param value 输出：值的堆指针
 * @param length 输出：值长度
 * @return OPRT_OK 成功，OPRT_NOT_FOUND 键不存在，负值为其它错误码
 */
int tal_kv_get(const char *key, uint8_t **value, size_t *length);

/**
 * @brief 删除一个键（键不存在视为成功）
 *
 * @param key 键（C 字符串，非 NULL）
 * @return OPRT_OK 成功，负值为错误码
 */
int tal_kv_del(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* __TAL_KV_H__ */
