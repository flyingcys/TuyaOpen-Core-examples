/**
 * @file agent_downlink.h
 * @brief agent_demo 下行处理：只接文字（ASR 识别结果 + agent 回复），音频包丢弃
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __AGENT_DOWNLINK_H__
#define __AGENT_DOWNLINK_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册下行事件回调（ai_user_event_notify_register）
 *
 * 打印：
 *   [AGENT][ASR]  <语音识别结果>        （验证"上传的语音被云端识别"）
 *   [AGENT][NLG]  <agent 回复文字>      （全链路通的最终判据）
 * 下行音频包在 ai_agent.c 的 media_data_cb 里被丢弃（本 demo 不启用 AI 音频模块）。
 *
 * @return OPRT_OK 成功
 */
OPERATE_RET agent_downlink_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __AGENT_DOWNLINK_H__ */
