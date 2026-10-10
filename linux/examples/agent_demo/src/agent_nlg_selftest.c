/**
 * @file agent_nlg_selftest.c
 * @brief agent_demo 下行消费自测：STOP 携带非空末段必须被打印
 *
 * 背景（core ai_skill.c 语义）：多段 NLG 流的末段（eof=1）经
 * AI_USER_EVT_TEXT_STREAM_STOP 携带该包 content（可非空）；单帧场景 STOP 为
 * 显式空包 {NULL,0,0}。消费端若在 STOP 分支不读 event->data，会漏最后一段文字。
 *
 * 本自测用公开 API（ai_user_event_notify，同步调用）构造事件，捕获
 * agent_downlink 的 stdout 输出并断言——不依赖云端分包行为，可稳定复现。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"
#include "ai_user_event.h"
#include "agent_downlink.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int s_fail = 0;

#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                                                     \
            s_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

/* 捕获 fn() 执行期间的 stdout 输出到 out（返回字节数） */
static int capture_stdout(void (*fn)(void *), void *arg, char *out, size_t out_sz)
{
    int     fds[2];
    int     saved;
    ssize_t n;

    if (pipe(fds) != 0) {
        return -1;
    }

    fflush(stdout);
    saved = dup(STDOUT_FILENO);
    dup2(fds[1], STDOUT_FILENO);
    close(fds[1]);

    fn(arg);
    fflush(stdout);

    dup2(saved, STDOUT_FILENO);
    close(saved);

    n = read(fds[0], out, out_sz - 1);
    close(fds[0]);
    if (n < 0) {
        n = 0;
    }
    out[n] = '\0';

    return (int)n;
}

static void __emit_stop_with_text(void *arg)
{
    ai_user_event_notify(AI_USER_EVT_TEXT_STREAM_STOP, arg);
}

static void __emit_stop_empty(void *arg)
{
    AI_NOTIFY_TEXT_T empty = {.data = NULL, .datalen = 0, .timeindex = 0};

    (void)arg;
    ai_user_event_notify(AI_USER_EVT_TEXT_STREAM_STOP, &empty);
}

int main(void)
{
    char out[512];

    tal_time_service_init();
    tal_log_init(TAL_LOG_LEVEL_WARN, 1024, NULL); /* 压低日志噪音，避免干扰捕获 */
    agent_downlink_init();

    /* 1) STOP 携带非空末段（多段流场景）→ 内容必须出现在输出中 */
    AI_NOTIFY_TEXT_T tail = {.data = "末段文字", .datalen = (uint16_t)strlen("末段文字"), .timeindex = 0};
    capture_stdout(__emit_stop_with_text, &tail, out, sizeof(out));
    CHECK(strstr(out, "末段文字") != NULL, "STOP with non-empty content is printed");

    /* 2) STOP 空包（单帧/末段为空场景）→ 不应打印任何内容 */
    capture_stdout(__emit_stop_empty, NULL, out, sizeof(out));
    CHECK(strstr(out, "末段文字") == NULL, "empty STOP prints no content");

    if (s_fail) {
        printf("AGENT NLG SELFTEST FAILED: %d check(s)\n", s_fail);
        return 1;
    }

    printf("AGENT NLG SELFTEST PASSED\n");
    return 0;
}
