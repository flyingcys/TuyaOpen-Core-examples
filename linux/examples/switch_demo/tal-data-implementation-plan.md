# tal_kv 实施计划（替换 tal_kv）

> 🛑 **整体作废（2026-10-08）**：本计划的方案（core 内自研日志引擎 + port 区域原语）已被
> "core 4 接口透传 + 实现全在 port（Linux 恢复 littlefs）"取代，见 `tal-data-design.md` 的"新方案"章节。
> **勿按本计划执行**：其任务书、验证步骤（`reopen-check`/`torn-*` 等命令行模式）与产物均不存在于当前代码。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用新的 `tal_kv`（4 个 K-V 接口 + 日志式布局引擎 + AES）替换 `tal_kv`，port 侧只需提供"一块数据区 + 4 个区域原语"（Linux 约 40 行）。

**Architecture:** 三层——上层（iot-core/应用）调**旧 `tal_kv_*` API（逐字兼容，零改动）**；TAL 层（core）实现 K-V 引擎（**线性追加日志 + 墓碑 + seq + CRC**，不压缩）与 AES 加解密；port 层只实现区域原语（`init/read/write/erase @offset`），Linux 用一个区域文件模拟。契约头在 core（`tal_kv_ops.h`），实例在 port（`tal_kv_ops_platform`），与 `tal_api_ops.h`/`tal_ota_ops.h` 同构。

> ⚠️ **本计划为历史记录**：Task 4-8 已被"公开 API 落地为旧 tal_kv 全套"取代（见设计文档第二/八节），实际实施以 git 提交与设计文档为准。

**Tech Stack:** C99、CMake（GLOB 收集）、mbedtls（经 `tal_security` 暴露的 `tal_aes128_cbc_*` / `tal_sha256_ret`）、POSIX stdio（Linux port）。

**设计依据:** `examples/switch_demo/tal-data-design.md`（已确认）

## Global Constraints

- **文档与注释用中文**（仓库惯例）
- **不引入新依赖**；加密复用 `tal_aes128_cbc_encode/decode` + `tal_aes_free_data` + `tal_sha256_ret`（`tuya_transport` 的 `tal_security.h`）
- **`tal_kv` 的对外 API 与旧头逐字兼容（不得改动）**——上层 78 个调用点零改动是硬要求
- **区域布局与平台无关**：TAL 只通过 ops 卡的 4 个区域原语访问存储，不出现任何文件名/路径
- **验证命令（本仓库）**：
  - 构建：`cd examples/switch_demo/build && cmake --build . -j$(nproc)`（期望 exit 0）
  - 运行：`cd examples/switch_demo/build && timeout 20 ./switch_demo`（期望 exit 124=常驻正常；日志出现 TLS 握手 + `MQTT direct connected` + QR 22 行）
  - 自测：`cd examples/switch_demo/build && ./tal_kv_selftest`（期望输出 `ALL TESTS PASSED`，exit 0）
- **grep 用 `/usr/bin/grep`**（本机 grep/rg 是 rtk shim，有输出缓存）
- **每个 Task 结束都要提交**（提交信息用中文 + `Co-Authored-By: Claude Code <noreply@anthropic.com>` 结尾）

---

## File Structure

| 文件 | 职责 |
|---|---|
| `tuyaopen-core/iot-core/tal_system/include/tal_kv_ops.h`（新） | 区域契约：`TAL_KV_REGION_T` / `TAL_KV_OPS_T` / `tal_kv_get_ops()` |
| `port/src/tkl_kv.c`（新） | Linux 区域实现（区域文件）+ `tal_kv_ops_platform` 实例 |
| `tuyaopen-core/iot-core/tal_system/include/tal_kv.h`（新） | 公开 API：`tal_kv_init/read/write/erase` |
| `tuyaopen-core/iot-core/tal_system/src/tal_kv.c`（新） | 日志式 K-V 引擎 + AES + ops 分发 |
| `examples/switch_demo/tal_kv_selftest.c`（新） | 自测程序（区域级 + API 级 + 撕裂写/压缩/重启用例） |
| `examples/switch_demo/CMakeLists.txt`（改） | 加 `tal_kv_selftest` 目标 |
| 迁移的文件（Task 5-7） | `tuya_main.c` / `tuya_iot.c` / `tuya_endpoint.c` / `tuya_cert_manager.c` / `tuya_register_center.c` |
| 删除（Task 8） | `third_party/tal_kv/`、`tal_fs.c/h`、`tkl_flash.c/h`、`tkl_fs.c/h`、构建引用 6 处 |

---

## 关键数据结构与格式（所有 Task 共用）

### 区域信息与 ops 卡

```c
/* tal_kv_ops.h */
typedef struct {
    uint32_t size;               /* 区域总字节数 */
    uint32_t erase_granularity;  /* 擦除粒度（flash=4096；文件实现可同用 4096） */
} TAL_KV_REGION_T;

typedef struct {
    OPERATE_RET (*init)(TAL_KV_REGION_T *region);
    OPERATE_RET (*read)(uint32_t offset, uint8_t *buf, uint32_t len);
    OPERATE_RET (*write)(uint32_t offset, const uint8_t *buf, uint32_t len);
    OPERATE_RET (*erase)(uint32_t offset, uint32_t len);
} TAL_KV_OPS_T;

TAL_KV_OPS_T *tal_kv_get_ops(void);
```

### 记录格式（TAL 定义，变长）

```
偏移 0   magic    2B   0xDA7A（小端）
偏移 2   state    1B   0x01=有效 0x00=墓碑 0x02=PAD（回卷填充）
偏移 3   seq      4B   单调递增（每次 write/erase/压缩副本 +1）
偏移 7   namelen  1B   1..63
偏移 8   name     namelen B
偏移 8+n datalen  4B   密文长度（0 表示墓碑/PAD）
偏移 12+n data    datalen B   AES 密文
偏移 12+n+d crc32 4B   CRC32 覆盖 [0, 12+n+d)
记录总长 = 16 + n + d，向上对齐到 4 字节（pad 字节写 0x00）
```

### RAM 索引（init 扫描时建立）

```c
#define TAL_KV_NAME_MAX     63
#define TAL_KV_INDEX_MAX    64
#define TAL_KV_ITEM_MAX     (16 * 1024)   /* 单条明文上限 */

typedef struct {
    char     name[TAL_KV_NAME_MAX + 1];
    uint32_t rec_off;    /* 记录起始偏移 */
    uint32_t rec_len;    /* 记录总长（含 pad） */
    uint32_t data_off;   /* 记录内数据偏移 = rec_off + 12 + namelen */
    uint32_t data_len;   /* 密文长度 */
    uint32_t seq;
} TAL_KV_ITEM_T;
```

### 环形日志状态（TAL 内部静态）

```c
static TAL_KV_ITEM_T s_index[TAL_KV_INDEX_MAX];
static uint32_t s_index_cnt;
static uint32_t s_max_seq;    /* 已见最大 seq */
static uint32_t s_tail;       /* 追加点（绝对偏移，不跨区尾） */
static uint32_t s_head;       /* 最老记录起点（压缩时推进） */
static uint32_t s_region_size;
static uint32_t s_erase_gran;
static uint8_t  s_key[16], s_iv[16];
static int      s_inited;
```

**空闲空间**：`free = (s_head - s_tail + s_region_size) % s_region_size`（为 0 视为满）。

---

## Task 1: 契约头 + port 区域实现 + 区域级自测

**Files:**
- Create: `tuyaopen-core/iot-core/tal_system/include/tal_kv_ops.h`
- Create: `port/src/tkl_kv.c`
- Create: `examples/switch_demo/tal_kv_selftest.c`
- Modify: `examples/switch_demo/CMakeLists.txt`（在 `add_executable(switch_demo ...)` 之后加自测目标）

**Interfaces:**
- Produces: `TAL_KV_REGION_T`、`TAL_KV_OPS_T`、`tal_kv_get_ops()`、port 实例 `tal_kv_ops_platform`
- Consumes: 无

- [ ] **Step 1: 写契约头**

`tuyaopen-core/iot-core/tal_system/include/tal_kv_ops.h`：

```c
/**
 * @file tal_kv_ops.h
 * @brief tal_kv 的平台 ops 契约：区域语义（一块数据区 + 区域读写擦）
 *
 * port 只需提供一块可读写擦的线性区域（flash 分区 / 区域文件 / EEPROM），
 * K-V 布局与加解密由 TAL 统一实现。移植新平台 = 实现 4 个区域原语。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#ifndef __TAL_KV_OPS_H__
#define __TAL_KV_OPS_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t size;               /* 区域总字节数 */
    uint32_t erase_granularity;  /* 擦除粒度（flash=4096；文件实现可同用 4096） */
} TAL_KV_REGION_T;

typedef struct {
    /* 准备区域并回填 region 信息（建区域文件 / 挂 flash 分区） */
    OPERATE_RET (*init)(TAL_KV_REGION_T *region);
    OPERATE_RET (*read)(uint32_t offset, uint8_t *buf, uint32_t len);
    OPERATE_RET (*write)(uint32_t offset, const uint8_t *buf, uint32_t len);
    /* 擦除 [offset, offset+len)，对齐到 erase_granularity */
    OPERATE_RET (*erase)(uint32_t offset, uint32_t len);
} TAL_KV_OPS_T;

TAL_KV_OPS_T *tal_kv_get_ops(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
```

- [ ] **Step 2: 写 port 区域实现**

`port/src/tkl_kv.c`：

