/**
 * @file tal_data_selftest.c
 * @brief tal_data 自测：区域原语（init / read / write / erase）+ K-V API 级用例
 *        （init/write/read/erase、墓碑与重启重开路径、覆盖写正确性、区域满安全）
 *
 * 运行：cd build && ./tal_data_selftest
 *       两段式重启用例（需分两次进程调用，区域跨进程保留）：
 *         rm -rf tuyadb && ./tal_data_selftest restart-setup && ./tal_data_selftest restart-check
 *       重开用例（在完整自测跑完后，区域非空时调用）：
 *         ./tal_data_selftest reopen-check
 *       撕裂写用例（干净区域上分两次进程调用，第二次重扫时 CRC 失败须视为不存在）：
 *         rm -f tuyadb/data.bin && ./tal_data_selftest torn-setup && ./tal_data_selftest torn-check
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

static int test_kv_overwrite(void)
{
    /* a) 覆盖写正确性 + 老 key 存活（不触发容量上限） */
    uint8_t old_blob[512], blob[256], rd[512];
    size_t out_len = 0;
    char name[16];

    memset(old_blob, 0x33, sizeof(old_blob));
    CHECK(OPRT_OK == tal_data_write("old.key", old_blob, sizeof(old_blob)));

    for (int i = 0; i < 20; i++) {
        memset(blob, (uint8_t)i, sizeof(blob));
        snprintf(name, sizeof(name), "ovw.%d", i % 4);
        CHECK(OPRT_OK == tal_data_write(name, blob, sizeof(blob)));
    }
    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "ovw.%d", i);
        memset(rd, 0, sizeof(rd));
        CHECK(OPRT_OK == tal_data_read(name, rd, sizeof(rd), &out_len));
        CHECK(out_len == sizeof(blob));
        CHECK(rd[0] == (uint8_t)(16 + i));           /* 最后一轮 i=16..19 */
    }
    /* 老 key 经 20 次覆盖后仍可读且内容不变 */
    memset(rd, 0, sizeof(rd));
    CHECK(OPRT_OK == tal_data_read("old.key", rd, sizeof(rd), &out_len));
    CHECK(out_len == sizeof(old_blob) && 0 == memcmp(old_blob, rd, out_len));
    return 0;
}

static int test_kv_region_full(void)
{
    /* b) 区域满安全：持续写大数据直到报满，已写入的 key 必须全部仍可读（不得覆盖/丢失） */
    uint8_t blob[8 * 1024], rd[8 * 1024];
    size_t out_len = 0;
    char name[16];
    int written = 0, full = 0;

    for (int i = 0; i < 128; i++) {
        memset(blob, (uint8_t)(i & 0xFF), sizeof(blob));
        snprintf(name, sizeof(name), "full.%d", i);
        if (OPRT_OK != tal_data_write(name, blob, sizeof(blob))) {
            full = 1;
            break;
        }
        written++;
    }
    CHECK(1 == full);                                /* 必须真的写满并报错 */
    CHECK(written > 0);
    for (int i = 0; i < written; i++) {              /* 已写入的全部可读且内容正确 */
        snprintf(name, sizeof(name), "full.%d", i);
        memset(rd, 0, sizeof(rd));
        CHECK(OPRT_OK == tal_data_read(name, rd, sizeof(rd), &out_len));
        CHECK(out_len == sizeof(blob));
        CHECK(rd[0] == (uint8_t)(i & 0xFF));
    }
    return 0;
}

/* 撕裂写在**进程内**观察不到（读路径用 RAM 索引 + 复核 CRC，但损坏发生在
 * 本次运行写入之后时，需重启重扫才能体现）→ 拆成 setup / check 两个模式，
 * 由 main 的 torn-setup / torn-check 分支分两次调用。
 *
 * 压缩中途掉电的安全性（副本先写、旧记录后擦）由代码评审核对顺序不变量，
 * 不做运行时注入测试——见设计文档第五节的压缩步骤与 __compact 实现。 */

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
    if (argc > 1 && 0 == strcmp(argv[1], "torn-setup")) {
        /* 干净区域上只写一条记录 → 记录必然从偏移 0 开始，数据区偏移 = 12 + namelen */
        uint8_t wr[256];
        memset(wr, 0x11, sizeof(wr));
        CHECK(OPRT_OK == tal_data_init("0123456789abcdef", "fedcba9876543210"));
        CHECK(OPRT_OK == tal_data_write("torn.key", wr, sizeof(wr)));
        {
            uint8_t bad = 0x00;
            uint32_t data_off = 12 + (uint32_t)strlen("torn.key");
            CHECK(OPRT_OK == tal_data_get_ops()->write(data_off, &bad, 1));  /* 破坏数据首字节 */
        }
        printf("TORN SETUP OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "torn-check")) {
        uint8_t rd[256];
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_data_init("0123456789abcdef", "fedcba9876543210"));
        /* 重扫时该记录 CRC 失败 → 视为不存在 */
        CHECK(OPRT_NOT_FOUND == tal_data_read("torn.key", rd, sizeof(rd), &out_len));
        printf("TORN CHECK OK\n");
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
    if (0 != test_kv_overwrite()) {
        return 1;
    }
    if (0 != test_kv_region_full()) {
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
