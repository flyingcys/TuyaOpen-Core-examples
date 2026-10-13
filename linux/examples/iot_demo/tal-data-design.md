# tal_kv 设计（替换 tal_kv）

> ⚠️ **本方案已于 2026-10-08 作废**，被下方"新方案"取代。作废原因：线性追加日志的四条已知限制
> （区域写满即废、尾部撕裂不兜底、中段损坏截断其后记录、扩容需按写入总量评估）对产品不可接受。
> 下方"历史方案"内容仅作记录，勿作为实现依据——照其验证步骤跑会失败。

## 新方案（2026-10-08 定稿，已实现并通过评审）

**架构**：core 只留 4 接口透传，**实现全在 port**（iot-core 零 KV 逻辑）。

| 层 | 内容 |
|---|---|
| core | `tal_kv.h`（`init/set/get/del` 4 接口 + `tal_kv_cfg_t`/`TAL_LV_KEY_LEN`）、`tal_kv_ops.h`（`TAL_KV_OPS_T` 四函数指针）、`tal_kv.c`（`TAL_KV_EXEC_OP` 透传，约 60 行） |
| port（Linux） | `port/src/port_kv.c`：旧 littlefs 实现改造（`__kv_init/set/get/del` + `tal_kv_ops_platform`），并**内联 flash 模拟**（原 tkl_flash.c：`./tuyadb/tuyadb` 文件 + UF 分区，标准 C IO，不依赖 tkl_fs）；配套恢复 `port/littlefs/`、`port/include/system/lfs_config.h`（源自子模块 `8fa1d6a^`） |
| port（其他平台） | 换平台 = 换一个 `port_kv.c`（ESP32 可映射 NVS），core 零改动 |

**接口收敛**（旧 7 → 新 4）：

- 删 `tal_kv_free` → 调用者改用 `tal_free`（`tal_kv_get` 返回堆内存、含结尾 `'\0'`；失败时 `*value=NULL`、`*length=0`）
- 删 `tal_kv_serialize_set/get` 与 `kv_db_t`/`kv_tp_t`/`KV_*` 类型 → `tuya_endpoint.c` 的 domain 改为 5 个字段 key（`atop.host`/`atop.port`/`atop.path`/`mqtt.host`/`mqtt.port`）逐项存取
- `del` 幂等（键不存在返回 OK）；`init` 幂等（重复 init 只刷新密钥派生，不重挂介质）

**分工原则**：存储介质、记录格式、加密、序列化**全部是 port 内部细节**；core 不做任何 KV 逻辑。
Linux 实现保留旧行为：AES-128-CBC（seed/key 经 SHA256 派生，IV=seed）+ mutex 线程安全 + littlefs（自带磨损均衡与掉电保护）。

**介质契约（port 实现相关）**：Linux 文件实现的 `erase` 为 **no-op**——文件可覆盖写，靠预填 0xFF + littlefs 的 CRC 提交语义维持；**真 flash 平台必须实现块擦除命令**（写 0xFF 对 NOR 是 no-op），并把 `port_kv.c` 的 `__flash_*` 替换为实际分区访问。`FLASH_FILE_PATH` 与原先 `port/src/tkl_wifi/linux_wifi.h` 的覆盖点协同（该文件已随 tkl_wifi 目录删除）；构建可 `-D` 注入统一改目录。

**提交**：子模块 `ba58b24`…`43a79bc`、外层 `9961ff8` 起（2026-10-08；后续增量见 git log）。

**验证**：干净目录单独构建测试目标 exit 0；全量构建 exit 0；自测（10 用例黑盒）新旧存储路径各 ALL TESTS PASSED（含变异反证）；e2e exit 124 + TLS×2 + MQTT + QR 22 行；压力探针 160KB 写入（>96KB 分区）触发 littlefs 块回收全验证；ASan + UBSan 自测全绿。

