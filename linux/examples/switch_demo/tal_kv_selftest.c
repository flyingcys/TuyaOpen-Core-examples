/**
 * @file tal_kv_selftest.c
 * @brief tal_kv 自测：区域原语（init / read / write / erase）+ 公开 API 级用例
 *
 * 区域级用例用 ops 契约（tal_kv_get_ops()）；K-V 用例一律用旧签名公开 API
 * （tal_kv_init/set/get/free/del），覆盖：基本读写、get 的分配语义（堆指针 +
 * 末尾 '\0' + tal_kv_free）、serialize 往返、覆盖写、删除幂等、name 规则、
 * 不存在、参数校验、单条上限边界、并发安全（多线程写）、区域满安全。
 *
 * 运行：cd build && ./tal_kv_selftest
 *       两段式重启用例（需分两次进程调用，区域跨进程保留）：
 *         rm -rf tuyadb && ./tal_kv_selftest restart-setup && ./tal_kv_selftest restart-check
 *       重开用例（完整自测把区域写满后调用，验证"满区域重开"：
 *       墓碑不复活 / 已写入数据完整 / 写入被拒且不覆盖；重开后写读正常在 restart-check）：
 *         ./tal_kv_selftest reopen-check
 *       撕裂写用例（干净区域上分两次进程调用，第二次重扫时 CRC 失败须视为不存在，
 *       且不得整区归零——第一条完好记录必须存活）：
 *         rm -f tuyadb/data.bin && ./tal_kv_selftest torn-setup && ./tal_kv_selftest torn-check
 *       未擦除区域用例（区域预填非 0xFF 垃圾 → init 应归零并正常工作，重启可读）：
 *         rm -rf tuyadb && ./tal_kv_selftest garbage-setup && ./tal_kv_selftest garbage-check \
 *           && ./tal_kv_selftest garbage-recheck
 *       错密钥用例（A 写入 → 换 B 重开：get 必须报错而不是返回垃圾）：
 *         rm -rf tuyadb && ./tal_kv_selftest wrongkey-setup && ./tal_kv_selftest wrongkey-check
 */
#include "tal_kv_ops.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            return -1;                                                                                                 \
        }                                                                                                              \
    } while (0)

/* 平台 ops 卡经 core 的 getter 分发（tal_kv.c 定义） */
#define OPS (tal_kv_get_ops())

/* 与引擎内部 TAL_KV_PLAIN_MAX（tal_kv.c，单条明文上限 16KB）镜像的边界值 */
#define SELFTEST_PLAIN_MAX (16 * 1024)

static int test_region_basic(void)
{
    TAL_KV_REGION_T region = {0};
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
    TAL_KV_REGION_T region = {0};
    uint8_t buf[8];

    CHECK(OPRT_INVALID_PARM == OPS->init(NULL));

    memset(buf, 0, sizeof(buf));
    CHECK(OPRT_OK == OPS->init(&region));
    CHECK(OPRT_INVALID_PARM == OPS->read(region.size, buf, 1));        /* offset 越界 */
    CHECK(OPRT_INVALID_PARM == OPS->write(region.size, buf, 1));       /* offset 越界 */
    CHECK(OPRT_INVALID_PARM == OPS->read(0, buf, region.size + 1));    /* len 越界 */
    return 0;
}

#include "tal_kv.h"
#include "tal_memory.h"

#include <pthread.h>

/* 旧 tal_kv 的 seed/key 形态：各 16 字节字符串（tal_kv_cfg_t.seed/key 为 char[17]） */
static tal_kv_cfg_t s_kv_cfg = {
    .seed = "0123456789abcdef",
    .key  = "fedcba9876543210",
};

