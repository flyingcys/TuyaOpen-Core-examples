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
快照，**不随 linux 侧自动联动**。**core 仓库保持芯片无关、零平台定制**；esp32
的适配全部在 port / 平台侧（下面的差异文件 + `components/tuyaopen-port`）。
同步方式：

```bash
rsync -a --exclude='.git' --exclude='.gitmodules' --exclude='third_party/mbedtls' \
      --exclude='CMakeLists.txt' \
      --exclude='iot-core/tuya_transport/Kconfig' \
      --exclude='iot-core/tuya_transport/config/threading_alt.h' \
      linux/tuyaopen-core/ esp32/examples/iot_demo/components/tuyaopen-core/
```

- `third_party/mbedtls` 除外：esp32 用 esp-idf 的 mbedtls（`CONFIG_ENABLE_PLATFORM_MBEDTLS=y`）；
- `third_party` 其余库（cJSON/coreHTTP/coreMQTT/backoffAlgorithm）为 vendored 内容
  （linux 侧为 submodule，同步后以普通目录入库）。

### 平台差异文件（同步排除，esp32 侧维护）

| 文件 | 差异 | 原因 |
|---|---|---|
| `CMakeLists.txt` | `idf_component_register(... REQUIRES mbedtls)` | 保证 esp-idf 的 mbedtls 组件先于 core 处理，core 模块里 `target_link_libraries(... mbedtls ...)` 裸名在组件处理期解析到 esp-idf 的 mbedtls 库目标 |
| `iot-core/tuya_transport/Kconfig` | 删 `ENABLE_CUSTOM_CONFIG` 段、`MBEDTLS_DEBUG_LEVEL` 改名 `ENABLE_MBEDTLS_DEBUG_LEVEL` | 裸名符号（`choice MBEDTLS_TLS_MODE`/`MBEDTLS_TLS_ENABLED` 等）与 esp-idf 的 mbedtls Kconfig 同名，同一 Kconfig 树合并后成依赖环（kconfgen 报错）；该段为死配置（宏注入表不含，linux 行为不变） |
| `iot-core/tuya_transport/config/threading_alt.h` | 删除 | esp32 的 THREADING_ALT 头统一用 esp-idf ROM 版（`mbedtls/port/mbedtls_rom/threading_alt.h`，由 `tuyaopen-port` 注入 core 环境基底），避免两套 `mbedtls_threading_mutex_t` 结构混用 |

core 仓库里的通用修复（非平台定制，正常同步）：`tal_thread.h` 悬空 `/**`
（吞掉 C++ 守卫、触发 esp-idf `-Werror=comment`）。

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