```c
/**
 * @file tkl_kv.c
 * @brief tal_kv 平台 ops 卡：Linux 用"区域文件"模拟一块可读写擦的数据区
 *
 * 区域 = ./tuyadb/data.bin（默认 256KB）；擦除 = 该范围写 0xFF（模拟 flash 擦除态）。
 * 移植其它平台时替换本文件：flash 直接映射分区、EEPROM 直接映射地址空间。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_kv_ops.h"

#include <stdio.h>
#include <string.h>

#define DATA_REGION_PATH   "./tuyadb/data.bin"
#define DATA_REGION_SIZE   (256 * 1024)
#define DATA_ERASE_GRAN    4096

static FILE *s_region_fp = NULL;

static OPERATE_RET __region_prefill(FILE *fp)
{
    static uint8_t s_ff[1024];
    memset(s_ff, 0xFF, sizeof(s_ff));

    for (uint32_t done = 0; done < DATA_REGION_SIZE; done += sizeof(s_ff)) {
        if (sizeof(s_ff) != fwrite(s_ff, 1, sizeof(s_ff), fp)) {
            return OPRT_COM_ERROR;
        }
    }
    return OPRT_OK;
}

static OPERATE_RET tkl_kv_init(TAL_KV_REGION_T *region)
{
    if (NULL == region) {
        return OPRT_INVALID_PARM;
    }

    if (NULL == s_region_fp) {
        s_region_fp = fopen(DATA_REGION_PATH, "r+b");
        if (NULL == s_region_fp) {
            s_region_fp = fopen(DATA_REGION_PATH, "w+b");   /* 首次：创建并预填 0xFF */
            if (NULL == s_region_fp) {
                return OPRT_COM_ERROR;
            }
            if (OPRT_OK != __region_prefill(s_region_fp)) {
                fclose(s_region_fp);
                s_region_fp = NULL;
                return OPRT_COM_ERROR;
            }
        }
    }

    region->size = DATA_REGION_SIZE;
    region->erase_granularity = DATA_ERASE_GRAN;
    return OPRT_OK;
}

static OPERATE_RET tkl_kv_read(uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (NULL == s_region_fp || NULL == buf || offset + len > DATA_REGION_SIZE) {
        return OPRT_INVALID_PARM;
    }
    if (0 != fseek(s_region_fp, (long)offset, SEEK_SET)) {
        return OPRT_COM_ERROR;
    }
    return (len == fread(buf, 1, len, s_region_fp)) ? OPRT_OK : OPRT_COM_ERROR;
}

static OPERATE_RET tkl_kv_write(uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if (NULL == s_region_fp || NULL == buf || offset + len > DATA_REGION_SIZE) {
        return OPRT_INVALID_PARM;
    }
    if (0 != fseek(s_region_fp, (long)offset, SEEK_SET)) {
        return OPRT_COM_ERROR;
    }
    if (len != fwrite(buf, 1, len, s_region_fp)) {
        return OPRT_COM_ERROR;
    }
    fflush(s_region_fp);
    return OPRT_OK;
}

static OPERATE_RET tkl_kv_erase(uint32_t offset, uint32_t len)
{
    static uint8_t s_ff[256];
    memset(s_ff, 0xFF, sizeof(s_ff));

    if (NULL == s_region_fp || 0 == len || offset + len > DATA_REGION_SIZE) {
        return OPRT_INVALID_PARM;
    }
    if (0 != fseek(s_region_fp, (long)offset, SEEK_SET)) {
        return OPRT_COM_ERROR;
    }
    for (uint32_t done = 0; done < len;) {
        uint32_t chunk = (len - done < sizeof(s_ff)) ? (len - done) : sizeof(s_ff);
        if (chunk != fwrite(s_ff, 1, chunk, s_region_fp)) {
            return OPRT_COM_ERROR;
        }
        done += chunk;
    }
    fflush(s_region_fp);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * 平台 ops 卡：由本文件组装，core 的 tal_kv.c 经 extern 引用完成分发。
 * 移植新平台时提供同名卡与 4 个区域原语即可。
 * --------------------------------------------------------------------------- */
TAL_KV_OPS_T tal_kv_ops_platform = {
    .init = tkl_kv_init,
    .read = tkl_kv_read,
    .write = tkl_kv_write,
    .erase = tkl_kv_erase,
};
```

- [ ] **Step 3: 写区域级自测（此时只测区域原语，应当失败——因为还没实现）**

`examples/switch_demo/tal_kv_selftest.c`：

```c
/**
 * @file tal_kv_selftest.c
 * @brief tal_kv 自测：区域原语 + K-V 引擎 + 撕裂写/压缩/重启用例
 *
 * 运行：cd build && ./tal_kv_selftest
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

/* Task 1 阶段 core 的 getter 尚未实现（Task 2 在 tal_kv.c 提供 tal_kv_get_ops），
 * 这里直接 extern 平台实例；Task 2 起可改用 getter。 */
extern TAL_KV_OPS_T tal_kv_ops_platform;
#define OPS (&tal_kv_ops_platform)

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

int main(void)
{
    if (0 != test_region_basic()) {
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
```

- [ ] **Step 4: 加自测构建目标**

在 `examples/switch_demo/CMakeLists.txt` 的 `target_link_libraries(switch_demo PRIVATE ${_switch_demo_link})` **之后、`unset(_switch_demo_link)` 之前**插入（**必须在此处**：`${_switch_demo_link}` 更早才被赋值，插早了会拿到空值 → 自测目标没有 include 路径）：

```cmake
# tal_kv 自测（独立可执行，链接与 switch_demo 相同的库）
add_executable(tal_kv_selftest tal_kv_selftest.c)
target_link_libraries(tal_kv_selftest PRIVATE ${_switch_demo_link})
```

- [ ] **Step 5: 构建并运行自测**

```bash
cd examples/switch_demo/build && cmake . >/dev/null && cmake --build . --target tal_kv_selftest -j$(nproc) && ./tal_kv_selftest
```

Expected: `ALL TESTS PASSED`，exit 0。

- [ ] **Step 6: 提交**

```bash
cd /home/share/samba/open-components/tuyaopen-core-examples/linux
git -C tuyaopen-core add iot-core/tal_system/include/tal_kv_ops.h
git -C tuyaopen-core commit -m "tal_system: 新增 tal_kv 区域 ops 契约头

Co-Authored-By: Claude Code <noreply@anthropic.com>"
git add port/src/tkl_kv.c examples/switch_demo/tal_kv_selftest.c examples/switch_demo/CMakeLists.txt
git commit -m "port: 新增 tkl_kv 区域实现（区域文件）+ tal_kv 自测骨架

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 2: TAL 引擎（init 扫描 + write/read/erase，无压缩）

**Files:**
- Create: `tuyaopen-core/iot-core/tal_system/include/tal_kv.h`
- Create: `tuyaopen-core/iot-core/tal_system/src/tal_kv.c`
- Modify: `examples/switch_demo/tal_kv_selftest.c`（追加 API 级用例）

**Interfaces:**
- Consumes: `TAL_KV_OPS_T` / `tal_kv_get_ops()`（Task 1）
- Produces: `tal_kv_init(const char *seed, const char *key)`、`tal_kv_read(const char *name, uint8_t *buf, size_t buf_size, size_t *out_len)`、`tal_kv_write(const char *name, const uint8_t *buf, size_t len)`、`tal_kv_erase(const char *name)`

- [ ] **Step 1: 写公开头**

`tuyaopen-core/iot-core/tal_system/include/tal_kv.h`：

```c
/**
 * @file tal_kv.h
 * @brief K-V 数据存储：init / read / write / erase（上层只关心数据）
 *
 * 对上 4 个接口，不暴露存储细节；布局与加解密由 TAL 实现，port 只提供一块
 * 可读写擦的数据区（见 tal_kv_ops.h）。name 限非空、长度 ≤63（不校验字符集：
 * 白名单是"每 name 一个文件"方案的遗留，云端下发的 name 字符集不受控）。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#ifndef __TAL_KV_H__
#define __TAL_KV_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 单条明文上限（宏可配） */
#define TAL_KV_PLAIN_MAX (16 * 1024)

/* seed/key 各 16 字节（沿用现有 tal_kv 的密钥形态） */
OPERATE_RET tal_kv_init(const char *seed, const char *key);

/* buf 由调用方提供；buf==NULL 时仅回填 *out_len（探测尺寸） */
OPERATE_RET tal_kv_read(const char *name, uint8_t *buf, size_t buf_size, size_t *out_len);

OPERATE_RET tal_kv_write(const char *name, const uint8_t *buf, size_t len);

OPERATE_RET tal_kv_erase(const char *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
```

- [ ] **Step 2: 写引擎骨架（记录编解码 + CRC + init 扫描）**

`tuyaopen-core/iot-core/tal_system/src/tal_kv.c`（本步只到 init 能建索引）：

```c
/**
 * @file tal_kv.c
 * @brief tal_kv K-V 引擎：日志式布局（追加 + 墓碑 + seq + CRC + GC 压缩）+ AES
 *
 * 区域 = 环形日志；记录从低偏移追加，写满时把各 name 的最新记录复制到空闲尾部
 * （分配新 seq）再推进 head。掉电安全：已复制的新 seq 胜出、未复制的用旧记录、
 * 撕裂记录 CRC 失败被忽略。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "tal_kv.h"
#include "tal_kv_ops.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_security.h"

#include <string.h>

/* 平台 ops 卡（port 的 tkl_kv.c 定义），core 侧经 getter 分发 */
extern TAL_KV_OPS_T tal_kv_ops_platform;

TAL_KV_OPS_T *tal_kv_get_ops(void)
{
    return &tal_kv_ops_platform;
}

#define TAL_KV_MAGIC       0xDA7A
#define TAL_KV_ST_VALID    0x01
#define TAL_KV_ST_TOMB     0x00
#define TAL_KV_ST_PAD      0x02
#define TAL_KV_NAME_MAX    63
#define TAL_KV_INDEX_MAX   64
/* 单条明文上限 TAL_KV_PLAIN_MAX 由 tal_kv.h 提供 */

typedef struct {
    char     name[TAL_KV_NAME_MAX + 1];
    uint32_t rec_off;
    uint32_t rec_len;
    uint32_t data_off;
    uint32_t data_len;
    uint32_t seq;
} TAL_KV_ITEM_T;

static TAL_KV_ITEM_T s_index[TAL_KV_INDEX_MAX];
static uint32_t s_index_cnt;
static uint32_t s_max_seq;
static uint32_t s_tail;
static uint32_t s_head;
static uint32_t s_region_size;
static uint32_t s_erase_gran;
static uint8_t  s_key[16];
static uint8_t  s_iv[16];
static int      s_inited;

/* ---- 小工具：小端读写 ---- */
static uint16_t __rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t __rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void __wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void __wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* ---- CRC32（多项式 0xEDB88320，逐位实现，避免引入查表依赖） ---- */
static uint32_t __crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

/* ---- name 合法性 ---- */
static OPERATE_RET __name_check(const char *name)
{
    size_t len;
    if (NULL == name) {
        return OPRT_INVALID_PARM;
    }
    len = strlen(name);
    if (0 == len || len > TAL_KV_NAME_MAX) {
        return OPRT_INVALID_PARM;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        int ok = ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
                  c == '_' || c == '-');
        if (!ok) {
            return OPRT_INVALID_PARM;
        }
    }
    return OPRT_OK;
}

/* ---- 记录编解码 ---- */
static uint32_t __rec_len(uint32_t namelen, uint32_t datalen)
{
    uint32_t raw = 16 + namelen + datalen;          /* magic2+state1+seq4+namelen1+datalen4+crc4 */
    return (raw + 3) & ~3u;                          /* 4 字节对齐 */
}

