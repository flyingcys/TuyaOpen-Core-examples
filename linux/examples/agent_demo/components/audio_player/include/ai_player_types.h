/**
 * @file ai_player_types.h
 * @brief audio_player 的 core 自持镜像类型集：音频参数三枚举（TAL_AUDIO_*）
 *
 * 背景：上游 audio_player 的公开 API（AI_PLAYER_CFG_T）与内部解码输出
 * （DECODER_OUTPUT_T）直接引用平台音频类型（采样率 / 位宽 / 声道三枚举，
 * 定义在上游各平台 tuyaos_adapter/include/media/ 下的音频适配头中）。本仓
 * 铁律是 core 不出现任何平台层（tkl/tdl/tdd）前缀的类型与接口，故按 BLE 卡
 * 先例（iot-core/tal_system/include/tal_bluetooth_ops.h 的 TAL_BLE_* 镜像）
 * 镜像为 core 自持类型，前缀 TAL_AUDIO_。
 *
 * 约定：
 * - 本文件是**逐值镜像**（枚举值逐一对应平台音频类型），三个目标平台
 *   （LINUX / ESP32 / T5AI）的音频适配头取值一致，故镜像取同一套值；
 * - 播放器对外只暴露"采样率 / 位宽 / 声道数"三个量，消费端（port 注入的
 *   AI_PLAYER_CONSUMER_T 实现）自行把本类型映射到平台音频配置——core 不
 *   持有平台音频句柄，也不 include 平台音频头；
 * - port 侧适配层建议加编译期断言兜底（镜像一旦漂移即编译失败）：逐值相等
 *   （枚举值 == 平台枚举值）+ sizeof 相等，写法照 tal_bluetooth_ops.c 的
 *   TAL_BLE_LAYOUT_ASSERT / TAL_BLE_OFFSET_ASSERT 先例。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __AI_PLAYER_TYPES_H__
#define __AI_PLAYER_TYPES_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 位宽：逐值镜像平台音频位宽枚举 */
typedef enum {
    TAL_AUDIO_DATABITS_8   = 8,
    TAL_AUDIO_DATABITS_16  = 16,
    TAL_AUDIO_DATABITS_MAX = 0xFF
} TAL_AUDIO_DATABITS_E;

/* 声道数：逐值镜像平台音频声道枚举（播放器只用 MONO/STEREO，TDM 一并保留） */
typedef enum {
    TAL_AUDIO_CHANNEL_MONO    = 1,
    TAL_AUDIO_CHANNEL_STEREO,
    TAL_AUDIO_CHANNEL_TDM3CHS,
    TAL_AUDIO_CHANNEL_TDM4CHS,
    TAL_AUDIO_CHANNEL_TDM5CHS,
    TAL_AUDIO_CHANNEL_TDM6CHS,
    TAL_AUDIO_CHANNEL_TDM7CHS,
    TAL_AUDIO_CHANNEL_TDM8CHS
} TAL_AUDIO_CHANNEL_E;

/* 采样率：逐值镜像平台音频采样率枚举（11K/22K/44K 是上游既有取值，非笔误） */
typedef enum {
    TAL_AUDIO_SAMPLE_8K   = 8000,
    TAL_AUDIO_SAMPLE_11K  = 11000,
    TAL_AUDIO_SAMPLE_12K  = 12000,
    TAL_AUDIO_SAMPLE_16K  = 16000,
    TAL_AUDIO_SAMPLE_22K  = 22000,
    TAL_AUDIO_SAMPLE_24K  = 24000,
    TAL_AUDIO_SAMPLE_32K  = 32000,
    TAL_AUDIO_SAMPLE_44K  = 44000,
    TAL_AUDIO_SAMPLE_48K  = 48000,
    TAL_AUDIO_SAMPLE_MAX  = 0xFFFFFFFF
} TAL_AUDIO_SAMPLE_E;

#ifdef __cplusplus
}
#endif

#endif /* __AI_PLAYER_TYPES_H__ */
