/**
 * @file demo_vcam_selftest.c
 * @brief demo_vcam 独立自测：喂满 80 帧后校验索引统计、帧长、时间戳与 I 帧分布。
 *
 * 构建产物为独立可执行文件 vcam_selftest（与 p2p_demo 链接同一组库），
 * 用法：./build/vcam_selftest [视频路径]（默认可执行文件同目录的 demo_video.264，
 * CMake configure 期已把 assets/demo_video.264 复制到构建目录）。
 *
 * 校验口径分两档：
 *   - **内置视频（未传参数）走绝对口径**：AU 总数 / IDR 数 / 首帧长度 / I 帧下标
 *     都按 demo_video.264 的真实值钉死（真值来自 ffprobe -count_packets 与 NAL
 *     直方图：540 AU、9 IDR@每 60 帧、首 AU 54923B）。切分逻辑被改坏（如把
 *     SPS/PPS 切成独立帧、把 P 帧误判成 I 帧）时这些断言必然失败。
 *   - 自定义文件（传了参数）只做宽松下界检查（帧数/递增/非空），因为真值未知。
 *
 * 退出码：0 = PASSED，1 = 校验失败/打开失败。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "demo_vcam.h"
#include "tal_api.h"
#include "tal_api_ops.h"

/* 自测收帧数：15fps 下约 5.3 秒；内置视频 GOP=60，80 帧内应含 2 个 IDR（下标 0/60） */
#define VCAM_SELFTEST_FRAMES 80
#define VCAM_SELFTEST_FPS    15
#define VCAM_SELFTEST_WAIT_MS 10000 /* 收帧等待上限，超时判失败 */

/* 内置 demo_video.264 的绝对真值（ffprobe/NAL 直方图口径，换视频需同步更新）。
 * 绝对口径的判定按"文件大小 == 内置视频大小"（见 main），换任何别的视频自动降为宽松口径 */
#define VCAM_SELFTEST_EXPECT_AU       540u      /* AU 总数 */
#define VCAM_SELFTEST_EXPECT_IDR      9u        /* IDR AU 数（GOP=60） */
#define VCAM_SELFTEST_EXPECT_FIRST_AU 54923u    /* 首个 AU（SPS+PPS+SEI+IDR）字节数 */
#define VCAM_SELFTEST_EXPECT_MAX_AU   (64u * 1024u) /* 单 AU 上限（P2P 帧槽 256KB 内） */
#define VCAM_SELFTEST_BUNDLED_SIZE    4465130u      /* 内置视频字节数（绝对口径的判定依据） */

/* 视频源路径：未注入时兜底 "demo_video.264"（与 tuya_ipc_demo.c 同口径） */
#ifndef P2P_DEMO_H264_FILE
#define P2P_DEMO_H264_FILE "demo_video.264"
#endif

/* 回调运行在 vcam 线程：计数用 volatile，帧明细由回调线程写、主线程在
 * close（等待线程退出）之后读 */
static volatile uint32_t s_cb_count = 0;
static uint32_t          s_frame_len[VCAM_SELFTEST_FRAMES];
static uint64_t          s_frame_ts[VCAM_SELFTEST_FRAMES];
static BOOL_T            s_frame_is_i[VCAM_SELFTEST_FRAMES];
static uint32_t          s_frame_i_num = 0;

/**
 * @brief 帧回调：记录前 80 帧的长度/时间戳/I 帧标记
 */
static void __selftest_frame_cb(const DEMO_VCAM_FRAME_T *frame)
{
    uint32_t n = s_cb_count;

    if (n < VCAM_SELFTEST_FRAMES) {
        s_frame_len[n]  = frame->len;
        s_frame_ts[n]   = frame->ts_ms;
        s_frame_is_i[n] = frame->is_i_frame;
        if (frame->is_i_frame) {
            s_frame_i_num++;
        }
    }
    s_cb_count = n + 1;
}

/**
 * @brief 日志输出回调（与 iot_demo 同款：经 port 的 port_log_output 落 stdout）
 */
static void __selftest_log_output_cb(const char *str)
{
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->log_output) {
        ops->log_output(str);
    }
}

/**
 * @brief 默认视频路径：优先可执行文件同目录（构建目录的 demo_video.264，
 *        configure 期从 assets/ 复制而来），取不到再回退当前目录
 *
 * 这样 `./build/vcam_selftest`（从 example 目录执行）也能直接跑。
 *
 * @return 视频路径（静态缓冲，无需释放）
 */
static const char *__selftest_default_path(void)
{
    static char exe_dir_path[1024];
    char        link[512];
    ssize_t     n   = 0;
    char       *slash = NULL;

    /* P2P_DEMO_H264_FILE 可被 Kconfig/-D 覆盖（CMake 注入本目标） */
    if (0 == access(P2P_DEMO_H264_FILE, R_OK)) {
        return P2P_DEMO_H264_FILE;
    }
    n = readlink("/proc/self/exe", link, sizeof(link) - 1);
    if (n <= 0) {
        return P2P_DEMO_H264_FILE;
    }
    link[n] = '\0';
    slash   = strrchr(link, '/');
    if (NULL == slash) {
        return P2P_DEMO_H264_FILE;
    }
    slash[1] = '\0';
    snprintf(exe_dir_path, sizeof(exe_dir_path), "%s%s", link, P2P_DEMO_H264_FILE);
    if (0 == access(exe_dir_path, R_OK)) {
        return exe_dir_path;
    }

    return P2P_DEMO_H264_FILE;
}