**评审**：内部独立评审（opus，含 gdb 崩溃路径取证与旧镜像升级实测）结论"可合入、无 Critical"，3 Important + 9 Minor 全部处置或记录在案；对端会话 `linux-67` 两批 PK——它独立复现了字节兼容（真实 seed/key 逐 key 解密）、ASan/UBSan、P2P+local_store 配置构建——最终明确 **"第二版认可合入，无 BLOCKER"**，全部发现闭环。

**升级兼容性（实测）**：恢复的 littlefs + AES 与旧固件**字节兼容**——旧镜像（旧 7 接口固件写入的 `examples/iot_demo/tuyadb/tuyadb`）在新代码下正常挂载，`region`/`regist_key`/`endpoint.cert`/`endpoint.domain` 全部解密出正确明文；旧的单 key `endpoint.domain` 不再被读，5 个新字段 key 缺失返回 NOT_FOUND → 触发 `STATE_ENDPOINT_UPDATE` → iDNS 重取 → 写回 5 key，自愈。

- **复现方法**：`git worktree add /tmp/legacy <改动前提交>` → 在 worktree 中构建并运行一次 `iot_demo`（或自测），生成的 `examples/iot_demo/tuyadb/tuyadb` 即 legacy 镜像 → 拷回当前工作区，用新构建产物挂载并读真实业务 key 验证（`.legacy` 镜像本身不入库，105MB）。
- **边界**：若旧区域 `lfs_mount` 失败（superblock 损坏等），`kv_storage_init` 会 `lfs_format` 重建，**旧数据不可恢复**——兼容的前提是旧区域结构完好。

---

## 历史方案（已作废）

> 设计日期：2026-10-08（含 4K 扇区 flash 场景的修订；最终形态：**线性追加日志、不压缩**）
> 状态：**已作废**（曾被实现并通过评审，后被新方案整体替换；其产物 tal_kv.c 引擎已删除）
> 目标：删除 `tuyaopen-core/third_party/tal_kv`（含 littlefs），由自研 `tal_kv` 引擎替代；对外**保持旧 tal_kv API 逐字兼容**（上层 78 个调用点零改动）；port 侧用 ops 卡注册制放在 port 下，**只提供一块存储区域 + 区域读写擦**，KV 布局与加解密由 TAL 统一实现。

## 一、背景与设计取舍

当前存储链：`tal_kv`（KV + AES + serialize）→ `tal_fs` → `littlefs` → `tkl_flash`（256MB 模拟 flash 文件）。

**取舍一：为什么是"区域语义"而不是"每 name 一个文件"**
裸 flash（4K 扇区）的物理约束：写只能 1→0、擦只能整扇区、擦写寿命有限。若让 port 做"每 name 一个文件"，port 必须实现空间分配 + 索引 + 碎片回收 + 磨损均衡——等于把刚删掉的 littlefs 复杂度请回来，且每个平台各写一遍。故改为 **port 只给"一块区域 + 读写擦"，TAL 统一实现 flash 友好布局**。

**取舍二：为什么是"日志式"而不是"索引区 + 数据区"**
索引式布局每次 `write` 都要重写整个索引副本（4KB）→ 在 flash 上等于**每次写触发一次扇区擦除**：频繁写的 key（如 DP 版本缓存）会在数月内磨穿索引扇区；线性追加只写不擦、磨损天然分散，且掉电一致性简单（追加 + 记录级 CRC）。**最终取舍：不做压缩**（用户决策，三轮实测迭代后定稿）——代价是区域写满即报错，**必须按平台写入总量开得足够大**（见四、五节）。

```
tal_kv (TAL：4 个公开 API + 日志式 KV 引擎 + AES 加解密 + ops 分发)
  └─ ops 卡（tal_kv_ops.h / tal_kv_get_ops()，区域语义：init/read/write/erase @offset）
       └─ tkl_kv (port：一块区域 + 区域读写擦，Linux 实现 = 一个区域文件)
```

## 二、公开 API（`tal_kv.h`）—— 与旧 tal_kv 逐字兼容

