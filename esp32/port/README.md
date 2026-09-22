# esp32 port（TKL 适配层）

本目录是 ESP32 目标的 TKL（Tuya Kernel Layer）适配，基于 **esp-idf / FreeRTOS** API 实现。

## 当前状态

- `include/`：TKL API 头文件（与各目标共享的接口定义，从 linux port 同步）
- `tuya_kconfig.h.in`：板级宏模板（`OPERATING_SYSTEM 98` = SYSTEM_FREERTOS）
- **TKL 实现尚未补齐**：需要在 `src/`（自建）下按 `include/` 中的头文件实现各 `tkl_*` 接口

## 优先级建议（对照 tuyaopen 的 ESP32 平台）

最小可跑通云端的最小集：

1. `tkl_system.c` / `tkl_memory.c` / `tkl_thread.c` / `tkl_mutex.c` / `tkl_semaphore.c` / `tkl_queue.c`（系统原语，FreeRTOS API 直映射）
2. `tkl_output.c`（printf → ESP_LOG）
3. `tkl_fs.c`（esp-idf FATFS/LittleFS/NVS，供 tal_kv）
4. `tkl_network.c`（lwIP socket 封装）
5. WiFi 场景：`tkl_wifi.c`（esp_wifi）+ netcfg 所需接口
6. `tkl_sleep.c` / `tkl_timer` 相关

实现放本目录任意子路径即可（构建按 `*.c` 递归收集，参考 `linux/port` 的布局）；不需要的接口对应模块用 CMake 开关关掉（如 `CONFIG_ENABLE_BLUETOOTH=OFF` 时无需 `tkl_bt`）。

参考实现：tuyaopen 仓库 `platform/<芯片>/tuyaos_adapter/`。