static int test_kv_basic(void)
{
    uint8_t wr[64];
    uint8_t *rd = NULL;
    size_t out_len = 0;

    CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));

    for (size_t i = 0; i < sizeof(wr); i++) {
        wr[i] = (uint8_t)(i + 1);
    }
    CHECK(OPRT_OK == tal_kv_set("test.kv", wr, sizeof(wr)));

    /* get：堆分配返回 + 末尾 '\0'（旧语义，用 tal_kv_free 释放） */
    CHECK(OPRT_OK == tal_kv_get("test.kv", &rd, &out_len));
    CHECK(NULL != rd);
    CHECK(out_len == sizeof(wr));
    CHECK(0 == memcmp(wr, rd, sizeof(wr)));
    CHECK(0 == rd[out_len]);                     /* 末尾补 '\0' */
    CHECK(OPRT_OK == tal_kv_free(rd));
    rd = NULL;

    /* 连续第二次读取同一 key：IV 必须每次从派生值重取，不能被 CBC 就地改写污染
     * （只回退 read 侧 iv 副本时，仅此断言会失败；write 侧的由上面的用例覆盖） */
    CHECK(OPRT_OK == tal_kv_get("test.kv", &rd, &out_len));
    CHECK(out_len == sizeof(wr));
    CHECK(0 == memcmp(wr, rd, sizeof(wr)));
    CHECK(0 == rd[out_len]);
    CHECK(OPRT_OK == tal_kv_free(rd));
    rd = NULL;

    /* 字符串语义：存 C 串（不含 '\0'）→ 取回后可直接当字符串用 */
    CHECK(OPRT_OK == tal_kv_set("str.kv", (const uint8_t *)"hello.kv", strlen("hello.kv")));
    CHECK(OPRT_OK == tal_kv_get("str.kv", &rd, &out_len));
    CHECK(out_len == strlen("hello.kv"));
    CHECK(0 == strcmp((char *)rd, "hello.kv"));
    CHECK(OPRT_OK == tal_kv_free(rd));
    rd = NULL;

    /* 二进制安全：内嵌 0x00 的值按长度原样返回 */
    memset(wr, 0, 16);
    wr[0] = 0x11;
    wr[8] = 0x22;
    CHECK(OPRT_OK == tal_kv_set("bin.kv", wr, 16));
    CHECK(OPRT_OK == tal_kv_get("bin.kv", &rd, &out_len));
    CHECK(out_len == 16);
    CHECK(0 == memcmp(wr, rd, 16));
    CHECK(0 == rd[16]);
    CHECK(OPRT_OK == tal_kv_free(rd));
    rd = NULL;

    /* 不存在 → NOT_FOUND，且出参被置空（旧语义） */
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    CHECK(OPRT_NOT_FOUND == tal_kv_get("no.such", &rd, &out_len));
    CHECK(NULL == rd && 0 == out_len);

    /* 覆盖写 */
    memset(wr, 0x5A, 16);
    CHECK(OPRT_OK == tal_kv_set("test.kv", wr, 16));
    CHECK(OPRT_OK == tal_kv_get("test.kv", &rd, &out_len));
    CHECK(out_len == 16 && 0 == memcmp(wr, rd, 16));
    CHECK(OPRT_OK == tal_kv_free(rd));
    rd = NULL;

    /* 删除（幂等） */
    CHECK(OPRT_OK == tal_kv_del("test.kv"));
    CHECK(OPRT_OK == tal_kv_del("test.kv"));
    CHECK(OPRT_NOT_FOUND == tal_kv_get("test.kv", &rd, &out_len));
    /* 删除后其它 key 不受影响 */
    CHECK(OPRT_OK == tal_kv_get("bin.kv", &rd, &out_len));
    CHECK(out_len == 16);
    CHECK(OPRT_OK == tal_kv_free(rd));
    rd = NULL;

    /* name 规则 */
    CHECK(OPRT_INVALID_PARM == tal_kv_set("bad/name", wr, 16));
    CHECK(OPRT_INVALID_PARM == tal_kv_set("", wr, 16));
    CHECK(OPRT_INVALID_PARM == tal_kv_set("bad name", wr, 16));
    CHECK(OPRT_INVALID_PARM == tal_kv_set("0123456789012345678901234567890123456789012345678901234567890123", wr, 16)); /* 64B */
    CHECK(OPRT_INVALID_PARM == tal_kv_del("bad/name"));
    CHECK(OPRT_INVALID_PARM == tal_kv_get("bad/name", &rd, &out_len));

    /* 参数校验（与旧实现一致：key/value/length 任一非法即 INVALID_PARM） */
    CHECK(OPRT_INVALID_PARM == tal_kv_set(NULL, wr, 16));
    CHECK(OPRT_INVALID_PARM == tal_kv_set("ok.name", NULL, 16));
    CHECK(OPRT_INVALID_PARM == tal_kv_set("ok.name", wr, 0));
    CHECK(OPRT_INVALID_PARM == tal_kv_set("ok.name", wr, SELFTEST_PLAIN_MAX + 1));  /* 超长 */
    CHECK(OPRT_INVALID_PARM == tal_kv_get(NULL, &rd, &out_len));
    CHECK(OPRT_INVALID_PARM == tal_kv_get("ok.name", NULL, &out_len));
    CHECK(OPRT_INVALID_PARM == tal_kv_get("ok.name", &rd, NULL));
    CHECK(OPRT_INVALID_PARM == tal_kv_free(NULL));
    CHECK(OPRT_INVALID_PARM == tal_kv_del(NULL));

    /* 单条上限边界：恰好 16KB 可写可读，16KB+1 被拒 */
    {
        uint8_t *big = (uint8_t *)tal_malloc(SELFTEST_PLAIN_MAX);
        CHECK(NULL != big);
        memset(big, 0x7E, SELFTEST_PLAIN_MAX);
        CHECK(OPRT_OK == tal_kv_set("max.kv", big, SELFTEST_PLAIN_MAX));
        CHECK(OPRT_OK == tal_kv_get("max.kv", &rd, &out_len));
        CHECK(out_len == SELFTEST_PLAIN_MAX && 0 == memcmp(big, rd, SELFTEST_PLAIN_MAX));
        CHECK(0 == rd[out_len]);                     /* 末尾补 '\0'（与小值用例一致） */
        CHECK(OPRT_OK == tal_kv_free(rd));
        rd = NULL;
        CHECK(OPRT_OK == tal_kv_del("max.kv"));
        tal_free(big);
    }
    return 0;
}

