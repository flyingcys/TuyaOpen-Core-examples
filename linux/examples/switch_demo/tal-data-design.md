# tal_data 设计（替换 tal_kv）

> 设计日期：2026-10-08（含 4K 扇区 flash 场景的修订，最终采用日志式布局）
> 状态：**设计已确认，待实现**
> 目标：删除 `tuyaopen-core/third_party/tal_kv`（含 littlefs），用全新的 `tal_data` 四接口替代；tkl 侧用 ops 卡注册制放在 port 下，**port 只提供一块存储区域 + 区域读写擦**（区域语义），KV 布局与加解密由 TAL 统一实现。

## 一、背景与设计取舍

当前存储链：`tal_kv`（KV + AES + serialize）→ `tal_fs` → `littlefs` → `tkl_flash`（256MB 模拟 flash 文件）。

**取舍一：为什么是"区域语义"而不是"每 name 一个文件"**
裸 flash（4K 扇区）的物理约束：写只能 1→0、擦只能整扇区、擦写寿命有限。若让 port 做"每 name 一个文件"，port 必须实现空间分配 + 索引 + 碎片回收 + 磨损均衡——等于把刚删掉的 littlefs 复杂度请回来，且每个平台各写一遍。故改为 **port 只给"一块区域 + 读写擦"，TAL 统一实现 flash 友好布局**。

**取舍二：为什么是"日志式"而不是"索引区 + 数据区"**
索引式布局每次 `write` 都要重写整个索引副本（4KB）→ 在 flash 上等于**每次写触发一次扇区擦除**：频繁写的 key（如 DP 版本缓存）会在数月内磨穿索引扇区；日志式只追加记录、擦除仅在压缩时发生，磨损低若干数量级，且掉电一致性天然（追加 + 记录级 CRC）。代价是需要压缩逻辑（见四、五节）。

```
tal_data (TAL：4 个公开 API + 日志式 KV 引擎 + AES 加解密 + ops 分发)
  └─ ops 卡（tal_data_ops.h / tal_data_get_ops()，区域语义：init/read/write/erase @offset）
       └─ tkl_data (port：一块区域 + 区域读写擦，Linux 实现 = 一个区域文件)
```

## 二、公开 API（`tal_data.h`，对上 4 个接口）

```c
OPERATE_RET tal_data_init(const char *seed, const char *key);
OPERATE_RET tal_data_read(const char *name, uint8_t *buf, size_t buf_size, size_t *out_len);
OPERATE_RET tal_data_write(const char *name, const uint8_t *buf, size_t len);
OPERATE_RET tal_data_erase(const char *name);
```

- `init(seed, key)`：准备区域 + 全区域扫描建索引 + 派生 AES 密钥；重复调用幂等
- `read`：调用方提供 buffer 与尺寸，`*out_len` 回填实际长度；**无 free 接口**
- `write`：同名覆盖（追加新记录，旧记录自然失效）
- `erase`：追加墓碑记录；name 不存在视为成功（幂等）

## 三、ops 卡契约（`tal_data_ops.h`，区域语义）

```c
typedef struct {
    uint32_t size;               /* 区域总字节数 */
    uint32_t erase_granularity;  /* 擦除粒度（flash=4096；文件实现可同用 4096） */
} TAL_DATA_REGION_T;

typedef struct {
    /* 准备区域并回填 region 信息（建区域文件 / 挂 flash 分区） */
    OPERATE_RET (*init)(TAL_DATA_REGION_T *region);
    OPERATE_RET (*read)(uint32_t offset, uint8_t *buf, uint32_t len);
    OPERATE_RET (*write)(uint32_t offset, const uint8_t *buf, uint32_t len);
    /* 擦除 [offset, offset+len)，对齐到 erase_granularity */
    OPERATE_RET (*erase)(uint32_t offset, uint32_t len);
} TAL_DATA_OPS_T;

TAL_DATA_OPS_T *tal_data_get_ops(void);
```

与 `tal_api_ops.h` / `tal_ota_ops.h` 同构：契约头在 core、实例在 port（`tal_data_ops_platform`）、强引用（省略 `tkl_data.c` 会链接失败）。

**移植契约与实现量**：新平台只需实现 4 个区域原语——

| 平台 | 实现方式 | 代码量 |
|---|---|---|
| Linux（本仓库） | 一个区域文件 `./tuyadb/data.bin`（fopen/fseek/fread/fwrite；擦除 = 该范围写 0xFF） | ~40 行 |
| ESP32 / STM32（flash 分区） | 直接映射分区 base/size；read/write 调 flash 读写；erase 调扇区擦除 | ~20 行 |
| EEPROM / FRAM | 线性地址空间；无擦除概念（erase = 写 0xFF） | ~20 行 |

**复杂度分工（一次实现，全平台共享）**：

| 层 | 职责 | 代码量 |
|---|---|---|
| 上层（iot-core / 应用） | 只调 `tal_data_read/write/erase(name, …)`，不关心存储 | 迁移 46 处（机械替换） |
| TAL（core） | ① K-V 布局引擎（日志式：追加 + 墓碑 + 压缩 + 掉电保护）② AES 加解密 ③ ops 分发 | ~350 行 |
| port（每平台） | 一块区域 + 4 个原语 | ~20-40 行 |