/**
 * @brief 文件大小（用于判定是否内置视频；取不到返回 0）
 */
static uint32_t __selftest_file_size(const char *path)
{
    struct stat st;

    if (path == NULL || 0 != stat(path, &st)) {
        return 0;
    }
    return (uint32_t)st.st_size;
}

int main(int argc, char *argv[])
{
    const char *path = (argc > 1) ? argv[1] : __selftest_default_path();
    /* 内置视频走绝对口径（按文件大小判定，含 -D 覆盖后的路径）；
     * 自定义文件真值未知，只做宽松下界 */
    BOOL_T      bundled   = (__selftest_file_size(path) == VCAM_SELFTEST_BUNDLED_SIZE) ? TRUE : FALSE;
    OPERATE_RET rt        = OPRT_OK;
    uint32_t    i         = 0;
    uint32_t    waited_ms = 0;
    uint32_t    fail      = 0;
    uint32_t    au_num = 0, idr_num = 0, max_au = 0;
    BOOL_T      first_is_i = FALSE;

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, __selftest_log_output_cb);

    rt = demo_vcam_open(path, VCAM_SELFTEST_FPS, __selftest_frame_cb);
    if (OPRT_OK != rt) {
        printf("VCAM SELFTEST FAILED: open '%s' ret %d\n", path, rt);
        return 1;
    }
    printf("vcam opened (%s), mode=%s, waiting %u frames...\n", path, bundled ? "absolute" : "loose",
           VCAM_SELFTEST_FRAMES);

    /* 校验 0：索引统计（AU 总数 / IDR 数 / 最大 AU / 首帧是否 IDR） */
    if (OPRT_OK != demo_vcam_get_stats(&au_num, &idr_num, &max_au, &first_is_i)) {
        printf("VCAM SELFTEST FAILED: get_stats failed\n");
        demo_vcam_close();
        return 1;
    }
    printf("[check] index: %u AU, %u IDR, max AU %u B, first %s IDR\n", au_num, idr_num, max_au,
           first_is_i ? "is" : "not");
    if (bundled) {
        if (au_num != VCAM_SELFTEST_EXPECT_AU) {
            printf("VCAM SELFTEST FAILED: AU count %u != %u (切分逻辑被改坏?)\n", au_num,
                   (uint32_t)VCAM_SELFTEST_EXPECT_AU);
            fail++;
        }
        if (idr_num != VCAM_SELFTEST_EXPECT_IDR) {
            printf("VCAM SELFTEST FAILED: IDR count %u != %u (IDR 判定被改坏?)\n", idr_num,
                   (uint32_t)VCAM_SELFTEST_EXPECT_IDR);
            fail++;
        }
        if (max_au > VCAM_SELFTEST_EXPECT_MAX_AU) {
            printf("VCAM SELFTEST FAILED: max AU %u > %u\n", max_au, (uint32_t)VCAM_SELFTEST_EXPECT_MAX_AU);
            fail++;
        }
        if (!first_is_i) {
            printf("VCAM SELFTEST FAILED: first AU is not IDR\n");
            fail++;
        }
    } else if (au_num == 0 || idr_num == 0) {
        printf("VCAM SELFTEST FAILED: empty index (AU=%u IDR=%u)\n", au_num, idr_num);
        fail++;
    }

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
        if (bundled && s_frame_len[0] != VCAM_SELFTEST_EXPECT_FIRST_AU) {
            printf("VCAM SELFTEST FAILED: first emitted frame len %u != %u (AU 边界被改坏?)\n", s_frame_len[0],
                   (uint32_t)VCAM_SELFTEST_EXPECT_FIRST_AU);
            fail++;
        }
    }
    /* 校验 4：I 帧分布。内置视频 GOP=60 → 前 80 帧的 IDR 必须恰好在下标 0 与 60
     * （把 P 帧误标成 I 帧、或漏标 IDR 都会在这里失败）；自定义文件只要求至少 1 个 */
    printf("[check] I-frame in %u frames: %u\n", VCAM_SELFTEST_FRAMES, s_frame_i_num);
    if (bundled) {
        if (!s_frame_is_i[0] || s_frame_i_num != 2) {
            printf("VCAM SELFTEST FAILED: expect exactly 2 IDR (idx 0/60), got %u\n", s_frame_i_num);
            fail++;
        } else {
            for (i = 1; i < VCAM_SELFTEST_FRAMES; i++) {
                if (s_frame_is_i[i] && i != 60) {
                    printf("VCAM SELFTEST FAILED: unexpected IDR at frame %u (P 帧被误判?)\n", i);
                    fail++;
                    break;
                }
            }
        }
    } else if (s_frame_i_num < 1) {
        printf("VCAM SELFTEST FAILED: no IDR in %u frames\n", VCAM_SELFTEST_FRAMES);
        fail++;
    }

    if (fail) {
        printf("VCAM SELFTEST FAILED: %u check(s) failed\n", fail);
        return 1;
    }

    printf("VCAM SELFTEST PASSED\n");
    return 0;
}
