/**
 * @file tal_data_selftest.c
 * @brief tal_data 自测：区域原语（init / read / write / erase）+ K-V API 级用例
 *        （init/write/read/erase、墓碑与重启重开路径）；撕裂写 / 压缩等用例在 Task 3 追加
 *
 * 运行：cd build && ./tal_data_selftest
 *       两段式重启用例（需分两次进程调用，区域跨进程保留）：
 *         rm -rf tuyadb && ./tal_data_selftest restart-setup && ./tal_data_selftest restart-check
 *       重开用例（在完整自测跑完后，区域非空时调用）：
 *         ./tal_data_selftest reopen-check
 */
#include "tal_data_ops.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            return -1;                                                                                                 \
        }                                                                                                              \
    } while (0)

/* 平台 ops 卡经 core 的 getter 分发（tal_data.c 定义；Task 1 曾是直接 extern 平台实例） */
#define OPS (tal_data_get_ops())

static int test_region_basic(void)
{
    TAL_DATA_REGION_T region = {0};
    uint8_t wr[32], rd[32];

    CHECK(OPRT_OK == OPS->init(&region));
    CHECK(region.size >= 64 * 1024);
    CHECK(region.erase_granularity >= 1);

    memset(wr, 0xA5, sizeof(wr));
    CHECK(OPRT_OK == OPS->write(0, wr, sizeof(wr)));
    memset(rd, 0, sizeof(rd));
    CHECK(OPRT_OK == OPS->read(0, rd, sizeof(rd)));
    CHECK(0 == memcmp(wr, rd, sizeof(wr)));

    CHECK(OPRT_OK == OPS->erase(0, 4096));
    memset(rd, 0, sizeof(rd));
    CHECK(OPRT_OK == OPS->read(0, rd, sizeof(rd)));
    for (size_t i = 0; i < sizeof(rd); i++) {
        CHECK(0xFF == rd[i]);   /* 擦除态 = 0xFF */
    }
    return 0;
}

/* 负例：非法参数必须被拒绝（且越界判定要回绕安全） */
static int test_region_negative(void)
{
    TAL_DATA_REGION_T region = {0};
    uint8_t buf[8];

    CHECK(OPRT_INVALID_PARM == OPS->init(NULL));

    memset(buf, 0, sizeof(buf));
    CHECK(OPRT_OK == OPS->init(&region));
    CHECK(OPRT_INVALID_PARM == OPS->read(region.size, buf, 1));        /* offset 越界 */
    CHECK(OPRT_INVALID_PARM == OPS->write(region.size, buf, 1));       /* offset 越界 */
    CHECK(OPRT_INVALID_PARM == OPS->read(0, buf, region.size + 1));    /* len 越界 */
    return 0;
}

#include "tal_data.h"

#define SELFTEST_REGION "./tuyadb/data.bin"