**考虑过但未采用的 port 接口**：
- **port 做 name→bytes**（每 name 一个文件）：Linux 最直观，但 flash 平台做不了（要么自带 FS、要么自己写 K-V），与"port 尽量简单"冲突
- **port 只给定长扇区**（read/write/erase_sector）：port 更简，但 TAL 需自己做扇区内分配——复杂度转移且更死板，小数据写放大严重

## 四、区域布局（TAL 定义，与平台无关）—— 日志式

区域是一段**环形日志**：记录从低偏移向高偏移追加，写满后压缩回收。

```
偏移 0                                                          size
┌──────────────────────────────┬────────────────┬───────────────┐
│ 已写区（有效记录 + 墓碑 + 失效旧记录）│  空闲（0xFF）   │
└──────────────────────────────┴────────────────┴───────────────┘
       ↑ head（最老记录）              ↑ tail（追加点）
```

**记录格式（变长，不跨区域末尾——放不下时写 PAD 记录后回卷到 0）**：

```
┌───────┬───────┬──────┬─────────┬───────┬──────┬───────┐
│ magic │ state │ seq  │ namelen │ name  │ ...  │ crc32 │
│  2B   │  1B   │ 4B   │   1B    │ ≤63B  │      │  4B   │
└───────┴───────┴──────┴─────────┴───────┴──────┴───────┘
（name 之后为 datalen(4B) + data(密文)，crc32 覆盖 magic 之后到 data 结束）
state：0x01=有效 0x00=墓碑 0x02=PAD（回卷填充）
```

- **seq**：单调递增序号（每次 write/erase +1），用于解决"同名多条记录谁最新"与压缩期间新旧副本并存的问题
- **数据区内容为 AES 密文**（加密在 TAL，port 只见字节）

## 五、数据流

| 调用 | TAL 侧 | port 侧 |
|---|---|---|
| `init(seed,key)` | `ops->init(&region)` → **全区域顺序扫描**：逐条读记录、校验 magic/CRC、按 seq 最大者建 RAM 索引（`name → {offset, len, seq}`）、记录 max_seq 与 tail → SHA256 派生 AES key/IV | 建区域文件（0xFF 预填）并回填 size |
| `write(name,buf,len)` | AES-128-CBC + PKCS7 加密 → 组装记录（seq = max_seq+1）→ **追加到 tail** → 更新 RAM 索引 | `write(tail, rec, rec_len)` |
| `read(name,buf,buf_size,&out_len)` | 查 RAM 索引 → `ops->read` 取密文（`tal_malloc` 临时缓冲）→ 校验记录 CRC → 解密 → 校验 `buf_size` → memcpy → free | `read(offset,…)` |
| `erase(name)` | 组装墓碑记录（同 seq 规则）→ 追加 → 从 RAM 索引移除 | `write(tail, rec, rec_len)` |

**无压缩（最终决策，2026-10-08）**：

经三轮实测迭代（原设计"写不下才压"、修正版"环形空闲间隙 + 主动阈值压缩 + 空洞容忍扫描"）共暴露 8 处缺陷，用户决策：**flash 存储不做压缩，要求平台开足够大的存储区域**。

- 区域 = **纯线性追加日志**：`write`/`erase` 只追加，**从不回卷、从不压缩、从不擦除**
- `init`：**顺序扫描**（无空洞，遇非法记录即日志尾）
- 区域满 → `OPRT_COM_ERROR`，**绝不覆盖已有记录**
- **已知限制**：覆盖写与删除都占用空间且不回收 → **区域必须按平台写入量开得足够大**（本仓库默认 256KB，demo 的写入量远不会写满）；将来若需长寿命循环写，再单独立项引入压缩（届时需重新设计并评审）

## 六、错误码与约束

| 情况 | 返回 |
|---|---|
| 成功 | `OPRT_OK` |
| `name` 为 NULL/空、长度 > 63、含 `[A-Za-z0-9._-]` 之外字符 | `OPRT_INVALID_PARM` |
| `write` 的 `len` 为 0 或 > 16384（宏可配） | `OPRT_INVALID_PARM` |
| `read` 的 `out_len` 为 NULL | `OPRT_INVALID_PARM` |
| `read` 的 `buf` 为 NULL | 合法：仅回填 `*out_len`（探测尺寸），返回 `OPRT_OK` |
| `read` 时 name 不存在、或记录 CRC 校验失败（撕裂写） | `OPRT_NOT_FOUND` |
| `read` 时明文长度 > `buf_size` | `OPRT_INVALID_PARM`，`*out_len` 回填真实长度 |
| 区域满（压缩也无法腾出空间） | `OPRT_COM_ERROR` |
| 加解密失败 / port IO 失败 | `OPRT_COM_ERROR` / 透传 |

