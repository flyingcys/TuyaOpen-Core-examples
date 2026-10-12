# esp32 移植学习笔记（对照 linux）

这份文档以本次 esp32 移植为样本，把"core + port + demo 三层架构、ops 卡、构建系统、
mbedtls 接入、链接模型、平台差异判定"系统串一遍。适合先通读第 1-2 节建立框架，
再按需深入第 3-8 节；第 9 节是本次实战踩坑记录（学习价值最高）。

所有结论都有仓库内实证（文件路径/提交号），可逐条复核。

---

## 1. 分层架构总览

```
demo（examples/iot_demo 等，应用）
   │  直调 tal_* API（tal_network / tal_kv / tal_netif / tal_system / tal_log ...）
   ▼
tuyaopen-core（core，芯片无关：iot-core / agent-core / p2p-core / third_party）
   │  经"ops 卡"分发（tal_*_ops_platform，见 §2）
   ▼
port（平台适配层，每个平台一份：linux/port、esp32/port）
   │  实现 TKL（Tuya Kernel Layer）接口，组装卡
   ▼
平台原语（linux：pthread/POSIX/littlefs；esp32：FreeRTOS/esp-idf/lwIP/NVS）
```

三层职责与"不能做的事"：

| 层 | 做什么 | 不做什么 |
|---|---|---|
| demo | 业务逻辑（连 WiFi、触发上传、DP 处理） | 不碰平台 API（除 demo 层允许的平台专有接入，见 §7） |
| core | 协议/云服务/AI/工具链，**芯片无关** | 不直调平台 API；跨平台缺陷**可以改**（须通用，见 §6） |
| port | 把 core 的契约落到平台原语 | 不扩接口（接口集合与 core 契约一致，各平台同构） |

对照表（同一个东西在两个平台的位置）：

| 关注点 | linux | esp32 |
|---|---|---|
| port 目录 | `linux/port/` | `esp32/port/` |
| demo 目录 | `linux/examples/iot_demo/` | `esp32/examples/iot_demo/` |
| core 来源 | submodule `linux/tuyaopen-core`（git 跟踪） | vendored 快照 `esp32/examples/iot_demo/components/tuyaopen-core/`（rsync 同步） |
| 构建系统 | plain CMake + Kconfig(kconfiglib) | esp-idf 组件系统 + sdkconfig(menuconfig) |
| 配置文件 | `linux/.config` | `sdkconfig`（由 `sdkconfig.defaults` 生成） |
| 网络 | 有线（eth0/wlan0 直接可用） | WiFi（demo 层连上后"当有线用"，见 §7） |

---

## 2. 核心机制一：ops 卡模式（core ↔ port 的契约）

### 2.1 为什么不是"弱符号"或"直调"

- **弱符号模式**（传统 TKL）：core 直调 `tkl_xxx()`，port 提供强符号覆盖弱声明。
  问题：符号是**隐式契约**（没有类型检查）、**全局命名空间污染**（每个平台都要
  实现全量接口，未实现的弱符号运行时才炸）、链接器行为随平台而异。
- **直调模式**：core 直接调平台函数。问题：core 里出现平台名，无法多平台共存。
- **ops 卡模式**（本项目的选择）：core 只 extern 引用**结构体实例**
  （`tal_api_ops_platform` 等），成员是函数指针；port 组装实例。好处：
  契约集中在头文件（`tal_*_ops.h`）、单点组装、按需挂载（门控能力可让成员为
  NULL，core 侧有 "not available" 兜底）、类型检查（成员签名与实现必须一致）。

### 2.2 六张卡与分发路径

| 卡（core 定义） | 契约头 | port 组装点 | 谁在用 |
|---|---|---|---|
| `TAL_API_OPS_T`（主卡：log/memory/system/semaphore/mutex/thread/queue，24 成员；thread 组含 p2p 用的 `thread_get_id`） | `iot-core/tal_system/include/tal_api_ops.h` | `port/src/tkl_runtime.c` 文件尾 | `tal_api.c` / `tal_thread.c` / `tal_system.c` |
| `TAL_OTA_OPS_T`（3 成员） | `tal_ota_ops.h` | `port/src/tkl_ota.c` | `tal_api.c`（**强引用**：不实现也必须给空桩） |
| `TAL_KV_OPS_T`（4 成员） | `tal_kv_ops.h` | `port/src/tkl_kv.c` | `tal_kv.c`（4 接口纯透传） |
| `TAL_NETIF_OPS_T`（2 成员：get_ip/set_link_cb） | `tal_netif_ops.h` | `port/src/tkl_netif.c` | `tal_netif.c` |
| `TAL_NETWORK_CARD_T`（35 成员，含 ops 结构 + 卡信息） | `tal_network.h` 尾部 | `port/src/tkl_network.c` 文件尾 | `tal_network.c`（`tal_network_get_active_ops()`） |
| `TAL_FS_OPS_T`（18 成员，仅 p2p 用） | `tal_fs_ops.h` | （esp32 暂未适配） | p2p-core/local_store |

分发路径示例（一次 `tal_system_sleep()` 的完整旅程）：

```
demo: tal_system_sleep(500)
  → core: tal_system.c → TAL_EXEC_OP 宏 → tal_api_get_ops()（返回 &tal_api_ops_platform）
  → port: tkl_runtime.c 的 tkl_system_sleep → vTaskDelay(pdMS_TO_TICKS(...))
```

### 2.3 直调符号：已全部收编（2026-10 解耦）

**现状：core 在编代码零 tkl 符号**（调用 / 类型 / include 三个口径），可直接做门禁：

```bash
/usr/bin/grep -rn '\btkl_[a-z_]*(' linux/tuyaopen-core --include='*.c' --include='*.h'
# 期望（2026-10 基线，esp32 快照同数）：48 行命中 = 46 行门控代码
# （ble 32 + agent-core AI 14）+ 2 行注释（tal_system.c 的 doxygen）；在编代码 0 行

# 对象级补充门禁（更强，需构建产物）：core 全部归档的 tkl_ 未定义符号应为 0
# （二进制级 nm -u 为 0 是必要不充分——被 port 解析后就隐形了，对象级才是真门禁）
for a in $(find <build> -name 'lib*.a' -path '*tuyaopen-core*'); do nm -u "$a" | grep -c '\btkl_'; done
# 2026-10 基线实测：三个 demo 构建树 × 19 个 core 归档 → 全部 0；port 归档定义 92 个
```