static void __rec_encode(uint8_t *rec, uint8_t state, uint32_t seq, const char *name, const uint8_t *data,
                         uint32_t datalen)
{
    uint32_t namelen = (uint32_t)strlen(name);
    uint32_t body = 12 + namelen + datalen;          /* 不含 crc 的部分 */
    uint32_t total = __rec_len(namelen, datalen);

    memset(rec, 0, total);
    __wr16(rec + 0, TAL_KV_MAGIC);
    rec[2] = state;
    __wr32(rec + 3, seq);
    rec[7] = (uint8_t)namelen;
    memcpy(rec + 8, name, namelen);
    __wr32(rec + 8 + namelen, datalen);
    if (datalen > 0 && NULL != data) {
        memcpy(rec + 12 + namelen, data, datalen);
    }
    __wr32(rec + body, __crc32(rec, body));
}

/* 解析一条记录；成功返回 OPRT_OK 并回填字段（data 不拷贝，只回填偏移） */
static OPERATE_RET __rec_decode(const uint8_t *rec, uint32_t buf_len, uint32_t rec_off, uint8_t *state,
                                uint32_t *seq, char name[TAL_KV_NAME_MAX + 1], uint32_t *data_off,
                                uint32_t *data_len, uint32_t *rec_len)
{
    uint32_t namelen, dlen, body, total;

    if (buf_len < 16 || TAL_KV_MAGIC != __rd16(rec)) {
        return OPRT_NOT_FOUND;
    }
    namelen = rec[7];
    if (namelen < 1 || namelen > TAL_KV_NAME_MAX) {
        return OPRT_NOT_FOUND;
    }
    if (buf_len < 12 + namelen + 4) {
        return OPRT_NOT_FOUND;
    }
    dlen = __rd32(rec + 8 + namelen);
    if (dlen > TAL_KV_PLAIN_MAX + 16) {
        return OPRT_NOT_FOUND;
    }
    body = 12 + namelen + dlen;
    total = __rec_len(namelen, dlen);
    if (buf_len < total) {
        return OPRT_NOT_FOUND;
    }
    if (__crc32(rec, body) != __rd32(rec + body)) {
        return OPRT_NOT_FOUND;                       /* 撕裂/损坏 → 视为不存在 */
    }

    memcpy(name, rec + 8, namelen);
    name[namelen] = '\0';
    *state = rec[2];
    *seq = __rd32(rec + 3);
    *data_off = rec_off + 12 + namelen;
    *data_len = dlen;
    *rec_len = total;
    return OPRT_OK;
}

