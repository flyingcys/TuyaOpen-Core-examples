/**
 * @file tal_kv_selftest.c
 * @brief tal_kv 公开 API 黑盒自测（init / set / get / del 四个接口）
 *
 * 本文件只用 tal_kv.h 的 4 个公开接口，不触碰 ops 卡、区域原语、记录格式、加密
 * 方式等实现细节：断言仅依赖头文件明文承诺的语义——覆盖写、二进制安全（内嵌
 * 0x00 的值按长度原样返回）、get 返回堆内存且含结尾 '\0'（调用者 tal_free 释放）、
 * 失败时出参置 NULL/0、key 不存在时返回 OPRT_NOT_FOUND、del 幂等（不存在也 OK）。
 * 因此对任何平台实现（Linux littlefs+AES、NVS 类芯片 KV）都应成立；存储介质、
 * 单条上限、name 字符集/长度、区域/序列化等实现特定行为不在测试范围。
 *
 * 每个用例用完自行 tal_kv_del 清理 key，可反复运行（不假设干净存储）。
 *
 * 运行：cd build && ./tal_kv_selftest
 */
#include "tal_kv.h"
#include "tal_memory.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            return -1;                                                                                                 \
        }                                                                                                              \
    } while (0)

/* seed/key 各 16 字符（tal_kv_cfg_t.seed/key 为 char[17]，port 决定用途） */
static tal_kv_cfg_t s_kv_cfg = {
    .seed = "0123456789abcdef",
    .key  = "fedcba9876543210",
};

/* 读回校验：get 成功 + 堆指针非空 + 长度一致 + 内容一致 + 末尾 '\0'，校验后释放 */
static int check_roundtrip(const char *key, const uint8_t *expect, size_t expect_len)
{
    uint8_t *rd = NULL;
    size_t out_len = 0;

    CHECK(OPRT_OK == tal_kv_get(key, &rd, &out_len));
    CHECK(NULL != rd);
    CHECK(out_len == expect_len);
    if (expect_len > 0) {
        CHECK(0 == memcmp(expect, rd, expect_len));
    }
    CHECK(0 == rd[out_len]);                        /* 成功时含结尾 '\0'，可当 C 串 */
    tal_free(rd);
    return 0;
}

/* 1. 基本 set/get/del：原始字节往返 + 字符串语义 + 二进制安全 + 删除后不可见 */
static int test_basic_rw(void)
{
    uint8_t wr[64];
    uint8_t bin[16];
    uint8_t *rd = NULL;
    size_t out_len = 0;
    const char *str_val = "hello.kv";

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    /* 64B 递增字节往返（长度 + 内容逐字节一致） */
    for (size_t i = 0; i < sizeof(wr); i++) {
        wr[i] = (uint8_t)(i + 1);
    }
    CHECK(OPRT_OK == tal_kv_set("t1.raw", wr, sizeof(wr)));
    CHECK(0 == check_roundtrip("t1.raw", wr, sizeof(wr)));

    /* 字符串语义：存 C 串（不含结尾 '\0'）→ 取回可直接当字符串用 */
    CHECK(OPRT_OK == tal_kv_set("t1.str", (const uint8_t *)str_val, strlen(str_val)));
    CHECK(OPRT_OK == tal_kv_get("t1.str", &rd, &out_len));
    CHECK(NULL != rd);
    CHECK(out_len == strlen(str_val));
    CHECK(0 == strcmp((char *)rd, str_val));
    CHECK(0 == rd[out_len]);
    tal_free(rd);
    rd = NULL;

    /* 二进制安全：值内含 0x00 也按长度原样返回 */
    memset(bin, 0, sizeof(bin));
    bin[0] = 0x11;
    bin[8] = 0x22;
    CHECK(OPRT_OK == tal_kv_set("t1.bin", bin, sizeof(bin)));
    CHECK(0 == check_roundtrip("t1.bin", bin, sizeof(bin)));

    /* del 之后 key 不可见 */
    CHECK(OPRT_OK == tal_kv_del("t1.raw"));
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    CHECK(OPRT_NOT_FOUND == tal_kv_get("t1.raw", &rd, &out_len));
    CHECK(NULL == rd && 0 == out_len);

    /* 清理本用例用过的全部 key */
    CHECK(OPRT_OK == tal_kv_del("t1.raw"));
    CHECK(OPRT_OK == tal_kv_del("t1.str"));
    CHECK(OPRT_OK == tal_kv_del("t1.bin"));
    return 0;
}

