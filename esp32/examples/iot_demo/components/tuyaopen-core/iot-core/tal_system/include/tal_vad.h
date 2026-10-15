/**
 * @file tal_vad.h
 * @brief 平台 VAD（语音活动检测）的 TAL 接口
 *
 * AI 音频输入（agent-core/ai_audio 的 ai_audio_input.c）经本接口把一次 VAD
 * 操作（建/启/停/查）交给平台执行——上游为 tkl_vad_* 直调，2026-10 解耦为
 * 经卡分发（契约见 tal_vad_ops.h）。
 *
 * @note 类型随契约迁至 port/interface/tal_vad_ops.h：TAL_VAD_CONFIG_T 逐字段
 *       镜像上游 TKL_VAD_CONFIG_T，TAL_VAD_STATUS_E 与上游 TKL_VAD_STATUS_T
 *       （uint8_t + NONE/SPEECH 宏）值一一对应——port 接实现时可原样转发，
 *       无需换算。
 *
 * @note 上游另有 tkl_vad_set_threshold / tkl_vad_feed / tkl_vad_deinit，
 *       core 当前未使用，未纳入卡；将来需要时按同一形态加成员。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TAL_VAD_H__
#define __TAL_VAD_H__

#include "tuya_cloud_types.h"

/* 契约层（interface，随所选平台的 port 提供）：TAL_VAD_CONFIG_T /
 * TAL_VAD_STATUS_E / TAL_VAD_OPS_T 的定义在 port/interface/tal_vad_ops.h
 * —— core 只消费契约，不定义契约（分层：port 不依赖 core） */
#include "tal_vad_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the platform ops of tal_vad
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_VAD_OPS_T *tal_vad_get_ops(void);

/**
 * @brief 初始化 VAD（按配置建检测器）
 *
 * @param[in] cfg: 采样率/通道数/语音与噪声最短时长/帧长/灵敏度；见 TAL_VAD_CONFIG_T
 *
 * @return OPRT_OK on success；OPRT_NOT_SUPPORTED：平台未提供该能力
 *         （port 的卡成员为 NULL）；其余见 tuya_error_code.h
 */
OPERATE_RET tal_vad_init(const TAL_VAD_CONFIG_T *cfg);

/**
 * @brief 启动 VAD 检测
 *
 * @return OPRT_OK on success；OPRT_NOT_SUPPORTED：平台未提供该能力
 */
OPERATE_RET tal_vad_start(void);

/**
 * @brief 停止 VAD 检测
 *
 * @return OPRT_OK on success；OPRT_NOT_SUPPORTED：平台未提供该能力
 */
OPERATE_RET tal_vad_stop(void);

/**
 * @brief 查询当前 VAD 状态
 *
 * @return TAL_VAD_STATUS_SPEECH：检测到语音；TAL_VAD_STATUS_NONE：无语音
 *         （平台未提供该能力时也返回 NONE，见 tal_vad.c 的分发说明）
 */
TAL_VAD_STATUS_E tal_vad_get_status(void);

#ifdef __cplusplus
}
#endif

#endif /* __TAL_VAD_H__ */
