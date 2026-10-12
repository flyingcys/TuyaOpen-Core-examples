# esp32 示例

`examples/iot_demo/` 是 ESP32 平台的示例工程（esp-idf）：连涂鸦云、扫码激活、
DP 收发。与 linux 侧同构（`linux/examples/iot_demo`），demo 代码从 linux 版逐段
移植，差异仅在网络接入（见下）。

## 结构

```
esp32/
  port/                       # ESP32 TKL 适配（ops 卡模式，与 linux port 同构）
    include/                  #   TKL 域头（从 linux port 同步）
    src/tkl_runtime.c         #   主卡：log/memory/system/sem/mutex/thread/queue（FreeRTOS）
    src/tkl_ota.c             #   OTA 卡（esp_ota）
    src/tkl_kv.c              #   KV 卡（NVS + 超长 key 映射）
    src/tkl_netif.c           #   netif 卡（esp_netif 事件驱动）
    src/tkl_network.c         #   network 卡（lwIP socket）
  examples/iot_demo/
    components/
      tuyaopen-core/          #   core 快照（从 linux/tuyaopen-core 同步，vendored）
      tuyaopen-port/          #   包装组件：REQUIRES core + idf 组件，引入 ../../port
    main/                     #   demo（tuya_main.c 从 linux 版移植 + wifi_sta 连接）
```

## 网络模型："WiFi 连上即当有线"

core 已无 wifi 抽象（`tal_wifi`/`tkl_wifi` 随 netmgr 整体移除）：

- **demo 层**（`main/wifi_sta.c`）用 esp-idf 原生 API 连 AP（SSID/密码走 menuconfig）；
- **port 层**（`tkl_netif.c`）监听 `IP_EVENT_STA_GOT_IP` / `WIFI_EVENT_STA_DISCONNECTED`，
  把链路状态报给 core（`tal_netif_set_link_cb`，链路 up = 已获 IP）；
- 之后流程与 linux 完全一致：socket 走 `tkl_network`（lwIP），云连接/激活/DP 复用
  同一套 core。

## core 同步

`components/tuyaopen-core/` 是从 `linux/tuyaopen-core`（子模块）同步的 vendored
快照，**不随 linux 侧自动联动**。core 里影响跨平台能力的问题**在 core 仓库内
通用修复**（不搞芯片特例）；esp32 专有的接入（idf 组件顺序、mbedtls 目标桥接）
全部在 port / 平台侧。同步方式：

```bash
rsync -a --exclude='.git' --exclude='.gitmodules' --exclude='third_party/mbedtls' \
      --exclude='/CMakeLists.txt' \
      linux/tuyaopen-core/ esp32/examples/iot_demo/components/tuyaopen-core/
```

- `third_party/mbedtls` 除外：esp32 用 esp-idf 的 mbedtls（`CONFIG_ENABLE_PLATFORM_MBEDTLS=y`）；
- `third_party` 其余库（cJSON/coreHTTP/coreMQTT/backoffAlgorithm）为 vendored 内容
  （linux 侧为 submodule，同步后以普通目录入库）；
- 排除模式注意：`--exclude='CMakeLists.txt'` 会匹配**任意层级**的同名文件，
  根目录限定必须写 `--exclude='/CMakeLists.txt'`。

### 平台差异文件（同步排除，esp32 侧维护）

| 文件 | 差异 | 原因 |
|---|---|---|
| `CMakeLists.txt`（组件入口） | `idf_component_register(... REQUIRES mbedtls)` | 保证 esp-idf 的 mbedtls 组件先于 core 处理，core 模块里 `target_link_libraries(... mbedtls ...)` 裸名在组件处理期解析到 esp-idf 的 mbedtls 库目标（esp-idf 专有的组件顺序机制，不通用，故留在平台侧） |

已回 core 仓库的跨平台通用修复（正常同步，非平台定制）：

