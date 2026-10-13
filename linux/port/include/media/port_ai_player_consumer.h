/**
 * @file port_ai_player_consumer.h
 * @brief AI 播放器（agent_demo/components/audio_player）平台消费端：Linux ALSA 输出
 *
 * core 的 svc_ai_player 默认装载空消费端（consumer_default.c，六回调全 OK 不
 * 发声），平台侧实现自己的 AI_PLAYER_CONSUMER_T 并经
 * tuya_ai_player_set_consumer() 注入——本文件暴露的 install 函数就是该注入的
 * port 侧入口：把本 port 的 ALSA 实现（tkl_ai_player_consumer.c）挂到播放器。
 *
 * 调用时机（与 svc_ai_player.c 的时序强绑定）：
 *   1. tuya_ai_player_service_init(cfg)    —— 装载默认空消费端并 open
 *   2. port_ai_player_consumer_install()    —— 替换为 ALSA 消费端（close 旧的 + open 新的）
 * install 必须晚于 service_init：service_init 内部 memcpy 会覆盖 consumer，
 * 先注入会被默认实例顶掉（见 tuya_ai_player_set_consumer 实现）。
 *
 * 输出格式：16kHz / 16bit / 单声道（与 service_init 的配置约定一致；consumer
 * 的 start() 不带格式参数，设备在 open() 时按此格式打开）。无声卡/设备打开
 * 失败时 install 仍成功（播放器链路照常跑，声音丢弃），仅打错误日志。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __TKL_AI_PLAYER_CONSUMER_H__
#define __TKL_AI_PLAYER_CONSUMER_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 把本 port 的 ALSA 消费端注入 AI 播放器（tuya_ai_player_set_consumer）
 *
 * 须在 tuya_ai_player_service_init() 之后调用（见文件头说明）。
 * 内部会先关闭默认空消费端、再打开 ALSA 设备；设备打开失败不返回错误
 * （播放链路可继续，仅无声音），错误日志给出原因。
 *
 * @return OPRT_OK 注入完成（含设备打开失败的降级）；其他见 tuya_error_code.h
 */
OPERATE_RET port_ai_player_consumer_install(void);

#ifdef __cplusplus
}
#endif

#endif /* __TKL_AI_PLAYER_CONSUMER_H__ */
