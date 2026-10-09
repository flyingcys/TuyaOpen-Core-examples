/**
 * @file demo_media_event.h
 * @brief p2p_demo 回放（MEDIA_STREAM 事件）契约：Task 5 实现 local_store 回放。
 *
 * 从上游 ipc_p2p_demo 的 include/demo_media_event.h 裁剪而来，只保留两个
 * 对外接口（查询月/日、起播/暂停/倍速/拖动等都在 .c 内部实现）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __DEMO_MEDIA_EVENT_H__
#define __DEMO_MEDIA_EVENT_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 MEDIA_STREAM 事件回调（回放查询/控制）
 * @note 在 tuya_ipc_demo_start() 里调用（此时 P2P 已 TUYA_APP_Start/p2p_init）
 */
void demo_media_event_register(void);

/**
 * @brief 停止回放送帧线程（若在跑）
 */
void demo_media_pb_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEMO_MEDIA_EVENT_H__ */
