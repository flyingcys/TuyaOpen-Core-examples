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
| `TAL_API_OPS_T`（主卡：log/memory/system/semaphore/mutex/thread/queue） | `iot-core/tal_system/include/tal_api_ops.h` | `port/src/tkl_runtime.c` 文件尾 | `tal_api.c` / `tal_thread.c` / `tal_system.c` |
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

### 2.3 直调符号的例外（不走卡）

少数符号是**直接链接**的（core 里 `extern` 后直调，不经卡）：

- `tkl_system_malloc` / `tkl_system_free`：core 的 `tuya_transport`（mbedtls 适配层、
  base64 等）与 `tuya_utilities` 直调（`mbedtls_hash.c` 等）；
- `tkl_mutex_create_init` / `lock` / `unlock` / `release`：core 的
  `iot-core/utilities/tuya_queue.c` 经宏直调；
- `tkl_thread_get_id`、`tkl_system_psram_malloc/free`：仅 p2p（`ENABLE_TUYA_P2P` 门控）。

**这些符号由 port 以普通全局符号提供**（不是 static 成员），链接时必须能找到——
这也是 §5"循环引用"的成因之一。

### 2.4 TKL 头与 core 的关系

- TKL 接口头（`tkl_*.h`）**全部在 port 里**（`port/include/{common,system,network,security}/`），
  core 内没有任何 `tkl_*.h`；core 只引用**部分** TKL 头（`tkl_memory.h`/`tkl_hash.h`/
  `tkl_symmetry.h`/`tkl_asymmetrical.h`/`tkl_mutex.h` 等，用于直调声明与 mbedtls 适配）。
- port 头经构建系统**注入 core 的环境基底**（`tuyaopen-headers`），core 编译单元
  才能 include 到（详见 §3.3）。
- 各平台 port 的 TKL 接口集合**同构**（esp32 port 的 15 个头从 linux port 同步而来，
  不含平台多余接口）——这是"不新扩"原则的落地。

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

## 4. 核心机制三：mbedtls 接入（THREADING_ALT 专题）

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
| 注册时机 | 运行时 `tuya_tls_init()`（`tuya_tls.c:383`） | 启动期**构造函数** `mbedtls_rom_osi_functions_init`（`__attribute__((constructor))`，`mbedtls_rom_osi.c:447`） |
| 服务对象 | core 的 TLS 栈（`tuya_tls.c`） | esp-idf 的 mbedtls 消费者（wifi/wpa_supplicant/esp-tls 等） |

**谁生效**：构造函数在启动早期注册 ROM 回调 → 之后 `tuya_tls_init()` 覆盖为
tal_mutex 回调 → **全局生效的是 tal_mutex 版**（esp32 的 tal_mutex 底层就是
`xSemaphoreCreateRecursiveMutex`，与 ROM 版同为 FreeRTOS 信号量，句柄二进制兼容）。

**风险点（如实记录，未真机验证）**：两套的**递归性不同**——tal 版用递归信号量
（`xSemaphoreTakeRecursive`），ROM 版用非递归（`xSemaphoreTake`）。全局回调被
覆盖后，esp-idf 的 mbedtls 消费者（如 wpa_supplicant）的 mutex 会用**递归 API
操作非递归信号量**（FreeRTOS 对这类混用行为未定义）；上游 esp32 工程同样如此
配置且长期运行，推测 mbedtls 的锁用法不触发递归路径，但机制上不干净。
**改善方向**（通用改动，可讨论）：core 增加"宿主已提供 THREADING_ALT 回调时
不再覆盖"的开关（例如随 `CONFIG_ENABLE_PLATFORM_MBEDTLS` 联动）。

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

## 5. 核心机制四：链接模型（循环引用）

### 5.1 环从哪来

- core 的库引用 port 的符号：`tkl_system_malloc/free`、`tkl_mutex_*`（§2.3 直调）、
  以及**卡的实例**（`tal_*_ops_platform`，core 的 `tal_*.c` extern 引用）；
- port 的库引用 core 的符号：`tal_mutex_*`（卡的实现里用 core 的 API）、
  `tal_log` 等；
- 两个方向的引用都真实存在 → **静态库循环依赖**。

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
