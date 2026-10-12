/**
 * @file tal_audio_enc_ops.h
 * @brief tal_audio_enc 的平台 ops 契约：IPC 音频编码请求（单接口）
 *
 * 定义 TAL_AUDIO_ENC_OPS_T：IPC 音频编码为**平台可选能力**（双核平台的
 * 音频编码服务，如 T5）。port 具备该能力时组装
 * tal_audio_enc_ops_platform（成员绑定平台实现）；不具备时成员置 NULL——
 * core 的 tal_audio_enc.c 分发检测到 NULL 返回 OPRT_NOT_SUPPORTED，对应
 * AI 服务的 IPC 编码器变体保持不可用（与门控关闭的现状一致）。
 *
 * 与主卡 TAL_API_OPS_T（tal_api_ops.h）及 OTA/KV 卡解耦，由 port 独立
 * 组装（linux/esp32 本仓均为空卡，见 port/src/tkl_ai_codec.c）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_AUDIO_ENC_OPS_H__
#define __TAL_AUDIO_ENC_OPS_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 请求平台执行一次音频编码操作；enc_para 为 IPC_AUDIO_ENC_PARA_T*
     * （AI 服务定义，本层保持 void* 以维持分层） */
    OPERATE_RET (*request)(void *enc_para);
} TAL_AUDIO_ENC_OPS_T;

/**
 * @brief Get the platform ops of tal_audio_enc
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_AUDIO_ENC_OPS_T *tal_audio_enc_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* __TAL_AUDIO_ENC_OPS_H__ */
