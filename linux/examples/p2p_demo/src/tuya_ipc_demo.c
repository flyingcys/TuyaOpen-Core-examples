/**
 * @file tuya_ipc_demo.c
 * @brief p2p_demo 媒体回调占位实现：Task 4 将整体替换为真实媒体链路
 *        （vcam 样片源 + ring buffer + local_store）。
 *
 * 本文件只为 Task 3 的真实启动链提供可链接、可运行的空实现：
 *   - TUYA_APP_Start() 会真实执行（skill 上报 / p2p init / listen），
 *     它注册的回调在 App 拉起直播前不会被调用，这里一律空转；
 *   - 音频整体不做（与上游 DEMO_HAS_AUDIO=0 分支一致）：取音频帧返回 -1
 *     （告诉 P2P 无帧；返回 0 会让 P2P 按"有帧"发送未填充的 MEDIA_FRAME）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"
#include "tuya_ipc_demo.h"

/**
 * @brief 初始化 demo 媒体链路（占位）
 */
void tuya_ipc_demo_start(void)
{
    PR_NOTICE("media stub: Task 4 will replace this");
}

/**
 * @brief 释放 demo 媒体链路资源（占位）
 */
void tuya_ipc_demo_end(void)
{
}

/**
 * @brief P2P 断链回调（占位）
 */
int demo_on_signal_disconnect_callback(void)
{
    return 0;
}

/**
 * @brief App 开始看直播（占位）
 */
int demo_on_live_video_start_callback(void)
{
    return 0;
}

/**
 * @brief App 停止看直播（占位）
 */
int demo_on_live_video_stop_callback(void)
{
    return 0;
}

/**
 * @brief P2P 取视频帧（占位：无帧源，Task 4 接 vcam）
 */
int demo_on_get_video_frame_callback(MEDIA_FRAME *media_frame)
{
    (void)media_frame;
    return 0;
}

/**
 * @brief P2P 取音频上行帧（本 demo 无音频：恒无帧）
 */
int demo_on_get_audio_frame_callback(MEDIA_FRAME *media_frame)
{
    (void)media_frame;
    return -1;
}

/**
 * @brief App 开始对讲（占位）
 */
int demo_on_live_audio_start_callback(void)
{
    return 0;
}

/**
 * @brief App 结束对讲（占位）
 */
int demo_on_live_audio_stop_callback(void)
{
    return 0;
}

/**
 * @brief 接收 App 下行音频（本 demo 无音频，丢弃）
 */
int demo_on_recv_audio_frame_callback(MEDIA_FRAME *media_frame)
{
    (void)media_frame;
    return 0;
}

/**
 * @brief App 请求 I 帧（占位：Task 4 转 demo_vcam_request_i_frame）
 */
int demo_on_request_i_frame_callback(void)
{
    return 0;
}

/**
 * @brief App 调整码率（占位：样片源无编码器，仅接受）
 */
int demo_on_set_video_bitrate_callback(uint32_t kbps)
{
    (void)kbps;
    return 0;
}
