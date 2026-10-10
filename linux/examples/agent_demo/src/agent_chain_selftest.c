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
    if (tkl_ai_demo_open("voice_16k.pcm") != OPRT_OK) {
        printf("FAIL: audio sample 'voice_16k.pcm' not found: run from build/ "
               "(or set CONFIG_AGENT_DEMO_AUDIO_FILE to an absolute path)\n");
        return 1;
    }
    tkl_ai_demo_close();

    /* 1) 打开内置素材 */
    CHECK(tkl_ai_demo_open("voice_16k.pcm") == OPRT_OK, "open bundled pcm");

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
    CHECK(tkl_ai_demo_open("voice_16k.pcm") == OPRT_OK, "reopen");
    CHECK(tkl_ai_demo_close() == OPRT_OK, "close again");

    /* 5) 坏路径被拒且不影响后续使用 */
    CHECK(tkl_ai_demo_open("/nonexistent/agent_demo_selftest.pcm") != OPRT_OK, "bad path rejected");
    CHECK(tkl_ai_demo_open("voice_16k.pcm") == OPRT_OK, "usable after bad path");
    CHECK(tkl_ai_demo_close() == OPRT_OK, "final close");

    if (s_fail) {
        printf("AGENT CHAIN SELFTEST FAILED: %d check(s)\n", s_fail);
        return 1;
    }

    printf("AGENT CHAIN SELFTEST PASSED\n");
    return 0;
}