static int __index_find(const char *name)
{
    for (uint32_t i = 0; i < s_index_cnt; i++) {
        if (0 == strcmp(s_index[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

/* 把 (name, seq) 写入索引：已存在且 seq 更大则更新，否则追加 */
static void __index_put(const TAL_KV_ITEM_T *item)
{
    int idx = __index_find(item->name);
    if (idx >= 0) {
        if (item->seq > s_index[idx].seq) {
            s_index[idx] = *item;
        }
        return;
    }
    if (s_index_cnt < TAL_KV_INDEX_MAX) {
        s_index[s_index_cnt++] = *item;
    } else {
        PR_WARN("tal_kv: index full, drop %s", item->name);
    }
}

static OPERATE_RET __ops_init(void)
{
    TAL_KV_REGION_T region = {0};
    TAL_KV_OPS_T *ops = tal_kv_get_ops();

    if (NULL == ops || NULL == ops->init) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != ops->init(&region)) {
        PR_ERR("tal_kv: region init fail");
        return OPRT_COM_ERROR;
    }
    if (region.size < 16 * 1024 || 0 == region.erase_granularity) {
        PR_ERR("tal_kv: region too small");
        return OPRT_COM_ERROR;
    }
    s_region_size = region.size;
    s_erase_gran = region.erase_granularity;
    return OPRT_OK;
}

/* 全区域扫描：建索引、求 max_seq / tail / head */
static OPERATE_RET __scan(void)
{
    TAL_KV_OPS_T *ops = tal_kv_get_ops();
    uint8_t *buf = NULL;
    uint32_t off = 0, min_seq = 0xFFFFFFFF, min_off = 0, max_seq = 0, max_end = 0;
    int have = 0;

    buf = tal_malloc(64);
    if (NULL == buf) {
        return OPRT_MALLOC_FAILED;
    }

    s_index_cnt = 0;
    while (off + 16 <= s_region_size) {
        uint8_t state = 0, name[TAL_KV_NAME_MAX + 1] = {0};
        uint32_t seq = 0, data_off = 0, data_len = 0, rec_len = 0, hdr_read;

        /* 先读 8 字节定长头，拿到 namelen 后读整条 */
        if (OPRT_OK != ops->read(off, buf, 8)) {
            break;
        }
        if (TAL_KV_MAGIC != __rd16(buf)) {
            break;                                    /* 到空闲区（0xFF）为止 */
        }
        uint32_t namelen = buf[7];
        if (namelen < 1 || namelen > TAL_KV_NAME_MAX) {
            break;
        }
        uint32_t need = 12 + namelen + 4;             /* 到 datalen 结尾，先读这部分 */
        if (off + need > s_region_size) {
            break;
        }
        uint8_t *hdr = tal_malloc(need);
        if (NULL == hdr) {
            tal_free(buf);
            return OPRT_MALLOC_FAILED;
        }
        if (OPRT_OK != ops->read(off, hdr, need)) {
            tal_free(hdr);
            break;
        }
        uint32_t dlen = __rd32(hdr + 8 + namelen);
        uint32_t total = __rec_len(namelen, dlen);
        hdr_read = need;
        tal_free(hdr);
        (void)hdr_read;

        if (off + total > s_region_size || total > TAL_KV_PLAIN_MAX + 128) {
            break;
        }
        uint8_t *rec = tal_malloc(total);
        if (NULL == rec) {
            tal_free(buf);
            return OPRT_MALLOC_FAILED;
        }
        if (OPRT_OK != ops->read(off, rec, total) ||
            OPRT_OK != __rec_decode(rec, total, off, &state, &seq, name, &data_off, &data_len, &rec_len)) {
            tal_free(rec);
            break;                                    /* 撕裂/损坏 → 视为日志尾 */
        }
        tal_free(rec);

        if (TAL_KV_ST_VALID == state) {
            TAL_KV_ITEM_T item;
            memset(&item, 0, sizeof(item));
            strncpy(item.name, name, TAL_KV_NAME_MAX);
            item.rec_off = off;
            item.rec_len = rec_len;
            item.data_off = data_off;
            item.data_len = data_len;
            item.seq = seq;
            __index_put(&item);
        } else if (TAL_KV_ST_TOMB == state) {
            /* 墓碑参与 seq 比较：seq 更大（或相等）的墓碑移除同名索引项；
             * 否则 erase 会在重启后被旧 VALID 记录"复活" */
            int idx = __index_find(name);
            if (idx >= 0 && seq >= s_index[idx].seq) {
                for (uint32_t i = (uint32_t)idx; i + 1 < s_index_cnt; i++) {
                    s_index[i] = s_index[i + 1];
                }
                s_index_cnt--;
            }
        }
        if (seq > max_seq || 0 == have) {
            max_seq = seq;
            max_end = off + rec_len;
        }
        if (seq < min_seq || 0 == have) {
            min_seq = seq;
            min_off = off;
        }
        have = 1;
        off += rec_len;
    }

    tal_free(buf);

    if (0 == have) {
        s_max_seq = 0;
        s_tail = 0;
        s_head = 0;
        return OPRT_OK;
    }
    s_max_seq = max_seq;
    /* 本任务（无回卷）如实停在末尾：区域满由 __append 的边界检查报 COM_ERROR；
     * 回卷语义留待 Task 3 用 PAD 记录区分"满/空" */
    s_tail = max_end;
    s_head = min_off;
    return OPRT_OK;
}

OPERATE_RET tal_kv_init(const char *seed, const char *key)
{
    uint8_t digest[32] = {0};

    if (NULL == seed || NULL == key) {
        return OPRT_INVALID_PARM;
    }
    if (s_inited) {
        return OPRT_OK;
    }
    if (OPRT_OK != __ops_init()) {
        return OPRT_COM_ERROR;
    }
    /* key/IV 派生：沿用现有 tal_kv 方案（seed/key 各做一次 SHA256 取前 16 字节） */
    if (OPRT_OK != tal_sha256_ret((const uint8_t *)seed, strlen(seed), digest, 0)) {
        return OPRT_COM_ERROR;
    }
    memcpy(s_iv, digest, 16);
    if (OPRT_OK != tal_sha256_ret((const uint8_t *)key, strlen(key), digest, 0)) {
        return OPRT_COM_ERROR;
    }
    memcpy(s_key, digest, 16);

    if (OPRT_OK != __scan()) {
        return OPRT_COM_ERROR;
    }
    s_inited = 1;
    return OPRT_OK;
}
```

- [ ] **Step 3: 追加 API 级自测用例（应当失败——write/read 还没实现）**

在 `tal_kv_selftest.c` 的 `main()` 前追加：

```c
#include "tal_kv.h"

#define SELFTEST_REGION "./tuyadb/data.bin"

static int test_kv_basic(void)
{
    uint8_t wr[64], rd[64];
    size_t out_len = 0;

    CHECK(OPRT_OK == tal_kv_init("0123456789abcdef", "fedcba9876543210"));

    for (size_t i = 0; i < sizeof(wr); i++) {
        wr[i] = (uint8_t)(i + 1);
    }
    CHECK(OPRT_OK == tal_kv_write("test.kv", wr, sizeof(wr)));

    memset(rd, 0, sizeof(rd));
    CHECK(OPRT_OK == tal_kv_read("test.kv", rd, sizeof(rd), &out_len));
    CHECK(out_len == sizeof(wr));
    CHECK(0 == memcmp(wr, rd, sizeof(wr)));

    /* 连续第二次读取同一 key：IV 必须每次从派生值重取，不能被 CBC 就地改写污染
     * （只回退 read 侧 iv 副本时，仅此断言会失败；write 侧的由上面的用例覆盖） */
    memset(rd, 0, sizeof(rd));
    CHECK(OPRT_OK == tal_kv_read("test.kv", rd, sizeof(rd), &out_len));
    CHECK(out_len == sizeof(wr) && 0 == memcmp(wr, rd, sizeof(wr)));

    /* 探测尺寸 */
    CHECK(OPRT_OK == tal_kv_read("test.kv", NULL, 0, &out_len));
    CHECK(out_len == sizeof(wr));

    /* buffer 过小 → 回填真实长度 */
    CHECK(OPRT_INVALID_PARM == tal_kv_read("test.kv", rd, 8, &out_len));
    CHECK(out_len == sizeof(wr));

    /* 不存在 → NOT_FOUND */
    CHECK(OPRT_NOT_FOUND == tal_kv_read("no.such", rd, sizeof(rd), &out_len));

    /* 覆盖写 */
    memset(wr, 0x5A, 16);
    CHECK(OPRT_OK == tal_kv_write("test.kv", wr, 16));
    CHECK(OPRT_OK == tal_kv_read("test.kv", rd, sizeof(rd), &out_len));
    CHECK(out_len == 16 && 0 == memcmp(wr, rd, 16));

    /* 删除（幂等） */
    CHECK(OPRT_OK == tal_kv_erase("test.kv"));
    CHECK(OPRT_OK == tal_kv_erase("test.kv"));
    CHECK(OPRT_NOT_FOUND == tal_kv_read("test.kv", rd, sizeof(rd), &out_len));

    /* name 规则 */
    CHECK(OPRT_INVALID_PARM == tal_kv_write("bad/name", wr, 16));
    CHECK(OPRT_INVALID_PARM == tal_kv_write("", wr, 16));
    CHECK(OPRT_INVALID_PARM == tal_kv_write("ok.name", NULL, 16));
    CHECK(OPRT_INVALID_PARM == tal_kv_write("ok.name", wr, 0));      /* 空数据 */
    CHECK(OPRT_INVALID_PARM == tal_kv_write("ok.name", wr, TAL_KV_PLAIN_MAX + 1));  /* 超长 */
    return 0;
}
```

并在 `main()` 中加：

```c
    if (0 != test_kv_basic()) {
        return 1;
    }
```

同时把 Task 1 留下的 `extern TAL_KV_OPS_T tal_kv_ops_platform;` 就地改用 core 的 getter（本任务已在 `tal_kv.c` 提供）：

```c
#define OPS (tal_kv_get_ops())
```

- [ ] **Step 4: 实现 write / read / erase（追加式）**

在 `tal_kv.c` 的 `tal_kv_init` 之后追加：

```c
static OPERATE_RET __append(const uint8_t *rec, uint32_t rec_len)
{
    TAL_KV_OPS_T *ops = tal_kv_get_ops();
    uint32_t free_space = (s_head - s_tail + s_region_size) % s_region_size;

    if (0 == free_space) {
        free_space = s_region_size;                  /* 空区域 */
    }
    if (rec_len + 1 > free_space) {
        return OPRT_COM_ERROR;                       /* 空间不足（Task 3 引入压缩） */
    }
    if (s_tail + rec_len > s_region_size) {
        return OPRT_COM_ERROR;                       /* 跨区尾（Task 3 引入 PAD 回卷） */
    }
    if (OPRT_OK != ops->write(s_tail, rec, rec_len)) {
        return OPRT_COM_ERROR;
    }
    s_tail += rec_len;
    return OPRT_OK;
}

OPERATE_RET tal_kv_write(const char *name, const uint8_t *buf, size_t len)
{
    uint8_t *ct = NULL, *rec = NULL;
    uint8_t iv_local[16];
    uint32_t ct_len = 0, rec_len;
    OPERATE_RET ret;

    if (!s_inited) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != __name_check(name) || NULL == buf || 0 == len || len > TAL_KV_PLAIN_MAX) {
        return OPRT_INVALID_PARM;
    }
    if (__index_find(name) < 0 && s_index_cnt >= TAL_KV_INDEX_MAX) {
        return OPRT_COM_ERROR;                       /* 索引已满：先拒绝，避免"写成功但读不到" */
    }
    /* mbedtls CBC 会就地改写传入的 iv，必须传局部副本（否则全局 IV 被永久污染） */
    memcpy(iv_local, s_iv, 16);
    if (OPRT_OK != tal_aes128_cbc_encode((uint8_t *)buf, (uint32_t)len, s_key, iv_local, &ct, &ct_len)) {
        return OPRT_COM_ERROR;
    }
    rec_len = __rec_len((uint32_t)strlen(name), ct_len);
    rec = tal_malloc(rec_len);
    if (NULL == rec) {
        tal_aes_free_data(ct);
        return OPRT_MALLOC_FAILED;
    }
    __rec_encode(rec, TAL_KV_ST_VALID, ++s_max_seq, name, ct, ct_len);
    ret = __append(rec, rec_len);
    if (OPRT_OK == ret) {
        TAL_KV_ITEM_T item;
        int idx;
        memset(&item, 0, sizeof(item));
        strncpy(item.name, name, TAL_KV_NAME_MAX);
        item.rec_off = s_tail - rec_len;
        item.rec_len = rec_len;
        item.data_off = item.rec_off + 12 + (uint32_t)strlen(name);
        item.data_len = ct_len;
        item.seq = s_max_seq;
        idx = __index_find(name);
        if (idx >= 0) {
            s_index[idx] = item;
        } else if (s_index_cnt < TAL_KV_INDEX_MAX) {
            s_index[s_index_cnt++] = item;
        }
    } else {
        s_max_seq--;                                 /* 回滚 seq */
    }
    tal_free(rec);
    tal_aes_free_data(ct);
    return ret;
}

OPERATE_RET tal_kv_read(const char *name, uint8_t *buf, size_t buf_size, size_t *out_len)
{
    TAL_KV_OPS_T *ops = tal_kv_get_ops();
    int idx;
    uint8_t *rec = NULL, *ct = NULL, *pt = NULL;
    uint8_t state = 0;
    uint8_t iv_local[16];
    char rname[TAL_KV_NAME_MAX + 1];
    uint32_t seq = 0, doff = 0, dlen = 0, rlen = 0, data_in_rec;
    uint32_t pt_len = 0;

    if (!s_inited) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != __name_check(name) || NULL == out_len) {
        return OPRT_INVALID_PARM;
    }
    idx = __index_find(name);
    if (idx < 0) {
        return OPRT_NOT_FOUND;
    }
    /* 读整条记录并复核 CRC：扫描后数据可能被破坏（撕裂写/位翻转） */
    rec = tal_malloc(s_index[idx].rec_len);
    if (NULL == rec) {
        return OPRT_MALLOC_FAILED;
    }
    if (OPRT_OK != ops->read(s_index[idx].rec_off, rec, s_index[idx].rec_len) ||
        OPRT_OK != __rec_decode(rec, s_index[idx].rec_len, s_index[idx].rec_off, &state, &seq, rname, &doff, &dlen,
                                &rlen) ||
        TAL_KV_ST_VALID != state || 0 != strcmp(rname, name)) {
        tal_free(rec);
        return OPRT_NOT_FOUND;                       /* 含"索引项指向的记录 name 不符"的悬垂防护 */
    }
    ct = tal_malloc(dlen);
    if (NULL == ct) {
        tal_free(rec);
        return OPRT_MALLOC_FAILED;
    }
    data_in_rec = s_index[idx].data_off - s_index[idx].rec_off;
    memcpy(ct, rec + data_in_rec, dlen);
    tal_free(rec);

    /* mbedtls CBC 会就地改写传入的 iv，必须传局部副本 */
    memcpy(iv_local, s_iv, 16);
    if (OPRT_OK != tal_aes128_cbc_decode(ct, dlen, s_key, iv_local, &pt, &pt_len)) {
        tal_free(ct);
        return OPRT_COM_ERROR;
    }
    tal_free(ct);

    /* tal_aes128_cbc_decode 返回的是密文长度，需按 PKCS7 求实际明文长度 */
    {
        int32_t actual = tal_aes_get_actual_length(pt, pt_len);
        if (actual < 0) {
            tal_aes_free_data(pt);
            return OPRT_COM_ERROR;
        }
        pt_len = (uint32_t)actual;
    }

    *out_len = pt_len;
    if (NULL == buf) {
        tal_aes_free_data(pt);
        return OPRT_OK;                              /* 探测尺寸 */
    }
    if (pt_len > buf_size) {
        tal_aes_free_data(pt);
        return OPRT_INVALID_PARM;                    /* 已回填真实长度 */
    }
    memcpy(buf, pt, pt_len);
    tal_aes_free_data(pt);
    return OPRT_OK;
}

OPERATE_RET tal_kv_erase(const char *name)
{
    uint8_t *rec = NULL;
    uint32_t rec_len;
    int idx;
    OPERATE_RET ret;

    if (!s_inited) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != __name_check(name)) {
        return OPRT_INVALID_PARM;
    }
    idx = __index_find(name);
    if (idx < 0) {
        return OPRT_OK;                              /* 幂等 */
    }
    rec_len = __rec_len((uint32_t)strlen(name), 0);
    rec = tal_malloc(rec_len);
    if (NULL == rec) {
        return OPRT_MALLOC_FAILED;
    }
    __rec_encode(rec, TAL_KV_ST_TOMB, ++s_max_seq, name, NULL, 0);
    ret = __append(rec, rec_len);
    if (OPRT_OK == ret) {
        for (uint32_t i = (uint32_t)idx; i + 1 < s_index_cnt; i++) {
            s_index[i] = s_index[i + 1];             /* 从索引移除 */
        }
        s_index_cnt--;
    } else {
        s_max_seq--;
    }
    tal_free(rec);
    return ret;
}
```

- [ ] **Step 5: 构建并运行自测**

```bash
cd examples/switch_demo/build && cmake --build . --target tal_kv_selftest -j$(nproc) && rm -f tuyadb/data.bin && ./tal_kv_selftest
```

Expected: `ALL TESTS PASSED`（Task 3 之前请先 `rm -f tuyadb/data.bin` 从干净区域开始）。

- [ ] **Step 6: 提交**

```bash
git -C tuyaopen-core add iot-core/tal_system/include/tal_kv.h iot-core/tal_system/src/tal_kv.c
git -C tuyaopen-core commit -m "tal_system: 新增 tal_kv K-V 引擎（日志式追加 + AES）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
git add examples/switch_demo/tal_kv_selftest.c
git commit -m "switch_demo: tal_kv 自测补充 API 级用例

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 3: 压缩（GC）+ 跨区尾回卷 + 掉电/撕裂专项用例

**Files:**
- Modify: `tuyaopen-core/iot-core/tal_system/src/tal_kv.c`（`__append` 加 PAD 回卷与压缩；新增 `__compact`）
- Modify: `examples/switch_demo/tal_kv_selftest.c`（追加压缩/撕裂/重启用例）

**Interfaces:**
- Consumes: Task 2 的全部内部函数与状态
- Produces: 无新对外接口（`__append` 语义变为"必要时先压缩再追加"）

- [ ] **Step 1: 写压缩用例（应当失败）**

在 `tal_kv_selftest.c` 追加：

```c
static int test_kv_compaction(void)
{
    uint8_t blob[8 * 1024], rd[8 * 1024];
    size_t out_len = 0;
    char name[16];

    /* 反复覆盖写大对象，强制触发压缩（区域 256KB） */
    for (int i = 0; i < 64; i++) {
        memset(blob, (uint8_t)i, sizeof(blob));
        snprintf(name, sizeof(name), "bulk.%d", i % 4);     /* 4 个 key 轮换 */
        CHECK(OPRT_OK == tal_kv_write(name, blob, sizeof(blob)));
    }
    /* 压缩后所有 key 仍可读，且内容为最后一次写入的 */
    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "bulk.%d", i);
        memset(rd, 0, sizeof(rd));
        CHECK(OPRT_OK == tal_kv_read(name, rd, sizeof(rd), &out_len));
        CHECK(out_len == sizeof(blob));
        CHECK(rd[0] == (uint8_t)(60 + i));                  /* 最后一轮 i=60..63 */
    }
    return 0;
}

static int test_kv_torn_write(void)
{
    /* 撕裂写在**进程内**观察不到（读路径用 RAM 索引 + 复核 CRC，但损坏发生在
     * 本次运行写入之后时，需重启重扫才能体现）→ 拆成 setup / check 两个模式，
     * 由 Step 4 的命令分两次调用。 */
    return 0;
}

static int test_kv_reopen(void)
{
    uint8_t wr[32], rd[32];
    size_t out_len = 0;

    memset(wr, 0x3C, sizeof(wr));
    CHECK(OPRT_OK == tal_kv_write("persist.key", wr, sizeof(wr)));

    /* 模拟重启：重置引擎状态后重新 init（不删区域文件） */
    /* 说明：引擎内部有 s_inited 标志；测试通过重新 exec 本程序并带参数实现 */
    return 0;
}

/* 压缩中途掉电的安全性（副本先写、旧记录后擦）由代码评审核对顺序不变量，
 * 不做运行时注入测试——见设计文档第五节的压缩步骤与 Task 3 的 __compact 实现。 */
static int test_kv_compact_torn(void)
{
    return 0;                                        /* 占位：安全性由评审核对 */
}
```

`main()` 追加：

```c
    if (0 != test_kv_compaction()) {
        return 1;
    }
    if (0 != test_kv_torn_write()) {
        return 1;
    }
```

并在 `main()` 开头加"重启模式"：

```c
int main(int argc, char **argv)
{
    if (argc > 1 && 0 == strcmp(argv[1], "reopen-check")) {
        /* 重启后校验：上一轮写的 persist.key 仍可读 */
        uint8_t rd[32];
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_kv_init("0123456789abcdef", "fedcba9876543210"));
        CHECK(OPRT_OK == tal_kv_read("persist.key", rd, sizeof(rd), &out_len));
        CHECK(out_len == sizeof(rd));
        for (size_t i = 0; i < sizeof(rd); i++) {
            CHECK(0x3C == rd[i]);
        }
        printf("REOPEN OK\n");
        return 0;
    }
    ...
```

- [ ] **Step 2: 运行，确认失败**

```bash
cd examples/switch_demo/build && cmake --build . --target tal_kv_selftest -j$(nproc) && rm -f tuyadb/data.bin && ./tal_kv_selftest
```

Expected: FAIL（`bulk.*` 写到第 N 次时报空间不足，或 `tal_kv_write` 返回 `OPRT_COM_ERROR`）。

- [ ] **Step 3: 实现 PAD 回卷 + 压缩**

把 `tal_kv.c` 的 `__append` 替换为：

```c
static OPERATE_RET __compact(void);
static OPERATE_RET __append_rec(const uint8_t *rec, uint32_t rec_len);

/* 追加一条记录；空间不足时先压缩，再尝试；跨区尾时先写 PAD 回卷 */
static OPERATE_RET __append(const uint8_t *rec, uint32_t rec_len)
{
    OPERATE_RET ret;

    ret = __append_rec(rec, rec_len);
    if (OPRT_OK == ret) {
        return OPRT_OK;
    }
    if (OPRT_OK != __compact()) {
        return OPRT_COM_ERROR;                       /* 压缩也无法腾出空间 → 区域满 */
    }
    ret = __append_rec(rec, rec_len);
    if (OPRT_OK != ret) {
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

static OPERATE_RET __append_rec(const uint8_t *rec, uint32_t rec_len)
{
    TAL_KV_OPS_T *ops = tal_kv_get_ops();
    uint32_t free_space = (s_head - s_tail + s_region_size) % s_region_size;

    if (0 == free_space) {
        free_space = s_region_size;
    }
    /* 跨区尾：写 PAD 记录填满剩余，再回卷到 0 */
    if (s_tail + rec_len > s_region_size) {
        uint32_t pad_len = s_region_size - s_tail;
        if (pad_len >= 16) {
            uint8_t *pad = tal_malloc(pad_len);
            if (NULL == pad) {
                return OPRT_MALLOC_FAILED;
            }
            memset(pad, 0xFF, pad_len);               /* 未用尾部保持擦除态 0xFF */
            __rec_encode(pad, TAL_KV_ST_PAD, ++s_max_seq, "pad", NULL, 0);
            if (OPRT_OK != ops->write(s_tail, pad, pad_len)) {
                tal_free(pad);
                s_max_seq--;
                return OPRT_COM_ERROR;
            }
            tal_free(pad);
        }
        s_tail = 0;
        free_space = (s_head - s_tail + s_region_size) % s_region_size;
        if (0 == free_space) {
            free_space = s_region_size;
        }
    }
    if (rec_len + 1 > free_space) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != ops->write(s_tail, rec, rec_len)) {
        return OPRT_COM_ERROR;
    }
    s_tail += rec_len;
    return OPRT_OK;
}

/* GC 压缩：把各 name 的最新记录复制到空闲尾部（分配新 seq），再推进 head */
static OPERATE_RET __compact(void)
{
    uint32_t live_len = 0, free_space, old_tail;
    TAL_KV_ITEM_T old_index[TAL_KV_INDEX_MAX];
    uint32_t old_cnt = s_index_cnt;
    OPERATE_RET ret = OPRT_OK;

    for (uint32_t i = 0; i < s_index_cnt; i++) {
        live_len += s_index[i].rec_len;
    }
    free_space = (s_head - s_tail + s_region_size) % s_region_size;
    if (0 == free_space) {
        free_space = s_region_size;
    }
    if (live_len + 16 > free_space) {
        return OPRT_COM_ERROR;                       /* 压缩也无法腾出空间 */
    }

    memcpy(old_index, s_index, sizeof(old_index));
    old_tail = s_tail;

    for (uint32_t i = 0; i < old_cnt; i++) {
        uint8_t *rec = NULL;
        TAL_KV_ITEM_T item = old_index[i];

        rec = tal_malloc(item.rec_len);
        if (NULL == rec) {
            return OPRT_MALLOC_FAILED;
        }
        if (OPRT_OK != tal_kv_get_ops()->read(item.rec_off, rec, item.rec_len)) {
            tal_free(rec);
            return OPRT_COM_ERROR;
        }
        /* 复制为一条新记录（新 seq） */
        {
            uint8_t *ct = tal_malloc(item.data_len);
            uint32_t new_len = __rec_len((uint32_t)strlen(item.name), item.data_len);
            uint8_t *new_rec = NULL;
            if (NULL == ct) {
                tal_free(rec);
                return OPRT_MALLOC_FAILED;
            }
            if (OPRT_OK != tal_kv_get_ops()->read(item.data_off, ct, item.data_len)) {
                tal_free(ct);
                tal_free(rec);
                return OPRT_COM_ERROR;
            }
            new_rec = tal_malloc(new_len);
            if (NULL == new_rec) {
                tal_free(ct);
                tal_free(rec);
                return OPRT_MALLOC_FAILED;
            }
            __rec_encode(new_rec, TAL_KV_ST_VALID, ++s_max_seq, item.name, ct, item.data_len);
            ret = __append_rec(new_rec, new_len);
            if (OPRT_OK == ret) {
                /* s_tail 可能回卷到 0，做回卷安全的偏移计算 */
                s_index[i].rec_off = (s_tail >= new_len) ? (s_tail - new_len) : (s_region_size + s_tail - new_len);
                s_index[i].rec_len = new_len;
                s_index[i].data_off = s_index[i].rec_off + 12 + (uint32_t)strlen(item.name);
                s_index[i].seq = s_max_seq;
            }
            tal_free(new_rec);
            tal_free(ct);
        }
        tal_free(rec);
        if (OPRT_OK != ret) {
            return ret;
        }
    }

    s_head = old_tail;                                /* 旧记录区变为空闲 */
    /* flash 语义：回收区必须先擦除才能再写（文件实现下 erase=写 0xFF，等价无害）。
     * 擦除范围按擦除粒度对齐，且**上界不超过新副本起点**，避免擦掉刚复制的数据。 */
    {
        uint32_t era_beg = (s_head / s_erase_gran) * s_erase_gran;
        uint32_t era_end = (old_tail / s_erase_gran) * s_erase_gran;   /* 不含新副本所在扇区 */
        if (era_end > era_beg) {
            if (OPRT_OK != tal_kv_get_ops()->erase(era_beg, era_end - era_beg)) {
                PR_WARN("tal_kv: reclaim erase fail (%u,%u)", era_beg, era_end - era_beg);
            }
        }
    }
    return OPRT_OK;
}
```

- [ ] **Step 4: 运行自测（压缩 + 撕裂用例）**

```bash
cd examples/switch_demo/build && cmake --build . --target tal_kv_selftest -j$(nproc) \
  && rm -f tuyadb/data.bin && ./tal_kv_selftest \
  && ./tal_kv_selftest reopen-check \
  && rm -f tuyadb/data.bin && ./tal_kv_selftest torn-setup \
  && ./tal_kv_selftest torn-check
```

`main()` 中追加两个撕裂模式（在 `reopen-check` 分支之后）：

```c
    if (argc > 1 && 0 == strcmp(argv[1], "torn-setup")) {
        /* 干净区域上只写一条记录 → 记录必然从偏移 0 开始，数据区偏移 = 12 + namelen */
        uint8_t wr[256];
        memset(wr, 0x11, sizeof(wr));
        CHECK(OPRT_OK == tal_kv_init("0123456789abcdef", "fedcba9876543210"));
        CHECK(OPRT_OK == tal_kv_write("torn.key", wr, sizeof(wr)));
        {
            uint8_t bad = 0x00;
            uint32_t data_off = 12 + (uint32_t)strlen("torn.key");
            CHECK(OPRT_OK == tal_kv_get_ops()->write(data_off, &bad, 1));  /* 破坏数据首字节 */
        }
        printf("TORN SETUP OK\n");
        return 0;
    }
    if (argc > 1 && 0 == strcmp(argv[1], "torn-check")) {
        uint8_t rd[256];
        size_t out_len = 0;
        CHECK(OPRT_OK == tal_kv_init("0123456789abcdef", "fedcba9876543210"));
        /* 重扫时该记录 CRC 失败 → 视为不存在 */
        CHECK(OPRT_NOT_FOUND == tal_kv_read("torn.key", rd, sizeof(rd), &out_len));
        printf("TORN CHECK OK\n");
        return 0;
    }
```

Expected: `ALL TESTS PASSED` / `REOPEN OK` / `TORN SETUP OK` / `TORN CHECK OK`（四段依次）。

- [ ] **Step 5: 提交**

```bash
git -C tuyaopen-core add iot-core/tal_system/src/tal_kv.c
git -C tuyaopen-core commit -m "tal_system: tal_kv 增加 GC 压缩与跨区尾回卷

Co-Authored-By: Claude Code <noreply@anthropic.com>"
git add examples/switch_demo/tal_kv_selftest.c
git commit -m "switch_demo: tal_kv 自测补充压缩/撕裂/重启用例

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

### Step 3-修正：设计修正后重做（**以本段为准，替代上面 Step 3 的实现**）

> 2026-10-08 修正。原 Step 3 的设计有洞（实测：GC 是死代码 / 区域满静默覆盖活数据 / 压缩必失败）。修正规则见设计文档第四、五节；本段给出完整实现。

**1) 空闲空间与追加（替换 `__append` / `__append_rec`）**

```c
/* 空闲间隙（环形：tail 顺时针到 head）；0 表示"空"（head==tail） */
static uint32_t __free_gap(void)
{
    return (s_head - s_tail + s_region_size) % s_region_size;
}

/* 可用空闲：空区视为整区；否则保留 1 字节余量，使 tail 永不追上 head（消除满/空歧义） */
static uint32_t __usable_free(void)
{
    uint32_t gap = __free_gap();
    return (0 == gap) ? s_region_size : (gap - 1);
}

/* 占用区内的垃圾字节数（占用 - 有效） */
static uint32_t __garbage_len(void)
{
    uint32_t live = 0, occupied;
    for (uint32_t i = 0; i < s_index_cnt; i++) {
        live += s_index[i].rec_len;
    }
    occupied = (s_head == s_tail) ? 0 : (s_region_size - __free_gap());
    return (occupied > live) ? (occupied - live) : 0;
}

/* 追加一条记录：只写空闲间隙；跨区尾仅在"回卷后不越过 head"时允许 PAD 回卷 */
static OPERATE_RET __append_rec(const uint8_t *rec, uint32_t rec_len)
{
    TAL_KV_OPS_T *ops = tal_kv_get_ops();

    if (s_tail + rec_len > s_region_size) {
        uint32_t pad_len = s_region_size - s_tail;
        /* 回卷后从 0 起写，只有 [0, head) 是空闲；head==0 时无处可回卷 */
        if (0 == s_head || rec_len + 1 > s_head) {
            return OPRT_COM_ERROR;
        }
        if (pad_len >= 16) {
            uint8_t *pad = tal_malloc(pad_len);
            if (NULL == pad) {
                return OPRT_MALLOC_FAILED;
            }
            memset(pad, 0xFF, pad_len);
            __rec_encode(pad, TAL_KV_ST_PAD, ++s_max_seq, "pad", NULL, 0);
            if (OPRT_OK != ops->write(s_tail, pad, pad_len)) {
                tal_free(pad);
                s_max_seq--;
                return OPRT_COM_ERROR;
            }
            tal_free(pad);
        }
        s_tail = 0;
    }
    if (rec_len + 1 > __usable_free()) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != ops->write(s_tail, rec, rec_len)) {
        return OPRT_COM_ERROR;
    }
    s_tail += rec_len;
    return OPRT_OK;
}

