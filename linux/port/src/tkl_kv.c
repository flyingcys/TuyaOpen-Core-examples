/**
 * @file tkl_kv.c
 * @brief tal_kv 平台 ops 卡：Linux 用"区域文件"模拟一块可读写擦的数据区
 *
 * 区域 = ./tuyadb/data.bin（默认 256KB）；擦除 = 该范围写 0xFF（模拟 flash 擦除态）。
 * 移植其它平台时替换本文件：flash 直接映射分区、EEPROM 直接映射地址空间。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_kv_ops.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DATA_REGION_PATH   "./tuyadb/data.bin"
/* 需按产品写入总量评估（本引擎不回收空间）；256KB 对 demo 够用，不代表对产品够用 */
#define DATA_REGION_SIZE   (256 * 1024)
#define DATA_ERASE_GRAN    4096

static FILE *s_region_fp = NULL;

static OPERATE_RET data_region_prefill(FILE *fp)
{
    static uint8_t s_ff[1024];
    memset(s_ff, 0xFF, sizeof(s_ff));

    for (uint32_t done = 0; done < DATA_REGION_SIZE; done += sizeof(s_ff)) {
        if (sizeof(s_ff) != fwrite(s_ff, 1, sizeof(s_ff), fp)) {
            return OPRT_COM_ERROR;
        }
    }
    return OPRT_OK;
}

static OPERATE_RET tkl_kv_init(TAL_KV_REGION_T *region)
{
    if (NULL == region) {
        return OPRT_INVALID_PARM;
    }

    if (NULL == s_region_fp) {
        /* 确保目录存在（首次运行 / 干净环境） */
        if (0 != mkdir("./tuyadb", 0755) && EEXIST != errno) {
            return OPRT_COM_ERROR;
        }

        s_region_fp = fopen(DATA_REGION_PATH, "r+b");
        if (NULL == s_region_fp) {
            s_region_fp = fopen(DATA_REGION_PATH, "w+b");   /* 首次：创建并预填 0xFF */
            if (NULL == s_region_fp) {
                return OPRT_COM_ERROR;
            }
            if (OPRT_OK != data_region_prefill(s_region_fp)) {
                fclose(s_region_fp);
                s_region_fp = NULL;
                return OPRT_COM_ERROR;
            }
        } else {
            long fsize = 0;
            if (0 != fseek(s_region_fp, 0, SEEK_END) || (fsize = ftell(s_region_fp)) != (long)DATA_REGION_SIZE) {
                /* 尺寸不符（被截断/半截文件）→ 重建为全 0xFF 区域，保持"空态=0xFF"不变量。
                 * 注意：这会清空已存 KV，必须留痕，不能静默。 */
                fprintf(stderr, "[tkl_kv] region file size mismatch (got %ld, expect %d), rebuild: KV cleared\n",
                        fsize, DATA_REGION_SIZE);
                fclose(s_region_fp);
                s_region_fp = fopen(DATA_REGION_PATH, "w+b");
                if (NULL == s_region_fp) {
                    return OPRT_COM_ERROR;
                }
                if (OPRT_OK != data_region_prefill(s_region_fp)) {
                    fclose(s_region_fp);
                    s_region_fp = NULL;
                    return OPRT_COM_ERROR;
                }
            }
        }
    }

    region->size = DATA_REGION_SIZE;
    region->erase_granularity = DATA_ERASE_GRAN;
    return OPRT_OK;
}

static OPERATE_RET tkl_kv_read(uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (NULL == s_region_fp || NULL == buf || offset > DATA_REGION_SIZE || len > DATA_REGION_SIZE - offset) {
        return OPRT_INVALID_PARM;
    }
    if (0 != fseek(s_region_fp, (long)offset, SEEK_SET)) {
        return OPRT_COM_ERROR;
    }
    return (len == fread(buf, 1, len, s_region_fp)) ? OPRT_OK : OPRT_COM_ERROR;
}

static OPERATE_RET tkl_kv_write(uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if (NULL == s_region_fp || NULL == buf || offset > DATA_REGION_SIZE || len > DATA_REGION_SIZE - offset) {
        return OPRT_INVALID_PARM;
    }
    if (0 != fseek(s_region_fp, (long)offset, SEEK_SET)) {
        return OPRT_COM_ERROR;
    }
    if (len != fwrite(buf, 1, len, s_region_fp)) {
        return OPRT_COM_ERROR;
    }
    fflush(s_region_fp);
    fsync(fileno(s_region_fp));   /* 掉电时最后几笔不丢（旧实现经 tal_fsync 落盘） */
    return OPRT_OK;
}

static OPERATE_RET tkl_kv_erase(uint32_t offset, uint32_t len)
{
    static uint8_t s_ff[256];
    memset(s_ff, 0xFF, sizeof(s_ff));

    if (NULL == s_region_fp || 0 == len || offset > DATA_REGION_SIZE || len > DATA_REGION_SIZE - offset) {
        return OPRT_INVALID_PARM;
    }
    if (0 != fseek(s_region_fp, (long)offset, SEEK_SET)) {
        return OPRT_COM_ERROR;
    }
    for (uint32_t done = 0; done < len;) {
        uint32_t chunk = (len - done < sizeof(s_ff)) ? (len - done) : sizeof(s_ff);
        if (chunk != fwrite(s_ff, 1, chunk, s_region_fp)) {
            return OPRT_COM_ERROR;
        }
        done += chunk;
    }
    fflush(s_region_fp);
    fsync(fileno(s_region_fp));   /* 擦除同样落盘（旧实现经 tal_fsync 落盘） */
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * 平台 ops 卡：由本文件组装，core 的 tal_kv.c 经 extern 引用完成分发。
 * 移植新平台时提供同名卡与 4 个区域原语即可。
 * --------------------------------------------------------------------------- */
TAL_KV_OPS_T tal_kv_ops_platform = {
    .init = tkl_kv_init,
    .read = tkl_kv_read,
    .write = tkl_kv_write,
    .erase = tkl_kv_erase,
};
