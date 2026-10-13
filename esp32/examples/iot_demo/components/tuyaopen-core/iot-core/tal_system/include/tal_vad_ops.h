/**
 * @file tal_vad_ops.h
 * @brief tal_vad 的平台 ops 契约：VAD 四接口（init / start / stop / get_status）
 *
 * 定义 TAL_VAD_OPS_T：VAD 为**平台可选能力**（AI 音频平台能力）。port 具备该
 * 能力时组装 tal_vad_ops_platform（成员绑定平台实现）；不具备时成员置 NULL——
 * core 的 tal_vad.c 分发检测到 NULL 后，init/start/stop 返回
 * OPRT_NOT_SUPPORTED（PR_ERR 提示），get_status 返回中性值
 * TAL_VAD_STATUS_NONE（"无语音"，见 tal_vad.c 的说明）。
 *
 * 与主卡 TAL_API_OPS_T（tal_api_ops.h）及 OTA/KV 卡解耦，由 port 独立组装
 * （linux/esp32 本仓均为空卡，见 port/src/port_ai_vad.c）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_VAD_OPS_H__
#define __TAL_VAD_OPS_H__

#include "tuya_cloud_types.h"

#include "tal_vad.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief tal_vad 平台 ops：成员与 tal_vad.h 的 API 一一对应
 */
typedef struct {
    /* 按配置初始化 VAD；见 tal_vad.h 的 tal_vad_init */
    OPERATE_RET (*init)(const TAL_VAD_CONFIG_T *cfg);
    /* 启动检测；见 tal_vad.h 的 tal_vad_start */
    OPERATE_RET (*start)(void);
    /* 停止检测；见 tal_vad.h 的 tal_vad_stop */
    OPERATE_RET (*stop)(void);
    /* 查询状态；见 tal_vad.h 的 tal_vad_get_status */
    TAL_VAD_STATUS_E (*get_status)(void);
} TAL_VAD_OPS_T;

/**
 * @brief Get the platform ops of tal_vad
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_VAD_OPS_T *tal_vad_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* __TAL_VAD_OPS_H__ */