曾经有 5 组"不走卡"的直调例外，已按"core 不直调 tkl"原则逐一收编：

| 原直调 | 收编方式 |
|---|---|
| `tkl_system_malloc/free`（tuya_queue / tuya_ringbuf / mbedtls 适配层） | 前两模块**上移** `iot-core/tal_system/`，改走 `tal_malloc`/`tal_free`（经主卡分发）；mbedtls 适配层改 `tal_*` |
| `tkl_mutex_*`（tuya_queue 宏直调） | 同上，改走 `tal_mutex_*`；句柄类型 `TKL_MUTEX_HANDLE` → `MUTEX_HANDLE` |
| crypto 透传层（`tal_hash.c`/`tal_symmetry.c` → `tkl_sha256_*`/`tkl_aes_*`） | 透传层删除，`mbedtls_hash.c`/`mbedtls_symmetry.c` 的实现**直接就是** `tal_*`；句柄类型改 core 自持的 `TAL_HASH_HANDLE`/`TAL_SYMMETRY_HANDLE`；原 `ENABLE_PLATFORM_*` 平台覆盖点一并删除（全仓无人定义，是死代码；硬件加速走 mbedtls ALT，见 §4.5） |
| `tkl_system_psram_malloc/free`（p2p） | **删除**（口径：psram 全部去掉）；`tuya_ipc_skill.c` 改 `tal_malloc`/`tal_free`；两 port 的 tkl psram 符号与声明一并清理。**注意语义变化**：原为 PSRAM 专属分配，现走普通堆——esp32 开 SPIRAM 时 IDF 的 malloc 默认优先用 PSRAM，但大 buffer（如 SKILL_PARAM_LEN）的分配区域**不再是显式保证** |
| `tkl_thread_get_id`（p2p pjlib） | 主卡新增 `thread_get_id` 成员 + 新 TAL API `tal_thread_get_id`；两 port 原有实现解门控、接线到卡 |

**为什么 tuya_queue / tuya_ringbuf 要上移**：它们在 `tuya_utilities`（最底层，被
`tal_system` PUBLIC 依赖，`tal_workqueue` 就在 tal_system 内用它），不能反向依赖
`tal_system` 去调 `tal_mutex_*`；上移到 `tal_system` 目标后即普通 TAL 层文件
（消费方本就全链 tal_system，include 裸名不变）。

**仅剩的门控代码直调**（当前构建不编译，记录在案）：`iot-core/ble/tal_bluetooth.c`
（32 处 `tkl_ble_*` + 112 处 `TKL_BLE_*` 类型，`CONFIG_ENABLE_BLUETOOTH` 门控）、
`agent-core/ai_audio/src/ai_audio_input.c`（`tkl_vad_*`）、
`agent-core/tuya_ai_service/svc_ai_codec/src/tuya_ai_encoder_{opus,speex}_ipc.c`
（`tkl_audio_enc_request`——该符号在 core/port 均无实现，是死代码）。注意 encoder
两个文件是**二级开关**门控：`CONFIG_ENABLE_TUYA_CODEC` 只带进 `tuya_ai_encoder.c`，
带 tkl 的 `*_ipc.c` 由 `CONFIG_ENABLE_TUYA_CODEC_OPUS_IPC`/`_SPEEX` 控制
（`agent_demo` 只开了第一级）——未来误开二级开关会让 tkl_ 进编译，本门禁会立即报红。
要清这三处需分别建 BLE 卡（约 34 成员）与 AI 音频卡，工作量另计。

### 2.4 TKL 头与 core 的关系

- TKL 接口头（`tkl_*.h`）**全部在 port 里**（`port/include/{common,system,network,security}/`），
  core 内没有任何 `tkl_*.h`。
- **core 在编代码零 TKL 头依赖**（2026-10 解耦后）：曾用于直调声明与 mbedtls 适配的
  `tkl_memory.h`/`tkl_hash.h`/`tkl_symmetry.h`/`tkl_asymmetrical.h`/`tkl_mutex.h` 均已清除
  （见 §2.3）；仅门控代码（ble / agent-core AI，当前构建不编译）仍 include
  `tkl_bluetooth*.h`/`tkl_vad.h`/`tkl_ipc.h`/`tkl_memory.h`。
- port 头经构建系统**注入 core 的环境基底**（`tuyaopen-headers`），core 编译单元
  才能 include 到（详见 §3.3）。
- 各平台 port 的 TKL 接口集合**同构**（esp32 port 的 15 个头从 linux port 同步而来，
  不含平台多余接口）——这是"不新扩"原则的落地。解耦后 `tkl_hash.h`/`tkl_symmetry.h`/
  `tkl_asymmetrical.h` 已无 includer（crypto 实现归 core），作为 TKL 接口面暂留。

---

## 3. 核心机制二：构建系统（Kconfig → 宏 → 源码门控）

### 3.1 一条主线：开关从哪来、到哪去

```
用户配置
  ├─ linux:   Kconfig(kconfiglib) → .config（CONFIG_XXX=y/n）
  └─ esp32:   Kconfig.projbuild → menuconfig → sdkconfig（CONFIG_XXX=y/n）

构建系统读配置
  ├─ linux:   demo 的 CMakeLists 手动读 .config → CMake 变量 CONFIG_XXX
  └─ esp32:   sdkconfig 的 CONFIG_XXX 本身就是 CMake 变量（esp-idf 机制）

core 的 cmake/TuyaOpenOptions.cmake 归一化（y/1/on → ON）后：
  ├─ tuyaopen-core.cmake 的"宏注入表"：只对 ON 的开关给 tuyaopen-headers
  │  注入 ENABLE_XXX=1（源码守卫是 defined() 风格，OFF=不定义）
  └─ 各模块 CMakeLists 按 CONFIG_XXX 增删源码（如 tuya_lan.c 随 LOCAL_LINK）

port 目标的宏注入：core 的宏**不传播**到 port 目标（tuyaopen-headers 的
INTERFACE 只作用于链接它的目标），port/CMakeLists.txt 必须**自行注入同名宏**
（ENABLE_TUYA_P2P/ENABLE_LOCAL_LINK/ENABLE_AI_COMPONENTS，见文件内注释）
```

