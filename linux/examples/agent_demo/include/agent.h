/**
 * @file agent.h
 * @brief AI 链路启动编排（MQTT 连上后初始化 agent + 自动触发一次语音上传）
 *
 * 自 tuya_main.c 剥离（2026-10-14）：主流程只负责"连云与事件分发"，
 * 本模块负责 MQTT_CONNECTED 之后的 AI 链路启动（幂等）与一次性上行触发。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __AGENT_H__
#define __AGENT_H__

/* 上行语音素材：CMake 注入路径（Kconfig AGENT_DEMO_AUDIO_FILE）；
 * 为空时用构建目录的内置素材（assets/voice_16k.pcm 复制而来）。
 * 两处消费：tuya_main.c 的启动信息打印、agent.c 的触发上传。 */
#ifndef AGENT_DEMO_AUDIO_FILE
#define AGENT_DEMO_AUDIO_FILE ""
#endif
#define AGENT_AUDIO_SRC_PATH ((AGENT_DEMO_AUDIO_FILE)[0] ? (AGENT_DEMO_AUDIO_FILE) : "voice_16k.pcm")

/**
 * @brief 启动 AI 链路（幂等）：初始化 agent + 注册下行打印 + 起触发线程
 *
 * 在 MQTT_CONNECTED 事件里调用；重复调用安全（guard 语义见实现内注释）。
 */
void agent_start(void);

#endif /* __AGENT_H__ */