**对外门面 = 旧 `tal_kv` 全套**（用户最终决策，2026-10-08）：签名与旧头**逐字一致**，故上层 78 个调用点**零改动**；新引擎作为内部 static 实现。

```c
int tal_kv_init(tal_kv_cfg_t *kv_cfg);                              /* cfg 提供 seed/key */
int tal_kv_set(const char *key, const uint8_t *value, size_t length);
int tal_kv_get(const char *key, uint8_t **value, size_t *length);   /* 分配返回，末尾补 '\0' */
int tal_kv_free(uint8_t *value);
int tal_kv_del(const char *key);
int tal_kv_serialize_set(const char *key, kv_db_t *db, size_t dbcnt);
int tal_kv_serialize_get(const char *key, kv_db_t *db, size_t dbcnt);
```

类型 `kv_tp_t` / `kv_db_t` / `tal_kv_cfg_t` / `TAL_LV_KEY_LEN` 同样逐字保留；`tal_kv_cmd`（CLI）与 `tal_lfs_get` 随旧链路删除。

**内部引擎（static，不对外）**：

```c
static OPERATE_RET __engine_init(const char *seed, const char *key);
static OPERATE_RET __engine_read(const char *name, uint8_t *buf, size_t buf_size, size_t *out_len);
static OPERATE_RET __engine_write(const char *name, const uint8_t *buf, size_t len);
static OPERATE_RET __engine_erase(const char *name);
```

- `tal_kv_get` 用"探测尺寸 → 分配 → 复读 → 补 `\0`"包住引擎的"调用方给 buffer"语义，调用方用 `tal_kv_free` 释放
- `tal_kv_serialize_*` 沿用旧 `kv_serialize.c` 语义（cJSON 实现；`db[i].len` 在 STRING 下是缓冲容量、在 RAW 下是编码前长度）
- **线程安全**：旧实现持 `lfs_mutex`，新实现在**公开 API 层**持一把 `s_kv_mutex`（`tal_kv_init` 创建；`tal_kv_get` 的探测+复读整段持锁；引擎 static 函数不加锁以避免重入）

## 三、ops 卡契约（`tal_kv_ops.h`，区域语义）

```c
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
```

与 `tal_api_ops.h` 同构：契约头在 core、实例在 port（`tal_kv_ops_platform`）、强引用（省略 `port_kv.c` 会链接失败）。

**移植契约与实现量**：新平台只需实现 4 个区域原语——

| 平台 | 实现方式 | 代码量 |
|---|---|---|
| Linux（本仓库） | 一个区域文件 `./tuyadb/data.bin`（fopen/fseek/fread/fwrite；擦除 = 该范围写 0xFF） | ~40 行 |
| ESP32 / STM32（flash 分区） | 直接映射分区 base/size；read/write 调 flash 读写；erase 调扇区擦除 | ~20 行 |
| EEPROM / FRAM | 线性地址空间；无擦除概念（erase = 写 0xFF） | ~20 行 |

> ⚠️ **"20-40 行"的边界（67 评审）**：仅对"**能跑起来的 demo**"成立（前提：区域独占且足够大）。**产品级可用**还需额外处理：块擦除与写入对齐/跨页拆分、磨损均衡，且必须接受"**写满即废**"——引擎只有"整区擦"一条归零路径（`__ops_init` 的空区兜底），没有任何部分回收路径；这是"字节级追加日志"与"块级擦除"语义的固有冲突，**port 层写多少行都补不上**。移植预算请按此评估。

**复杂度分工（一次实现，全平台共享）**：

| 层 | 职责 | 代码量 |
|---|---|---|
| 上层（iot-core / 应用） | 调旧 `tal_kv_*` API（逐字兼容），不关心存储 | **零改动** |
| TAL（core） | ① K-V 引擎（线性追加 + 墓碑 + seq + CRC 掉电保护）② AES 加解密 ③ 旧 API 门面 + 锁 ④ ops 分发 | ~1000 行 |
| port（每平台） | 一块区域 + 4 个原语 | ~20-40 行 |