/* 2. 覆盖写：同 key 后写覆盖前写（长度可变），另一 key 不受影响 */
static int test_overwrite(void)
{
    uint8_t v1[64], v2[32], other[48];

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    memset(v1, 0x11, sizeof(v1));
    memset(v2, 0x22, sizeof(v2));
    memset(other, 0x33, sizeof(other));

    CHECK(OPRT_OK == tal_kv_set("t2.kv", v1, sizeof(v1)));
    CHECK(OPRT_OK == tal_kv_set("t2.other", other, sizeof(other)));

    /* 二次写入（长度也变短）→ 读回必须是后写的值 */
    CHECK(OPRT_OK == tal_kv_set("t2.kv", v2, sizeof(v2)));
    CHECK(0 == check_roundtrip("t2.kv", v2, sizeof(v2)));

    /* 另一 key 不受影响 */
    CHECK(0 == check_roundtrip("t2.other", other, sizeof(other)));

    CHECK(OPRT_OK == tal_kv_del("t2.kv"));
    CHECK(OPRT_OK == tal_kv_del("t2.other"));
    return 0;
}

/* 3. 不存在的 key：get 返回 NOT_FOUND 且出参置 NULL/0；del 幂等返回 OPRT_OK */
static int test_missing_key(void)
{
    uint8_t *rd;
    size_t out_len;

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    /* 先幂等删除，保证"不存在"不依赖干净存储 */
    CHECK(OPRT_OK == tal_kv_del("t3.none"));

    /* get：NOT_FOUND + 出参被置 NULL/0（预置哨兵值验证） */
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    CHECK(OPRT_NOT_FOUND == tal_kv_get("t3.none", &rd, &out_len));
    CHECK(NULL == rd);
    CHECK(0 == out_len);

    /* del 幂等：对不存在的 key 连续调用都返回 OPRT_OK */
    CHECK(OPRT_OK == tal_kv_del("t3.none"));
    CHECK(OPRT_OK == tal_kv_del("t3.none"));

    /* 存在过的 key 被删后，再 del 同样成功（幂等），get 仍 NOT_FOUND */
    CHECK(OPRT_OK == tal_kv_set("t3.kv", (const uint8_t *)"x", 1));
    CHECK(OPRT_OK == tal_kv_del("t3.kv"));
    CHECK(OPRT_OK == tal_kv_del("t3.kv"));
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    CHECK(OPRT_NOT_FOUND == tal_kv_get("t3.kv", &rd, &out_len));
    CHECK(NULL == rd && 0 == out_len);
    return 0;
}

/* 4a. 空串：写入长度 1（仅结尾 '\0'）→ 读回 length==1 且首字节为 0 */
static int test_empty_value(void)
{
    uint8_t *rd = NULL;
    size_t out_len = 0;

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    CHECK(OPRT_OK == tal_kv_set("t4.empty", (const uint8_t *)"", 1));
    CHECK(OPRT_OK == tal_kv_get("t4.empty", &rd, &out_len));
    CHECK(NULL != rd);
    CHECK(1 == out_len);
    CHECK(0 == rd[0]);
    CHECK(0 == rd[1]);                              /* 末尾 '\0' 由 get 补上 */
    tal_free(rd);
    rd = NULL;

    CHECK(OPRT_OK == tal_kv_del("t4.empty"));
    return 0;
}

/* 4b. 长度 0 的宽容用例：新头文件未定义 length==0 的返回码，故不断言具体错误码；
 *     只要求不崩溃，且若写入被接受则读回自洽（OK+length==0，或 NOT_FOUND） */
static int test_zero_length(void)
{
    const uint8_t dummy = 0x5A;
    uint8_t *rd = NULL;
    size_t out_len = 0;
    int ret;
    int gret;

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
    CHECK(OPRT_OK == tal_kv_del("t4.zero"));        /* 幂等清理，不依赖干净存储 */

    ret = tal_kv_set("t4.zero", &dummy, 0);
    printf("INFO: tal_kv_set(len=0) -> %d（头文件未定义，两种结果均接受）\n", ret);
    if (OPRT_OK == ret) {
        gret = tal_kv_get("t4.zero", &rd, &out_len);
        CHECK(OPRT_OK == gret || OPRT_NOT_FOUND == gret);
        if (OPRT_OK == gret) {
            CHECK(NULL != rd);
            CHECK(0 == out_len);
            CHECK(0 == rd[0]);                      /* 仍含结尾 '\0' */
            tal_free(rd);
            rd = NULL;
        } else {
            CHECK(NULL == rd && 0 == out_len);
        }
    }

    /* 无论写入是否被接受，清理后该 key 必须重新可写（不留下损坏状态） */
    CHECK(OPRT_OK == tal_kv_del("t4.zero"));
    CHECK(OPRT_OK == tal_kv_set("t4.zero", (const uint8_t *)"ok", 2));
    CHECK(0 == check_roundtrip("t4.zero", (const uint8_t *)"ok", 2));
    CHECK(OPRT_OK == tal_kv_del("t4.zero"));
    return 0;
}

