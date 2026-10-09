/**
 * @file demo_vcam.c
 * @brief 虚拟摄像头实现：Annex-B 样片 → AU 索引 → 按帧率循环喂帧。
 *
 * 上游 demo 的视频源是 tdl_camera（DVP 摄像头 + T5 硬编 H264），Linux 上
 * 没有对应实现，这里用样片文件顶替：整文件读入内存（样片约 200KB），
 * 按 Annex-B 起始码扫描出 NAL，再按 AU 边界建索引；一个 vcam 线程按
 * 1000/fps ms 的节拍回调 AU，用绝对时间戳做漂移校正（不裸 sleep 累积误差），
 * 到文件末尾循环回第 0 帧。
 *
 * AU 切分规则（与上游 demo 对编码帧的假设一致：AUD 开头、AU 内单 VCL）：
 *   - 遇 AUD（NAL type 9）→ 开新 AU（AUD 属于新 AU）
 *   - 遇 VCL（type 1..5）且当前 AU 已有 VCL → 开新 AU
 *   - 其余非 VCL（SPS/PPS/SEI）并入当前 AU
 *   - AU 内出现 type 5（IDR）→ is_i_frame = TRUE
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <stdio.h>
#include <string.h>

#include "demo_vcam.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tal_thread.h"

/* 样片大小安全上限：demo 允许全量驻留，但不接受明显异常的文件 */
#define DEMO_VCAM_MAX_FILE_SIZE (64 * 1024 * 1024)

/* 喂帧线程：栈与优先级沿用上游 demo 的线程习惯（4096 / THREAD_PRIO_3） */
#define DEMO_VCAM_THREAD_STACK 4096

/* 节拍等待分片：线程以 <=10ms 的粒度醒来检查停止/I 帧标志，close 能及时返回 */
#define DEMO_VCAM_WAIT_SLICE_MS 10

/* close 等待线程退出的上限（tal_thread_delete 只置停止态，线程实际退出在 vcam 线程内完成） */
#define DEMO_VCAM_EXIT_WAIT_MS 2000

/**
 * @brief AU 索引项
 */
typedef struct {
    uint32_t offset; /* AU 首字节在样片缓冲中的偏移（含起始码） */
    uint32_t len;    /* AU 字节数 */
    BOOL_T   is_i;   /* AU 内是否含 IDR（type 5） */
} DEMO_VCAM_AU_T;

/**
 * @brief vcam 全局状态（单实例；demo 只会同时开一路虚拟摄像头）
 */
static struct {
    uint8_t         *buf;     /* 样片整文件缓冲（tal_malloc） */
    uint32_t         buf_len; /* 样片字节数 */
    DEMO_VCAM_AU_T  *aus;     /* AU 索引数组 */
    uint32_t         au_num;  /* AU 总数 */
    uint32_t         i_num;   /* I 帧（IDR AU）数 */
    uint32_t         fps;
    DEMO_VCAM_FRAME_CB cb;
    THREAD_HANDLE    thread;
    volatile BOOL_T  running;      /* open 成功到 close 之间为 TRUE */
    volatile BOOL_T  stop;         /* close 置位，线程下一拍退出 */
    volatile BOOL_T  started;      /* 线程函数已进入（此时线程状态必为 RUNNING） */
    volatile BOOL_T  exited;       /* 线程函数已返回：此后不再触碰 buf，close 可安全释放 */
    volatile BOOL_T  want_i_frame; /* request_i_frame 置位，线程下一拍跳 IDR */
} s_vcam;

/**
 * @brief 在 [from, len) 内找下一个 Annex-B 起始码（00 00 01 / 00 00 00 01）
 *
 * @param[in]  buf    缓冲
 * @param[in]  len    缓冲长度
 * @param[in]  from   起始搜索偏移
 * @param[out] off    起始码首字节偏移
 * @param[out] sc_len 起始码长度（3 或 4）
 * @return TRUE 找到
 */