/* serialize 往返（tal_kv_serialize_set/get，语义照旧 kv_serialize.c）：
 * KV_STRING 的 db[i].len 是"缓冲容量"（超容量报错、JSON 缺 key 整缓冲清零、
 * 空串 ↔ null）；KV_RAW 的 db[i].len 是"编码前长度"（0 ↔ null，取回时 null 置 0）；
 * 覆盖 endpoint.domain 同形态的多类型组合（STRING/USHORT/INT/BOOL/RAW） */
static int test_kv_serialize(void)
{
    struct {
        char     host[32];
        uint16_t port;
        char     path[16];
        int32_t  num;
        BOOL_T   flag;
        char     empty[8];
        uint8_t  raw[16];
    } src, dst;
    uint8_t raw0_out[4];
    uint8_t miss_buf[8];
    char    tiny[4];

    memset(&src, 0, sizeof(src));
    strcpy(src.host, "h6.iot-dns.com");
    src.port = 443;
    strcpy(src.path, "/d.json");
    src.num  = -12345;
    src.flag = TRUE;
    src.raw[0] = 0xDE; src.raw[1] = 0xAD; src.raw[2] = 0xBE; src.raw[3] = 0xEF; src.raw[4] = 0x00;

    kv_db_t set_db[] = {
        {"atop.host", KV_STRING, src.host, sizeof(src.host)},
        {"atop.port", KV_USHORT, &src.port, sizeof(src.port)},
        {"atop.path", KV_STRING, src.path, sizeof(src.path)},
        {"num", KV_INT, &src.num, sizeof(src.num)},
        {"flag", KV_BOOL, &src.flag, sizeof(src.flag)},
        {"empty", KV_STRING, src.empty, sizeof(src.empty)},
        {"raw", KV_RAW, src.raw, 5},
        {"raw0", KV_RAW, src.raw, 0},
    };
    CHECK(OPRT_OK == tal_kv_serialize_set("ser.kv", set_db, sizeof(set_db) / sizeof(set_db[0])));

    memset(&dst, 0xAA, sizeof(dst));
    memset(raw0_out, 0xAA, sizeof(raw0_out));
    memset(miss_buf, 0xAA, sizeof(miss_buf));
    kv_db_t get_db[] = {
        {"atop.host", KV_STRING, dst.host, sizeof(dst.host)},
        {"atop.port", KV_USHORT, &dst.port, sizeof(dst.port)},
        {"atop.path", KV_STRING, dst.path, sizeof(dst.path)},
        {"num", KV_INT, &dst.num, sizeof(dst.num)},
        {"flag", KV_BOOL, &dst.flag, sizeof(dst.flag)},
        {"empty", KV_STRING, dst.empty, sizeof(dst.empty)},
        {"raw", KV_RAW, dst.raw, 0},
        {"raw0", KV_RAW, raw0_out, 0},
        {"miss", KV_STRING, miss_buf, sizeof(miss_buf)},
    };
    CHECK(OPRT_OK == tal_kv_serialize_get("ser.kv", get_db, sizeof(get_db) / sizeof(get_db[0])));
    CHECK(0 == strcmp(dst.host, src.host));
    CHECK(443 == dst.port);
    CHECK(0 == strcmp(dst.path, src.path));
    CHECK(-12345 == dst.num);
    CHECK(TRUE == dst.flag);
    CHECK(0 == dst.empty[0]);                        /* 空串 → null → 取回空串 */
    CHECK(0 == memcmp(dst.raw, src.raw, 5));         /* base64 往返（含 0x00 字节） */
    CHECK(0 == get_db[7].len && 0xAA == raw0_out[0]);/* 空 RAW → null，取回时 len 置 0 且不写缓冲 */
    for (size_t i = 0; i < sizeof(miss_buf); i++) {
        CHECK(0 == miss_buf[i]);                     /* JSON 缺该 key → 整缓冲清零 */
    }

    /* KV_STRING 容量不足 → 报错（len 是"缓冲容量"而非期望长度） */
    kv_db_t small_db[] = {
        {"atop.host", KV_STRING, tiny, sizeof(tiny)},
    };
    CHECK(OPRT_COM_ERROR == tal_kv_serialize_get("ser.kv", small_db, 1));

    /* 参数校验与不存在 key */
    CHECK(OPRT_INVALID_PARM == tal_kv_serialize_set("ser.kv", NULL, 1));
    CHECK(OPRT_INVALID_PARM == tal_kv_serialize_set(NULL, set_db, 1));
    CHECK(OPRT_INVALID_PARM == tal_kv_serialize_set("ser.kv", set_db, 0));
    CHECK(OPRT_INVALID_PARM == tal_kv_serialize_get("ser.kv", NULL, 1));
    CHECK(OPRT_NOT_FOUND == tal_kv_serialize_get("no.such.kv", get_db, 1));
    return 0;
}

