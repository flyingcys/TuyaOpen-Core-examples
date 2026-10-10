#ifndef MBEDTLS_THREADING_ALT_H
#define MBEDTLS_THREADING_ALT_H

/* 【ESP32 平台差异文件】（同步时排除，见 esp32/README.md）。
 *
 * 与 core 仓库版本（`#include "tal_mutex.h"` + `MUTEX_HANDLE mutex` 成员）
 * 语义等价：MUTEX_HANDLE 即 void*，这里直接写 void* 使本头**自包含**——
 * esp-idf 的组件（esp_common 等）经 mbedtls 目标的 INTERFACE 也会编译到
 * 本头（core 的 tuya_transport/CMakeLists 给 mbedtls 目标注入了 config
 * 目录），它们没有 core 的 tal_system 头路径，不能展开 tal_mutex.h。
 *
 * 两套 THREADING_ALT 结构的分工：tuya 侧编译单元（tuya_tls.c，用 tal_mutex
 * 实现 mbedtls 的 THREADING_ALT 回调）命中本头（tal_mutex 兼容结构，且
 * tuya_transport 自己的 config 目录在 include 顺序上先于 mbedtls 目标）；
 * esp-idf 的 mbedtls 库/组件自身的编译用 ROM 版
 * （mbedtls/port/mbedtls_rom/threading_alt.h，由 main 组件注入 mbedtls
 * 目标）——两套结构各归各的分配者（mbedtls 内部只经 set_alt 回调操作
 * mutex），互不冲突。
 */
typedef struct mbedtls_threading_mutex_t {
    void *mutex; /* MUTEX_HANDLE（tal_mutex 句柄） */
    char is_valid;
} mbedtls_threading_mutex_t;

#endif /* threading_alt.h */