static BOOL_T __demo_vcam_find_sc(const uint8_t *buf, uint32_t len, uint32_t from, uint32_t *off, uint32_t *sc_len)
{
    uint32_t i = 0;

    for (i = from; i + 3 <= len; i++) {
        if (0 != buf[i] || 0 != buf[i + 1]) {
            continue;
        }
        if (1 == buf[i + 2]) { /* 00 00 01 */
            *off    = i;
            *sc_len = 3;
            return TRUE;
        }
        /* 4 字节起始码要在 3 字节判断之前识别，否则会把前导 0 丢掉 */
        if (0 == buf[i + 2] && i + 4 <= len && 1 == buf[i + 3]) { /* 00 00 00 01 */
            *off    = i;
            *sc_len = 4;
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * @brief 记录一个 AU（数组满则只计数，由调用方负责容量）
 */
static void __demo_vcam_emit(DEMO_VCAM_AU_T *aus, uint32_t cap, uint32_t idx, uint32_t off, uint32_t len, BOOL_T is_i)
{
    if (NULL == aus || idx >= cap) {
        return;
    }
    aus[idx].offset = off;
    aus[idx].len    = len;
    aus[idx].is_i   = is_i;
}

/**
 * @brief 扫描样片并按 AU 边界建索引
 *
 * @param[in]  buf 样片缓冲
 * @param[in]  len 缓冲长度
 * @param[out] aus AU 数组；为 NULL 时只统计数量（首轮定容量用）
 * @param[in]  cap 数组容量
 * @return AU 总数（aus 非空时只填前 cap 个）
 */
static uint32_t __demo_vcam_index(const uint8_t *buf, uint32_t len, DEMO_VCAM_AU_T *aus, uint32_t cap)
{
    uint32_t count      = 0;
    uint32_t cur_off    = 0;
    uint32_t cur_end    = 0;
    BOOL_T   cur_open   = FALSE;
    BOOL_T   cur_has_vcl = FALSE;
    BOOL_T   cur_is_i   = FALSE;
    uint32_t sc_off     = 0;
    uint32_t sc_len     = 0;
    BOOL_T   has        = __demo_vcam_find_sc(buf, len, 0, &sc_off, &sc_len);

    while (has) {
        uint32_t nal_off = sc_off;
        uint32_t nal_end = len;
        uint32_t next_off = 0;
        uint32_t next_len = 0;
        BOOL_T   has_next = FALSE;
        uint8_t  nal_type = 0;
        BOOL_T   is_vcl   = FALSE;
        BOOL_T   new_au   = FALSE;

        /* NAL 负载首字节缺失（起始码落在文件尾）时结束扫描 */
        if (nal_off + sc_len >= len) {
            break;
        }
        nal_type = buf[nal_off + sc_len] & 0x1F;
        is_vcl   = (nal_type >= 1 && nal_type <= 5);

        has_next = __demo_vcam_find_sc(buf, len, nal_off + sc_len, &next_off, &next_len);
        if (has_next) {
            nal_end = next_off;
        }

        if (cur_open) {
            if (9 == nal_type) { /* AUD：新 AU 开始 */
                new_au = TRUE;
            } else if (is_vcl && cur_has_vcl) { /* 第二个 VCL：上一 AU 结束 */
                new_au = TRUE;
            }
        }
        if (new_au) {
            __demo_vcam_emit(aus, cap, count, cur_off, cur_end - cur_off, cur_is_i);
            count++;
            cur_open = FALSE;
        }
        if (!cur_open) {
            cur_off     = nal_off;
            cur_has_vcl = FALSE;
            cur_is_i    = FALSE;
            cur_open    = TRUE;
        }
        cur_end = nal_end;
        if (is_vcl) {
            cur_has_vcl = TRUE;
            if (5 == nal_type) { /* IDR slice */
                cur_is_i = TRUE;
            }
        }

        if (has_next) {
            sc_off = next_off;
            sc_len = next_len;
        }
        has = has_next;
    }

    /* 收尾：最后一个 AU */
    if (cur_open && cur_end > cur_off) {
        __demo_vcam_emit(aus, cap, count, cur_off, cur_end - cur_off, cur_is_i);
        count++;
    }

    return count;
}

/**
 * @brief 从 cur 往后找下一个 I 帧 AU（到末尾回 0 重扫；都没有则停在 cur）
 */
static uint32_t __demo_vcam_next_i(uint32_t cur)
{
    uint32_t i = 0;

    for (i = cur + 1; i < s_vcam.au_num; i++) {
        if (s_vcam.aus[i].is_i) {
            return i;
        }
    }
    for (i = 0; i < cur; i++) {
        if (s_vcam.aus[i].is_i) {
            return i;
        }
    }

    return cur;
}

/**
 * @brief 喂帧线程：按 1000/fps 节拍回调 AU，EOF 循环，响应停止/I 帧请求
 */
static void __demo_vcam_thread(void *args)
{
    uint32_t period_ms = 0;
    uint32_t idx       = 0;
    uint64_t tick      = 0;
    uint64_t start_ms  = 0;

    (void)args;

    /* 先置 started：close 等它置位后再 tal_thread_delete（EMPTY 态 delete 无效） */
    s_vcam.started = TRUE;

    period_ms = 1000 / s_vcam.fps;
    if (0 == period_ms) { /* fps > 1000 时至少 1ms 一拍 */
        period_ms = 1;
    }
    start_ms = tal_system_get_millisecond();

    while (!s_vcam.stop) {
        uint64_t due_ms = 0;
        DEMO_VCAM_FRAME_T frame;

        /* App 请求 I 帧：下一拍直接跳到下一个 IDR AU */
        if (s_vcam.want_i_frame) {
            s_vcam.want_i_frame = FALSE;
            idx                 = __demo_vcam_next_i(idx);
            PR_DEBUG("vcam request i-frame -> AU %u/%u", idx, s_vcam.au_num);
        }

        /* 漂移校正：第 n 拍的目标时刻 = 起点 + n*period，而不是每拍 sleep(period) */
        due_ms = start_ms + tick * period_ms;
        while (!s_vcam.stop) {
            uint64_t now_ms = tal_system_get_millisecond();
            uint32_t slice  = 0;

            if (now_ms >= due_ms) {
                break;
            }
            slice = (due_ms - now_ms) > DEMO_VCAM_WAIT_SLICE_MS ? DEMO_VCAM_WAIT_SLICE_MS : (uint32_t)(due_ms - now_ms);
            tal_system_sleep(slice ? slice : 1);
        }
        if (s_vcam.stop) {
            break;
        }

        frame.data       = s_vcam.buf + s_vcam.aus[idx].offset;
        frame.len        = s_vcam.aus[idx].len;
        frame.ts_ms      = tick * 1000 / s_vcam.fps; /* 与节拍一致、严格递增的单调时间戳 */
        frame.is_i_frame = s_vcam.aus[idx].is_i;
        if (s_vcam.cb) {
            s_vcam.cb(&frame);
        }

        idx = (idx + 1) % s_vcam.au_num; /* EOF 循环回第 0 帧 */
        tick++;
    }

    /* 置位后不再触碰 buf/aus：close 据此安全释放 */
    s_vcam.exited = TRUE;
}

OPERATE_RET demo_vcam_open(const char *path, uint32_t fps, DEMO_VCAM_FRAME_CB cb)
{
    FILE            *fp     = NULL;
    long             fsize  = 0;
    size_t           got    = 0;
    uint8_t         *buf    = NULL;
    DEMO_VCAM_AU_T  *aus    = NULL;
    uint32_t         au_num = 0;
    uint32_t         i_num  = 0;
    uint32_t         i      = 0;
    THREAD_CFG_T     cfg    = {0};
    OPERATE_RET      rt     = OPRT_OK;

    if (NULL == path || '\0' == path[0] || 0 == fps || NULL == cb) {
        PR_ERR("vcam open: invalid parm (path/fps/cb)");
        return OPRT_INVALID_PARM;
    }
    if (s_vcam.running) {
        PR_ERR("vcam open: already open");
        return OPRT_COM_ERROR;
    }

    /* 1) 整文件读入（样片约 200KB，允许全量驻留） */
    fp = fopen(path, "rb");
    if (NULL == fp) {
        PR_ERR("vcam open: cannot open sample '%s'", path);
        return OPRT_NOT_EXIST;
    }
    if (0 != fseek(fp, 0, SEEK_END) || (fsize = ftell(fp)) <= 0 || fsize > DEMO_VCAM_MAX_FILE_SIZE) {
        PR_ERR("vcam open: sample '%s' size invalid (%ld)", path, fsize);
        fclose(fp);
        return OPRT_INVALID_PARM;
    }
    rewind(fp);
    buf = (uint8_t *)tal_malloc((size_t)fsize);
    if (NULL == buf) {
        PR_ERR("vcam open: malloc %ld failed", fsize);
        fclose(fp);
        return OPRT_MALLOC_FAILED;
    }
    got = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    fp = NULL;
    if (got != (size_t)fsize) {
        PR_ERR("vcam open: read '%s' short (%u/%ld)", path, (uint32_t)got, fsize);
        tal_free(buf);
        return OPRT_COM_ERROR;
    }

    /* 2) 扫 AU：首轮计数，二轮填索引 */
    au_num = __demo_vcam_index(buf, (uint32_t)fsize, NULL, 0);
    if (0 == au_num) {
        PR_ERR("vcam open: no Annex-B AU in '%s'", path);
        tal_free(buf);
        return OPRT_INVALID_PARM;
    }
    aus = (DEMO_VCAM_AU_T *)tal_malloc(au_num * sizeof(DEMO_VCAM_AU_T));
    if (NULL == aus) {
        PR_ERR("vcam open: malloc au index failed (%u)", au_num);
        tal_free(buf);
        return OPRT_MALLOC_FAILED;
    }
    au_num = __demo_vcam_index(buf, (uint32_t)fsize, aus, au_num);
    for (i = 0; i < au_num; i++) {
        if (aus[i].is_i) {
            i_num++;
        }
    }
    if (0 == i_num) {
        PR_ERR("vcam open: no IDR AU in '%s'", path);
        tal_free(aus);
        tal_free(buf);
        return OPRT_INVALID_PARM;
    }

    /* 3) 状态就绪 + 起喂帧线程 */
    s_vcam.buf          = buf;
    s_vcam.buf_len      = (uint32_t)fsize;
    s_vcam.aus          = aus;
    s_vcam.au_num       = au_num;
    s_vcam.i_num        = i_num;
    s_vcam.fps          = fps;
    s_vcam.cb           = cb;
    s_vcam.stop         = FALSE;
    s_vcam.started      = FALSE;
    s_vcam.exited       = FALSE;
    s_vcam.want_i_frame = FALSE;
    s_vcam.thread       = NULL;

    PR_NOTICE("vcam open: %s, %u bytes, %u AU, %u I-frame, %ufps", path, s_vcam.buf_len, au_num, i_num, fps);

    cfg.stackDepth = DEMO_VCAM_THREAD_STACK;
    cfg.priority   = THREAD_PRIO_3;
    cfg.thrdname   = "demo_vcam";
    rt             = tal_thread_create_and_start(&s_vcam.thread, NULL, NULL, __demo_vcam_thread, NULL, &cfg);
    if (OPRT_OK != rt) {
        PR_ERR("vcam open: create thread failed (%d)", rt);
        tal_free(aus);
        tal_free(buf);
        s_vcam.buf    = NULL;
        s_vcam.aus    = NULL;
        s_vcam.thread = NULL;
        return rt;
    }

    s_vcam.running = TRUE;

    return OPRT_OK;
}

OPERATE_RET demo_vcam_close(void)
{
    uint32_t waited_ms = 0;

    if (!s_vcam.running) {
        return OPRT_OK; /* 未 open：可重入 */
    }
    s_vcam.running = FALSE;

    /* 停线程：tal_thread_delete 只置停止态，线程退出（并自释放线程资源）在
     * vcam 线程内完成，所以要等 exited 标志确认它不再触碰样片缓冲 */
    s_vcam.stop = TRUE;
    if (NULL != s_vcam.thread) {
        /* 先等线程入口置 started：tal_thread_delete 对 EMPTY 态（刚创建、尚未
         * 进入 RUNNING）返回 OPRT_COM_ERROR 且不置停止态，会让线程包装层永远
         * 等不到 STOP（线程跑成僵尸）。线程入口置 started 时状态必为 RUNNING */
        while (!s_vcam.started && waited_ms < DEMO_VCAM_EXIT_WAIT_MS) {
            tal_system_sleep(5);
            waited_ms += 5;
        }
        if (!s_vcam.started) {
            /* 线程没跑起来（超时）：不 delete、不释放缓冲，宁可泄漏也不 use-after-free */
            PR_ERR("vcam close: thread not started in %ums, keep buffers", waited_ms);
            return OPRT_TIMEOUT;
        }
        tal_thread_delete(s_vcam.thread);
        s_vcam.thread = NULL;
        while (!s_vcam.exited && waited_ms < DEMO_VCAM_EXIT_WAIT_MS) {
            tal_system_sleep(5);
            waited_ms += 5;
        }
        if (!s_vcam.exited) {
            /* 线程仍没退出（回调阻塞等）：同样保留缓冲，宁可泄漏也不 use-after-free */
            PR_ERR("vcam close: thread not exited in %ums, keep buffers", waited_ms);
            return OPRT_TIMEOUT;
        }
    }

    /* 状态归零并释放缓冲 */
    s_vcam.cb           = NULL;
    s_vcam.fps          = 0;
    s_vcam.au_num       = 0;
    s_vcam.i_num        = 0;
    s_vcam.buf_len      = 0;
    s_vcam.want_i_frame = FALSE;
    tal_free(s_vcam.aus);
    tal_free(s_vcam.buf);
    s_vcam.aus = NULL;
    s_vcam.buf = NULL;

    PR_NOTICE("vcam close");

    return OPRT_OK;
}

OPERATE_RET demo_vcam_request_i_frame(void)
{
    if (!s_vcam.running) {
        return OPRT_COM_ERROR;
    }
    s_vcam.want_i_frame = TRUE;

    return OPRT_OK;
}

BOOL_T demo_vcam_is_open(void)
{
    return s_vcam.running;
}
