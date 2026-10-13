/**
 * @file port_ai_vad.c
 * @brief TAL VAD 卡（语音活动检测）——本 port 无该能力，空卡
 *
 * core 的 AI 音频输入（agent-core/ai_audio 的 ai_audio_input.c）经 tal_vad_*
 * 请求平台做 VAD（上游为平台 TKL VAD 接口直调，2026-10 解耦为经卡分发，
 * 契约见 tal_vad_ops.h）。VAD 属 AI 音频平台能力，linux/esp32 本 port 均无
 * 实现——成员置 NULL，core 侧分发优雅降级：init/start/stop 返回
 * OPRT_NOT_SUPPORTED，get_status 返回 TAL_VAD_STATUS_NONE（无语音）。
 * 将来平台具备该能力时，在此把成员接到平台实现即可。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_vad_ops.h"

TAL_VAD_OPS_T tal_vad_ops_platform = {
    .init       = NULL,
    .start      = NULL,
    .stop       = NULL,
    .get_status = NULL,
};