static int test_kv_overwrite(void)
{
    /* a) 覆盖写正确性 + 老 key 存活（不触发容量上限） */
    uint8_t old_blob[512], blob[256];
    uint8_t *rd = NULL;
    size_t out_len = 0;
    char name[16];

    memset(old_blob, 0x33, sizeof(old_blob));
    CHECK(OPRT_OK == tal_kv_set("old.key", old_blob, sizeof(old_blob)));

    for (int i = 0; i < 20; i++) {
        memset(blob, (uint8_t)i, sizeof(blob));
        snprintf(name, sizeof(name), "ovw.%d", i % 4);
        CHECK(OPRT_OK == tal_kv_set(name, blob, sizeof(blob)));
    }
    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "ovw.%d", i);
        CHECK(OPRT_OK == tal_kv_get(name, &rd, &out_len));
        CHECK(out_len == sizeof(blob));
        CHECK(rd[0] == (uint8_t)(16 + i));           /* 最后一轮 i=16..19 */
        CHECK(OPRT_OK == tal_kv_free(rd));
        rd = NULL;
    }
    /* 老 key 经 20 次覆盖后仍可读且内容不变 */
    CHECK(OPRT_OK == tal_kv_get("old.key", &rd, &out_len));
    CHECK(out_len == sizeof(old_blob) && 0 == memcmp(old_blob, rd, out_len));
    CHECK(OPRT_OK == tal_kv_free(rd));
    rd = NULL;
    return 0;
}

