/**
 * @file demo_vcam.h
 * @brief 虚拟摄像头（virtual camera）：把内置 H.264 视频按帧率循环喂给回调。
 *
 * Linux 上没有摄像头也没有 T5 的硬编码器，本模块用上游 demo 自带的 Annex-B
 * 视频（assets/demo_video.264，CMake 复制到构建目录）顶替上游 tdl_camera：
 * open 时整文件读入并按 AU（access unit）边界建索引，起一个线程按 fps
 * 节拍逐 AU 回调，到文件末尾循环回第 0 帧。
 *
 * 回调运行在 vcam 线程上下文，回调内不得长时间阻塞（demo 侧会拷贝入队）；
 * frame->data 指向 vcam 内部缓冲，仅在回调内有效。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __DEMO_VCAM_H__
#define __DEMO_VCAM_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 一帧（一个 AU）的描述
 */
typedef struct {
    const uint8_t *data;    /* Annex-B AU（含起始码），指向 vcam 内部缓冲，回调内有效 */
    uint32_t       len;     /* AU 字节数 */
    uint64_t       ts_ms;   /* 从 0 起、按 1000/fps 递增的单调时间戳 */
    BOOL_T         is_i_frame; /* AU 内含 IDR（NAL type 5）时为 TRUE */
} DEMO_VCAM_FRAME_T;

/**
 * @brief 帧回调类型（运行在 vcam 线程）
 */
typedef void (*DEMO_VCAM_FRAME_CB)(const DEMO_VCAM_FRAME_T *frame);

/**
 * @brief 打开虚拟摄像头：读样片、建 AU 索引、起喂帧线程
 *
 * @param[in] path 样片文件路径（Annex-B H.264；NULL 或空串返回 OPRT_INVALID_PARM）
 * @param[in] fps  喂帧帧率（0 返回 OPRT_INVALID_PARM）
 * @param[in] cb   帧回调（NULL 返回 OPRT_INVALID_PARM）
 * @return OPRT_OK 成功；文件不存在返回 OPRT_NOT_EXIST；
 *         文件无 AU / 参数非法返回 OPRT_INVALID_PARM；
 *         重复 open、或上一次 close 超时（线程未能停住，状态不可恢复）返回 OPRT_COM_ERROR
 */
OPERATE_RET demo_vcam_open(const char *path, uint32_t fps, DEMO_VCAM_FRAME_CB cb);

/**
 * @brief 关闭虚拟摄像头：停线程、释放样片缓冲、状态归零
 *
 * 未打开时直接返回 OPRT_OK（可重入）。返回前保证喂帧线程已退出，
 * 回调不会再被触发。
 *
 * 约束：**不得在帧回调内调用本函数**——回调运行在 vcam 线程上，而 close 需要
 * 该线程先退出（自等会一直等到超时）；请从其他线程/主线程关闭。
 *
 * @return OPRT_OK 成功；线程在时限内未启动/未退出时返回 OPRT_TIMEOUT
 *         （此时缓冲与线程都保留，宁可泄漏也不 use-after-free）
 */
OPERATE_RET demo_vcam_close(void);

/**
 * @brief 请求 I 帧：线程下一拍跳到下一个 IDR AU（到末尾则回 0 重扫）
 *
 * 对应 App 的"请求 I 帧"语义（上游 __demo_camera_request_i_frame 的占位实现）。
 *
 * @return OPRT_OK 成功；未打开返回 OPRT_COM_ERROR
 */
OPERATE_RET demo_vcam_request_i_frame(void);

/**
 * @brief 查询是否已打开
 *
 * @return TRUE 已打开
 */
BOOL_T demo_vcam_is_open(void);

/**
 * @brief 查询当前索引统计（诊断/自测用；open 成功后可用）
 *
 * @param[out] au_num    AU 总数
 * @param[out] i_num     IDR AU 数
 * @param[out] max_len   最大 AU 字节数
 * @param[out] first_is_i 首个 AU 是否为 IDR
 * @return OPRT_OK；未打开返回 OPRT_COM_ERROR
 */
OPERATE_RET demo_vcam_get_stats(uint32_t *au_num, uint32_t *i_num, uint32_t *max_len, BOOL_T *first_is_i);

#ifdef __cplusplus
}
#endif

#endif /* __DEMO_VCAM_H__ */