static int test_kv_basic(void)
{
    uint8_t wr[64], rd[64];
    size_t out_len = 0;

    CHECK(OPRT_OK == tal_data_init("0123456789abcdef", "fedcba9876543210"));

    for (size_t i = 0; i < sizeof(wr); i++) {
        wr[i] = (uint8_t)(i + 1);
    }
    CHECK(OPRT_OK == tal_data_write("test.kv", wr, sizeof(wr)));

    memset(rd, 0, sizeof(rd));
    CHECK(OPRT_OK == tal_data_read("test.kv", rd, sizeof(rd), &out_len));
    CHECK(out_len == sizeof(wr));
    CHECK(0 == memcmp(wr, rd, sizeof(wr)));

    /* 连续第二次读取同一 key：IV 必须每次从派生值重取，不能被 CBC 就地改写污染
     * （只回退 read 侧 iv 副本时，仅此断言会失败；write 侧的由上面的用例覆盖） */
    memset(rd, 0, sizeof(rd));
    CHECK(OPRT_OK == tal_data_read("test.kv", rd, sizeof(rd), &out_len));
    CHECK(out_len == sizeof(wr));
    CHECK(0 == memcmp(wr, rd, sizeof(wr)));

    /* 探测尺寸 */
    CHECK(OPRT_OK == tal_data_read("test.kv", NULL, 0, &out_len));
    CHECK(out_len == sizeof(wr));

    /* buffer 过小 → 回填真实长度 */
    CHECK(OPRT_INVALID_PARM == tal_data_read("test.kv", rd, 8, &out_len));
    CHECK(out_len == sizeof(wr));

    /* 不存在 → NOT_FOUND */
    CHECK(OPRT_NOT_FOUND == tal_data_read("no.such", rd, sizeof(rd), &out_len));

    /* 覆盖写 */
    memset(wr, 0x5A, 16);
    CHECK(OPRT_OK == tal_data_write("test.kv", wr, 16));
    CHECK(OPRT_OK == tal_data_read("test.kv", rd, sizeof(rd), &out_len));
    CHECK(out_len == 16 && 0 == memcmp(wr, rd, 16));

    /* 删除（幂等） */
    CHECK(OPRT_OK == tal_data_erase("test.kv"));
    CHECK(OPRT_OK == tal_data_erase("test.kv"));
    CHECK(OPRT_NOT_FOUND == tal_data_read("test.kv", rd, sizeof(rd), &out_len));

    /* name 规则 */
    CHECK(OPRT_INVALID_PARM == tal_data_write("bad/name", wr, 16));
    CHECK(OPRT_INVALID_PARM == tal_data_write("", wr, 16));
    CHECK(OPRT_INVALID_PARM == tal_data_write("ok.name", NULL, 16));
    CHECK(OPRT_INVALID_PARM == tal_data_write("ok.name", wr, 0));      /* 空数据 */
    CHECK(OPRT_INVALID_PARM == tal_data_write("ok.name", wr, TAL_DATA_PLAIN_MAX + 1));  /* 超长 */
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && 0 == strcmp(argv[1], "restart-setup")) {
        uint8_t v1[32], v2[48];
        memset(v1, 0xA1, sizeof(v1));
        memset(v2, 0xB2, sizeof(v2));
        CHECK(OPRT_OK == tal_data_init("0123456789abcdef", "fedcba9876543210"));
        CHECK(OPRT_OK == tal_data_write("rst.a", v1, sizeof(v1)));
        CHECK(OPRT_OK == tal_data_write("rst.b", v2, sizeof(v2)));
        CHECK(OPRT_OK == tal_data_erase("rst.a"));
        printf("RESTART SETUP OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "restart-check")) {
        uint8_t rd[64];
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_data_init("0123456789abcdef", "fedcba9876543210"));
        /* 墓碑生效：被 erase 的 key 必须 NOT_FOUND（不得被旧记录复活） */
        CHECK(OPRT_NOT_FOUND == tal_data_read("rst.a", rd, sizeof(rd), &out_len));
        /* 扫描建索引正确：未删的 key 内容完整 */
        memset(rd, 0, sizeof(rd));
        CHECK(OPRT_OK == tal_data_read("rst.b", rd, sizeof(rd), &out_len));
        CHECK(out_len == 48);
        for (size_t i = 0; i < out_len; i++) {
            CHECK(0xB2 == rd[i]);
        }
        printf("RESTART CHECK OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "reopen-check")) {
        uint8_t rd[64];
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_data_init("0123456789abcdef", "fedcba9876543210"));
        /* 上一轮完整自测留下的区域：test.kv 已被 erase（墓碑）→ 重开仍须 NOT_FOUND */
        CHECK(OPRT_NOT_FOUND == tal_data_read("test.kv", rd, sizeof(rd), &out_len));
        /* 重开（扫描已有区域）后仍可正常写读 */
        for (size_t i = 0; i < sizeof(rd); i++) {
            rd[i] = (uint8_t)(0xC0 + i);
        }
        CHECK(OPRT_OK == tal_data_write("reopen.k", rd, sizeof(rd)));
        memset(rd, 0, sizeof(rd));
        CHECK(OPRT_OK == tal_data_read("reopen.k", rd, sizeof(rd), &out_len));
        CHECK(out_len == sizeof(rd));
        for (size_t i = 0; i < out_len; i++) {
            CHECK((uint8_t)(0xC0 + i) == rd[i]);
        }
        printf("REOPEN OK\n");
        return 0;
    }
    if (0 != test_region_basic()) {
        return 1;
    }
    if (0 != test_region_negative()) {
        return 1;
    }
    if (0 != test_kv_basic()) {
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