/* c) 并发安全：多个线程同时写各自的 key（并发追加 + 索引/seq/tail 更新），
 * 主线程事后逐个校验内容。公开 API 必须线程安全（旧实现每次读写持 lfs_mutex）：
 * 无锁时 s_index/s_tail/s_max_seq 存在读改写竞态 → 索引错乱/记录互相覆盖/丢数据
 * → 本用例变红。 */
#define CONC_THREADS 4
#define CONC_ITERS   50
#define CONC_VAL_LEN 512

typedef struct {
    int id;
    int fail_at;                                     /* 线程内首个写失败的下标，-1 = 全部成功 */
} conc_arg_t;

static void *conc_worker(void *arg)
{
    conc_arg_t *a = (conc_arg_t *)arg;
    uint8_t val[CONC_VAL_LEN];
    char name[16];

    snprintf(name, sizeof(name), "conc.%d", a->id);
    for (int it = 0; it < CONC_ITERS; it++) {
        memset(val, (uint8_t)(a->id * 64 + it), sizeof(val));
        if (OPRT_OK != tal_kv_set(name, val, sizeof(val))) {
            a->fail_at = it;
            return NULL;
        }
    }
    return NULL;
}

static int test_kv_concurrent(void)
{
    pthread_t th[CONC_THREADS];
    conc_arg_t args[CONC_THREADS];
    uint8_t sentinel[128], expect[CONC_VAL_LEN];
    uint8_t *rd = NULL;
    size_t out_len = 0;
    char name[16];

    /* 并发前的哨兵 key：并发结束后必须内容不变 */
    memset(sentinel, 0x9C, sizeof(sentinel));
    CHECK(OPRT_OK == tal_kv_set("conc.sentinel", sentinel, sizeof(sentinel)));

    for (int i = 0; i < CONC_THREADS; i++) {
        args[i].id = i;
        args[i].fail_at = -1;
        CHECK(0 == pthread_create(&th[i], NULL, conc_worker, &args[i]));
    }
    for (int i = 0; i < CONC_THREADS; i++) {
        CHECK(0 == pthread_join(th[i], NULL));
        CHECK(-1 == args[i].fail_at);                /* 并发写入不得报错 */
    }

    /* 每个 key 必须能读回，且内容 = 本线程最后一次写入 */
    for (int i = 0; i < CONC_THREADS; i++) {
        snprintf(name, sizeof(name), "conc.%d", i);
        memset(expect, (uint8_t)(i * 64 + (CONC_ITERS - 1)), sizeof(expect));
        CHECK(OPRT_OK == tal_kv_get(name, &rd, &out_len));
        CHECK(out_len == CONC_VAL_LEN);
        CHECK(0 == memcmp(expect, rd, CONC_VAL_LEN));
        CHECK(OPRT_OK == tal_kv_free(rd));
        rd = NULL;
    }
    /* 哨兵不受并发影响 */
    CHECK(OPRT_OK == tal_kv_get("conc.sentinel", &rd, &out_len));
    CHECK(out_len == sizeof(sentinel));
    CHECK(0 == memcmp(sentinel, rd, sizeof(sentinel)));
    CHECK(OPRT_OK == tal_kv_free(rd));
    rd = NULL;
    return 0;
}