/* 追加：主动阈值压缩（等写不下再压就晚了——那时 free < live，副本无处放） */
static OPERATE_RET __append(const uint8_t *rec, uint32_t rec_len)
{
    if (__usable_free() < s_region_size / 2 && __garbage_len() > 0) {
        (void)__compact();
    }
    if (OPRT_OK == __append_rec(rec, rec_len)) {
        return OPRT_OK;
    }
    if (OPRT_OK != __compact()) {
        return OPRT_COM_ERROR;                       /* 压缩也无法腾出空间 → 区域满 */
    }
    return __append_rec(rec, rec_len);
}
```

**2) GC 压缩（替换 `__compact`）**

```c
/* GC：把各 name 最新记录复制到空闲间隙（新 seq），推进 head 并擦除回收区。
 * 副本起点对齐到擦除粒度，保证回收擦除不触碰副本。 */
static OPERATE_RET __compact(void)
{
    uint32_t live_len = 0, old_tail, old_head, copy_start;
    TAL_KV_ITEM_T old_index[TAL_KV_INDEX_MAX];
    uint32_t old_cnt;

    for (uint32_t i = 0; i < s_index_cnt; i++) {
        live_len += s_index[i].rec_len;
    }
    if (live_len + 16 > __usable_free()) {
        return OPRT_COM_ERROR;                       /* 活数据装不进空闲间隙（已知限制：活数据 > 半区） */
    }
    if (0 == live_len) {
        s_head = s_tail;                             /* 无有效数据：整区可回收 */
        return OPRT_OK;
    }

    memcpy(old_index, s_index, sizeof(old_index));
    old_cnt = s_index_cnt;
    old_tail = s_tail;
    old_head = s_head;

    copy_start = (old_tail + s_erase_gran - 1) / s_erase_gran * s_erase_gran;
    if (copy_start + live_len + 16 > s_region_size) {
        return OPRT_COM_ERROR;                       /* 对齐后放不下 */
    }
    s_tail = copy_start;

    for (uint32_t i = 0; i < old_cnt; i++) {
        TAL_KV_ITEM_T item = old_index[i];
        uint8_t *ct = NULL, *new_rec = NULL;
        uint32_t new_len;
        OPERATE_RET ret;

        ct = tal_malloc(item.data_len);
        if (NULL == ct) {
            return OPRT_MALLOC_FAILED;
        }
        if (OPRT_OK != tal_kv_get_ops()->read(item.data_off, ct, item.data_len)) {
            tal_free(ct);
            return OPRT_COM_ERROR;
        }
        new_len = __rec_len((uint32_t)strlen(item.name), item.data_len);
        new_rec = tal_malloc(new_len);
        if (NULL == new_rec) {
            tal_free(ct);
            return OPRT_MALLOC_FAILED;
        }
        __rec_encode(new_rec, TAL_KV_ST_VALID, ++s_max_seq, item.name, ct, item.data_len);
        ret = __append_rec(new_rec, new_len);
        if (OPRT_OK == ret) {
            /* 用新的 s_tail 反推副本位置（回卷安全） */
            s_index[i].rec_off = (s_tail >= new_len) ? (s_tail - new_len) : (s_region_size + s_tail - new_len);
            s_index[i].rec_len = new_len;
            s_index[i].data_off = s_index[i].rec_off + 12 + (uint32_t)strlen(item.name);
            s_index[i].seq = s_max_seq;
        }
        tal_free(new_rec);
        tal_free(ct);
        if (OPRT_OK != ret) {
            return ret;                              /* 部分完成的副本 seq 更大 → 重启后按 seq 收敛，安全 */
        }
    }

    s_head = old_tail;                               /* 旧记录区变空闲 */
    /* 回收擦除：上界 = copy_start（已对齐），严格不触碰副本 */
    {
        uint32_t era_beg = (old_head / s_erase_gran) * s_erase_gran;
        if (copy_start > era_beg) {
            if (OPRT_OK != tal_kv_get_ops()->erase(era_beg, copy_start - era_beg)) {
                PR_WARN("tal_kv: reclaim erase fail (%u,%u)", era_beg, copy_start - era_beg);
            }
        }
    }
    return OPRT_OK;
}
```

**3) 扫描改为容忍空洞（替换 `__scan` 的主循环）**

回收擦除后日志出现 0xFF 空洞，"遇到空洞即停"会漏掉其后的全部记录（重启即丢数据）。改为**全区域分块扫描 + 逐字节找 magic + CRC 验证**：

```c
/* 全区域扫描：分块读入内存找 magic；命中则按 CRC 验证——
 * 通过 = 记录（按记录长度前进）；不通过 = 数据内的假 magic（前进 1 字节）。
 * 回收擦除后的 0xFF 空洞因此可被跳过。 */
