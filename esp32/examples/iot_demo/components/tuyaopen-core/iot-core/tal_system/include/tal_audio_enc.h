/**
 * @file tal_audio_enc.h
 * @brief 平台 IPC 音频编码请求的 TAL 接口
 *
 * AI 服务（svc_ai_codec 的 opus/speex IPC 编码器变体）经本接口把一次音频
 * 编码操作（建/编/销）交给平台执行——平台实现通常把请求经 IPC 转给另一核
 * 的编码服务（上游为 tkl_audio_enc_request 直调，2026-10 解耦为经卡分发，
 * 契约见 tal_audio_enc_ops.h）。
 *
 * @note 参数为 AI 服务层的 IPC_AUDIO_ENC_PARA_T*（定义在
 *       tuya_ai_encoder_*_ipc_server.h），本层保持 void* 以维持分层
 *       （tal 层不反向依赖 agent-core）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TAL_AUDIO_ENC_H__
#define __TAL_AUDIO_ENC_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 请求平台执行一次 IPC 音频编码操作
 *
 * @param[in,out] enc_para: IPC_AUDIO_ENC_PARA_T*（AI 服务定义；含 ops/编码
 *                          类型/输入输出缓冲/句柄等字段）
 *
 * @return OPRT_OK on success；OPRT_NOT_SUPPORTED：平台未提供该能力
 *         （port 的卡成员为 NULL）；其余见 tuya_error_code.h
 */
OPERATE_RET tal_audio_enc_request(void *enc_para);

#ifdef __cplusplus
}
#endif

#endif /* __TAL_AUDIO_ENC_H__ */