/* 4c. 大值：4096 字节往返（不断言平台单条上限，只验当前实现能存下的尺寸） */
static int test_large_value(void)
{
    const size_t big_len = 4096;
    uint8_t *big = NULL;

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    big = (uint8_t *)tal_malloc(big_len);
    CHECK(NULL != big);
    for (size_t i = 0; i < big_len; i++) {
        big[i] = (uint8_t)(i * 7 + 3);              /* 含 0x00 的填充，兼验二进制安全 */
    }

    CHECK(OPRT_OK == tal_kv_set("t5.big", big, big_len));
    CHECK(0 == check_roundtrip("t5.big", big, big_len));

    CHECK(OPRT_OK == tal_kv_del("t5.big"));
    tal_free(big);
    return 0;
}

/* 5. get 内存语义：每次成功都返回独立堆内存（两次读的指针必须不同——两份内存
 *    都归调用者所有，同一地址意味着重叠分配），内容一致，末尾 '\0'，tal_free 释放 */
static int test_get_memory(void)
{
    uint8_t wr[32];
    uint8_t *rd1 = NULL, *rd2 = NULL;
    size_t len1 = 0, len2 = 0;

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    for (size_t i = 0; i < sizeof(wr); i++) {
        wr[i] = (uint8_t)(0xA0 + i);
    }
    CHECK(OPRT_OK == tal_kv_set("t6.kv", wr, sizeof(wr)));

    /* 连续读两次，两份结果都必须有效（第一次不释放，第二次照读） */
    CHECK(OPRT_OK == tal_kv_get("t6.kv", &rd1, &len1));
    CHECK(OPRT_OK == tal_kv_get("t6.kv", &rd2, &len2));
    CHECK(NULL != rd1 && NULL != rd2);
    CHECK(rd1 != rd2);                              /* 两份独立堆内存 */
    CHECK(len1 == sizeof(wr) && len2 == sizeof(wr));
    CHECK(0 == memcmp(rd1, wr, sizeof(wr)));
    CHECK(0 == memcmp(rd2, wr, sizeof(wr)));
    CHECK(0 == rd1[len1] && 0 == rd2[len2]);        /* 均可安全当 C 串 */
    tal_free(rd1);
    tal_free(rd2);
    rd1 = NULL;
    rd2 = NULL;

    CHECK(OPRT_OK == tal_kv_del("t6.kv"));
    return 0;
}

/* 6. 重复 init：连续两次 tal_kv_init 都成功，之后 set/get 正常 */
static int test_reinit(void)
{
    uint8_t wr[48];

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    memset(wr, 0x6C, sizeof(wr));
    CHECK(OPRT_OK == tal_kv_set("t7.kv", wr, sizeof(wr)));
    CHECK(0 == check_roundtrip("t7.kv", wr, sizeof(wr)));
    CHECK(OPRT_OK == tal_kv_del("t7.kv"));
    return 0;
}

/* 7. 持久化：写 → 再次 init → 内容一致；del 后再次 init 也不得复活 */
static int test_persistence(void)
{
    uint8_t wr[128];
    uint8_t *rd = NULL;
    size_t out_len = 0;

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    for (size_t i = 0; i < sizeof(wr); i++) {
        wr[i] = (uint8_t)(0xF0 ^ i);
    }
    CHECK(OPRT_OK == tal_kv_set("t8.kv", wr, sizeof(wr)));

    /* 重开（再次 init）后内容一致 */
    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
    CHECK(0 == check_roundtrip("t8.kv", wr, sizeof(wr)));

    /* 删除是持久的：重开后仍 NOT_FOUND */
    CHECK(OPRT_OK == tal_kv_del("t8.kv"));
    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    CHECK(OPRT_NOT_FOUND == tal_kv_get("t8.kv", &rd, &out_len));
    CHECK(NULL == rd && 0 == out_len);
    return 0;
}

/* 8. 多线程并发：4 线程各写自己的 key（各 50 次 512B 覆盖写），join 后逐个读回
 *    校验 = 本线程最后一次写入；并发前的哨兵 key 内容不受影响。
 *    线程函数返回 void*，CHECK 宏的 return -1 不适用 → 用 fail_at 标志带回失败。 */
#define CONC_THREADS 4
#define CONC_ITERS   50
#define CONC_VAL_LEN 512

typedef struct {
    int id;
    int fail_at;                                    /* 线程内首个写失败的下标，-1 = 全部成功 */
} conc_arg_t;