### 3.2 esp32 侧的关键差异

- **Kconfig 树的合并**：esp-idf 把所有组件的 `Kconfig.projbuild` 收集到**同一棵树**——
  core 的开关与 esp-idf 自己的开关**共处一树**。这带来两个约束：
  1. **符号命名空间**：core 的开关名不能与 esp-idf 撞（本次踩过：core 的
     `choice MBEDTLS_TLS_MODE` 等裸名与 esp-idf 的 mbedtls Kconfig 撞名，
     同树合并后成依赖环、kconfgen 直接报错——已回 core 通用修复，加 `ENABLE_` 前缀）；
  2. **默认值一致**：Kconfig 的默认值必须与 `TuyaOpenOptions.cmake` 的 `option()`
     默认值保持一致（两处都改才不漂移）。
- **CMake 变量可见性**：esp-idf 把 sdkconfig 的 `CONFIG_*` 以"宏展开"方式设为
  CMake 变量（组件 CMakeLists 作用域可见），因此 `tuyaopen-core.cmake` 的
  `if(CONFIG_${opt})` 判断在两个平台**同一份代码**工作——这是 core 保持芯片无关
  的关键设计（core 的 cmake 不知道宿主是谁）。
- **组件目标**：`idf_component_register` 注册的组件有"组件目标"（`__idf_x`）与
  别名（`idf::x`）两套名字；**裸名（如 `mbedtls`）指向的是 esp-idf 子目录里
  上游自己创建的同名库目标**，不是组件——本次 mbedtls 接入的坑多源于此（见 §4）。

### 3.3 port 头如何进入 core 的编译

```
port/CMakeLists.txt（末尾）: target_include_directories(tuyaopen-headers INTERFACE ${port_incs})
  → core 模块统一 PRIVATE 依赖 tuyaopen-headers（tal_system/CMakeLists 等）
  → core 全部编译单元都能 include "tuya_cloud_types.h"/"tuya_kconfig.h"/"tkl_*.h"
```

esp32 侧同构：`esp32/port/CMakeLists.txt` 做同样注入；包装组件
（`components/tuyaopen-port`）负责把 esp-idf 组件的头路径注入 port 目标
（plain CMake 目标不会自动获得 idf 组件头）。

---

## 4. 核心机制三：mbedtls 接入（THREADING_ALT 与硬件加速）

### 4.1 mbedtls 的三种"配置注入"层次

mbedtls 自身不绑定平台，靠三个钩子：

| 钩子 | 机制 | 本项目里的落点 |
|---|---|---|
| 编译配置头 | `MBEDTLS_CONFIG_FILE` 宏指向一个配置头（定义启用哪些模块） | core 自带 mbedtls：`tuya_transport/config/tuya_tls_config.h`；esp32：esp-idf 的 `mbedtls/esp_config.h` |
| 平台抽象 | `mbedtls_platform_set_calloc_free()`（内存） | `tuya_tls_init()` 里注册为 `tal_calloc/tal_free` |
| **线程抽象（THREADING_ALT）** | `threading_alt.h`（结构）+ `mbedtls_threading_set_alt()`（回调） | 见 4.2 |

### 4.2 THREADING_ALT 机制详解

**它是什么**：mbedtls 需要在多线程环境保护共享状态（TLS 会话、CTR_DRBG、证书缓存等），
但**不内置锁实现**。开启 `MBEDTLS_THREADING_ALT` 后：

1. **编译期**：mbedtls 的 `threading.h` 会 `#include "threading_alt.h"`——
   这个头**由使用者提供**，定义 `mbedtls_threading_mutex_t` 结构体
   （mbedtls 内部会实例化它，但**不直接操作**其成员）；
2. **运行期**：调用 `mbedtls_threading_set_alt(init, free, lock, unlock)`
   注册 4 个回调——**这组回调是全局的**（后注册者覆盖前者）。

mbedtls 内部所有锁操作 = 对 `mbedtls_threading_mutex_t` 实例调这 4 个全局回调。
**结构（编译期、每个编译单元各自展开）+ 回调（运行期、全局唯一）必须配对。**

**本项目里的两份实现**：

| | core 版 | esp-idf ROM 版 |
|---|---|---|
| 头文件 | `core/iot-core/tuya_transport/config/threading_alt.h` | `esp-idf/components/mbedtls/port/mbedtls_rom/threading_alt.h` |
| mutex 成员 | `MUTEX_HANDLE`（tal_mutex 句柄，void*） | `SemaphoreHandle_t`（FreeRTOS） |
| 回调实现 | `tuya_tls.c` 的 `__tuya_tls_mutex_*`（用 `tal_mutex_*`） | `mbedtls_rom_osi.c`（`xSemaphoreCreateMutex` 等，**非递归**） |
| 注册时机 | 运行时 `tuya_tls_init()`（`tuya_tls.c:383`） | 启动期**构造函数** `mbedtls_rom_osi_functions_init`（`__attribute__((constructor))`，`mbedtls_rom_osi.c:447`）——**仅 `CONFIG_MBEDTLS_USE_CRYPTO_ROM_IMPL=y` 时该源文件才参与编译**（S3 无此能力，见 §4.5.7） |
| 服务对象 | core 的 TLS 栈（`tuya_tls.c`） | esp-idf 的 mbedtls 消费者（wifi/wpa_supplicant/esp-tls 等） |

**谁生效（2026-10-10 按实测更正）**：在**本项目的 esp32s3 固件里，ROM 版压根没有
注册**——`MBEDTLS_USE_CRYPTO_ROM_IMPL` 依赖 `ESP_ROM_HAS_MBEDTLS_CRYPTO_LIB`（只有
esp32c2 有），S3 上该 config 不存在、`mbedtls_rom_osi.c` 不参与编译。三重实证：
build 目录无 `mbedtls_rom_osi*` 目标文件 / sdkconfig 无该 config / 固件符号表
`nm | grep mbedtls_rom` = 0。**全固件唯一的注册点就是 tuya 的 `tuya_tls_init()`**
→ 全局生效的是 tal_mutex 版（esp32 的 tal_mutex 底层就是
`xSemaphoreCreateRecursiveMutex`）。
（原表述"构造函数先注册、tuya 后注册覆盖"只在支持 ROM crypto 的芯片上成立。
教训：核实"某代码注册了什么"要先核"它编进固件了吗"——1c 此前的复核基于源码
存在性、漏了这一跳。
**未来注意**（1c 补充）：若目标换成支持 ROM crypto 的芯片（如 esp32c2），
本节"两套回调递归性混用"的风险会从"不存在"变为"存在"（ROM 版会真的注册），
需重新评估。）

