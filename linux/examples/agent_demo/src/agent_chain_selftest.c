/**
 * @file agent_chain_selftest.c
 * @brief agent_demo 离线自测：素材加载 → 按帧读取 → 边界
 *
 * 不连云、不依赖凭据；只验证上行音频源的读取契约与内置素材的帧切分口径
 * （上传编排 agent_audio_upload_once 依赖 AI 客户端上下文，属在线实跑范畴）。
 *
 * 判据（内置素材 assets/voice_16k.pcm：16kHz/16bit/mono，2.592s）：
 *   总字节 = 82944；按 80ms 帧（2560B）切分 = 33 帧（32 整帧 + 末帧 1024B）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tkl_ai_demo.h"

#include <stdio.h>

#define FRAME_BYTES 2560         /* 80ms @16kHz/16bit/mono */
#define EXPECTED_TOTAL_BYTES 82944 /* 2.592s × 16000 × 2B */
#define EXPECTED_FRAMES 33         /* 32×2560 + 1024 */
#define EXPECTED_LAST_FRAME 1024

/* 素材路径：与 app 同源（tuya_main.c 的 AGENT_AUDIO_SRC_PATH 口径）。
 * CMake 已给本目标注入 AGENT_DEMO_AUDIO_FILE（见 CMakeLists.txt），配置了绝对
 * 路径就测配置的素材；为空则用构建目录的内置素材（从 build/ 运行）。
 * 早先此处的失败提示让用户"去设 CONFIG_AGENT_DEMO_FILE"，但源码并不读该宏——
 * 设了也无效（假承诺）；现已改为真正使用，提示与行为一致。 */
#ifndef AGENT_DEMO_AUDIO_FILE
#define AGENT_DEMO_AUDIO_FILE ""
#endif
#define SELFTEST_AUDIO_SRC ((AGENT_DEMO_AUDIO_FILE)[0] ? (AGENT_DEMO_AUDIO_FILE) : "voice_16k.pcm")

static int s_fail = 0;

#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                                                     \
            s_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(void)
{
    uint8_t  buf[FRAME_BYTES];
    uint32_t total  = 0;
    uint32_t frames = 0;
    uint32_t last   = 0;
    INT_T    n;

    /* 0) 素材预检：由 CMake 复制到构建目录，从 build/ 运行；
     *    从别处运行给出明确指引（学 p2p_demo 的 "run from build/" 惯例），
     *    避免后续断言成片失败、看不出根因。 */
    if (tkl_ai_demo_open(SELFTEST_AUDIO_SRC) != OPRT_OK) {
        printf("FAIL: audio sample '%s' not found: run from build/ \n", SELFTEST_AUDIO_SRC);
        return 1;
    }
    tkl_ai_demo_close();

    /* 1) 打开内置素材 */
    CHECK(tkl_ai_demo_open(SELFTEST_AUDIO_SRC) == OPRT_OK, "open bundled pcm");

    /* 2) 按 80ms 帧读取，统计帧数/总字节/末帧长度 */
    for (;;) {
        n = tkl_ai_demo_read(buf, FRAME_BYTES);
        CHECK(n >= 0, "read returns no error");
        if (n <= 0) {
            break;
        }
        CHECK(n <= FRAME_BYTES, "frame not larger than 80ms");
        total += (uint32_t)n;
        frames++;
        last = (uint32_t)n;
    }

    CHECK(total == EXPECTED_TOTAL_BYTES, "total bytes");
    CHECK(frames == EXPECTED_FRAMES, "frame count");
    CHECK(last == EXPECTED_LAST_FRAME, "last frame bytes");

    /* 3) EOF 后继续读仍为 0 */
    CHECK(tkl_ai_demo_read(buf, FRAME_BYTES) == 0, "eof stays eof");

    /* 4) 关闭后可重开 */
    CHECK(tkl_ai_demo_close() == OPRT_OK, "close");
    CHECK(tkl_ai_demo_open(SELFTEST_AUDIO_SRC) == OPRT_OK, "reopen");
    CHECK(tkl_ai_demo_close() == OPRT_OK, "close again");

    /* 5) 坏路径被拒且不影响后续使用 */
    CHECK(tkl_ai_demo_open("/nonexistent/agent_demo_selftest.pcm") != OPRT_OK, "bad path rejected");
    CHECK(tkl_ai_demo_open(SELFTEST_AUDIO_SRC) == OPRT_OK, "usable after bad path");
    CHECK(tkl_ai_demo_close() == OPRT_OK, "final close");

    /* 6) I/O 错误与 EOF 必须区分（契约：>0 字节 / 0 EOF / <0 错误）：
     *    目录路径 fopen 可成功但 fread 必失败（ferror）——若把 ferror 吞成 0(EOF)，
     *    调用方会把"读盘错误"当"读完"，0 字节却报成功（静默假绿）。 */
    CHECK(tkl_ai_demo_open(".") == OPRT_OK, "open dir path (fopen succeeds)");
    CHECK(tkl_ai_demo_read(buf, FRAME_BYTES) < 0, "read on dir returns error, not EOF");
    CHECK(tkl_ai_demo_close() == OPRT_OK, "close after dir probe");

    if (s_fail) {
        printf("AGENT CHAIN SELFTEST FAILED: %d check(s)\n", s_fail);
        return 1;
    }

    printf("AGENT CHAIN SELFTEST PASSED\n");
    return 0;
}
