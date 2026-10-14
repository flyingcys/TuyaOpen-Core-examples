/**
 * @file agent_audio.h
 * @brief agent_demo 音频链路对接：上行喂送 + 下行打印（2026-10-14 两文件合并）
 *
 * 上行：把一段 PCM 素材按帧喂给云端 agent——音频源经应用侧 play_local_in_* 契约
 * 取得（Linux：PCM 文件回放；真实设备：替换为麦克风采集，见 play_local_in.h）。
 * 下行：注册事件回调打印 ASR 识别结果与 NLG 回复文字（agent_downlink_init）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __AGENT_AUDIO_H__
#define __AGENT_AUDIO_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 上行音频参数：16kHz / 16bit / 单声道，80ms 帧（与云端上行属性一致） */
#define AGENT_AUDIO_SAMPLE_RATE 16000
#define AGENT_AUDIO_SAMPLE_BYTES 2 /* 16bit */
#define AGENT_AUDIO_FRAME_MS 80
#define AGENT_AUDIO_FRAME_BYTES (AGENT_AUDIO_SAMPLE_RATE * AGENT_AUDIO_SAMPLE_BYTES * AGENT_AUDIO_FRAME_MS / 1000)

/* 上行校验超时：等云端 ASR 响应（实测 ASR 往返 ~1-3.5s，15s 余量充分） */
#define AGENT_UPLINK_VERIFY_TIMEOUT_MS 15000

/* 上传统计（日志与自测判据用） */
typedef struct {
    uint32_t frames;           /* 已上传帧数 */
    uint32_t bytes;            /* 已上传总字节数 */
    uint32_t last_frame_bytes; /* 末帧字节数（非整帧时可据此验证边界） */
} agent_audio_stat_t;

/**
 * @brief 上传一段语音（一次性）：打开音频源 → 开始一轮输入 → 按帧喂数据 → 结束本轮输入
 *
 * 调用前需保证 AI 客户端已就绪（tuya_ai_agent_is_ready()）。
 * 全部数据喂完后调用 tuya_ai_input_stop()，云端据此判定"说完了"并开始处理。
 *
 * @param[in]  src_path 音频源路径（Linux：16kHz/16bit/mono PCM 文件）
 * @param[out] stat     上传统计（可为 NULL）
 * @return OPRT_OK 全部上传成功；其他见 tuya_error_code.h
 */
OPERATE_RET agent_audio_upload_once(const char *src_path, agent_audio_stat_t *stat);

/**
 * @brief 上行结果校验：以"云端是否回 ASR"为上传成功的硬判据
 *
 * 背景（1c 评审 finding）：断线时 ringbuf 写入仍成功、tuya_ai_audio_input 一路返回
 * OK，upload_once 会打出 "upload done" —— 该行只表示"本地喂帧完成"，**不代表上行
 * 成功**。连接状态查询（tuya_ai_agent_is_ready）在断线后要等 ping 超时累计（~18s
 * 起步）才失效，抓不到 2.59s 的上传窗口，属假防护。故以"云端 ASR 响应"为终判据。
 *
 * 用法（触发线程内，一次性）：init → upload_once → wait(timeout)。
 * 注意边界：信号量按"单次触发"设计（一次性 demo 口径）；若将来扩展多轮，需在
 * 每轮前重置（init 幂等，wait 消费后需重新 init 清零语义——当前不跨轮复用）。
 *
 * @return init: OPRT_OK；wait: OPRT_OK=收到 ASR（ASR_OK/ASR_EMPTY 均算链路通），
 *         其他（超时/未初始化）= 未收到
 */
OPERATE_RET agent_audio_uplink_verify_init(void);
OPERATE_RET agent_audio_uplink_verify_wait(uint32_t timeout_ms);

/**
 * @brief ASR 事件到达时调用（由下行回调 post；ASR_OK 与 ASR_EMPTY 都应调用——
 *        空识别是"链路通了但没内容"，不是上传失败）
 */
void agent_audio_uplink_verify_notify(void);

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

#endif /* __AGENT_AUDIO_H__ */
