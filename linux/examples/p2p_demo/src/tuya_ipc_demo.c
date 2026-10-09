/**
 * @file tuya_ipc_demo.c
 * @brief p2p_demo 媒体链路：vcam 样片源 → 帧队列/ring buffer → P2P 取帧回调。
 *
 * 上游蓝本：apps/tuya_cloud/ipc_p2p_demo/src/tuya_ipc_demo.c（T5AI 板 GC2145
 * DVP 摄像头 + 硬编 H264 + 板载音频），本移植的明确改写：
 *   - 摄像头 → demo_vcam（内置 Annex-B 样片按帧率循环喂帧，接口对齐上游
 *     __demo_camera_* 系列）；帧回调签名随之改为 DEMO_VCAM_FRAME_CB（void 返回），
 *     并删掉 is_complete/total_frame_len 检查（vcam 的 AU 天然完整）；
 *   - 音频整体不做（与上游 DEMO_HAS_AUDIO=0 分支一致：取音频帧返回 -1，
 *     其余音频回调空实现）；
 *   - PSRAM（ENABLE_EXT_RAM）分支删除：Linux 无 PSRAM，统一 tal_malloc/tal_free；
 *   - SD 卡挂载（tkl_fs_mount）删除：Linux 无挂载语义，local_store 直接落在
 *     进程 cwd 的 ./media；
 *   - 时间戳照上游在帧回调到达时取 tuya_p2p_misc_get_current_time_ms()（与 P2P
 *     栈同一时钟基准），不用 vcam 的 ts_ms。
 *
 * 数据流（与上游一致）：
 *   vcam 帧回调 → 校验 Annex-B → 2 槽帧队列 + ring buffer（可选 local_store 录制）
 *   → P2P 取帧回调 demo_on_get_video_frame_callback（优先 ring buffer，回退队列）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tuya_ipc_demo.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_system.h"
#include "demo_media_event.h"
#include "demo_vcam.h"

/* 与 P2P 栈同一时钟基准：该符号无公共头，上游同样在 app 内自 extern */
extern uint64_t tuya_p2p_misc_get_current_time_ms(void);

#if defined(ENABLE_IPC_RING_BUFFER) && (ENABLE_IPC_RING_BUFFER == 1)
#include "tuya_ring_buffer.h"
#define DEMO_HAS_RING_BUFFER 1
#else
#define DEMO_HAS_RING_BUFFER 0
#endif

#if DEMO_HAS_LOCAL_STORE
#include "local_store.h"
#endif

#define DEMO_VIDEO_CODEC TY_AV_CODEC_VIDEO_H264

/* 直播录制：demo 开关（P2P_DEMO_LOCAL_STORE）与录制开关同时打开才启用 */
#if DEMO_HAS_LOCAL_STORE && defined(P2P_DEMO_LIVE_RECORD) && (P2P_DEMO_LIVE_RECORD == 1)
#define DEMO_LIVE_RECORD 1
#ifndef P2P_DEMO_RECORD_MAX_SEC
#define P2P_DEMO_RECORD_MAX_SEC 120
#endif
#else
#define DEMO_LIVE_RECORD 0
#endif

#if !defined(P2P_DEMO_WIDTH) || !defined(P2P_DEMO_HEIGHT) || !defined(P2P_DEMO_FPS) || !defined(P2P_DEMO_KBPS)
#error "CMake 必须注入 P2P_DEMO_{WIDTH,HEIGHT,FPS,KBPS}（见 CMakeLists.txt / Kconfig）"
#endif

#define DEMO_CAM_WIDTH  P2P_DEMO_WIDTH
#define DEMO_CAM_HEIGHT P2P_DEMO_HEIGHT
#define DEMO_CAM_FPS    P2P_DEMO_FPS
#define DEMO_CAM_KBPS   P2P_DEMO_KBPS

#ifndef P2P_DEMO_GOP
#define P2P_DEMO_GOP 30
#endif
#define DEMO_CAM_GOP P2P_DEMO_GOP

