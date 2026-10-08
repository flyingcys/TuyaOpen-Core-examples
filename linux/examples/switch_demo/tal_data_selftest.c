/**
 * @file tal_data_selftest.c
 * @brief tal_data 自测：区域原语 + K-V 引擎 + 撕裂写/压缩/重启用例
 *
 * 运行：cd build && ./tal_data_selftest
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

/* Task 1 阶段 core 的 getter 尚未实现（Task 2 在 tal_data.c 提供 tal_data_get_ops），
 * 这里直接 extern 平台实例；Task 2 起可改用 getter。 */
extern TAL_DATA_OPS_T tal_data_ops_platform;
#define OPS (&tal_data_ops_platform)

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

int main(void)
{
    if (0 != test_region_basic()) {
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
