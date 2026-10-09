/**
 * @file demo_vcam_selftest.c
 * @brief demo_vcam 独立自测：喂满 80 帧后校验帧数/时间戳/长度/I 帧分布。
 *
 * 构建产物为独立可执行文件 vcam_selftest（与 p2p_demo 链接同一组库），
 * 用法：./build/vcam_selftest [样片路径]（默认当前目录 demo_video.264，
 * CMake configure 期已把 assets/demo_video.264 复制到构建目录）。
 *
 * 退出码：0 = PASSED，1 = 校验失败/打开失败。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "demo_vcam.h"
#include "tal_api.h"
#include "tal_api_ops.h"

/* 自测收帧数：20fps 下约 2 秒；样片 GOP=20，40 帧内应含 2 个 IDR */
#define VCAM_SELFTEST_FRAMES 80
#define VCAM_SELFTEST_FPS    15
#define VCAM_SELFTEST_WAIT_MS 10000 /* 收帧等待上限，超时判失败 */

/* 回调运行在 vcam 线程：计数用 volatile，帧明细由回调线程写、主线程在
 * close（等待线程退出）之后读 */
static volatile uint32_t s_cb_count = 0;
static uint32_t          s_frame_len[VCAM_SELFTEST_FRAMES];
static uint64_t          s_frame_ts[VCAM_SELFTEST_FRAMES];
static uint32_t          s_frame_i_num = 0;

/**
 * @brief 帧回调：记录前 80 帧的长度/时间戳/I 帧标记
 */
static void __selftest_frame_cb(const DEMO_VCAM_FRAME_T *frame)
{
    uint32_t n = s_cb_count;

    if (n < VCAM_SELFTEST_FRAMES) {
        s_frame_len[n] = frame->len;
        s_frame_ts[n]  = frame->ts_ms;
        if (frame->is_i_frame) {
            s_frame_i_num++;
        }
    }
    s_cb_count = n + 1;
}

/**
 * @brief 日志输出回调（与 iot_demo 同款：经 port 的 tkl_log_output 落 stdout）
 */
static void __selftest_log_output_cb(const char *str)
{
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->log_output) {
        ops->log_output(str);
    }
}

/**
 * @brief 默认样片路径：优先可执行文件同目录（构建目录的 demo_video.264，
 *        configure 期从 assets/ 复制而来），取不到再回退当前目录
 *
 * 这样 `./build/vcam_selftest`（从 example 目录执行）也能直接跑。
 *
 * @return 样片路径（静态缓冲，无需释放）
 */
static const char *__selftest_default_path(void)
{
    static char exe_dir_path[1024];
    char        link[512];
    ssize_t     n   = 0;
    char       *slash = NULL;

    n = readlink("/proc/self/exe", link, sizeof(link) - 1);
    if (n <= 0) {
        return "demo_video.264";
    }
    link[n] = '\0';
    slash   = strrchr(link, '/');
    if (NULL == slash) {
        return "demo_video.264";
    }
    slash[1] = '\0';
    snprintf(exe_dir_path, sizeof(exe_dir_path), "%sdemo_video.264", link);
    if (0 == access(exe_dir_path, R_OK)) {
        return exe_dir_path;
    }

    return "demo_video.264";
}

int main(int argc, char *argv[])
{
    const char *path = (argc > 1) ? argv[1] : __selftest_default_path();
    OPERATE_RET rt   = OPRT_OK;
    uint32_t    i    = 0;
    uint32_t    waited_ms = 0;
    uint32_t    fail = 0;

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, __selftest_log_output_cb);

    rt = demo_vcam_open(path, VCAM_SELFTEST_FPS, __selftest_frame_cb);
    if (OPRT_OK != rt) {
        printf("VCAM SELFTEST FAILED: open '%s' ret %d\n", path, rt);
        return 1;
    }
    printf("vcam opened (%s), waiting %u frames...\n", path, VCAM_SELFTEST_FRAMES);

    /* 主线程轮询等待回调收满（回调在 vcam 线程） */
    while (s_cb_count < VCAM_SELFTEST_FRAMES && waited_ms < VCAM_SELFTEST_WAIT_MS) {
        tal_system_sleep(5);
        waited_ms += 5;
    }
    if (s_cb_count < VCAM_SELFTEST_FRAMES) {
        printf("VCAM SELFTEST FAILED: only %u frames in %ums\n", s_cb_count, waited_ms);
        demo_vcam_close();
        return 1;
    }

    demo_vcam_close();

    /* 校验 1：回调帧数 */
    printf("[check] callback frames: %u (expect >= %u)\n", s_cb_count, VCAM_SELFTEST_FRAMES);
    /* 校验 2：时间戳从 0 起、严格递增（按 1000/fps 递增） */
    if (0 != s_frame_ts[0]) {
        printf("VCAM SELFTEST FAILED: first ts_ms should start at 0\n");
        fail++;
    }
    for (i = 1; i < VCAM_SELFTEST_FRAMES; i++) {
        if (s_frame_ts[i] <= s_frame_ts[i - 1]) {
            printf("VCAM SELFTEST FAILED: ts not increasing at %u (%llu <= %llu)\n", i,
                   (unsigned long long)s_frame_ts[i], (unsigned long long)s_frame_ts[i - 1]);
            fail++;
            break;
        }
    }
    printf("[check] ts_ms: first %llu, last %llu, step %llu\n", (unsigned long long)s_frame_ts[0],
           (unsigned long long)s_frame_ts[VCAM_SELFTEST_FRAMES - 1],
           (unsigned long long)(s_frame_ts[1] - s_frame_ts[0]));
    /* 校验 3：每帧 len > 0（顺带统计最小/最大帧长） */
    {
        uint32_t len_min = s_frame_len[0];
        uint32_t len_max = s_frame_len[0];
        for (i = 0; i < VCAM_SELFTEST_FRAMES; i++) {
            if (0 == s_frame_len[i]) {
                printf("VCAM SELFTEST FAILED: frame %u len == 0\n", i);
                fail++;
                break;
            }
            if (s_frame_len[i] < len_min) {
                len_min = s_frame_len[i];
            }
            if (s_frame_len[i] > len_max) {
                len_max = s_frame_len[i];
            }
        }
        printf("[check] frame len: min %u, max %u\n", len_min, len_max);
    }
    /* 校验 4：80 帧内 I 帧数 >= 2（内置视频 GOP=60：帧 0 与 60 为 IDR） */
    printf("[check] I-frame in %u frames: %u (expect >= 2)\n", VCAM_SELFTEST_FRAMES, s_frame_i_num);
    if (s_frame_i_num < 2) {
        fail++;
    }

    if (fail) {
        printf("VCAM SELFTEST FAILED: %u check(s) failed\n", fail);
        return 1;
    }

    printf("VCAM SELFTEST PASSED\n");
    return 0;
}