/* 上报给云的 fps：与喂帧帧率一致。上游此处硬编码 25、与实际 20fps 摄像头不一致，
 * 文件源改为如实上报（P2P_DEMO_FPS），否则 App 的节奏估算与实际码流不符。 */
#define DEMO_AV_FPS DEMO_CAM_FPS

/* 样片路径：未注入时兜底 "demo_video.264"（构建目录里由 CMake configure 期从
 * assets/ 复制而来）；实际解析顺序见 __demo_h264_path() */
#ifndef P2P_DEMO_H264_FILE
#define P2P_DEMO_H264_FILE "demo_video.264"
#endif

/* 必须与 p2p_init() 的 media_frame 缓冲一致；不要读 media_frame->size */
#define DEMO_P2P_FRAME_CAP    (300 * 1024)
#define DEMO_FRAME_BUF_SIZE   (256 * 1024)
#define DEMO_P2P_QUEUE_DEPTH  2

typedef struct {
    uint32_t len;
    BOOL_T   is_key;
    uint64_t ts_ms;
} DEMO_P2P_Q_SLOT_T;

static MUTEX_HANDLE      s_frame_mutex = NULL;
static uint8_t          *s_q_pool[DEMO_P2P_QUEUE_DEPTH] = {0};
static DEMO_P2P_Q_SLOT_T s_q_slot[DEMO_P2P_QUEUE_DEPTH];
static uint32_t          s_q_head = 0;
static uint32_t          s_q_tail = 0;
static uint32_t          s_q_count = 0;
static uint32_t          s_frame_slot_cap = 0;
static BOOL_T            s_media_ready = FALSE;
static BOOL_T            s_live_push_enable = FALSE;
static BOOL_T            s_live_want_cam = FALSE;
static MUTEX_HANDLE      s_cam_lock = NULL;
static BOOL_T            s_queue_need_iframe = FALSE;
static BOOL_T            s_cam_running = FALSE;

#if DEMO_HAS_RING_BUFFER
static RING_BUFFER_USER_HANDLE_T s_ring_w = NULL;
static RING_BUFFER_USER_HANDLE_T s_ring_r = NULL;
static BOOL_T                    s_ring_ready = FALSE;
#endif

/**
 * @brief 清空帧队列，并强制等到下一个 I 帧再入队（丢帧后解码器需要关键帧）
 */
static void __demo_p2p_queue_clear(void)
{
    s_q_head = 0;
    s_q_tail = 0;
    s_q_count = 0;
    s_queue_need_iframe = TRUE;
}

/**
 * @brief 帧入队（2 槽环形队列；满则丢最旧，并重新等 I 帧）
 */
static OPERATE_RET __demo_p2p_queue_push(const uint8_t *data, uint32_t len, BOOL_T is_key, uint64_t ts_ms)
{
    uint8_t *dst;

    if (data == NULL || len == 0 || len > s_frame_slot_cap) {
        return OPRT_INVALID_PARM;
    }
    if (s_q_count >= DEMO_P2P_QUEUE_DEPTH) {
        s_q_head = (s_q_head + 1U) % DEMO_P2P_QUEUE_DEPTH;
        s_q_count--;
        s_queue_need_iframe = TRUE;
    }
    if (s_queue_need_iframe && !is_key) {
        return OPRT_OK;
    }
    dst = s_q_pool[s_q_tail];
    if (dst == NULL) {
        return OPRT_COM_ERROR;
    }
    memcpy(dst, data, len);
    s_q_slot[s_q_tail].len = len;
    s_q_slot[s_q_tail].is_key = is_key;
    s_q_slot[s_q_tail].ts_ms = ts_ms;
    s_q_tail = (s_q_tail + 1U) % DEMO_P2P_QUEUE_DEPTH;
    s_q_count++;
    if (is_key) {
        s_queue_need_iframe = FALSE;
    }
    return OPRT_OK;
}

/**
 * @brief 帧出队，拷进 P2P 的 MEDIA_FRAME（调用方持 s_frame_mutex）
 */
