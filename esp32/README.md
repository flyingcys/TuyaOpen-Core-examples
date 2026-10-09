# esp32 示例

`examples/switch_demo/` 是 ESP32 平台的示例工程：经 idf 组件管理器把
`tuyaopen-core` 拉到 `managed_components/`（**vendored 快照**，不随 linux 侧的
core 子模块联动）。

## core 刷新后的迁移代价（2026-10-09 记录）

本示例当前照常构建；一旦把 `managed_components/tuyaopen-core` 刷到较新的 core
提交（linux 侧 `b638625` 起，WiFi 相关已整体移除），以下符号会失效——**注意
其中多数是静默失效、不报编译错误**，需要按需处理：

| 失效符号 | 层级 | 表现 |
|---|---|---|
| `CONFIG_ENABLE_WIFI` | CMake option（`TuyaOpenOptions.cmake`） | 传入时 CMake 报 "Manually-specified variables were not used"（明确提示，不静默） |
| `CONFIG_ENABLE_WIFI_ULTRA_LOWPOWER` | 同上 | 同上 |
| `ENABLE_WIFI` / `ENABLE_WIFI_ULTRA_LOWPOWER` | 编译宏（`tuyaopen-core.cmake` 宏注入表） | 不再定义 |
| `main/tuya_main.c:21`、`:264`、`:275` 的 `#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)` | 应用代码 | **恒假、不报错**——WiFi 分支静默消失（这正是最需要主动检查的一类） |

同批变更还包括：linux port `tuya_kconfig.h` 的 `WLAN_DEV` / `WLAN_AP` /
`WIFI_DB_PATH` 板级宏删除、p2p 的 WiFi 取本机 IP 分支删除、`tal_wifi` / `tkl_wifi`
实现早已移除（core 侧随死接口清理 `b606a2a`）。若 ESP32 需要 WiFi：由平台自行
提供 `tal_wifi`，并按 git 历史恢复各消费点（p2p 本地 IP、`ENABLE_WIFI` 守卫等）。

保留的 WiFi 字样只剩 Tuya **协议常量**（BLE 配网协议帧与能力位、LAN
`FRM_CFG_WIFI_INFO`、云状态 `TUYA_STATUS_WIFI_CONNECTED`、`OPRT_SVC_WIFI_*`
错误码）——"去掉 WiFi 实现"与"去掉 WiFi 协议概念"是两件事，后者属云端契约，
不随平台实现删除。