**风险点（已做代码级核实，2026-10-10）**：两套的**递归性不同**——tal 版用递归
信号量（`xSemaphoreTakeRecursive`），ROM 版用非递归（`xSemaphoreTake`）。全局
回调被覆盖后，esp-idf 的 mbedtls 消费者（wifi/wpa_supplicant/esp-tls）的 mutex
会用**递归 API 操作非递归信号量**。核实结论（两层）：

1. **不触发**：mbedtls 3.6 的锁只在公开 API 入口、内部函数不加锁（如
   `mbedtls_ctr_drbg_random` 持锁、`ctr_drbg_reseed_internal` 不加锁）；持锁
   期间回调出去的 4 处（rsa 的 `f_rng`、drbg 的 `f_entropy`、entropy 的
   `f_source`、ticket 的 `f_rng`）在 esp-idf + tuya 全栈里都是 mbedtls 自身
   函数且作用于**不同对象**（drbg→entropy、rsa→drbg→entropy、ticket→drbg），
   不存在同一 mutex 重入。
2. **即使触发也不坏（FreeRTOS 兜底）**：esp-idf 的 `xQueueCreateMutex` 对递归/
   非递归两种类型都初始化 `xMutexHolder`/`uxRecursiveCallCount` 字段（类型常量
   只用于调试），`xQueueTakeMutexRecursive` 只依赖这两个字段 → 递归 API 作用于
   非递归 mutex 在功能上等价（仅"跨任务 give"会失败，mbedtls 不会那样用）。
   注意：这是 esp32 平台巧合（linux 上 tal_mutex 是 pthread mutex，但 linux
   不用 esp-idf mbedtls，无混用场景）。

**更值得记录的副作用（仅对支持 ROM crypto 的芯片成立）**：`mbedtls_threading_set_alt`
二次调用会**重新初始化**三个全局锁（readdir/gmtime/PSA，见 mbedtls
`library/threading.c`），后注册者会泄漏先注册者的信号量、且若此刻另一线程正持有
全局锁会丢失互斥；若某线程的 lock/unlock 跨越了这次指针切换（plain take +
recursive give）还会导致 `uxRecursiveCallCount` 下溢、永不释放。**正常启动顺序
不满足触发条件**（`tuya_tls_init` 早于联网）；且 S3 上只有一次注册，本副作用不适用。

**改善方向**（通用改动，可讨论）：core 增加"宿主已提供 THREADING_ALT 回调时
不再覆盖"的开关（例如随 `CONFIG_ENABLE_PLATFORM_MBEDTLS` 联动）——同时消除
上述两类隐患。

**未覆盖**：应用层回调（`cert_select_cb` / 自定义 rng 等）、ROM/WiFi 闭源 blob、
PSA 全路径（仅核了 RNG/init 两条）。

### 4.3 "threading_alt 兼容头"的来龙去脉

本次移植中途做过一个**自包含版** threading_alt.h（`void *mutex;` 不 include
`tal_mutex.h`），目的是让"所有编译单元都能编过"。它是**临时方案**，根因修复后
已废弃。演进过程（每一段都有编译实证）：

1. **起点**：esp32 侧把 core 的 `config/threading_alt.h` 同步排除（当时为避开
   esp-idf 组件编到它），core 的 `tuya_tls.c` 于是命中 **ROM 版** → 它的回调代码
   `tal_mutex_create_init(&mutex->mutex)` 的参数类型变成 `SemaphoreHandle_t*`，
   与 `void**` 不匹配 → `-Wincompatible-pointer-types` 编译失败；
2. **临时方案**：做"自包含兼容头"（`void *mutex`，不需要 tal_mutex.h）——
   这样无论谁编到都能过；但 esp-idf 组件经 mbedtls 目标的 INTERFACE 拿到 core
   的 config 目录时，仍会**优先命中它**（include 顺序：core 的 config 在
   mbedtls 目标路径之前）；
3. **根因与修复**（回 core 的通用修复）：core 的 `tuya_transport/CMakeLists.txt`
   给 mbedtls 目标注入 core config 目录时，**没有判断平台是否已提供 mbedtls**
   ——平台模式下误把 core 的 config 注入宿主 mbedtls 目标、经依赖链污染
   esp-idf 组件（esp_common 编到 core 的 threading_alt.h 却找不到 tal_mutex.h）。
   加 `NOT CONFIG_ENABLE_PLATFORM_MBEDTLS` 条件后，core 的 config **只进 core
   自己的编译单元**（它们有 tal_system 头路径），问题从根上消失；
4. **现状**：esp32 侧不再有"兼容头"——core 原版 `threading_alt.h`（tal_mutex 版）
   直接使用；esp-idf 的 mbedtls 库/组件用 ROM 版；两边各走各的 include 路径。

**从这件事能学到的**：
- 同名头（`#include "threading_alt.h"`）的命中**完全由 include 路径顺序决定**，
  而顺序由"目标自己的 include → 链接依赖的 include"层层展开——调试这类问题时，
  直接看**编译命令**（`compile_commands.json`）里 `-I` 的先后是最快的；
- 传播类问题（INTERFACE include 泄漏）的修复应该**切断传播源**，而不是做"兼容
  头"让下游都能过——后者掩盖问题且引入双份结构。

### 4.4 core 自带 vs 平台提供 mbedtls

- `CONFIG_ENABLE_PLATFORM_MBEDTLS=ON`（esp32 的配置）：core 的 `third_party`
  **不注册** mbedtls 目标，模块里 `target_link_libraries(... mbedtls ...)` 的裸名
  解析到**宿主**的 mbedtls（esp-idf 的库目标）；配置头/线程钩子都走宿主体系。