static OPERATE_RET __demo_p2p_queue_pop(MEDIA_FRAME *media_frame)
{
    const DEMO_P2P_Q_SLOT_T *slot;
    const uint8_t           *src;

    if (media_frame == NULL || media_frame->data == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (s_q_count == 0) {
        return OPRT_NOT_FOUND;
    }
    slot = &s_q_slot[s_q_head];
    src = s_q_pool[s_q_head];
    if (src == NULL || slot->len == 0 || slot->len > DEMO_P2P_FRAME_CAP) {
        __demo_p2p_queue_clear();
        return OPRT_COM_ERROR;
    }
    memcpy(media_frame->data, src, slot->len);
    media_frame->size = slot->len;
    media_frame->type = slot->is_key ? eVideoIFrame : eVideoPBFrame;
    media_frame->pts = slot->ts_ms;
    media_frame->timestamp = (uint32_t)slot->ts_ms;
    s_q_head = (s_q_head + 1U) % DEMO_P2P_QUEUE_DEPTH;
    s_q_count--;
    return OPRT_OK;
}

#if DEMO_LIVE_RECORD
/**
 * @brief 开始直播录像（local_store，分段时长 P2P_DEMO_RECORD_MAX_SEC）
 */
static void __demo_rec_start(void)
{
    OPERATE_RET rt = local_store_rec_start("live", P2P_DEMO_RECORD_MAX_SEC);

    if (rt != OPRT_OK) {
        PR_ERR("live rec start failed: %d", rt);
    }
}

/**
 * @brief 停止直播录像
 */
static void __demo_rec_stop(void)
{
    (void)local_store_rec_stop();
}
#endif /* DEMO_LIVE_RECORD */

/**
 * @brief 校验 AU 是否含 Annex-B 起始码（00 00 01 / 00 00 00 01）
 */
static BOOL_T __demo_au_has_annexb(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL || len < 4) {
        return FALSE;
    }
    for (i = 0; i + 3 < len; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            return TRUE;
        }
        if (i + 4 < len && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * @brief vcam 帧回调（运行在 vcam 线程）：校验 → 帧队列 + ring buffer + 录制
 *
 * 与上游 __demo_encoded_frame_cb 的差异：签名改为 DEMO_VCAM_FRAME_CB（void 返回）；
 * 删 is_complete/total_frame_len 检查（vcam 的 AU 天然完整）；时间戳照上游取
 * tuya_p2p_misc_get_current_time_ms()（与 P2P 栈同时钟基准）。
 */
static void __demo_encoded_frame_cb(const DEMO_VCAM_FRAME_T *frame)
{
    BOOL_T   is_key;
    uint64_t ts_ms;

    if (frame == NULL || frame->data == NULL || frame->len == 0) {
        return;
    }
    if (s_frame_mutex == NULL || s_frame_slot_cap == 0) {
        return;
    }
    if (!s_live_push_enable) {
        return;
    }
    if (frame->len > s_frame_slot_cap) {
        PR_WARN("encoded frame too large: %u > %u", (uint32_t)frame->len, (uint32_t)s_frame_slot_cap);
        return;
    }
    if (!__demo_au_has_annexb(frame->data, frame->len)) {
        static uint32_t s_bad_au_cnt = 0;
        if ((s_bad_au_cnt++ % 30) == 0) {
            PR_NOTICE("drop AU without Annex-B start code len=%u cnt=%u", (uint32_t)frame->len, s_bad_au_cnt);
        }
        return;
    }

    is_key = frame->is_i_frame ? TRUE : FALSE;
    ts_ms = tuya_p2p_misc_get_current_time_ms();

    if (s_live_push_enable) {
        tal_mutex_lock(s_frame_mutex);
        (void)__demo_p2p_queue_push(frame->data, frame->len, is_key, ts_ms);
        tal_mutex_unlock(s_frame_mutex);
    }

#if DEMO_HAS_RING_BUFFER
    if (s_live_push_enable && s_ring_ready && s_ring_w != NULL) {
        (void)tuya_ipc_ring_buffer_append_data_with_timestamp(s_ring_w, (uint8_t *)frame->data, frame->len,
                                                             is_key ? E_VIDEO_I_FRAME : E_VIDEO_PB_FRAME,
                                                             ts_ms * 1000ULL, ts_ms);
    }
#endif

#if DEMO_LIVE_RECORD
    (void)local_store_rec_write((const uint8_t *)frame->data, frame->len, ts_ms, is_key);
#endif
}

/**
 * @brief 解析样片路径：先按 cwd，再按可执行文件所在目录拼一次
 *
 * P2P_DEMO_H264_FILE 由 CMake 注入（Kconfig 可覆盖），未注入时兜底
 * "demo_video.264"——验收命令是在 example 目录里执行 ./build/p2p_demo，cwd 下
 * 没有样片，需要再按可执行文件（build/p2p_demo）所在目录找到 build/demo_video.264。
 *
 * @return 可读的样片路径（静态缓冲，无需释放）；两处都找不到返回 NULL
 */
static const char *__demo_h264_path(void)
{
    static char resolved[1024];
    const char *path  = P2P_DEMO_H264_FILE;
    char        link[512];
    ssize_t     n     = 0;
    char       *slash = NULL;

    if (path[0] == '\0') {
        return NULL;
    }
    if (access(path, R_OK) == 0) {
        return path; /* cwd 下的相对路径或绝对路径，直接可读 */
    }
    if (path[0] == '/') {
        return NULL; /* 绝对路径不可读：不再按 exe 目录拼 */
    }
    n = readlink("/proc/self/exe", link, sizeof(link) - 1);
    if (n <= 0) {
        return NULL;
    }
    link[n] = '\0';
    slash   = strrchr(link, '/');
    if (slash == NULL) {
        return NULL;
    }
    slash[1] = '\0';
    snprintf(resolved, sizeof(resolved), "%s%s", link, path);
    if (access(resolved, R_OK) != 0) {
        return NULL;
    }
    return resolved;
}

/**
 * @brief 对外暴露的样片路径解析（demo_media_event 的 PB_SEED 与直播同源）
 *
 * @return 可读的样片路径（静态缓冲）；找不到返回 NULL
 */
const char *demo_h264_source_path(void)
{
    return __demo_h264_path();
}

/**
 * @brief 视频源是否就绪：样片文件可读即视为 present（替代上游摄像头探测）
 */
static BOOL_T __demo_camera_present(void)
{
    const char *path = __demo_h264_path();
    FILE       *fp   = NULL;

    if (path == NULL) {
        PR_ERR("sample not found: run from build/ or set P2P_DEMO_H264_FILE");
        return FALSE;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        PR_ERR("sample '%s' not readable", path);
        return FALSE;
    }
    fclose(fp);
    return TRUE;
}

/**
 * @brief 打开虚拟摄像头（demo_vcam_open），失败即报错返回
 */
static OPERATE_RET __demo_camera_open(void)
{
    const char *path = NULL;
    OPERATE_RET rt   = OPRT_OK;

    if (s_cam_running) {
        return OPRT_OK;
    }

    path = __demo_h264_path();
    if (path == NULL) {
        PR_ERR("sample not found: run from build/ or set P2P_DEMO_H264_FILE");
        return OPRT_NOT_EXIST;
    }

    rt = demo_vcam_open(path, DEMO_CAM_FPS, __demo_encoded_frame_cb);
    if (rt != OPRT_OK) {
        PR_ERR("demo_vcam_open('%s') failed: %d", path, rt);
        return rt;
    }

    s_cam_running = TRUE;
    PR_NOTICE("live vcam started: %ux%u@%u %s (source=%s)", (uint32_t)DEMO_CAM_WIDTH, (uint32_t)DEMO_CAM_HEIGHT,
              (uint32_t)DEMO_CAM_FPS, DEMO_VIDEO_CODEC == TY_AV_CODEC_VIDEO_H265 ? "H265" : "H264", path);
    return OPRT_OK;
}

/**
 * @brief 关闭虚拟摄像头（约束：不得在 vcam 帧回调内调用，见 demo_vcam.h）
 */
static void __demo_camera_close(void)
{
    OPERATE_RET rt;

    if (!s_cam_running) {
        return;
    }
    rt = demo_vcam_close();
    if (rt != OPRT_OK) {
        /* close 超时：vcam 保留缓冲与线程（宁可泄漏也不 use-after-free），此处只报错 */
        PR_ERR("vcam close failed: %d", rt);
    }
    s_cam_running = FALSE;
    PR_NOTICE("live vcam stopped");
}

/**
 * @brief App 请求 I 帧：转发给 vcam（下一拍跳到下一个 IDR AU）
 */
static OPERATE_RET __demo_camera_request_i_frame(void)
{
    return demo_vcam_request_i_frame();
}

/**
 * @brief 调整码率：样片源没有编码器，无法改码率（同上游占位）
 */
static OPERATE_RET __demo_camera_set_bitrate(uint32_t kbps)
{
    (void)kbps;
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief 按 s_live_want_cam 开/关 vcam（持 s_cam_lock）
 */
static void __demo_camera_sync(void)
{
    BOOL_T wanted;

    if (s_cam_lock == NULL) {
        return; /* tuya_ipc_demo_start() 之前：还没有人持锁 */
    }

    tal_mutex_lock(s_cam_lock);
    wanted = s_live_want_cam;

    if (wanted && !s_cam_running) {
        if (__demo_camera_open() != OPRT_OK) {
            PR_ERR("vcam open failed, nothing will be streamed");
        }
    } else if (!wanted && s_cam_running) {
        __demo_camera_close();
    }
    tal_mutex_unlock(s_cam_lock);
}

/**
 * @brief 组装并上报 AV 能力（只报视频；音频字段整段删除）
 */
static void __demo_init_p2p_av_info(BOOL_T from_camera)
{
    TRANS_IPC_AV_INFO_T av_info;
    OPERATE_RET         rt;
    uint32_t            w, h, fps, gop, kbps;
    int                 i;

    if (from_camera) {
        w = DEMO_CAM_WIDTH;
        h = DEMO_CAM_HEIGHT;
        fps = DEMO_AV_FPS;
        gop = DEMO_CAM_GOP;
        kbps = DEMO_CAM_KBPS;
    } else {
        return;
    }

    memset(&av_info, 0, sizeof(av_info));
    for (i = 0; i < 2; i++) {
        int s = (i == 0) ? eIpcStreamVideoMain : eIpcStreamVideoSub;

        av_info.video_codec[s] = DEMO_VIDEO_CODEC;
        av_info.fps[s] = fps;
        av_info.gop[s] = gop;
        av_info.bitrate[s] = kbps;
        av_info.width[s] = w;
        av_info.height[s] = h;
    }

    rt = tuya_ipc_init_trans_av_info(&av_info);
    if (rt != OPRT_OK) {
        /* 无 p2p 会话时（如媒体链路自测）会走到这里，属预期，不阻断媒体启动 */
        PR_ERR("tuya_ipc_init_trans_av_info failed: %d", rt);
    }
}

/**
 * @brief 初始化 demo 媒体链路：帧池 / AV info / ring buffer / local_store
 */
void tuya_ipc_demo_start(void)
{
    OPERATE_RET rt;
    BOOL_T      have_camera;
    uint32_t    i;
    const char *src;

    if (s_media_ready) {
        return;
    }

    rt = tal_mutex_create_init(&s_frame_mutex);
    if (rt != OPRT_OK) {
        PR_ERR("frame mutex create failed: %d", rt);
        return;
    }
    rt = tal_mutex_create_init(&s_cam_lock);
    if (rt != OPRT_OK) {
        PR_ERR("camera mutex create failed: %d", rt);
        return;
    }

    s_live_push_enable = FALSE;
    s_live_want_cam = FALSE;
    __demo_p2p_queue_clear();

    for (i = 0; i < DEMO_P2P_QUEUE_DEPTH; i++) {
        s_q_pool[i] = (uint8_t *)tal_malloc(DEMO_FRAME_BUF_SIZE);
        if (s_q_pool[i] == NULL) {
            PR_ERR("alloc p2p queue slot %u failed", i);
            return;
        }
    }
    s_frame_slot_cap = DEMO_FRAME_BUF_SIZE;

    have_camera = __demo_camera_present();
    if (!have_camera) {
        /* 具体原因（找不到/不可读）已在 __demo_camera_present 里打印 */
        PR_ERR("no video source, live start will fail");
    }

    __demo_init_p2p_av_info(have_camera);

#if DEMO_HAS_RING_BUFFER
    {
        RING_BUFFER_INIT_PARAM_T rp = {0};

        rp.bitrate = DEMO_CAM_KBPS;
        rp.fps = DEMO_AV_FPS;
        rp.max_buffer_seconds = 2;
        if (tuya_ipc_ring_buffer_init(0, 0, E_IPC_STREAM_VIDEO_MAIN, &rp) == OPRT_OK) {
            s_ring_w = tuya_ipc_ring_buffer_open(0, 0, E_IPC_STREAM_VIDEO_MAIN, E_RBUF_WRITE);
            s_ring_r = tuya_ipc_ring_buffer_open(0, 0, E_IPC_STREAM_VIDEO_MAIN, E_RBUF_READ);
            s_ring_ready = (s_ring_w != NULL && s_ring_r != NULL) ? TRUE : FALSE;
            PR_NOTICE("ring_buffer live video %s", s_ring_ready ? "ready" : "open fail");
        }
    }
#endif

#if DEMO_HAS_LOCAL_STORE
    /* Linux 无挂载语义：local_store 直接落在进程 cwd 的 ./media，不做 tkl_fs_mount */
    if (local_store_init() != OPRT_OK) {
        PR_ERR("local_store init failed");
    }
#endif

    s_media_ready = TRUE;
    demo_media_event_register();

    src = __demo_h264_path();
    PR_NOTICE("tuya_ipc_demo: ready (source=%s, sensor %ux%u@%u, av fps %u, live_rec=%d)",
              (src != NULL) ? src : "none", (uint32_t)DEMO_CAM_WIDTH, (uint32_t)DEMO_CAM_HEIGHT,
              (uint32_t)DEMO_CAM_FPS, (uint32_t)DEMO_AV_FPS, (int)DEMO_LIVE_RECORD);
}

/**
 * @brief 释放 demo 媒体链路资源
 */
void tuya_ipc_demo_end(void)
{
    uint32_t i;

    s_live_push_enable = FALSE;
    s_live_want_cam = FALSE;
    if (s_cam_lock != NULL) {
        tal_mutex_lock(s_cam_lock);
        __demo_camera_close();
        tal_mutex_unlock(s_cam_lock);
    } else {
        __demo_camera_close();
    }
    s_media_ready = FALSE;

    if (s_frame_mutex != NULL) {
        tal_mutex_lock(s_frame_mutex);
        __demo_p2p_queue_clear();
        tal_mutex_unlock(s_frame_mutex);
    }
    for (i = 0; i < DEMO_P2P_QUEUE_DEPTH; i++) {
        if (s_q_pool[i] != NULL) {
            tal_free(s_q_pool[i]);
            s_q_pool[i] = NULL;
        }
    }
    s_frame_slot_cap = 0;
    if (s_frame_mutex != NULL) {
        tal_mutex_release(s_frame_mutex);
        s_frame_mutex = NULL;
    }
    if (s_cam_lock != NULL) {
        tal_mutex_release(s_cam_lock);
        s_cam_lock = NULL;
    }
}

/**
 * @brief App 开始看直播：开 vcam、开录制、清队列、开推流
 */
int demo_on_live_video_start_callback(void)
{
    if (!s_media_ready) {
        return -1;
    }

    s_live_want_cam = TRUE;
    __demo_camera_sync();
    if (!s_cam_running) {
        PR_ERR("LIVE start: vcam open failed");
        s_live_want_cam = FALSE;
        return -1;
    }

#if DEMO_LIVE_RECORD
    if (!local_store_rec_is_open()) {
        __demo_rec_start();
    }
#endif

    if (s_frame_mutex != NULL) {
        tal_mutex_lock(s_frame_mutex);
        __demo_p2p_queue_clear();
        tal_mutex_unlock(s_frame_mutex);
    }

    s_live_push_enable = TRUE;
    PR_NOTICE("LIVE video start: push enabled");
    return 0;
}

/**
 * @brief App 停止看直播：停推流、清队列、关 vcam、停录制
 */
int demo_on_live_video_stop_callback(void)
{
    s_live_want_cam = FALSE;
    s_live_push_enable = FALSE;
    if (s_frame_mutex != NULL) {
        tal_mutex_lock(s_frame_mutex);
        __demo_p2p_queue_clear();
        tal_mutex_unlock(s_frame_mutex);
    }
    __demo_camera_sync();
#if DEMO_LIVE_RECORD
    __demo_rec_stop();
#endif
    PR_NOTICE("LIVE video stop: vcam %s", s_cam_running ? "kept for cloud recording" : "closed");
    return 0;
}

/**
 * @brief P2P 断链：停回放 + 走 live stop
 */
int demo_on_signal_disconnect_callback(void)
{
    demo_media_pb_stop();
    (void)demo_on_live_video_stop_callback();
    return 0;
}

/**
 * @brief P2P 取视频帧：优先 ring buffer，回退 2 槽帧队列；无帧返回 -1
 *
 * 注意：无帧时必须返回 -1——P2P 栈把返回 0 当作"有帧"继续发包，
 * 未填充的 MEDIA_FRAME 会走错误分支。
 */
int demo_on_get_video_frame_callback(MEDIA_FRAME *media_frame)
{
    OPERATE_RET rt;

    if (media_frame == NULL || media_frame->data == NULL || !s_media_ready) {
        tal_system_sleep(10);
        return -1;
    }

#if DEMO_HAS_RING_BUFFER
    if (s_ring_ready && s_ring_r != NULL) {
        RING_BUFFER_NODE_T *node = tuya_ipc_ring_buffer_get_frame(s_ring_r, FALSE);

        if (node != NULL && node->raw_data != NULL && node->size > 0 && node->size <= DEMO_P2P_FRAME_CAP) {
            memcpy(media_frame->data, node->raw_data, node->size);
            media_frame->size = node->size;
            media_frame->type = (node->type == E_VIDEO_I_FRAME) ? eVideoIFrame : eVideoPBFrame;
            media_frame->pts = node->timestamp;
            media_frame->timestamp = (uint32_t)node->timestamp;
            return 0;
        }
        tal_system_sleep(10);
        return -1;
    }
#endif

    tal_mutex_lock(s_frame_mutex);
    rt = __demo_p2p_queue_pop(media_frame);
    tal_mutex_unlock(s_frame_mutex);
    if (rt != OPRT_OK) {
        tal_system_sleep(10);
        return -1;
    }
    return 0;
}

/**
 * @brief App 请求 I 帧：真实转发 demo_vcam_request_i_frame（不接受即返回 -1）
 */
int demo_on_request_i_frame_callback(void)
{
    return (__demo_camera_request_i_frame() == OPRT_OK) ? 0 : -1;
}

/**
 * @brief App 调整码率：样片源无法改码率，如实返回 -1（同上游占位映射）
 */
int demo_on_set_video_bitrate_callback(uint32_t kbps)
{
    return (__demo_camera_set_bitrate(kbps) == OPRT_OK) ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * 音频：本 demo 不做音频采集（同上游 DEMO_HAS_AUDIO=0 分支的空实现）。
 * 取音频帧必须返回 -1（返回 0 会让 P2P 栈按"有帧"发送未填充的 MEDIA_FRAME）。
 * --------------------------------------------------------------------------- */

int demo_on_get_audio_frame_callback(MEDIA_FRAME *media_frame)
{
    (void)media_frame;
    return -1;
}

int demo_on_live_audio_start_callback(void)
{
    return 0;
}

int demo_on_live_audio_stop_callback(void)
{
    return 0;
}

int demo_on_recv_audio_frame_callback(MEDIA_FRAME *media_frame)
{
    (void)media_frame;
    return 0;
}
