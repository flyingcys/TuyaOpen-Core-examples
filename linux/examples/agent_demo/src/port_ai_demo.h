/**
 * @file port_ai_demo.h
 * @brief agent_demo 音频输入最小契约：从音频源取 PCM 帧
 *
 * 用途：agent_demo 上行链路需要"一段语音"作为云端 agent 的触发输入。
 * 本契约把"音频从哪来"隔离在应用侧适配层（2026-10-13 自平台 port 下沉）：
 *   - Linux（本仓）：从 PCM 文件回放（只为链路能跑通，不做真实音频硬件适配）；
 *   - 真实设备（将来）：替换实现为麦克风采集（可参照官方 tkl_audio.h 的 tkl_ai_* 全栈，
 *     接入时把本契约替换/映射即可）。
 *
 * 音频格式约定：16kHz / 16bit / 单声道（与云端上行属性一致），调用方按
 * 80ms 帧（2560 字节）读取。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __PORT_AI_DEMO_H__
#define __PORT_AI_DEMO_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打开音频源
 *
 * @param[in] src_path 音频源路径（Linux：PCM 文件路径；真实设备可忽略/改为设备参数）
 * @return OPRT_OK 成功；其他见 tuya_error_code.h
 */
OPERATE_RET port_ai_demo_open(const char *src_path);

/**
 * @brief 读取一段 PCM 数据
 *
 * @param[out] buf 输出缓冲区
 * @param[in]  len 期望读取的字节数
 * @return >0 实际读到的字节数；0 数据结束(EOF)；<0 错误
 */
INT_T port_ai_demo_read(UINT8_T *buf, UINT_T len);

/**
 * @brief 关闭音频源
 *
 * @return OPRT_OK 成功
 */
OPERATE_RET port_ai_demo_close(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __PORT_AI_DEMO_H__ */