**考虑过但未采用的 port 接口**：
- **port 做 name→bytes**（每 name 一个文件）：Linux 最直观，但 flash 平台做不了（要么自带 FS、要么自己写 K-V），与"port 尽量简单"冲突
- **port 只给定长扇区**（read/write/erase_sector）：port 更简，但 TAL 需自己做扇区内分配——复杂度转移且更死板，小数据写放大严重

## 四、区域布局（TAL 定义，与平台无关）—— 线性追加日志

区域是一段**只追加的线性日志**：记录从偏移 0 顺序追加，**从不回卷、从不压缩、从不擦除**；区域满即报错（见第五、六节）。

```
偏移 0                                                          size
┌──────────────────────────────────────────────┬───────────────┐
│ 已写区（有效记录 + 墓碑 + 被覆盖的旧记录）        │  空闲（0xFF）  │
└──────────────────────────────────────────────┴───────────────┘
                                                ↑ tail（追加点）
```

**记录格式（变长，不跨区域末尾——放不下即视为区域满）**：

```
┌───────┬───────┬──────┬─────────┬───────┬──────┬───────┐
│ magic │ state │ seq  │ namelen │ name  │ ...  │ crc32 │
│  2B   │  1B   │ 4B   │   1B    │ ≤63B  │      │  4B   │
└───────┴───────┴──────┴─────────┴───────┴──────┴───────┘
（name 之后为 datalen(4B) + data(密文)；crc32 覆盖记录起始（含 magic）到 data 结束）
state：0x01=有效 0x00=墓碑（0x02=PAD 为历史设计保留，当前不产生）
```

- **seq**：单调递增序号（每次 write/erase +1），用于"同名多条记录谁最新"——`init` 扫描时取每个 name 的 seq 最大者；墓碑按同规则移除同名索引项（否则 erase 会在重启后被旧记录"复活"）
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
| `name` 为 NULL/空、长度 > 63 | `OPRT_INVALID_PARM` |
| `write` 的 `len` 为 0 或 > 16384（宏可配） | `OPRT_INVALID_PARM` |
| `read` 的 `out_len` 为 NULL | `OPRT_INVALID_PARM` |
| `read` 的 `buf` 为 NULL | 合法：仅回填 `*out_len`（探测尺寸），返回 `OPRT_OK` |
| `read` 时 name 不存在、或记录 CRC 校验失败（撕裂写） | `OPRT_NOT_FOUND` |
| `read` 时明文长度 > `buf_size` | `OPRT_INVALID_PARM`，`*out_len` 回填真实长度 |
| 区域满（追加空间不足） | `OPRT_COM_ERROR`（**先判后写，绝不覆盖已有记录**） |
| 加解密失败 / port IO 失败 | `OPRT_COM_ERROR` / 透传 |

- name 字符集与现有 key 完全兼容（`region`、`endpoint.cert`、`<uuid>.devid`(42 字符)、`tls_ca%d` 等均合法）
- 单条记录上限 16KB（含头），一条记录必然能放入 256KB 区域
- **活 key 上限 64 条**（`TAL_KV_INDEX_MAX`）：索引满时新 key 写入报 `OPRT_COM_ERROR`；扫描期超出部分被丢弃（`PR_WARN`）。仓内运行时约 8 个 key、key 模板全集 34 个，不触发；外部 app 需注意
- **区域必须初始为擦除态（全 0xFF）**（port 契约要求）：若把引擎指向未擦除/曾被占用的区域（如老 littlefs 分区原地升级），真 NOR flash 上会因"只能 1→0"写入失败而内存索引无感，表现为**每次重启都丢数据**。Linux 文件实现无此问题（可覆盖写）
- **中段记录损坏的放大效应**：`init` 扫描遇非法记录即断为日志尾 → **其后的有效记录全部不可见**，且后续写入会就地覆盖它们。对只存激活信息的设备，一次位翻转可能导致整库失效需重新激活（缓解方案"跳过损坏继续扫描"会重新引入空洞容忍复杂度，需单独立项）

