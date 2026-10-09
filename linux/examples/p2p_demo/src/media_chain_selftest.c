/**
 * @file media_chain_selftest.c
 * @brief 媒体链路独立自测（无云、无 App）：vcam → 校验/队列/ring buffer → P2P 取帧回调。
 *
 * 构建产物为独立可执行文件 media_chain_selftest（与 p2p_demo 链接同一组库），
 * 用法：从构建目录执行 ./media_chain_selftest（样片路径由 demo 侧解析：
 * cwd 或可执行文件所在目录的 sample.h264）。
 *
 * 流程（全部用 demo 的公开契约函数，不新造接口）：
 *   1. tal 运行时初始化（time service + log）；
 *   2. tuya_ipc_demo_start()：建帧池 / ring buffer；无 p2p 会话时
 *      tuya_ipc_init_trans_av_info 优雅返回错误，日志报错属预期；
 *   3. demo_on_live_video_start_callback()：打开 vcam、开推流；
 *   4. 循环 demo_on_get_video_frame_callback() 收 60 帧有效帧（或 10s 超时），
 *      校验：帧 size ∈ (0, 300KB]、eVideoIFrame/eVideoPBFrame 都出现过、
 *      pts 单调不减；
 *   5. demo_on_request_i_frame_callback() 后 2s 内应出现 I 帧；
 *   6. demo_on_live_video_stop_callback() + tuya_ipc_demo_end()，
 *      打印 MEDIA CHAIN SELFTEST PASSED 并 return 0；任一校验失败 return 1。
 *
 * 退出码：0 = PASSED，1 = 校验失败/取帧超时。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <stdio.h>
#include <string.h>

#include "tal_api.h"
#include "tal_api_ops.h"
#include "tuya_ipc_demo.h"

/* 收帧目标：20fps 下约 3 秒；上限 10s */
#define SELFTEST_FRAMES         60
#define SELFTEST_WAIT_MS        10000
/* 请求 I 帧后的等待上限：GOP=20 @20fps 自然间隔 1s，请求后应更快出现 */
#define SELFTEST_IFRAME_WAIT_MS 2000
/* 请求后到下一个 I 帧之间允许的最大帧数：正常（vcam 跳到下个 IDR AU）≤ 几帧；
 * 请求被静默忽略时按 GOP=20 要等 ~19 个 P 帧——用帧数而非时长做断言，避免
 * "恰好在 GOP 末尾请求"导致的假通过 */
#define SELFTEST_IFRAME_MAX_FRAMES 8
/* 取帧缓冲：p2p_init 的 media_frame 缓冲为 300KB，自测缓冲对齐该上限 */
#define SELFTEST_FRAME_CAP      (300 * 1024)

/* 取帧回调把帧拷进 data 指向的缓冲；300KB 不能放栈上 */
static uint8_t s_frame_buf[SELFTEST_FRAME_CAP];

/**
 * @brief 日志输出回调（与 iot_demo/vcam 自测同款：经 port 的 tkl_log_output 落 stdout）
 */
static void __selftest_log_output_cb(const char *str)
{
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->log_output) {
        ops->log_output(str);
    }
}