static void *conc_worker(void *arg)
{
    conc_arg_t *a = (conc_arg_t *)arg;
    uint8_t val[CONC_VAL_LEN];
    char name[16];

    snprintf(name, sizeof(name), "t9.conc.%d", a->id);
    for (int it = 0; it < CONC_ITERS; it++) {
        memset(val, (uint8_t)(a->id * 64 + it), sizeof(val));
        if (OPRT_OK != tal_kv_set(name, val, sizeof(val))) {
            a->fail_at = it;
            return NULL;
        }
    }
    return NULL;
}

static int test_concurrent(void)
{
    pthread_t th[CONC_THREADS];
    conc_arg_t args[CONC_THREADS];
    uint8_t sentinel[128], expect[CONC_VAL_LEN];
    char name[16];

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    /* 并发前的哨兵 key：并发结束后必须内容不变 */
    memset(sentinel, 0x9C, sizeof(sentinel));
    CHECK(OPRT_OK == tal_kv_set("t9.sentinel", sentinel, sizeof(sentinel)));

    for (int i = 0; i < CONC_THREADS; i++) {
        args[i].id = i;
        args[i].fail_at = -1;
        CHECK(0 == pthread_create(&th[i], NULL, conc_worker, &args[i]));
    }
    for (int i = 0; i < CONC_THREADS; i++) {
        CHECK(0 == pthread_join(th[i], NULL));
        CHECK(-1 == args[i].fail_at);               /* 并发写入不得报错 */
    }

    /* 每个 key 必须能读回，且内容 = 本线程最后一次写入 */
    for (int i = 0; i < CONC_THREADS; i++) {
        snprintf(name, sizeof(name), "t9.conc.%d", i);
        memset(expect, (uint8_t)(i * 64 + (CONC_ITERS - 1)), sizeof(expect));
        CHECK(0 == check_roundtrip(name, expect, CONC_VAL_LEN));
    }

    /* 哨兵不受并发影响 */
    CHECK(0 == check_roundtrip("t9.sentinel", sentinel, sizeof(sentinel)));

    /* 清理并发 key + 哨兵 */
    for (int i = 0; i < CONC_THREADS; i++) {
        snprintf(name, sizeof(name), "t9.conc.%d", i);
        CHECK(OPRT_OK == tal_kv_del(name));
    }
    CHECK(OPRT_OK == tal_kv_del("t9.sentinel"));
    return 0;
}

/* 10. 错密钥：换一套 seed/key 重新 init 后，旧数据必须解不开（非 OK 且出参置
 *     NULL/0）；恢复原 cfg 重新 init 后数据完好（验证 port 每次 init 都按传入
 *     配置刷新密钥派生）。用例依赖加密实现——Linux port 有 AES-128-CBC；无加密
 *     平台（NVS 直映射）可调整或删除该用例。 */
static int test_wrong_key(void)
{
    static const uint8_t wr[32] = "wrong-key-payload-0123456789abc";
    tal_kv_cfg_t other_cfg = {
        .seed = "aaaaaaaaaaaaaaaa",
        .key  = "bbbbbbbbbbbbbbbb",
    };
    uint8_t *rd = NULL;
    size_t out_len = 0;
    int ret;

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    CHECK(OPRT_OK == tal_kv_set("t10.wrong", wr, sizeof(wr)));
    CHECK(0 == check_roundtrip("t10.wrong", wr, sizeof(wr)));

    /* 换一套 seed/key：旧密文解不开 → 必须报错，且出参置 NULL/0（预置哨兵验证） */
    CHECK(OPRT_OK == tal_kv_init(&other_cfg));
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    ret = tal_kv_get("t10.wrong", &rd, &out_len);
    CHECK(OPRT_OK != ret);
    CHECK(NULL == rd && 0 == out_len);

    /* 恢复原 cfg：数据必须完好（init 幂等但会刷新密钥派生） */
    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
    CHECK(0 == check_roundtrip("t10.wrong", wr, sizeof(wr)));

    CHECK(OPRT_OK == tal_kv_del("t10.wrong"));
    return 0;
}

int main(void)
{
    if (0 != test_basic_rw()) {
        return 1;
    }
    if (0 != test_overwrite()) {
        return 1;
    }
    if (0 != test_missing_key()) {
        return 1;
    }
    if (0 != test_empty_value()) {
        return 1;
    }
    if (0 != test_zero_length()) {
        return 1;
    }
    if (0 != test_large_value()) {
        return 1;
    }
    if (0 != test_get_memory()) {
        return 1;
    }
    if (0 != test_reinit()) {
        return 1;
    }
    if (0 != test_persistence()) {
        return 1;
    }
    if (0 != test_concurrent()) {
        return 1;
    }
    if (0 != test_wrong_key()) {
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