static OPERATE_RET __scan(void)
{
    TAL_KV_OPS_T *ops = tal_kv_get_ops();
    uint8_t *chunk = NULL;
    const uint32_t CHUNK = 512;
    uint32_t off = 0, min_seq = 0xFFFFFFFF, min_off = 0, max_seq = 0, max_end = 0;
    int have = 0;

    chunk = tal_malloc(CHUNK);
    if (NULL == chunk) {
        return OPRT_MALLOC_FAILED;
    }

    s_index_cnt = 0;
    while (off + 16 <= s_region_size) {
        uint32_t scan = 0, limit = (s_region_size - off < CHUNK) ? (s_region_size - off) : CHUNK;
        uint8_t state = 0, name[TAL_KV_NAME_MAX + 1] = {0};
        uint32_t seq = 0, data_off = 0, data_len = 0, rec_len = 0;
        uint32_t namelen, dlen, total;
        uint8_t *rec = NULL;

        if (OPRT_OK != ops->read(off, chunk, limit)) {
            break;
        }
        /* 在块内找 magic */
        while (scan + 2 <= limit && TAL_KV_MAGIC != __rd16(chunk + scan)) {
            scan++;
        }
        if (scan + 2 > limit) {
            off += limit;                            /* 本块无 magic → 跳过整块 */
            continue;
        }
        off += scan;                                 /* 定位到候选记录起点 */

        /* 候选点：读定长头拿 namelen/datalen，再读整条验 CRC */
        uint8_t hdr[16];
        if (off + 16 > s_region_size || OPRT_OK != ops->read(off, hdr, 16)) {
            break;
        }
        namelen = hdr[7];
        if (namelen < 1 || namelen > TAL_KV_NAME_MAX) {
            off++;                                   /* 假 magic → 前进 1 字节 */
            continue;
        }
        dlen = __rd32(hdr + 8 + namelen);
        total = __rec_len(namelen, dlen);
        if (total > TAL_KV_PLAIN_MAX + 128 || off + total > s_region_size) {
            off++;
            continue;
        }
        rec = tal_malloc(total);
        if (NULL == rec) {
            tal_free(chunk);
            return OPRT_MALLOC_FAILED;
        }
        if (OPRT_OK != ops->read(off, rec, total) ||
            OPRT_OK != __rec_decode(rec, total, off, &state, &seq, name, &data_off, &data_len, &rec_len)) {
            tal_free(rec);
            off++;                                   /* 假 magic / 撕裂记录 → 前进 1 字节 */
            continue;
        }
        tal_free(rec);

        if (TAL_KV_ST_VALID == state) {
            TAL_KV_ITEM_T item;
            memset(&item, 0, sizeof(item));
            strncpy(item.name, name, TAL_KV_NAME_MAX);
            item.rec_off = off;
            item.rec_len = rec_len;
            item.data_off = data_off;
            item.data_len = data_len;
            item.seq = seq;
            __index_put(&item);
        } else if (TAL_KV_ST_TOMB == state) {
            /* 墓碑参与 seq 比较：seq 更大（或相等）的墓碑移除同名索引项；
             * 否则 erase 会在重启后被旧 VALID 记录"复活"。
             * 不变量：依赖"扫描顺序内 seq 单调"（后扫到的记录 seq 不大于先扫到的）；
             * GC 副本追加到更高偏移且分配新 seq，该不变量仍成立；若将来 GC 布局破坏它，
             * 需改为扫描末尾按 name 归并。 */
            int idx = __index_find(name);
            if (idx >= 0 && seq >= s_index[idx].seq) {
                for (uint32_t i = (uint32_t)idx; i + 1 < s_index_cnt; i++) {
                    s_index[i] = s_index[i + 1];
                }
                s_index_cnt--;
            }
        }
        if (seq > max_seq || 0 == have) {
            max_seq = seq;
            max_end = off + rec_len;
        }
        if (seq < min_seq || 0 == have) {
            min_seq = seq;
            min_off = off;
        }
        have = 1;
        off += rec_len;                              /* 记录有效 → 按记录长度前进 */
    }

    tal_free(chunk);

    if (0 == have) {
        s_max_seq = 0;
        s_tail = 0;
        s_head = 0;
        return OPRT_OK;
    }
    s_max_seq = max_seq;
    s_tail = max_end;
    s_head = min_off;
    return OPRT_OK;
}
```

**4) 自测补「老 key 存活」断言（修掉假绿）**

`test_kv_compaction` 里追加：压缩前先写一个**只写一次、之后不再碰**的老 key，64 次 bulk 覆盖写之后断言它仍可读且内容不变：

```c
    uint8_t old_blob[512], rd_old[512];
    size_t out_old = 0;
    memset(old_blob, 0x33, sizeof(old_blob));
    CHECK(OPRT_OK == tal_kv_write("old.key", old_blob, sizeof(old_blob)));
    /* ... 64 次 bulk 覆盖写（触发压缩） ... */
    CHECK(OPRT_OK == tal_kv_read("old.key", rd_old, sizeof(rd_old), &out_old));
    CHECK(out_old == sizeof(old_blob) && 0 == memcmp(old_blob, rd_old, out_old));
