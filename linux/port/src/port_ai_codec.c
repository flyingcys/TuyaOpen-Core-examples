/**
 * @file port_ai_codec.c
 * @brief TAL 音频编码卡（IPC 音频编码请求）——本 port 无该能力，空卡
 *
 * core 的 AI 服务（svc_ai_codec 的 opus/speex IPC 编码器变体）经
 * tal_audio_enc_request 请求平台执行音频编码（上游为 tkl_audio_enc_request
 * 直调，2026-10 解耦为经卡分发，契约见 tal_audio_enc_ops.h）。IPC 音频编码
 * 属双核平台能力（如 T5 的音频核），linux/esp32 本 port 均无实现——成员置
 * NULL，core 侧分发返回 OPRT_NOT_SUPPORTED（与编码器变体门控关闭的现状
 * 一致）。将来平台具备该能力时，在此把成员接到平台实现即可。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_audio_enc_ops.h"

TAL_AUDIO_ENC_OPS_T tal_audio_enc_ops_platform = {
    .request = NULL,
};
