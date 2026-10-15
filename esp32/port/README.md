# esp32 port

本目录是 ESP32 目标的 port 适配，基于 **esp-idf / FreeRTOS**，
与 `linux/port` 同构（**ops 卡模式**：core 的 `tal_*.c` 经 extern 引用
`tal_*_ops_platform` 卡完成分发，port 组装这些卡）。

## 当前状态

| 文件 | 内容 | 对应 core 契约 |
|---|---|---|
| `src/port_runtime.c` | 主卡：log/memory/system/semaphore/mutex/thread/queue（FreeRTOS/esp-idf 原语） | `tal_api_ops.h` |
| `src/port_kv.c` | KV 卡（NVS；超长 key 折叠为 FNV-1a 56bit hex） | `tal_kv_ops.h` |
| `src/port_netif.c` | netif 卡（esp_netif 事件驱动，链路 up = 已获 IP） | `tal_netif_ops.h` |
| `src/port_network.c` | network 卡（lwIP socket；局域网/对端服务成员随开关编译） | `tal_network.h` 的 `TAL_NETWORK_CARD_T` |

> OTA 已移出 port：core 改为回调注入制（`tal_ota_register_cbs`，见
> `tal_ota.h`），esp32 实现由应用层提供（`examples/iot_demo/main/ota_app.c`），
> 不注册的应用即不支持 OTA（`tal_ota_*` 返回 `OPRT_NOT_SUPPORTED`）。

> 主卡还带**宏隔离**的 ex-RAM 成员（`.ex_ram_malloc/.ex_ram_free`）：参照平台
> ESP32 的 `tkl_system_psram_malloc`（`heap_caps_malloc(MALLOC_CAP_SPIRAM)`）。
> 开关 `CONFIG_ENABLE_EXT_RAM`（`components/tuyaopen-core/Kconfig`，**默认 n**，
> 与 linux 侧同名同门控）；开启需 sdkconfig 开 `CONFIG_SPIRAM` 且有 PSRAM 硬件
> ——否则 `heap_caps_malloc` 无 SPIRAM 能力、恒返回 NULL。不开时卡结构与 linux
> 侧一致（成员不存在，零开销）。

`include/` 原为核心接口域头（common/system/network/security）。2026-10 port 去 tkl 化后
只剩 `common/`（4 个跨目标公共类型头）：
- `security/` 三件套（tkl_hash/tkl_symmetry/tkl_asymmetrical）已删——零 includer，
  哈希/对称/非对称原语由 core 的 mbedtls_* 实现；
- `system/` 的 mutex/queue/semaphore/thread/system/memory/fs 与 `network/` 的头均已删除，
  类型与声明内联进对应 .c（它们各只有单文件 includer）；`tkl_memory.h` 一度改名保留为
  `port_memory.h`（port_kv.c 跨文件调用 port_runtime.c 的 malloc/free），2026-10 收口后
  port_kv.c 改用 tal_malloc/tal_free、memory/mutex 转 static，该头随之删除——两侧 port
  布局对齐（linux 侧同步转 static）。

暂未适配（需要时补，与 linux port 的门控一致）：

- `port_fs`（p2p local_store 用；linux 为 POSIX 直包装）；
- `port_bt`（BLE；esp32 需换 NimBLE/Bluedroid 实现）；
- `media/port_ai_demo.h`（agent_demo 的音频输入契约）。

## 构建接入

esp-idf 工程经 `examples/*/components/tuyaopen-port` 包装组件引入本目录：

- 包装组件 `REQUIRES tuyaopen-core esp_wifi esp_event esp_netif nvs_flash lwip esp_timer app_update`
  （保证 core 先处理、idf 组件可用）；
- `add_subdirectory(esp32/port)` 建 plain CMake 静态库（普通目标不自动获得 idf
  组件头，由包装组件 `target_link_libraries(... idf::xxx)` 注入）；
- 本目录 `CMakeLists.txt` 收集源码/头、链接 `tal_system`（core 头）、按
  `CONFIG_ENABLE_*` 注入开关宏（core 的宏不传播到 port 目标）、把 port 头注入
  `tuyaopen-headers`（core 编译单元经 `tuya_kconfig.h` 取板级宏）。
