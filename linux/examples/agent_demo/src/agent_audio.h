/**
 * @file agent_audio.h
 * @brief agent_demo 上行音频：把一段 PCM 素材按帧喂给云端 agent
 *
 * 音频源经 port 的 tkl_ai_demo_* 契约取得（Linux：PCM 文件回放；
 * 真实设备：替换为麦克风采集，见 port/include/media/tkl_ai_demo.h）。
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

#ifdef __cplusplus
}
#endif

#endif /* __AGENT_AUDIO_H__ */