static int test_kv_region_full(void)
{
    /* b) 区域满安全：持续写大数据直到报满，已写入的 key 必须全部仍可读（不得覆盖/丢失） */
    uint8_t blob[8 * 1024];
    uint8_t *rd = NULL;
    size_t out_len = 0;
    char name[16];
    int written = 0, full = 0;

    for (int i = 0; i < 128; i++) {
        memset(blob, (uint8_t)(i & 0xFF), sizeof(blob));
        snprintf(name, sizeof(name), "full.%d", i);
        if (OPRT_OK != tal_kv_set(name, blob, sizeof(blob))) {
            full = 1;
            break;
        }
        written++;
    }
    CHECK(1 == full);                                /* 必须真的写满并报错 */
    CHECK(written > 0);
    for (int i = 0; i < written; i++) {              /* 已写入的全部可读且内容正确 */
        snprintf(name, sizeof(name), "full.%d", i);
        CHECK(OPRT_OK == tal_kv_get(name, &rd, &out_len));
        CHECK(out_len == sizeof(blob));
        CHECK(rd[0] == (uint8_t)(i & 0xFF));
        CHECK(OPRT_OK == tal_kv_free(rd));
        rd = NULL;
    }
    return 0;
}

/* 撕裂写在**进程内**观察不到（读路径用 RAM 索引 + 复核 CRC，但损坏发生在
 * 本次运行写入之后时，需重启重扫才能体现）→ 拆成 setup / check 两个模式，
 * 由 main 的 torn-setup / torn-check 分支分两次调用。
 *
 * 区域不做回收（纯线性追加），无压缩中途掉电场景。 */