- `tal_thread.h`：删悬空 `/**`（吞掉 C++ 守卫、触发 esp-idf `-Werror=comment`）；
- `iot-core/tuya_transport/Kconfig`：mbedtls 细节段裸名符号（`choice
  MBEDTLS_TLS_MODE`/`config MBEDTLS_TLS_ENABLED`/`MBEDTLS_DEBUG_LEVEL`）加
  `ENABLE_` 前缀——裸名与任何带 mbedtls Kconfig 的宿主撞名，同树合并后成依赖环；
- `iot-core/tuya_transport/CMakeLists.txt`：mbedtls 配置头注入加
  `NOT CONFIG_ENABLE_PLATFORM_MBEDTLS` 条件——平台 mbedtls 模式下原会误把 core 的
  config 目录注入宿主 mbedtls 目标、污染宿主组件。
- **core 与 tkl 层解耦（2026-10）**：core 在编代码不再直调任何 `tkl_*`——`tuya_queue`/
  `tuya_ringbuf` 上移 `iot-core/tal_system/` 并改走 `tal_mutex_*`/`tal_malloc`/`tal_free`；
  crypto 透传层合并（`mbedtls_hash.c`/`mbedtls_symmetry.c` 即 `tal_*` 实现本体，句柄类型改
  `TAL_HASH_HANDLE`/`TAL_SYMMETRY_HANDLE`）；p2p 清 psram、`thread_get_id` 走主卡新成员
  （新 TAL API `tal_thread_get_id`）。**2026-10 二次清理**：`ENABLE_EXT_RAM` 死块、
  `AI_HEAP/AI_STACK_IN_PSRAM` 守卫与 Kconfig、`THREAD_CFG_T.psram_mode` 死字段、
  ringbuf 的 `OVERFLOW_PSRAM_*` 枚举全部删除（core 内零 psram/EXT_RAM 字样；
  port 的两个类型枚举与 demo/文档的历史注释亦已清零）。
  两 port 同步接线。详见 `porting-study.md` §2.3。

## 构建（需要 esp-idf 环境）

**esp-idf 版本要求：v5.x**（mbedtls 3.x）。v6.2+ 的 mbedtls 已重构为
TF-PSA-Crypto 4.x（sha256/aes/rsa/md/ctr_drbg 等低层 crypto API 全部移入
private 头，公开面只剩 ssl/x509 + PSA），core 的 TLS/crypto 层基于 mbedtls
3.x API，在 6.x 上编译失败。推荐直接用上游 tuyaopen 自带的 esp-idf
（`platform/ESP32/esp-idf`，v5.4，工具链在其 `.espressif` 下）：

```bash
# 上游 ESP32 的 esp-idf v5.4（mbedtls 3.x）；python 3.12 用 tuyaopen 自带
export PATH=/path/to/tuyaopen/.tools/python/3.12.13/cpython-3.12.13-linux-x86_64-gnu/bin:$PATH
export IDF_TOOLS_PATH=/path/to/tuyaopen/platform/ESP32/.espressif
source /path/to/tuyaopen/platform/ESP32/esp-idf/export.sh

cd esp32/examples/iot_demo
idf.py set-target esp32s3       # 或换目标芯片
idf.py menuconfig               # iot_demo → WiFi SSID/password
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 与 linux 侧的 port 对应

| 能力 | linux port | esp32 port |
|---|---|---|
| 主卡（OS 原语） | tkl_runtime.c（pthread/POSIX） | tkl_runtime.c（FreeRTOS/esp-idf） |
| KV | tkl_kv.c（littlefs+AES，文件模拟 flash） | tkl_kv.c（NVS + 超长 key 映射） |
| netif | tkl_netif.c（ioctl 3s 轮询） | tkl_netif.c（esp_netif 事件） |
| network | tkl_network.c（POSIX socket） | tkl_network.c（lwIP socket） |
| OTA | tkl_ota.c（写文件） | tkl_ota.c（esp_ota，双槽分区） |
| fs/bt/media | tkl_fs/tkl_bt/tkl_ai_demo | 暂未适配（p2p/agent demo 需要时补） |