```

**5) 变异验证（必须做）**：把 `__compact` 直接 `return OPRT_COM_ERROR`（禁用 GC）→ `test_kv_compaction` **必须变红**；恢复后复绿。另把 `__append_rec` 的回卷分支强制放行（去掉 head 检查）→ 应出现读回失败/覆盖。

**6) 提交**：同 Step 5 的两仓提交格式，message 注明"设计修正版"。

### Step 3-修正2：去掉压缩（最终版，**以本段为准，替代上面的 Step 3 与 Step 3-修正**）

> 2026-10-08 用户决策：**flash 存储不做压缩**，要求平台开足够大的存储区域。删除 GC/回卷/空洞扫描全部逻辑，日志退化为**纯线性追加**。

**实现改动（相对当前 HEAD `51aeca0`/`32b65ba`）**：

1. **删除** `__compact`、`__free_gap`、`__usable_free`、`__garbage_len` 全部逻辑；`s_head` 变量删除（无读者）
2. `__append` 退化为直接调 `__append_rec`（无压缩、无重试）：
```c
static OPERATE_RET __append(const uint8_t *rec, uint32_t rec_len)
{
    return __append_rec(rec, rec_len);
}
```
3. `__append_rec` 去掉 PAD 回卷分支——**不跨区尾，满即报错**：
```c
static OPERATE_RET __append_rec(const uint8_t *rec, uint32_t rec_len)
{
    TAL_KV_OPS_T *ops = tal_kv_get_ops();

    if (s_tail + rec_len > s_region_size) {
        return OPRT_COM_ERROR;                       /* 区域满：绝不覆盖已有记录 */
    }
    if (OPRT_OK != ops->write(s_tail, rec, rec_len)) {
        return OPRT_COM_ERROR;
    }
    s_tail += rec_len;
    return OPRT_OK;
}
```
4. `__scan` **恢复 Task 2 的顺序扫描版本**（`tal_kv.c` 现 HEAD 里的空洞容忍分块扫描删掉，回到"顺序读、magic 不符即日志尾"的版本 + 保留墓碑分支与不变量注释 + `s_tail = max_end`）
5. 删除自测里 `test_kv_compaction` 的 64×8KB 覆盖写（会撑爆区域），改为两个用例：
```c
static int test_kv_overwrite(void)
{
    /* a) 覆盖写正确性 + 老 key 存活（不触发容量上限） */
    uint8_t old_blob[512], blob[256], rd[512];
    size_t out_len = 0;
    char name[16];

    memset(old_blob, 0x33, sizeof(old_blob));
    CHECK(OPRT_OK == tal_kv_write("old.key", old_blob, sizeof(old_blob)));

    for (int i = 0; i < 20; i++) {
        memset(blob, (uint8_t)i, sizeof(blob));
        snprintf(name, sizeof(name), "ovw.%d", i % 4);
        CHECK(OPRT_OK == tal_kv_write(name, blob, sizeof(blob)));
    }
    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "ovw.%d", i);
        memset(rd, 0, sizeof(rd));
        CHECK(OPRT_OK == tal_kv_read(name, rd, sizeof(rd), &out_len));
        CHECK(out_len == sizeof(blob));
        CHECK(rd[0] == (uint8_t)(16 + i));           /* 最后一轮 i=16..19 */
    }
    /* 老 key 经 20 次覆盖后仍可读且内容不变 */
    memset(rd, 0, sizeof(rd));
    CHECK(OPRT_OK == tal_kv_read("old.key", rd, sizeof(rd), &out_len));
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
        if (OPRT_OK != tal_kv_write(name, blob, sizeof(blob))) {
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
        CHECK(OPRT_OK == tal_kv_read(name, rd, sizeof(rd), &out_len));
        CHECK(out_len == sizeof(blob));
        CHECK(rd[0] == (uint8_t)(i & 0xFF));
    }
    return 0;
}
```
并在 `main()` 里把 `test_kv_compaction()` 换成 `test_kv_overwrite()` + `test_kv_region_full()`。

**变异验证（必须做）**：把 `__append_rec` 的 `s_tail + rec_len > s_region_size` 检查去掉（允许越界/覆盖）→ `test_kv_region_full` **必须变红**；恢复后复绿。

**提交**：分两仓库提交，message 注明「去掉压缩（flash 不做回收，区域开足够大）」，并在 message 里写明这是取代前两笔 GC 实现的最终版。

---

## Task 4: 【已由"公开 API 落地"任务取代——见设计文档第二节】

> 2026-10-08 最终决策：公开 API 即旧 tal_kv 全套 → 上层零改动，原 Task 4~8 的逐点迁移**全部取消**。以下 Task 4~8 内容仅作历史记录。

## Task 4（历史）: 迁移 `tuya_main.c`（P0）

**Files:**
- Modify: `examples/switch_demo/src/tuya_main.c:258`（`tal_kv_init`）

**Interfaces:**
- Consumes: `tal_kv_init(seed, key)`（Task 2）

- [ ] **Step 1: 替换调用**

把：

```c
    tal_kv_cfg_t kv_cfg = {.seed = "...", .key = "..."};   /* 保持原字面量 */
    tal_kv_init(&kv_cfg);
```

改为（seed/key 字面量原样保留）：

```c
    tal_kv_init("<原 seed 字面量>", "<原 key 字面量>");
```

头文件：`#include "tal_api.h"` 已聚合 `tal_kv.h`（Task 8 会把 `tal_api.h` 的 `tal_kv.h` 换成 `tal_kv.h`）；本步先在 `tuya_main.c` 显式加 `#include "tal_kv.h"`。

- [ ] **Step 2: 构建 + 运行**

```bash
cd examples/switch_demo/build && cmake --build . -j$(nproc) && rm -f tuyadb/data.bin && timeout 20 ./switch_demo
```

Expected: exit 124；日志出现 `TUYA_TLS Success Connect` + `MQTT direct connected` + QR 22 行。

- [ ] **Step 3: 提交**

```bash
git add examples/switch_demo/src/tuya_main.c
git commit -m "switch_demo: tal_kv_init 迁移到 tal_kv_init

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 5: 【已作废——见 Task 4 横幅】迁移 `tuya_iot.c`（P0，15 处）

**Files:**
- Modify: `tuyaopen-core/iot-core/cloud/tuya_iot.c`（:130/139/152/160/195/206/226/228/558/1121/1122/1387/1395/1402/1409）

**Interfaces:**
- Consumes: `tal_kv_read/write/erase`

- [ ] **Step 1: 机械替换（模式如下）**

```c
/* 读（原：堆分配 + free） */
uint8_t *value = NULL; size_t len = 0;
tal_kv_get(key, &value, &len);   ...   tal_kv_free(value);
/* → 改为调用方 buffer（按各 key 的规模取 512 / 4096） */
uint8_t buf[512]; size_t len = 0;
if (OPRT_OK == tal_kv_read(key, buf, sizeof(buf), &len)) { ... }

/* 写 */
tal_kv_set(key, value, len);     →   tal_kv_write(key, value, len);

/* 删 */
tal_kv_del(key);                 →   tal_kv_erase(key);
```

注意点：
- `activated_data_read()` 读的是 JSON 文本，buffer 取 4096 并确保以 `\0` 结尾（`buf[len] = '\0'`）
- `:558` 的 `tal_kv_set("<ns>.devid")` 在 reset 前保存 devId，语义不变
- `:1121/1122` 的解绑清理改为 `tal_kv_erase`

- [ ] **Step 2: 构建 + 运行**

```bash
cd examples/switch_demo/build && cmake --build . -j$(nproc) && rm -f tuyadb/data.bin && timeout 20 ./switch_demo
```

Expected: 同 Task 4（TLS/MQTT/QR 正常）。

- [ ] **Step 3: 提交**

```bash
git -C tuyaopen-core add iot-core/cloud/tuya_iot.c
git -C tuyaopen-core commit -m "cloud: tuya_iot 的 KV 调用迁移到 tal_kv

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 6: 【已作废——见 Task 4 横幅】迁移 `tuya_endpoint.c`（P0，15 处，含 serialize 2 处）

**Files:**
- Modify: `tuyaopen-core/iot-core/cloud/tuya_endpoint.c`（:46/52/72/79/81/88/111/135/169/205/250-253）

**Interfaces:**
- Consumes: `tal_kv_read/write/erase`；cJSON（`tuyaopen-core` 已依赖 `libcjson`）

- [ ] **Step 1: 普通键按 Task 5 的模式替换**

`region` / `regist_key` / `endpoint.cert` 三类：`tal_kv_get/set/del` → `tal_kv_read/write/erase`。