- `=OFF`（linux 的配置）：core 编自带 mbedtls（`third_party/mbedtls` submodule），
  配置头 `tuya_tls_config.h`、线程钩子用 core 自己的 threading_alt.h。
- esp32 侧适配（都在 port / 平台侧，见 §6）：
  1. esp-idf 的 mbedtls **库目标**（上游 `add_subdirectory(mbedtls)` 创建，名字就叫
     `mbedtls`）带 `MBEDTLS_CONFIG_FILE` 定义但缺 `esp_config.h` 所在目录
     （该目录在**组件目标** `idf::mbedtls` 上）→ port 组件给 core 的环境基底
     补该 include；
  2. esp-idf 专有的组件顺序（core 处理时 mbedtls 目标须已存在）→ 组件入口
     CMakeLists 加 `REQUIRES mbedtls`（平台差异文件）。

---

### 4.5 mbedtls 硬件加速（ESP32 加密外设）

ESP32 芯片内置加密加速器，esp-idf 的 mbedtls 把部分算法**编译期重定向**到硬件。
本项目用平台 mbedtls，因此**自动继承**这套加速——这也是"优先用芯片自带 mbedtls"
（`CONFIG_ENABLE_PLATFORM_MBEDTLS=y`）的收益之一。

#### 4.5.1 两层结构

```
mbedtls 上游 API（mbedtls_aes_* / mbedtls_sha256_* / mbedtls_mpi_* / mbedtls_gcm_*）
   │ 编译期重定向：-DMBEDTLS_CONFIG_FILE="mbedtls/esp_config.h"
   │  ├─ ALT 宏（esp_config.h:150-239）：MBEDTLS_AES_ALT / SHA*_ALT / MPI_*_ALT / GCM_ALT…
   │  └─ ALT 头（aes_alt.h 等）把 context 类型与函数名 #define（或同名符号覆盖）到 esp_* 实现
   ▼
port 实现层（esp-idf/components/mbedtls/port/，11 个子目录）
   AES: esp_aes_*（aes/dma|block/esp_aes.c）→ DMA 描述符 + GDMA + 中断等待
   SHA: esp_sha_*（sha/dma|block|parallel_engine/）→ sha_hal_hash_dma（忙等）
   MPI: esp_mpi_*_hw_op（bignum/bignum_alt.c）→ RSA 外设（大数乘/模幂）
   MD5: port/md/esp_md.c → ROM 里的 md5；RNG: esp_hardware.c → esp_fill_random
   ▼
HAL（components/hal/：aes_hal.c/sha_hal.c/mpi_hal.c）→ LL 寄存器层
（esp32s3/include/hal/*_ll.h，含时钟门控）
   ▼
互斥：components/esp_security/src/esp_crypto_lock.c
（SHA/AES 共用一把锁——它们共享 GDMA；MPI 一把；DS/HMAC 各一把）
   ▼
硬件：AES / SHA / RSA(MPI) / DS / HMAC / RNG 外设（S3 无 ECC/ECDSA 外设）
```

#### 4.5.2 ALT 映射表（关键项）

| ALT 宏（esp_config.h） | 打开条件 | 实现来源 |
|---|---|---|
| `MBEDTLS_AES_ALT` | `CONFIG_MBEDTLS_HARDWARE_AES` | `aes_alt.h` → `port/aes/…/esp_aes.c`（CBC/CFB/CTR/OFB/XTS 全套） |
| `MBEDTLS_GCM_ALT` | **只看 HARDWARE_AES**（与 HARDWARE_GCM 无关） | `gcm_alt.h` → `esp_aes_gcm_*` |
| `MBEDTLS_SHA1/256_ALT`（`SHA512_ALT` 另加 SOC 条件） | `CONFIG_MBEDTLS_HARDWARE_SHA` | `sha*_alt.h` + `port/sha/<形态>/esp_sha{1,256,512}.c`（**同名符号直接覆盖上游**） |
| `MBEDTLS_MPI_MUL_MPI_ALT` / `EXP_MOD_ALT(_FALLBACK)` | `CONFIG_MBEDTLS_HARDWARE_MPI` | `port/bignum/esp_bignum.c`（只换这两个函数） |
| `MBEDTLS_MD5_ALT` | `CONFIG_MBEDTLS_ROM_MD5` | `port/md/esp_md.c` → ROM md5 |
| `MBEDTLS_ECP_MUL/VERIFY_ALT` | `CONFIG_MBEDTLS_HARDWARE_ECC`（**S3 无此项**） | `port/ecc/ecc_alt.c` |

ALT 语义：mbedtls 上游每个算法模块都用 `#if !defined(MBEDTLS_XXX_ALT)` 包住自带
实现——定义 ALT 后上游实现被排除，改由 port 提供。

#### 4.5.3 esp32s3 的加速能力（对照 classic）

| 能力 | ESP32-S3 | ESP32 classic |
|---|---|---|
| AES | ✅ 128/256 位 + **DMA/GDMA**；**无 AES-192** | ✅ 128/192/256，**无 DMA**（块模式） |
| AES 中断等待 | ✅（默认开） | ❌（只能忙等） |
| AES-GCM 全硬件 | ❌（AES 块硬件 + GHASH 软件） | ❌ |
| SHA | ✅ SHA1/224/256/384/512 + DMA | ✅（parallel_engine 形态，无 DMA、无 224） |
| MPI/RSA | ✅ 最大 4096 位，3 路流水 | ✅ 最大 4096 位，1 路 |
| ECC/ECDSA 外设 | ❌（无 `SOC_ECC_SUPPORTED`/`SOC_ECDSA_SUPPORTED`） | ❌ |
| DS / HMAC / RNG | ✅ | 未暴露 |
| ROM crypto 实现 | ❌（`ESP_ROM_HAS_MBEDTLS_CRYPTO_LIB` 仅 C2） | ❌ |

#### 4.5.4 我们固件的实际配置（sdkconfig 实测）

