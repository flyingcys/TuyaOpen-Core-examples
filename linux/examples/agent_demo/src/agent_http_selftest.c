/**
 * @file agent_http_selftest.c
 * @brief http_manager URL 解析边界自测：端口越界必须被拒绝（不依赖网络）
 *
 * 背景：自写 http_parse_url 的端口累加原用 uint16，越界会回绕——:73616 回绕成
 * 8080（可静默连到其他服务）、:65536 回绕成 0 后被"0 用默认端口"吞掉、静默落
 * 80/443。原 nodejs http_parser 对 >65535 明确报错，修复后必须对齐该语义。
 *
 * 判据只看 send 阶段的返回码（parser 层拒绝 = OPRT_INVALID_PARM），连接结果
 * 取决于环境（本机是否有服务监听），故只做"拒绝/不拒绝"断言、不做连通断言。
 *
 * **判据强度边界（重要，防止后人误用）**：第 3 组"边界内端口不被拒绝"用的是
 * `!= OPRT_INVALID_PARM`，它只能证明"parser 没拒绝"，**不能证明端口值解析正确**——
 * 因为"连接失败"同样返回非 INVALID_PARM。故该组是**防误杀**（合法端口被误判为非法
 * 时会红），**不防错放**（端口被解析成另一个合法值时抓不到，这正是本文件修复前的
 * bug 形态）。若要覆盖"解析值正确性"，需在 parser 层做可观测断言（如把解析结果
 * 暴露给测试），属后续增强。
 *
 * 另：本自测的连接失败用例（本机 65535/80 无服务）会走 http_session_send 的失败
 * 清理路径。该路径的 host/path 二次释放已修（core 69f4e0d，单一释放点），当前运行
 * **无** mem_heap 双重释放调试狗告警；若复见 `MEM_DEBUG_DOG_TAG err` +
 * `might be freed yet`，即该修复被削弱，应视为失败路径回归。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"
#include "http_manager.h"

#include <stdio.h>
#include <string.h>

static int s_fail = 0;

#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                                                     \
            s_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

/* 对 url 发起请求，返回 send 阶段结果（parser 拒绝时为 OPRT_INVALID_PARM） */
static OPERATE_RET __send_probe(const char *url)
{
    S_HTTP_MANAGER *mgr = get_http_manager_instance();
    SESSION_ID      s   = mgr->create_http_session(url, FALSE);
    if (!s) {
        return OPRT_INVALID_PARM; /* 建会话即失败，同样视为被拒 */
    }

    http_req_t req;
    memset(&req, 0, sizeof(req));
    req.type     = HTTP_GET;
    req.resource = url;
    req.version  = HTTP_VER_1_1;

    OPERATE_RET rt = mgr->send_http_request(s, &req, 0);
    mgr->destory_http_session(s);

    return rt;
}

int main(void)
{
    tal_time_service_init();
    tal_log_init(TAL_LOG_LEVEL_WARN, 1024, NULL);

    /* 1) 端口越界（含 uint16 回绕样本）必须被 parser 拒绝 */
    CHECK(__send_probe("http://127.0.0.1:73616/x") == OPRT_INVALID_PARM, "port 73616 (wraps to 8080) rejected");
    CHECK(__send_probe("http://127.0.0.1:65536/x") == OPRT_INVALID_PARM, "port 65536 (wraps to 0) rejected");
    CHECK(__send_probe("http://127.0.0.1:99999/x") == OPRT_INVALID_PARM, "port 99999 rejected");
    CHECK(__send_probe("http://127.0.0.1:4294967296/x") == OPRT_INVALID_PARM, "port 4294967296 rejected");

    /* 2) 显式 :0 非法（端口 0 保留）：与"未写端口用默认"、"回绕到 0"三态分离 */
    CHECK(__send_probe("http://127.0.0.1:0/x") == OPRT_INVALID_PARM, "explicit port 0 rejected");

    /* 3) 边界内端口不应被 parser 拒绝（连接成败取决于环境，不断言成功） */
    CHECK(__send_probe("http://127.0.0.1:65535/x") != OPRT_INVALID_PARM, "port 65535 not rejected by parser");
    CHECK(__send_probe("http://127.0.0.1:8080/x") != OPRT_INVALID_PARM, "port 8080 not rejected by parser");
    CHECK(__send_probe("http://127.0.0.1/x") != OPRT_INVALID_PARM, "no-port (default) not rejected by parser");

    /* 4) 空 host 仍被拒绝（回归） */
    CHECK(__send_probe("http:///x") == OPRT_INVALID_PARM, "empty host rejected");

    if (s_fail) {
        printf("AGENT HTTP SELFTEST FAILED: %d check(s)\n", s_fail);
        return 1;
    }

    printf("AGENT HTTP SELFTEST PASSED\n");
    return 0;
}