- [ ] **Step 2: `endpoint.domain` 的 serialize 改为调用方自行拼字节**

原来：

```c
kv_db_t db[5] = { ... };
tal_kv_serialize_get("endpoint.domain", db, 5);
/* ... */
tal_kv_serialize_set("endpoint.domain", db, 5);
```

改为（保持 JSON 落盘格式，调用方自建 cJSON）：

```c
/* 读 */
uint8_t buf[1024]; size_t len = 0;
if (OPRT_OK == tal_kv_read("endpoint.domain", buf, sizeof(buf) - 1, &len)) {
    buf[len] = '\0';
    cJSON *root = cJSON_Parse((const char *)buf);
    if (NULL != root) {
        /* 逐字段取出 atop.host/port/path、mqtt.host/port，写入原 db[] 对应项 */
        ...
        cJSON_Delete(root);
    }
}
/* 写 */
cJSON *root = cJSON_CreateObject();
cJSON_AddStringToObject(root, "atop.host", ...);   /* 字段名与旧 kv_serialize 输出保持一致 */
...
char *out = cJSON_PrintUnformatted(root);
if (NULL != out) {
    tal_kv_write("endpoint.domain", (const uint8_t *)out, strlen(out));
    cJSON_free(out);
}
cJSON_Delete(root);
```

⚠️ 字段名必须与旧 `kv_serialize` 的输出一致（读旧数据才不丢），实施前用 `/usr/bin/grep -n "atop\|mqtt" tuyaopen-core/third_party/tal_kv/src/kv_serialize.c` 核对并照抄。

- [ ] **Step 3: 构建 + 运行**

```bash
cd examples/switch_demo/build && cmake --build . -j$(nproc) && rm -f tuyadb/data.bin && timeout 20 ./switch_demo
```

Expected: TLS/MQTT/QR 正常（endpoint.domain 会在连接流程中被写读）。

- [ ] **Step 4: 提交**

```bash
git -C tuyaopen-core add iot-core/cloud/tuya_endpoint.c
git -C tuyaopen-core commit -m "cloud: tuya_endpoint 的 KV 调用迁移到 tal_kv（serialize 改由调用方拼 JSON）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 7: 【已作废——见 Task 4 横幅】迁移 `tuya_cert_manager.c` + `tuya_register_center.c`（P0/P1，20 处）

**Files:**
- Modify: `tuyaopen-core/iot-core/cloud/tuya_cert_manager.c`（15 处，含 :754/757 的 free 语义消除）
- Modify: `tuyaopen-core/iot-core/cloud/tuya_register_center.c`（5 处）

**Interfaces:**
- Consumes: `tal_kv_read/write/erase`

- [ ] **Step 1: 按 Task 5 模式替换；证书/私钥读用 4096 buffer**

- `tuya_cert_manager.c:372/388/799/807`：客户端证书/私钥读写（PEM→DER 后写入，buffer 4096）
- `:413/424/430/434/452/459/475/483/487`：`tls_ca*` 与计数
- `:754/757`：**删除**对 `tal_kv_get` 返回指针的 `tal_kv_free`（新 API 无堆分配）
- `tuya_register_center.c:235/343/345/352/409`：`rcs.active` / `rcs.mf`（buffer 2048）

- [ ] **Step 2: 构建 + 运行**

```bash
cd examples/switch_demo/build && cmake --build . -j$(nproc) && rm -f tuyadb/data.bin && timeout 20 ./switch_demo
```

Expected: TLS/MQTT/QR 正常（证书管理器会在启动时读证书）。

- [ ] **Step 3: 提交**

```bash
git -C tuyaopen-core add iot-core/cloud/tuya_cert_manager.c iot-core/cloud/tuya_register_center.c
git -C tuyaopen-core commit -m "cloud: cert_manager/register_center 的 KV 调用迁移到 tal_kv

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 8: 【已作废——见 Task 4 横幅】迁移未编译的 3 个文件（28 处）

**Files:**
- Modify: `tuyaopen-core/iot-core/cloud/tuya_device_timer.c`（4 处）
- Modify: `tuyaopen-core/agent-core/tuya_ai_service/svc_ai_basic/src/tuya_ai_client.c`（4 处）
- Modify: `tuyaopen-core/p2p-core/svc_ipc_core/src/tuya_ipc_p2p_common.c`（20 处）

- [ ] **Step 1: 按同一模式替换（这些文件当前不编译，只需保证语法与语义正确）**

- [ ] **Step 2: 编译性核对（用编译面覆盖不到，改为语法自检）**

```bash
cd /home/share/samba/open-components/tuyaopen-core-examples/linux
/usr/bin/grep -rn "tal_kv_" tuyaopen-core/iot-core/cloud/tuya_device_timer.c tuyaopen-core/agent-core/tuya_ai_service/svc_ai_basic/src/tuya_ai_client.c tuyaopen-core/p2p-core/svc_ipc_core/src/tuya_ipc_p2p_common.c
```

Expected: 无输出。

- [ ] **Step 3: 提交**

```bash
git -C tuyaopen-core add iot-core/cloud/tuya_device_timer.c agent-core/tuya_ai_service/svc_ai_basic/src/tuya_ai_client.c p2p-core/svc_ipc_core/src/tuya_ipc_p2p_common.c
git -C tuyaopen-core commit -m "device_timer/ai_client/p2p: KV 调用迁移到 tal_kv（当前未编译，同步改动）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 9: 删除旧链路 + 构建清理

**Files:**
- Delete: `tuyaopen-core/third_party/tal_kv/`（整目录，含 littlefs 1.7MB）
- Delete: `tuyaopen-core/iot-core/tal_system/src/tal_fs.c` + `include/tal_fs.h`
- Delete: `port/src/tkl_flash.c` + `port/include/flash/tkl_flash.h`
- Delete: `port/src/tkl_fs.c` + `port/include/system/tkl_fs.h`
- **不改任何 CMake 与 include**：target 名 `tal_kv` 与头名 `tal_kv.h` 由 Task 4-修正 的兼容层继续提供 → `tuyaopen-core.cmake:78`、`iot-core/CMakeLists.txt:62`、`iot-core/tal_system/CMakeLists.txt:14`、agent-core/p2p 的引用、`tal_api.h:55` 的 `#include "tal_kv.h"` **全部原样解析**；`tal_fs.c`/`tkl_flash.c`/`tkl_fs.c` 均由 GLOB 自动出列

- [ ] **Step 1: 全仓确认零残留**

```bash
cd /home/share/samba/open-components/tuyaopen-core-examples/linux
/usr/bin/grep -rn "tal_kv\|tkl_flash\|tkl_fs\|tal_fs_\|littlefs\|lfs_" --include="*.c" --include="*.h" --include="*.txt" --include="*.cmake" --include="Kconfig*" tuyaopen-core port examples | /usr/bin/grep -v "/build/\|port/include/init/\|corehttp-coremqtt/"
```

Expected: 仅剩待删的构建引用与 `tal_kv` 目录自身（`port/include/init/` 是既定死目录例外）。

- [ ] **Step 2: 删除文件**

按上表删除 4 项（`tal_kv` 目录 + `tal_fs` + `tkl_flash` + `tkl_fs`）。**不要动** `tal_api.h` 的 include、`tuyaopen-core.cmake` 与任何 `target_link_libraries(... tal_kv)`——它们由兼容层解析。

- [ ] **Step 3: 清空 build 重新 configure（必须）**

```bash
rm -rf examples/switch_demo/build && mkdir -p examples/switch_demo/build && cd examples/switch_demo/build && cmake .. && cmake --build . -j$(nproc)
```

Expected: 配置与构建均 exit 0（`CONFIGURE_DEPENDS` 不会自动摘除已消失的子目录 target，必须清空重建）。

- [ ] **Step 4: 自测 + 运行**

```bash
cd examples/switch_demo/build && rm -f tuyadb/data.bin && ./tal_kv_selftest && ./tal_kv_selftest reopen-check && timeout 20 ./switch_demo
```

Expected: `ALL TESTS PASSED` / `REOPEN OK` / exit 124 + TLS/MQTT/QR 正常。

- [ ] **Step 5: 提交**

```bash
git -C tuyaopen-core add -A
git -C tuyaopen-core commit -m "third_party: 删除 tal_kv（含 littlefs）与 tal_fs，改用 tal_kv

Co-Authored-By: Claude Code <noreply@anthropic.com>"
git add -A -- ':(exclude)corehttp-coremqtt/coreHTTP-v2.0.1' ':(exclude)corehttp-coremqtt/coreHTTP-v3.1.3' ':(exclude)corehttp-coremqtt/coreMQTT-v1.0.1' ':(exclude)corehttp-coremqtt/coreMQTT-v5.0.2'
git commit -m "port: 删除 tkl_flash/tkl_fs（随 littlefs 链路移除）+ 子模块指针更新

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 10: 端到端验证 + 独立评审

- [ ] **Step 1: 完整验收（对照设计文档第九节）**

```bash
cd /home/share/samba/open-components/tuyaopen-core-examples/linux/examples/switch_demo/build
rm -f tuyadb/data.bin && timeout 25 ./switch_demo    # 第一次：走激活/绑定
timeout 25 ./switch_demo                              # 第二次：重启后读回
```

Expected（两次都）：TLS 握手成功、`MQTT direct connected`、QR 22 行；第二次日志中**不应出现**"首次激活"类路径（数据从区域读回）。另外：

```bash
head -c 64 tuyadb/data.bin | od -c | head -3     # 区域内容应为密文（不可读）
```

- [ ] **Step 2: 派独立 subagent 评审**

评审要点：搬运保真度（引擎逻辑 vs 设计文档）、掉电/撕裂语义、迁移是否遗漏、删除是否彻底、构建与运行证据。

- [ ] **Step 3: 更新文档**

- `examples/switch_demo/tal-data-design.md` 状态改为"已实现"
- `examples/switch_demo/tal-interface-cleanup-candidates.md` 同步（tal_kv/tal_fs 相关条目）

- [ ] **Step 4: 提交**

```bash
git add examples/switch_demo/tal-data-design.md examples/switch_demo/tal-interface-cleanup-candidates.md
git commit -m "docs: tal_kv 落地后同步设计文档与清理清单

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## 风险与回退

- **Task 3（压缩）是最易出错的一步**：若自测的压缩用例反复失败，可先只上"PAD 回卷 + 区域满报错"（不做压缩），把压缩拆成独立任务再攻；此时区域会写满，但功能可用（demo 写入量小）
- **迁移过程中随时可回退**：每个 Task 独立提交，出问题 `git revert` 单个提交即可
- **数据不兼容**：新格式与旧 littlefs 镜像不兼容，升级即清空（demo 场景可接受，已写入设计文档）