int main(void)
{
    MEDIA_FRAME frame;
    uint32_t    got       = 0;
    uint32_t    waited_ms = 0;
    uint32_t    i_num     = 0;
    uint32_t    p_num     = 0;
    uint32_t    len_min   = 0xFFFFFFFFu;
    uint32_t    len_max   = 0;
    uint64_t    pts_first = 0;
    uint64_t    pts_last  = 0;
    uint32_t    fail      = 0;
    int         rt        = 0;

    tal_time_service_init();
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, __selftest_log_output_cb);

    printf("media chain selftest: start\n");

    /* 2) 媒体链路初始化（无 p2p 会话：AV info 上报失败属预期） */
    tuya_ipc_demo_start();

    /* 3) 开直播：打开 vcam、开推流 */
    if (demo_on_live_video_start_callback() != 0) {
        printf("MEDIA CHAIN SELFTEST FAILED: live video start (vcam open) failed\n");
        tuya_ipc_demo_end();
        return 1;
    }

    memset(&frame, 0, sizeof(frame));
    frame.data = s_frame_buf;

    /* 4) 收帧 + 逐帧校验（返回 0 = 有效帧；-1 = 无帧，回调内部已 sleep 10ms） */
    while (got < SELFTEST_FRAMES && waited_ms < SELFTEST_WAIT_MS) {
        rt = demo_on_get_video_frame_callback(&frame);
        if (rt != 0) {
            waited_ms += 10;
            continue;
        }
        got++;

        if (frame.size == 0 || frame.size > SELFTEST_FRAME_CAP) {
            printf("MEDIA CHAIN SELFTEST FAILED: frame %u size %u out of (0, %u]\n", got, frame.size,
                   (uint32_t)SELFTEST_FRAME_CAP);
            fail++;
            break;
        }
        if (frame.type == eVideoIFrame) {
            i_num++;
        } else if (frame.type == eVideoPBFrame) {
            p_num++;
        } else {
            printf("MEDIA CHAIN SELFTEST FAILED: frame %u unexpected type %d\n", got, (int)frame.type);
            fail++;
            break;
        }
        if (frame.size < len_min) {
            len_min = frame.size;
        }
        if (frame.size > len_max) {
            len_max = frame.size;
        }
        if (got == 1) {
            pts_first = frame.pts;
        } else if (frame.pts < pts_last) {
            printf("MEDIA CHAIN SELFTEST FAILED: pts not monotonic at frame %u (%llu < %llu)\n", got,
                   (unsigned long long)frame.pts, (unsigned long long)pts_last);
            fail++;
            break;
        }
        pts_last = frame.pts;
    }

    if (fail == 0) {
        if (got < SELFTEST_FRAMES) {
            printf("MEDIA CHAIN SELFTEST FAILED: only %u frames in %ums\n", got, waited_ms);
            fail++;
        } else if (i_num == 0 || p_num == 0) {
            printf("MEDIA CHAIN SELFTEST FAILED: frame types incomplete (I=%u, P=%u)\n", i_num, p_num);
            fail++;
        } else {
            printf("[check] frames: %u (I=%u, P=%u) in %ums\n", got, i_num, p_num, waited_ms);
            printf("[check] frame size: min %u, max %u (cap %u)\n", len_min, len_max, (uint32_t)SELFTEST_FRAME_CAP);
            printf("[check] pts: %llu -> %llu (monotonic non-decreasing)\n", (unsigned long long)pts_first,
                   (unsigned long long)pts_last);
        }
    }

    /* 5) 请求 I 帧：先在"刚收到 I 帧"的已知相位等待，再发请求，随后统计到下一个
     *    I 帧之间又收到几帧。vcam 语义：请求后下一拍直接跳到下一个 IDR AU（中间
     *    的 P 帧被跳过）——正常应 ≤ 几帧；若请求被静默忽略，按 GOP=20@20fps 还要
     *    等 ~19 个 P 帧（≈1s）才自然出现下一个 I 帧。 */
    if (fail == 0) {
        uint32_t iframe_waited = 0;
        uint32_t frames_after_req = 0;
        BOOL_T   got_i = FALSE;

        /* 5a) 等一个 I 帧，锁定相位（最多 2s） */
        while (iframe_waited < SELFTEST_IFRAME_WAIT_MS) {
            rt = demo_on_get_video_frame_callback(&frame);
            if (rt != 0) {
                iframe_waited += 10;
                continue;
            }
            if (frame.type == eVideoIFrame) {
                break;
            }
        }
        if (iframe_waited >= SELFTEST_IFRAME_WAIT_MS) {
            printf("MEDIA CHAIN SELFTEST FAILED: no i-frame to lock phase\n");
            fail++;
        } else if (demo_on_request_i_frame_callback() != 0) {
            printf("MEDIA CHAIN SELFTEST FAILED: request i-frame rejected\n");
            fail++;
        } else {
            /* 5b) 统计请求后到下一个 I 帧之间收到的帧数 */
            iframe_waited = 0;
            while (iframe_waited < SELFTEST_IFRAME_WAIT_MS) {
                rt = demo_on_get_video_frame_callback(&frame);
                if (rt != 0) {
                    iframe_waited += 10;
                    continue;
                }
                if (frame.type == eVideoIFrame) {
                    got_i = TRUE;
                    break;
                }
                frames_after_req++;
            }
            printf("[check] i-frame after request: %s (%u frame(s) in between, %ums)\n", got_i ? "yes" : "no",
                   frames_after_req, iframe_waited);
            if (!got_i) {
                printf("MEDIA CHAIN SELFTEST FAILED: no i-frame within %ums after request\n",
                       (uint32_t)SELFTEST_IFRAME_WAIT_MS);
                fail++;
            } else if (frames_after_req > SELFTEST_IFRAME_MAX_FRAMES) {
                printf("MEDIA CHAIN SELFTEST FAILED: %u frames before i-frame (> %u) — request likely ignored\n",
                       frames_after_req, (uint32_t)SELFTEST_IFRAME_MAX_FRAMES);
                fail++;
            }
        }
    }

    /* 6) 收尾（停流会关 vcam；end 释放帧池） */
    demo_on_live_video_stop_callback();
    tuya_ipc_demo_end();

    if (fail) {
        printf("MEDIA CHAIN SELFTEST FAILED: %u check(s) failed\n", fail);
        return 1;
    }

    printf("MEDIA CHAIN SELFTEST PASSED\n");
    return 0;
}
