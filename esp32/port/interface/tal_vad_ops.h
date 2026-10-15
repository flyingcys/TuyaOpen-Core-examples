/**
 * @file tal_vad_ops.h
 * @brief 契约层（interface）：tal_vad 平台 ops 卡类型 + VAD 公开类型。
 *
 * 分层定位与依赖方向见 tal_api_ops.h 的文件头。本头只含类型：
 *  - `TAL_VAD_CONFIG_T` / `TAL_VAD_STATUS_E`（原在 core 的 tal_vad.h，随契约
 *    一并迁到本层；core 的 tal_vad.h 反向 include 本头以保持其 API 面不变）
 *  - `TAL_VAD_OPS_T`（卡类型）
 *
 * VAD 为**平台可选能力**（AI 音频平台能力）。port 具备该能力时组装
 * tal_vad_ops_platform（成员绑定平台实现）；不具备时成员置 NULL——core 的
 * tal_vad.c 分发检测到 NULL 后，init/start/stop 返回 OPRT_NOT_SUPPORTED
 * （PR_ERR 提示），get_status 返回中性值 TAL_VAD_STATUS_NONE（"无语音"，
 * 见 tal_vad.c 的说明）。
 *
 * 与主卡 TAL_API_OPS_T（tal_api_ops.h）及 OTA/KV 卡解耦，由 port 独立组装
 * （本仓 linux/esp32 均为空卡，见 port/src/port_ai_vad.c）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_VAD_OPS_H__
#define __TAL_VAD_OPS_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief VAD 配置（镜像上游 TKL_VAD_CONFIG_T）
 */
typedef struct {
    uint32_t sample_rate;       /* 采样率（Hz） */
    uint8_t  channel_num;       /* 通道数 */
    int      speech_min_ms;     /* 判定为语音所需的最短语音时长（ms） */
    int      noise_min_ms;      /* 判定为噪声所需的最短噪声时长（ms） */
    int      frame_duration_ms; /* 单帧时长（ms） */
    float    scale;             /* 灵敏度缩放系数 */
} TAL_VAD_CONFIG_T;

/**
 * @brief VAD 状态（镜像上游 TKL_VAD_STATUS_T 的两个宏，值一一对应）
 */
typedef enum {
    TAL_VAD_STATUS_NONE   = 0, /* 无语音（空闲） */
    TAL_VAD_STATUS_SPEECH = 1, /* 检测到语音 */
} TAL_VAD_STATUS_E;

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

#ifdef __cplusplus
}
#endif

#endif /* __TAL_VAD_OPS_H__ */
