/**
 * @file demo_media_event.c
 * @brief 占位：Task 5 替换为 local_store 回放事件实现。
 *
 * 上游 demo_media_event.c（711 行）注册 MEDIA_STREAM 事件回调，用独立线程把
 * local_store 段里的 H264 帧经 tuya_ipc_media_playback_send_* 推给 App
 * （查询月/日、起播、暂停、倍速、拖动、跨段续播）。
 *
 * 本任务只提供可链接的占位，保证直播媒体链路不依赖回放代码即可跑通；
 * 真正的实现（含 P2P_DEMO_PB_SEED 灌样片）由 Task 5 整体替换本文件。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "demo_media_event.h"
#include "tal_log.h"

/**
 * @brief 注册回放事件（占位：只打印）
 */
void demo_media_event_register(void)
{
    PR_NOTICE("media event stub: Task 5 will replace this");
}

/**
 * @brief 停止回放送帧线程（占位：空实现）
 */
void demo_media_pb_stop(void)
{
}