## 七、删除清单

| 删除项 | 理由 |
|---|---|
| `tuyaopen-core/third_party/tal_kv/` 整目录 | 被 tal_kv 替代（含 littlefs 1.7MB） |
| `iot-core/tal_system/src/tal_fs.c` + `include/tal_fs.h` | 全仓唯一消费者是 tal_kv |
| `port/src/tkl_flash.c` + `port/include/flash/tkl_flash.h` | 唯一消费者是 littlefs 块设备 |
| `port/src/tkl_fs.c` + `port/include/system/tkl_fs.h` | 消费者是 tal_fs + tkl_flash（均删）+ 未编译的 p2p `local_store`（将来启用需补） |
| `tal_api.h:55` 的 `#include "tal_kv.h"` | 否则所有 tal_api.h 使用者断头 |
| 构建引用 6 处 | `third_party/CMakeLists.txt:10`、`third_party/Kconfig:7`、`tuyaopen-core.cmake:78`、`iot-core/CMakeLists.txt:62`、`iot-core/tal_system/CMakeLists.txt:14`、`agent-core`/`p2p-core` 两处（未编译但同步改） |

删后需**清空 build 目录重新 configure**（`CONFIGURE_DEPENDS` 不会摘除已消失的子目录 target）。

## 八、迁移清单（**2026-10-08 决策：公开 API 即旧 tal_kv 全套 → 上层零改动，无需迁移**）

> 用户决策：`tal_kv` 模块的**对外门面保持旧 API**（头名/符号/签名逐字不变），新引擎作内部实现 → iot-core / examples / p2p 的 78 个调用点**一个都不用改**，也不需要独立的兼容层文件。
>
> ⚠️ **"零改动"的准确含义（67 评审指出，已采纳）**：上层调用点**源码**零改动（编译与链接层面完全兼容）；但新引擎在**运行期**引入了旧实现没有的失败路径，需一并知悉：
> 1. **区域写满后写入/删除永久失败**（旧实现有回收）——容量模型见 §六、水位告警已加
> 2. **`tal_kv_get` 返回缓冲从"密文长+1"收紧为"明文长+1"**：使 `tuya_iot.c:1393` 的 `memcmp(version_buffer, readbuf, version_len)` 在"上次存的串比本次短"时越界读（旧实现被 PKCS7 填充冗余掩盖）→ 列入独立"调用点加固"提交
> 3. **name 字符集校验**（旧实现无）：已按评审放宽为仅校验长度 1..63
>
> 下表原为逐点迁移清单，**已作废**，仅作历史记录。

| 优先级 | 文件 | 处数 | 说明 |
|---|---|---|---|
| **P0** | `examples/iot_demo/src/tuya_main.c` | 1 | `tal_kv_init(&cfg)` → `tal_kv_init(seed, key)`（字面量沿用） |
| **P0** | `iot-core/cloud/tuya_iot.c` | 15 | schema/激活数据/devid 的读写删 |
| **P0** | `iot-core/cloud/tuya_endpoint.c` | 15 | region/regist_key/endpoint.cert；**serialize 2 处改为调用方自行 cJSON 拼字节** |
| **P0** | `iot-core/cloud/tuya_cert_manager.c` | 15 | 客户端证书/私钥、tls_ca*；注意 `:754/757` 释放的是 KV 分配的指针，改后语义消失 |
| **P1** | `iot-core/cloud/tuya_register_center.c` | 5 | rcs.active / rcs.mf |
| **P3** | `tuya_device_timer.c` / `tuya_ai_client.c` / `tuya_ipc_p2p_common.c` | 28 | 当前未编译，同步改以免将来断 |

调用范式转换：
- `tal_kv_get(k,&v,&len)` + `tal_kv_free(v)` → `tal_kv_read(k, buf, sizeof(buf), &len)`（20 处需按各自数据规模给出 buffer）
- `tal_kv_set(k,v,len)` → `tal_kv_write(k,v,len)`（机械替换）
- `tal_kv_del(k)` → `tal_kv_erase(k)`（机械替换）