| 项 | 值 | 说明 |
|---|---|---|
| `CONFIG_MBEDTLS_HARDWARE_AES` | **y** | AES 加速 |
| `CONFIG_MBEDTLS_AES_USE_INTERRUPT` | **y** | 长操作走中断（不忙等，其他任务可调度） |
| `CONFIG_MBEDTLS_HARDWARE_SHA` | **y** | SHA 加速 |
| `CONFIG_MBEDTLS_HARDWARE_MPI` | **y** | RSA 大数加速 |
| `CONFIG_MBEDTLS_MPI_USE_INTERRUPT` | **y** | |
| `CONFIG_MBEDTLS_LARGE_KEY_SOFTWARE_MPI` | **not set** | 纯硬件模式：>4096 位密钥直接失败（S3 上限），无软回退 |
| `CONFIG_MBEDTLS_ROM_MD5` | **y** | MD5 走 ROM |
| `CONFIG_ESP_TLS_USE_DS_PERIPHERAL` | **y** | 编入 DS 签名路径（真正使用需 efuse 烧密钥） |
| `CONFIG_MBEDTLS_USE_CRYPTO_ROM_IMPL` | **不存在** | S3 无此能力（见 §4.5.7） |
| `CONFIG_MBEDTLS_HARDWARE_ECC/ECDSA/GCM` | **不存在** | S3 无这些外设（是"没这项"，不是"关了"） |

**固件级证据**（`nm build/iot_demo.elf`）：`esp_aes_dma_start/done`、
`esp_sha_dma`、`esp_mpi_*_hw_op` 均在符号表 ✓（加速真实链入）；`mbedtls_rom_osi`
符号 = 0 ✓（与 §4.2 的更正互相印证）。

#### 4.5.5 与本项目的关系（两条链都受益）

1. **TLS 链**：`tuya_tls.c` → mbedtls_ssl（esp-idf mbedtls）→ 其内部 AES-GCM /
   SHA256 / ECDHE 运算**自动走硬件**；
2. **core 的 crypto 接口链**：`tal_hash` / `tal_symmetry`（DP 加解密、OTA 校验等）→
   core 的 `mbedtls_hash.c` / `mbedtls_symmetry.c`（用**标准 mbedtls API**：
   `mbedtls_sha256_*` / `mbedtls_aes_*` / `mbedtls_md5_*`；2026-10 解耦后这两文件即
   `tal_*` 原语的实现本体，原 `tkl_hash`/`tkl_symmetry` 透传层已删除，见 §2.3）→
   在 esp32 上同样解析到 esp-idf 的 mbedtls → **硬件加速** ✓（这条不经过 TLS，是 core
   自己的 crypto 层，同样受益）。

对照：**core 自带的 mbedtls（linux 用，3.1.0）是纯软件实现**——如果 esp32 选
`PLATFORM_MBEDTLS=OFF` 用 core 自带副本，除符号冲突外还会**丢掉全部硬件加速**。

#### 4.5.6 坑与注意点

1. **S3 无 AES-192**（classic 反而支持）——本地自测用 192 位密钥会莫名失败
   （`esp_aes_setkey` 返回 `MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED`）；
2. **S3 的 AES-GCM 是半硬件**（GHASH 软件）——别按"全硬件 GCM"理解；且
   `MBEDTLS_GCM_ALT` 只依赖 `HARDWARE_AES`，关 `HARDWARE_GCM` 也不会退回上游软件 GCM；
3. **"没有 CONFIG 项"≠"关了"**：S3 上 `MBEDTLS_HARDWARE_ECC/ECDSA/GCM` 根本不存在——
   条件编译要判 `SOC_*_SUPPORTED`，不能判 CONFIG；
4. **SHA 的等待是忙等**（`sha_hal_wait_idle`，无中断选项）——大块 hash 会阻塞 CPU
   （AES/MPI 有中断选项，SHA 没有）；
5. **flash 常量不能 DMA**：`esp_sha_dma` 会自动降级为逐块模式（证书等若在 flash，
   hash 走慢路径）；PSRAM 缓冲需 cache msync + 对齐；
6. **`mbedtls_rom` 目录名有误导**：`threading_alt.h`（线程适配，我们只借它）与
   `mbedtls_rom_osi.c`（ROM crypto 函数表，S3 不编译）是同目录的**两套无关机制**
   （详见 §4.2 与 §4.5.7）；
7. `MBEDTLS_HARDWARE_AES/SHA/MPI` 都带 `!SPIRAM_CACHE_WORKAROUND_STRATEGY_DUPLDST`
   依赖——某些 PSRAM 配置下加速会被**静默禁用**（我们没开 SPIRAM，不受影响）；
8. **中断注册在 `esp_aes_init` 里**（失败直接 `abort()`）——`MBEDTLS_AES_INTERRUPT_LEVEL`
   设太高可能影响系统其他中断（我们用默认 0 = 自动分配）。

#### 4.5.7 附：`CONFIG_MBEDTLS_USE_CRYPTO_ROM_IMPL`（S3 用不上，知识性）

芯片掩膜 ROM 里固化了 mbedtls crypto 代码（ECO4 为 3.6.0）。打开后**方向是反的**：
ROM 里的 mbedtls 在跑，它把 mutex/calloc/free 及 SHA 的 update 等回调**指回 IDF 的
实现**（因此 ROM 版仍然用到 IDF 的 SHA 硬件加速）。收益是省 flash，代价是版本冻结
（安全补丁要靠 IDF"patch 符号"）。**只有 esp32c2 支持**
（`ESP_ROM_HAS_MBEDTLS_CRYPTO_LIB`），S3 上该 config 不存在——§4.2 的注册点更正正
与此相关（ROM 版代码在 S3 上不参与编译）。

## 5. 核心机制四：链接模型（循环引用）

### 5.1 环从哪来

- core 的库引用 port 的符号：**卡的实例**（`tal_*_ops_platform`，core 的 `tal_*.c`
  extern 引用）。2026-10 解耦后 tkl_* 直调已清（§2.3），环只剩卡实例这一条；
- port 的库引用 core 的符号：`tal_mutex_*`（卡的实现里用 core 的 API）、
  `tal_log` 等；
- 两个方向的引用都真实存在 → **静态库循环依赖**。解耦不消环：core 引卡实例、
  port 引 tal API 是卡机制的固有双向依赖。**实测（2026-10 解耦后）**：
  - esp32：去掉链接组后链接失败，报的正是**卡实例未定义**
    （`tal_api_ops_platform`/`tal_ota_ops_platform`/…）→ 组必需；
  - linux：链接行由 CMake 按依赖闭包生成、库列表本身就含重复（`libtuyaopen-port.a`
    在组内/组外各出现一次，是 app 的 CMakeLists 显式列了两处；`libtal_system.a`
    出现三次是 CMake 的闭包展开），去掉显式组后仍链接成功 → 显式组在 linux 侧
    是冗余保险。

