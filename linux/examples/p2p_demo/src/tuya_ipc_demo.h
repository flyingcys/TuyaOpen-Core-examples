/**
 * @file tuya_ipc_demo.h
 * @brief p2p_demo 媒体回调契约（启动链与视频链路共用）。
 *
 * 从上游 ipc_p2p_demo 的 include/tuya_ipc_demo.h 裁剪而来：
 *   - 去掉音频开关（CAMERA_DEMO_AUDIO/DEMO_ENABLE_AUDIO，本 demo 不做音频采集，
 *     与上游 DEMO_HAS_AUDIO=0 分支一致）与 demo_mic_uplink_pause（仅音频路径用）；
 *   - 音频回调声明保留、由 .c 给空实现（P2P SDK 的回调集要求非空指针）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __TUYA_IPC_DEMO_H__
#define __TUYA_IPC_DEMO_H__

/* 本 demo 必须有 P2P：缺开关时给出明确构建错误而不是静默编出一个空壳。
 * 这组判断必须放在下面的 include 之前——P2P 关闭时 p2p-core 整棵树不编译，
 * tuya_ipc_p2p.h 连包含路径都不存在，先包含会报"找不到头文件"而掩盖真正的
 * 原因（构建错误应指向开关，而不是头文件缺失）。 */
#if !defined(ENABLE_TUYA_P2P) || (ENABLE_TUYA_P2P != 1)
#error "p2p_demo requires CONFIG_ENABLE_TUYA_P2P=ON (or delete linux/.config / pass -DCONFIG_ENABLE_TUYA_P2P=ON)"
#endif

#include "tuya_cloud_types.h"
#include "tuya_ipc_p2p.h"

/* local_store 回放开关：单一宏 P2P_DEMO_LOCAL_STORE（由 CMake 在
 * CONFIG_P2P_DEMO_LOCAL_STORE 与 CONFIG_ENABLE_LOCAL_STORE 同时打开时注入
 * =1；只开 demo 开关时 CMake 告警且不注入）。代码用 #ifdef 判断即可——
 * "宏在 = 回放可用"由构建期保证，不再派生第二个宏。 */

/**
 * @brief 初始化 demo 媒体链路（ring buffer / vcam / local_store）
 */
void tuya_ipc_demo_start(void);

/**
 * @brief 释放 demo 媒体链路资源
 */
void tuya_ipc_demo_end(void);

/**
 * @brief P2P 断链回调
 * @return 0
 */
int demo_on_signal_disconnect_callback(void);

/**
 * @brief App 开始看直播
 * @return 0
 */
int demo_on_live_video_start_callback(void);

/**
 * @brief App 停止看直播
 * @return 0
 */
int demo_on_live_video_stop_callback(void);

/**
 * @brief P2P 取视频帧（AU）
 * @param[in,out] media_frame 输出帧
 * @return 0 有帧；-1 暂无
 */
int demo_on_get_video_frame_callback(MEDIA_FRAME *media_frame);

/**
 * @brief P2P 取音频上行帧（本 demo 无音频，空实现返回 -1）
 * @param[in,out] media_frame 输出帧
 * @return -1
 */
int demo_on_get_audio_frame_callback(MEDIA_FRAME *media_frame);

/**
 * @brief App 开始对讲（空实现）
 * @return 0
 */
int demo_on_live_audio_start_callback(void);

/**
 * @brief App 结束对讲（空实现）
 * @return 0
 */
int demo_on_live_audio_stop_callback(void);

/**
 * @brief 接收 App 下行音频（本 demo 无音频，空实现）
 * @param[in] media_frame G.711U 负载
 * @return 0
 */
int demo_on_recv_audio_frame_callback(MEDIA_FRAME *media_frame);

/**
 * @brief App 请求 I 帧 → demo_vcam_request_i_frame
 * @return 0
 */
int demo_on_request_i_frame_callback(void);

/**
 * @brief App 调整码率（样片源无编码器，无法改码率）
 * @param[in] kbps 目标码率
 * @return -1（不支持；同上游占位映射：内部 __demo_camera_set_bitrate 返回 OPRT_NOT_SUPPORTED）
 */
int demo_on_set_video_bitrate_callback(uint32_t kbps);

/**
 * @brief 解析本 demo 的视频源样片路径（先 cwd 再可执行文件目录；P2P_DEMO_H264_FILE 可覆盖）
 *
 * 直播（vcam）与回放种子（demo_media_event 的 PB_SEED）共用同一个源文件。
 *
 * @return 可读路径（静态缓冲，勿释放）；两处都找不到返回 NULL
 */
const char *demo_h264_source_path(void);

#endif /* __TUYA_IPC_DEMO_H__ */