## 九、验收标准

1. 全仓零 `tal_kv` / `littlefs` / `tal_fs` / `tkl_flash` / `tkl_fs` 残留（编译面）
2. 构建通过；`iot_demo` 运行正常：激活/绑定流程走通（TLS 握手 + MQTT + QR）
3. 区域文件 `./tuyadb/data.bin` 生成；内容为密文记录（不可读）
4. **重启后数据可读**：跑一次绑定 → 杀掉 → 再跑，绑定状态保持
5. 边界用例：超长 name、不存在 name、buffer 过小、空数据、**撕裂写**（人为破坏某条记录后读取应返回 NOT_FOUND 而非脏数据）
6. **区域满安全**：持续写大数据直到返回 `OPRT_COM_ERROR`，**已写入的 key 必须全部仍可读**（不得覆盖/丢失）；并发用例（多线程各写各 key）验证锁的有效性

## 十、风险与限制

1. **init 扫描开销**：全区域顺序扫描（默认 256KB）——Linux 微秒级；SPI flash 约几十 ms，仅在启动时发生
2. **区域满**：追加空间不足即报 `OPRT_COM_ERROR`（**先判后写，绝不覆盖**）。**不做回收** → 覆盖写与删除都持续占用空间，**区域必须按平台写入总量开得足够大**（本仓库默认 256KB）；写满后连 `erase` 也会失败（墓碑同样要追加）。将来若需长寿命循环写，需单独立项引入压缩（届时重新设计并评审）
3. **磨损**：线性追加天然分散擦写（写满前不发生擦除），比"原地更新"方案磨损更低
4. **密钥仍由应用硬编码**（与现状一致）；将来可改为安全存储提供
5. `tal-interface-cleanup-candidates.md` 中关于 tal_kv/tal_fs 的"保留"条目需同步更新
6. **两个加密 API 的实现地雷（Task 2 实测踩到）**：
   - mbedtls CBC **会就地改写传入的 IV**——实现必须每次调用前把全局 IV 拷到局部副本再传（旧 `tal_kv` 正是这样规避），否则全局 IV 被污染后**每次解密首 16 字节全错**，属静默数据损坏
   - `tal_aes128_cbc_decode` 返回的是**密文长度**，需再用 `tal_aes_get_actual_length()` 按 PKCS7 求实际明文长度
7. **撕裂写后就地重写 + 残留字节（所有实现共有的已知缺陷，非仅 flash）**：`init` 扫描遇非法记录即断为日志尾，`s_tail` 指向该记录起点 → 下次追加会**就地重写**该偏移。若新记录比撕裂记录**短**，残留字节留在区域里（Linux 文件实现同样如此）——下次扫描读到这里可能截断，其后记录不可见。缓解方案：追加前清理 `s_tail` 之后一字节（文件实现可行）；更彻底的做法是扫描跳过损坏记录继续向后（会重新引入"空洞容忍"复杂度，建议单独立项）。
   ⚠️ **两种撕裂场景要分开看（67 评审）**：
   - ① **整区无有效记录**（如指向未擦除分区）→ **已由空区归零兜底覆盖** ✓
   - ② **有记录 + 尾部撕裂**（掉电后更常见）→ **不被兜底覆盖**：`s_tail` 停在撕裂记录起点、`have=1` 故不触发归零，追加落在"已含 0 位"的字节上——真 NOR flash 上是 no-op 但驱动返回成功 → 引擎以为写成功 → 重启再 CRC 错 → **同位置反复失败**。缓解需局部擦除 + 空洞容忍，建议单独立项
8. **port IO 失败被当日志尾**：`init` 扫描中任何 `ops->read` 失败都被当作日志结束 → 瞬时 IO 错误可能让 tail 停在半途、后续写覆盖其后数据。Linux 文件实现下不可达，移植时需加固（区分"读到空闲区"与"读失败"）