### 5.2 两个平台的解法（同一个机制）

GNU ld 对静态库是**单遍扫描**（从左到右，遇到未解析符号时回头找**前面**的库——
不对，是找**当前及之后**的库），环需要 `--start-group/--end-group`（组内反复重扫）：

```cmake
# linux（examples/iot_demo/CMakeLists.txt，core 的注释里也写了这个用法）
target_link_libraries(iot_demo PRIVATE
    -Wl,--start-group ${TUYAOPEN_CORE_LIBS} $<TARGET_FILE:tuyaopen-port> -Wl,--end-group
    tuyaopen-port tuyaopen-headers)
```

```cmake
# esp32（components/tuyaopen-port/CMakeLists.txt）：
# esp-idf 的组件库按依赖拓扑排序、没有 --start-group，单遍链接解析不了环。
# 把 core 全部库与 port 放进同一个链接组（CMake 对 "-Wl,..." 形式的链接项
# 原样传给链接器；TUYAOPEN_CORE_LIBS 由 core 的 tuyaopen-core.cmake 导出）。
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--start-group" ${TUYAOPEN_CORE_LIBS} tuyaopen-port "-Wl,--end-group")
```

### 5.3 怎么验证环真的解了

- 构建通过只是第一层；**符号级验证**：`nm iot_demo.elf | grep ' T tkl_'` 等，
  确认 port 的符号真的进了固件（本次验证了 5 张卡 + `tkl_system_malloc` +
  `tkl_mutex_create_init`）。

---

## 6. 平台差异的判定标准（"通用性"原则）

本次移植反复调整的核心问题：**一个问题该改 core 还是改平台侧？**

**标准**：
- 问题是 **core 的跨平台能力缺陷**（换任何平台都错/会踩）→ **改 core**，
  但必须**通用**（对所有平台行为正确，不能给某芯片开特例）；
- 问题是**宿主/平台专有机制**（如 esp-idf 的组件系统、idf 的目标命名）→
  **平台侧**（port 或 esp32 差异文件）。

本次的实际判定（可当案例库）：

| 问题 | 判定 | 落点 |
|---|---|---|
| `tal_thread.h` 悬空 `/**` 吞掉 C++ 守卫 | 通用语法 bug（任何平台编 C++ 都会错） | core（`748d089`） |
| Kconfig 裸名符号与宿主撞名（依赖环） | 通用命名空间缺陷（任何带 mbedtls Kconfig 的宿主都踩） | core（`eec2e31`，加 `ENABLE_` 前缀） |
| mbedtls config 注入误命中平台目标 | 通用条件缺陷（对"平台提供 mbedtls"的模式判断缺失） | core（`a2a2459`） |
| 组件顺序 `REQUIRES mbedtls` | esp-idf 专有组件机制 | esp32 差异文件（`components/tuyaopen-core/CMakeLists.txt`） |
| esp_config.h 路径注入 / 链接组 / `-Wno-error=format` | esp-idf 专有接入 | port 侧（`components/tuyaopen-port/CMakeLists.txt`） |

**差异文件的维护**：esp32 侧目前只有 1 个同步排除文件（组件入口 CMakeLists），
清单与同步命令在 `esp32/README.md`；排除模式注意 `'/CMakeLists.txt'`（前导斜杠
限定根目录，裸名会匹配任意层级——踩过）。

---

## 7. esp32 侧实现要点（对照 linux 各卡）

| 卡/模块 | linux 实现 | esp32 实现 | 关键差异与理由 |
|---|---|---|---|
| 主卡 memory | `tuya_mem_heap` 自管堆（预留大块内存） | `malloc/free` 直映射 esp-idf 系统堆 | esp32 堆是多区域分块管理，不适用"大块预留" |
| 主卡 system | `clock_gettime`/`usleep`/`exit` | `esp_timer_get_time()`/`vTaskDelay`/`esp_restart` | 毫秒时钟用 esp_timer（微秒级）而非 FreeRTOS tick（10ms 精度）；sleep <10ms 补齐 |
| 主卡 thread | pthread（忽略 stackDepth） | `xTaskCreate`（**字节计栈**，s3 另 +1KB） | 栈单位语义不同；句柄即 TaskHandle_t |
| 主卡 mutex | `pthread_mutex`（递归） | `xSemaphoreCreateRecursiveMutex` | 同为递归；esp32 的句柄即 FreeRTOS 信号量 |
| 主卡 sem/queue | POSIX 信号量 / 自建队列 | FreeRTOS 信号量/队列（按值拷贝） | 语义对齐（wait timeout 0=非阻塞） |
| KV | littlefs + AES（文件模拟 flash） | **NVS** + key 映射 | NVS key 上限 15 字符，core 实际用 26 字符（`uuidxxx.devid`）→ 超长 key 折叠为 `h`+FNV-1a 56bit hex；NVS 自身线程安全，免互斥 |
| netif | ioctl 遍历接口 + 3s 轮询线程 | **esp_netif 事件**（GOT_IP/DISCONNECTED） | 事件驱动免轮询；**链路 up = 已获 IP**（对齐"连上 WiFi 即当有线"） |
| network | POSIX socket | lwIP socket | errno 映射表同表（→ core 的 `UNW_*` 私有错误码空间）；`set_block` 用 fcntl（ioctlsocket 在 esp-idf 默认配置无声明） |
| OTA | 写文件 `./tuyadb/ota` | `esp_ota` 双槽轮换 | 需要分区表有 `otadata`+`ota_0/ota_1` |

### 7.1 网络模型："WiFi 连上即当有线"

```
demo 层（main/wifi_sta.c）：esp-idf 原生 API 连 AP（SSID 走 menuconfig），
                            断线用 esp_timer one-shot 做退避重连
      ↓ 连接成功（IP_EVENT_STA_GOT_IP）
port 层（tkl_netif.c）：事件回调 → core 的链路回调 → core 置 link up
      ↓
core：user_network_check() 返回 true → 发起云连接（MQTT/TLS）
      ↓ 之后完全同 linux：socket 走 lwIP，激活/DP/OTA 复用同一套 core
```