int main(int argc, char *argv[])
{
    if (argc > 1 && 0 == strcmp(argv[1], "restart-setup")) {
        uint8_t v1[32], v2[48];
        memset(v1, 0xA1, sizeof(v1));
        memset(v2, 0xB2, sizeof(v2));
        CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
        CHECK(OPRT_OK == tal_kv_set("rst.a", v1, sizeof(v1)));
        CHECK(OPRT_OK == tal_kv_set("rst.b", v2, sizeof(v2)));
        CHECK(OPRT_OK == tal_kv_del("rst.a"));
        printf("RESTART SETUP OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "restart-check")) {
        uint8_t wr[32];
        uint8_t *rd = NULL;
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
        /* 墓碑生效：被 del 的 key 必须 NOT_FOUND（不得被旧记录复活） */
        CHECK(OPRT_NOT_FOUND == tal_kv_get("rst.a", &rd, &out_len));
        /* 扫描建索引正确：未删的 key 内容完整 */
        CHECK(OPRT_OK == tal_kv_get("rst.b", &rd, &out_len));
        CHECK(out_len == 48);
        for (size_t i = 0; i < out_len; i++) {
            CHECK(0xB2 == rd[i]);
        }
        CHECK(OPRT_OK == tal_kv_free(rd));
        rd = NULL;
        /* 重开（扫描已有区域）后仍可正常写读 */
        for (size_t i = 0; i < sizeof(wr); i++) {
            wr[i] = (uint8_t)(0xD0 + i);
        }
        CHECK(OPRT_OK == tal_kv_set("rst.c", wr, sizeof(wr)));
        CHECK(OPRT_OK == tal_kv_get("rst.c", &rd, &out_len));
        CHECK(out_len == sizeof(wr));
        CHECK(0 == memcmp(wr, rd, sizeof(wr)));
        CHECK(OPRT_OK == tal_kv_free(rd));
        printf("RESTART CHECK OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "reopen-check")) {
        /* 上一轮完整自测把区域写满（region-full 用例，flash 不做回收 → 无空闲空间），
         * 此处验证"重开一个满区域"：已删除的 key 不得复活、已写入的数据完整可读、
         * 写入被拒且绝不覆盖已有记录。重开后的正常写读路径由 restart-check 覆盖。 */
        uint8_t big[16 * 1024];
        uint8_t *rd = NULL;
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
        /* 墓碑跨进程生效：test.kv 已被 del → 重开仍须 NOT_FOUND */
        CHECK(OPRT_NOT_FOUND == tal_kv_get("test.kv", &rd, &out_len));
        /* 已写入的数据完整（test_kv_overwrite 的 old.key：512B 全 0x33） */
        CHECK(OPRT_OK == tal_kv_get("old.key", &rd, &out_len));
        CHECK(out_len == 512);
        for (size_t i = 0; i < out_len; i++) {
            CHECK(0x33 == rd[i]);
        }
        CHECK(OPRT_OK == tal_kv_free(rd));
        rd = NULL;
        /* 大记录（region-full 的 full.0：8KB 全 0x00）完整可读 */
        CHECK(OPRT_OK == tal_kv_get("full.0", &rd, &out_len));
        CHECK(out_len == 8 * 1024);
        CHECK(0x00 == rd[0] && 0x00 == rd[out_len - 1]);
        CHECK(OPRT_OK == tal_kv_free(rd));
        rd = NULL;
        /* 区域满（收尾那条 8KB 记录都写不下 → 剩余必然 < 8KB）→ 写入必须报满 */
        memset(big, 0x55, sizeof(big));
        CHECK(OPRT_COM_ERROR == tal_kv_set("reopen.k", big, sizeof(big)));
        /* 被拒的写入不得破坏已有数据（复查 full.0） */
        CHECK(OPRT_OK == tal_kv_get("full.0", &rd, &out_len));
        CHECK(out_len == 8 * 1024);
        CHECK(0x00 == rd[0] && 0x00 == rd[out_len - 1]);
        CHECK(OPRT_OK == tal_kv_free(rd));
        printf("REOPEN OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "torn-setup")) {
        /* 干净区域上写两条记录：先写一条完好的（保证扫描 have=1，不会走"空区域归零"
         * 兜底），再写目标记录并破坏其数据首字节 → 重扫时第二条 CRC 失败应被当作
         * 日志尾丢弃，第一条必须完好（证明是"丢弃撕裂记录"而非"整区归零"）。 */
        uint8_t keep[256], wr[256];
        uint32_t rec1, data_off;
        uint8_t bad = 0x00;

        memset(keep, 0x22, sizeof(keep));
        memset(wr, 0x11, sizeof(wr));
        CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
        CHECK(OPRT_OK == tal_kv_set("torn.keep", keep, sizeof(keep)));
        CHECK(OPRT_OK == tal_kv_set("torn.key", wr, sizeof(wr)));

        /* 第二条记录的数据区偏移 = 第一条记录长度 + 12 + namelen("torn.key")；
         * 记录长度 = align4(16 + namelen + 密文长)，256B 明文 → 密文 272B（PKCS7 补满 16） */
        rec1 = (16 + (uint32_t)strlen("torn.keep") + (uint32_t)(sizeof(keep) + 16) + 3) & ~3u;
        data_off = rec1 + 12 + (uint32_t)strlen("torn.key");
        CHECK(OPRT_OK == OPS->write(data_off, &bad, 1));  /* 破坏第二条记录的数据首字节 */
        printf("TORN SETUP OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "torn-check")) {
        uint8_t *rd = NULL;
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
        /* 重扫时第二条记录 CRC 失败 → 视为不存在 */
        CHECK(OPRT_NOT_FOUND == tal_kv_get("torn.key", &rd, &out_len));
        /* 第一条完好记录必须存活（撕裂记录只截断日志尾，不得整区归零） */
        CHECK(OPRT_OK == tal_kv_get("torn.keep", &rd, &out_len));
        CHECK(out_len == 256);
        for (size_t i = 0; i < out_len; i++) {
            CHECK(0x22 == rd[i]);
        }
        CHECK(OPRT_OK == tal_kv_free(rd));
        printf("TORN CHECK OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "garbage-setup")) {
        /* 模拟"未擦除/曾被占用的区域"（如老 littlefs 分区原地升级）：用 ops 直接往
         * 干净区域写非 0xFF 垃圾，且不写任何可解析记录（首字节非记录 magic） */
        TAL_KV_REGION_T region = {0};
        uint8_t junk[4096];

        CHECK(OPRT_OK == OPS->init(&region));
        for (size_t i = 0; i < sizeof(junk); i++) {
            junk[i] = (uint8_t)(0x5A ^ i);
        }
        CHECK(OPRT_OK == OPS->write(0, junk, sizeof(junk)));
        printf("GARBAGE SETUP OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "garbage-check")) {
        /* init 扫描为空时必须把区域归零一次（兜底）：否则真 NOR flash 上只能 1→0，
         * 往非 0xFF 区域追加会静默写失败而内存索引无感 → 每次重启丢数据。
         * 归零后写读必须正常。 */
        TAL_KV_REGION_T region = {0};
        uint8_t wr[64];
        uint8_t *rd = NULL;
        size_t out_len = 0;

        CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
        /* 归零验证：整个区域必须回到擦除态（全 0xFF） */
        CHECK(OPRT_OK == OPS->init(&region));
        {
            uint8_t *chunk = (uint8_t *)tal_malloc(region.size);
            CHECK(NULL != chunk);
            CHECK(OPRT_OK == OPS->read(0, chunk, region.size));
            for (uint32_t i = 0; i < region.size; i++) {
                CHECK(0xFF == chunk[i]);
            }
            tal_free(chunk);
        }
        /* 归零后写读正常 */
        for (size_t i = 0; i < sizeof(wr); i++) {
            wr[i] = (uint8_t)(0xE0 + i);
        }
        CHECK(OPRT_OK == tal_kv_set("garb.kv", wr, sizeof(wr)));
        CHECK(OPRT_OK == tal_kv_get("garb.kv", &rd, &out_len));
        CHECK(out_len == sizeof(wr));
        CHECK(0 == memcmp(wr, rd, sizeof(wr)));
        CHECK(OPRT_OK == tal_kv_free(rd));
        printf("GARBAGE CHECK OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "garbage-recheck")) {
        /* 重启后仍可读（证明归零 + 写入真的落到区域，而非只在内存索引里） */
        uint8_t *rd = NULL;
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
        CHECK(OPRT_OK == tal_kv_get("garb.kv", &rd, &out_len));
        CHECK(out_len == 64);
        for (size_t i = 0; i < out_len; i++) {
            CHECK((uint8_t)(0xE0 + i) == rd[i]);
        }
        CHECK(OPRT_OK == tal_kv_free(rd));
        printf("GARBAGE RECHECK OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "wrongkey-setup")) {
        uint8_t wr[64];
        memset(wr, 0x77, sizeof(wr));
        CHECK(OPRT_OK == tal_kv_init(&s_kv_cfg));
        CHECK(OPRT_OK == tal_kv_set("wk.kv", wr, sizeof(wr)));
        printf("WRONGKEY SETUP OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "wrongkey-check")) {
        /* 换一套 seed/key 重开：解密后 PKCS7 全校验必须失败 → get 报错，绝不能把随机
         * 垃圾当有效值返回（旧 kv_decrypt 逐字节校验填充值）。
         * 该 key 对经筛选：解密垃圾的末字节落在 1..16，旧 tal_aes_get_actual_length
         * 只按末字节截断、不校验填充字节 → 会"成功"返回垃圾（本用例据此可红）。 */
        static tal_kv_cfg_t bad_cfg = {
            .seed = "S000000000000000",
            .key  = "B000000000000000",
        };
        uint8_t *rd = NULL;
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_kv_init(&bad_cfg));
        CHECK(OPRT_OK != tal_kv_get("wk.kv", &rd, &out_len));   /* 必须报错 */
        CHECK(NULL == rd && 0 == out_len);
        printf("WRONGKEY CHECK OK\n");
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
    if (0 != test_kv_serialize()) {
        return 1;
    }
    if (0 != test_kv_overwrite()) {
        return 1;
    }
    if (0 != test_kv_concurrent()) {
        return 1;
    }
    if (0 != test_kv_region_full()) {
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
