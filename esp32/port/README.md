# esp32 port（TKL 适配层）

本目录是 ESP32 目标的 TKL（Tuya Kernel Layer）适配，基于 **esp-idf / FreeRTOS**，
与 `linux/port` 同构（**ops 卡模式**：core 的 `tal_*.c` 经 extern 引用
`tal_*_ops_platform` 卡完成分发，port 组装这些卡）。

## 当前状态

| 文件 | 内容 | 对应 core 契约 |
|---|---|---|
| `src/tkl_runtime.c` | 主卡：log/memory/system/semaphore/mutex/thread/queue（FreeRTOS/esp-idf 原语） | `tal_api_ops.h` |
| `src/tkl_ota.c` | OTA 卡（esp_ota，双槽分区轮换） | `tal_ota_ops.h` |
| `src/tkl_kv.c` | KV 卡（NVS；超长 key 折叠为 FNV-1a 56bit hex） | `tal_kv_ops.h` |
| `src/tkl_netif.c` | netif 卡（esp_netif 事件驱动，链路 up = 已获 IP） | `tal_netif_ops.h` |
| `src/tkl_network.c` | network 卡（lwIP socket；局域网/对端服务成员随开关编译） | `tal_network.h` 的 `TAL_NETWORK_CARD_T` |

`include/` 为 TKL 域头（common/system/network/security），从 linux port 同步
（接口定义与各目标共享；security 三件套由 core 的 mbedtls_* 实现，port 只提供头）。

暂未适配（需要时补，与 linux port 的门控一致）：

- `tkl_fs`（p2p local_store 用；linux 为 POSIX 直包装）；
- `tkl_bt`（BLE；esp32 需换 NimBLE/Bluedroid 实现）；
- `media/tkl_ai_demo.h`（agent_demo 的音频输入契约）。

## 构建接入

esp-idf 工程经 `examples/*/components/tuyaopen-port` 包装组件引入本目录：

- 包装组件 `REQUIRES tuyaopen-core esp_wifi esp_event esp_netif nvs_flash lwip esp_timer app_update`
  （保证 core 先处理、idf 组件可用）；
- `add_subdirectory(esp32/port)` 建 plain CMake 静态库（普通目标不自动获得 idf
  组件头，由包装组件 `target_link_libraries(... idf::xxx)` 注入）；
- 本目录 `CMakeLists.txt` 收集源码/头、链接 `tal_system`（core 头）、按
  `CONFIG_ENABLE_*` 注入开关宏（core 的宏不传播到 port 目标）、把 port 头注入
  `tuyaopen-headers`（core 编译单元经 `tuya_kconfig.h` 取板级宏）。