core 已无 wifi 抽象（`tal_wifi`/`tkl_wifi` 随 netmgr 整体移除），所以
"把 WiFi 连上"是 **demo 的职责**，port 只负责把链路状态翻译给 core。

### 7.2 任务与栈（esp32 上的一个实操点）

- core 的任务（workq/timerq/msgq 等）栈大小来自 `tuya_kconfig.h` 的参数
  （与上游一致：workq 5120/timerq 4096/msgq 4096）；
- **demo 的 `tuya_app_main` 线程用 8KB**（linux 版 4KB）：`user_main` 的
  `tuya_iot_yield` 状态机在**同一线程**里跑 MQTT 循环，连接阶段的 TLS 握手与
  收包解密都在这个栈上，4KB 在 esp32 上偏紧；
- esp32 的 `xTaskCreate` 栈是**字节**（linux pthread 是字节但被忽略），
  port 对 s3 另补 1KB。

---

## 8. 构建与烧录速查（esp-idf v5.4）

```bash
# 环境（v5.4 的 venv 是 py3.12，需让 PATH 先找到 py3.12；用 tuyaopen 自带的）
export PATH=/home/share/samba/open-components/tuyaopen/.tools/python/3.12.13/cpython-3.12.13-linux-x86_64-gnu/bin:$PATH
export IDF_TOOLS_PATH=/home/share/samba/open-components/tuyaopen/platform/ESP32/.espressif
source /home/share/samba/open-components/tuyaopen/platform/ESP32/esp-idf/export.sh

cd esp32/examples/iot_demo
idf.py set-target esp32s3     # 或换芯片
idf.py menuconfig             # iot_demo → WiFi SSID/password（必须配）
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

**版本约束**：必须用 **esp-idf v5.x**（mbedtls 3.x）。v6.2+ 的 mbedtls 4.1
（TF-PSA-Crypto）把 sha256/aes/rsa/md/ctr_drbg 等低层 crypto API 全部移入
private 头，core 的 TLS/crypto 层基于 mbedtls 3.x API，编译实证失败
（`mbedtls/rsa.h: No such file`）。

---

## 9. 实战踩坑记录（时间线，按遇到顺序）

1. **esp-idf 版本**：v6.2-dev 的 mbedtls 4.x 与 core 不兼容（见 §8）→ 换 v5.4；
   注意 v5.4 的 venv 是 py3.12、export.sh 会挑 PATH 里第一个 python3——
   系统只有 3.10 时会报 "virtual environment not found"。
2. **Kconfig 撞名**：core 的 `choice MBEDTLS_TLS_MODE` 等裸名符号与 esp-idf 的
   mbedtls Kconfig 同名，同一棵树里符号合并后**依赖环**（kconfgen 报
   "Dependency loop"）——教训：Kconfig 符号要有项目命名空间；检测脚本也别只
   grep `config`（漏 `choice` 会假阴性，我踩过）。
3. **include 顺序决定一切**：`threading_alt.h` 同名两份，每个编译单元命中哪份
   由 include 路径顺序决定（见 §4.3）——类型不匹配的报错（`SemaphoreHandle_t*`
   vs `void**`）就是顺序错位的症状。
4. **INTERFACE 传播污染**：core 给 mbedtls 目标注入的 config 目录，经宿主组件
   的依赖链传播到 esp-idf 组件（esp_common 编译失败）——根因修复在 core（§4.3）。
5. **循环引用**：esp-idf 组件库无 `--start-group`，core↔port 的环需要自己插
   链接组（§5）——症状是链接期一堆 `undefined reference to tkl_*`。
6. **rsync 排除模式**：`--exclude='CMakeLists.txt'` 匹配**任意层级**同名文件
   （差点漏同步 `tuya_transport/CMakeLists.txt`）→ 用 `'/CMakeLists.txt'` 限定根。
7. **esp-idf 的 `-Werror` 家族**（v5.4 默认 `-Werror=all` 等）：
   - `-Werror=comment`：core 的 `tal_thread.h` 悬空 `/**`（通用修复）；
   - `-Werror=format`：core 用 `%u` 打 `uint32_t`，而 xtensa 上 `uint32_t` 是
     `long unsigned`——32 位平台同宽无实害，port 侧 `-Wno-error=format` 放行
     （建议 core 后续改 `PRIu32`）；
   - `-Wincompatible-pointer-types`：§4.3 的类型不匹配。
8. **分区表格式**：`gen_esp32part.py` **不支持行内注释**（`0x10000  # 注释`
   会把注释当字段值解析失败）——注释要独立成行。
9. **组件管理器 vs 本地组件**：原工程用 `idf_component.yml` 从 git 拉 core
   （vendored 到 managed_components + hash 校验），core 刷新后改为
   `components/tuyaopen-core` 本地组件（离线可构建、无 hash 维护），
   删除 `idf_component.yml`/`dependencies.lock`/`managed_components`。
10. **sdkconfig 是产物**：原工程把 `sdkconfig` 误提交了（构建产物，随环境漂移）
    ——已移除并加 `.gitignore`；手写配置只放 `sdkconfig.defaults`。

---

## 10. 速查：文件地图

| 想了解 | 看这里 |
|---|---|
| port 各卡实现 | `esp32/port/src/tkl_*.c`（每个文件头部注释写了与 linux 的差异） |
| core 的卡契约 | `components/tuyaopen-core/iot-core/tal_system/include/tal_*_ops.h`、`tal_network.h` 尾部 |
| esp32 构建接入 | `components/tuyaopen-port/CMakeLists.txt`（mbedtls 接入 + 链接组）、`components/tuyaopen-core/CMakeLists.txt`（组件入口） |
| 同步与差异清单 | `esp32/README.md`（core 同步命令 + 平台差异文件表 + 通用修复清单） |
| demo 主流程 | `esp32/examples/iot_demo/main/tuya_main.c`（与 linux 版逐段一致）、`wifi_sta.c` |
| 构建配置 | `esp32/examples/iot_demo/{sdkconfig.defaults,partitions.csv,main/Kconfig.projbuild}` |
| 平台差异判定原则 | 本文 §6 |