- name 字符集与现有 key 完全兼容（`region`、`endpoint.cert`、`<uuid>.devid`(42 字符)、`tls_ca%d` 等均合法）
- 单条记录上限 16KB（含头），一条记录必然能放入 256KB 区域

## 七、删除清单

| 删除项 | 理由 |
|---|---|
| `tuyaopen-core/third_party/tal_kv/` 整目录 | 被 tal_data 替代（含 littlefs 1.7MB） |
| `iot-core/tal_system/src/tal_fs.c` + `include/tal_fs.h` | 全仓唯一消费者是 tal_kv |
| `port/src/tkl_flash.c` + `port/include/flash/tkl_flash.h` | 唯一消费者是 littlefs 块设备 |
| `port/src/tkl_fs.c` + `port/include/system/tkl_fs.h` | 消费者是 tal_fs + tkl_flash（均删）+ 未编译的 p2p `local_store`（将来启用需补） |
| `tal_api.h:55` 的 `#include "tal_kv.h"` | 否则所有 tal_api.h 使用者断头 |
| 构建引用 6 处 | `third_party/CMakeLists.txt:10`、`third_party/Kconfig:7`、`tuyaopen-core.cmake:78`、`iot-core/CMakeLists.txt:62`、`iot-core/tal_system/CMakeLists.txt:14`、`agent-core`/`p2p-core` 两处（未编译但同步改） |

删后需**清空 build 目录重新 configure**（`CONFIGURE_DEPENDS` 不会摘除已消失的子目录 target）。

## 八、迁移清单（78 个调用点）

| 优先级 | 文件 | 处数 | 说明 |
|---|---|---|---|
| **P0** | `examples/switch_demo/src/tuya_main.c` | 1 | `tal_kv_init(&cfg)` → `tal_data_init(seed, key)`（字面量沿用） |
| **P0** | `iot-core/cloud/tuya_iot.c` | 15 | schema/激活数据/devid 的读写删 |
| **P0** | `iot-core/cloud/tuya_endpoint.c` | 15 | region/regist_key/endpoint.cert；**serialize 2 处改为调用方自行 cJSON 拼字节** |
| **P0** | `iot-core/cloud/tuya_cert_manager.c` | 15 | 客户端证书/私钥、tls_ca*；注意 `:754/757` 释放的是 KV 分配的指针，改后语义消失 |
| **P1** | `iot-core/cloud/tuya_register_center.c` | 5 | rcs.active / rcs.mf |
| **P3** | `tuya_device_timer.c` / `tuya_ai_client.c` / `tuya_ipc_p2p_common.c` | 28 | 当前未编译，同步改以免将来断 |

调用范式转换：
- `tal_kv_get(k,&v,&len)` + `tal_kv_free(v)` → `tal_data_read(k, buf, sizeof(buf), &len)`（20 处需按各自数据规模给出 buffer）
- `tal_kv_set(k,v,len)` → `tal_data_write(k,v,len)`（机械替换）
- `tal_kv_del(k)` → `tal_data_erase(k)`（机械替换）

## 九、验收标准

1. 全仓零 `tal_kv` / `littlefs` / `tal_fs` / `tkl_flash` / `tkl_fs` 残留（编译面）
2. 构建通过；`switch_demo` 运行正常：激活/绑定流程走通（TLS 握手 + MQTT + QR）
3. 区域文件 `./tuyadb/data.bin` 生成；内容为密文记录（不可读）
4. **重启后数据可读**：跑一次绑定 → 杀掉 → 再跑，绑定状态保持
5. 边界用例：超长 name、不存在 name、buffer 过小、空数据、**撕裂写**（人为破坏某条记录后读取应返回 NOT_FOUND 而非脏数据）
6. **压缩正确性**：构造"区域接近写满"场景，触发压缩后所有 name 仍可读；**压缩中途掉电模拟**（人为截断新副本）后重启，旧数据仍可读

## 十、风险与限制

1. **init 扫描开销**：全区域顺序扫描（默认 256KB）——Linux 微秒级；SPI flash 约几十 ms，仅在启动时发生
2. **压缩是必需路径**：触发条件是"追加空间不足"；压缩本身需要 `空闲 ≥ 有效数据量 + 新记录`，否则报区域满。若某场景长期"有效数据接近区域容量"，需增大区域（宏可配）
3. **磨损**：追加式天然分散；压缩时才发生擦除（搬移后的旧区）。相比索引式的"每次写一次擦"，磨损低若干数量级
4. **密钥仍由应用硬编码**（与现状一致）；将来可改为安全存储提供
5. `tal-interface-cleanup-candidates.md` 中关于 tal_kv/tal_fs 的"保留"条目需同步更新
6. **两个加密 API 的实现地雷（Task 2 实测踩到）**：
   - mbedtls CBC **会就地改写传入的 IV**——实现必须每次调用前把全局 IV 拷到局部副本再传（旧 `tal_kv` 正是这样规避），否则全局 IV 被污染后**每次解密首 16 字节全错**，属静默数据损坏
   - `tal_aes128_cbc_decode` 返回的是**密文长度**，需再用 `tal_aes_get_actual_length()` 按 PKCS7 求实际明文长度
