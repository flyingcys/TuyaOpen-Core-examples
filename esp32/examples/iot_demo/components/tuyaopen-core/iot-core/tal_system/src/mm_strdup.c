/**
 * @file mm_strdup.c
 * @brief 字符串复制（tal_malloc 分配，调用者用 tal_free 释放）
 *
 * 原实现位于 iot-core/utilities/mix_method.c：因其需要 tal_malloc（与调用者的
 * tal_free 配对，不能改用标准 malloc），会让 utilities 层反向依赖 tal_system；
 * 故实现迁至本处，声明仍留在 mix_method.h（调用方 50+ 处零改动）。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "tal_memory.h"

#include <string.h>

/**
 * @brief Duplicates a string.
 *
 * @param str 输入字符串（NULL 返回 NULL）
 * @return 堆指针（调用者用 tal_free 释放）；分配失败返回 NULL
 */
char *mm_strdup(const char *str)
{
    if ((void *)0 == str) {
        return (void *)0;
    }

    char *tmp = tal_malloc(strlen(str) + 1);
    if ((void *)0 == tmp) {
        return (void *)0;
    }
    memcpy(tmp, str, strlen(str));
    tmp[strlen(str)] = '\0';

    return tmp;
}
