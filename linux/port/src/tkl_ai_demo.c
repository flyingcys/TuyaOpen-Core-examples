/**
 * @file tkl_ai_demo.c
 * @brief agent_demo 音频输入实现（Linux：PCM 文件回放）
 *
 * 本实现不做真实 Linux 音频适配（不接 ALSA/麦克风），只为 agent_demo 上行链路
 * 提供"一段语音"数据源、让全链路能跑通。真实设备移植时在本文件替换为麦克风
 * 采集实现（契约见 include/media/tkl_ai_demo.h）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tkl_ai_demo.h"

#include <stdio.h>

/* 当前音频源（单实例；真实设备接入时替换为麦克风句柄） */
static FILE *s_ai_src = NULL;

OPERATE_RET tkl_ai_demo_open(const char *src_path)
{
    if (s_ai_src) {
        return OPRT_COM_ERROR; /* 已打开，防重入 */
    }
    if (!src_path) {
        return OPRT_INVALID_PARM;
    }

    s_ai_src = fopen(src_path, "rb");
    if (!s_ai_src) {
        printf("tkl_ai_demo: open %s failed\n", src_path);
        return OPRT_NOT_FOUND;
    }

    return OPRT_OK;
}

INT_T tkl_ai_demo_read(UINT8_T *buf, UINT_T len)
{
    size_t n = 0;

    if (!s_ai_src || !buf || !len) {
        return -1;
    }

    n = fread(buf, 1, len, s_ai_src);

    return (INT_T)n; /* 0 = 数据结束(EOF) */
}

OPERATE_RET tkl_ai_demo_close(VOID)
{
    if (s_ai_src) {
        fclose(s_ai_src);
        s_ai_src = NULL;
    }

    return OPRT_OK;
}
