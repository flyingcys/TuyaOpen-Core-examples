# iot_demo（ESP32 / esp-idf）

连涂鸦云、扫码激活、DP 收发的 ESP32 示例，与 `linux/examples/iot_demo` 同构
（demo 代码从 linux 版逐段移植），差异仅在网络接入：core 已无 wifi 抽象，
WiFi 连接由本 demo 完成（`main/wifi_sta.c`），连上后"当有线网络处理"
（port 的 `tkl_netif` 监听 IP 事件上报链路，socket 走 lwIP），后续流程与
linux 完全一致。

## 结构

```
iot_demo/
  CMakeLists.txt          # esp-idf 工程入口（components/ 自动发现）
  components/
    tuyaopen-core/        # core 快照（从 linux/tuyaopen-core 同步，vendored，见 ../../README.md）
    tuyaopen-port/        # 包装组件：REQUIRES core + idf 组件，引入 ../../port
  main/
    main.c                # app_main：nvs_flash_init → tuya_app_main()
    tuya_main.c           # 应用主流程（linux 版移植 + wifi_sta_init）
    wifi_sta.c/.h         # WiFi station 连接（esp-idf 原生 API，断线退避重连）
    reset_netcfg.c/.h     # 连续复位 3 次清配网（与 linux/p2p 版逐行一致）
    tuya_config.h         # PID/授权码（与 linux iot_demo 同一产品；secrets 头可覆盖）
    Kconfig.projbuild     # menuconfig：WiFi SSID/password
  partitions.csv          # 4MB flash：64KB nvs + OTA 双槽
  sdkconfig.defaults      # 默认配置（core 开关、目标芯片、分区）
```

## 构建与烧录

**esp-idf 版本要求 v5.x**（mbedtls 3.x；v6.2+ 的 mbedtls 4.x/TF-PSA-Crypto
把低层 crypto API 移入 private，与 core 不兼容）。推荐用上游 tuyaopen 自带的
esp-idf（`platform/ESP32/esp-idf` v5.4），环境导出方式见 `../../README.md`。

```bash
source <esp-idf>/export.sh           # v5.x；见 ../../README.md 的完整导出示例
cd esp32/examples/iot_demo
idf.py set-target esp32s3            # 或换目标芯片（esp32/esp32c3/...）
idf.py menuconfig                    # iot_demo → WiFi SSID / WiFi password
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 配置说明

- **WiFi**：`menuconfig` 的 `iot_demo` 菜单（`CONFIG_DEMO_WIFI_SSID` /
  `CONFIG_DEMO_WIFI_PASSWORD`）。**未配置（空 SSID）时的行为**：
  `wifi_sta_init` 早退并打印 `ssid empty, set it in menuconfig...`；事件循环
  未建立 → 后续 `tkl_netif` 的链路监听注册失败（再打印一条
  `netif event handler register failed ... (event loop created?)`）→ 链路恒
  down、不发起云连接。行为安全，串口那两条提示即为原因说明（PK 评审 E1）。
- **凭据**：`tuya_config.h` 已带与 linux iot_demo 相同的 PID/授权码；
  换产品改 `tuya_config.h`，或用 `tuya_config_secrets.h`（复制
  `tuya_config_secrets.h.example`，该文件已被 .gitignore 忽略）。
- **core 开关**：`menuconfig` 的 `tuyaopen-core` 菜单（QRCODE 默认开；
  P2P/AI/BT 默认关）。

## 行为

1. 上电 → NVS 初始化 → WiFi 连接（断线自动退避重连）；
2. 未激活：连上涂鸦 MQTT 直连服务后串口打印二维码，涂鸦 APP 扫码激活；
3. 已激活：连云端 MQTT，DP 收发（收到 DP 原样上报回显）；
4. 连续复位 3 次（每次间隔 <5s）触发 `tuya_iot_reset` 清配网并重启。

## 已知限制

- OTA 通道已接（`tkl_ota.c` + 双槽分区表），但未做真机升级验证；
- `tkl_fs`/`tkl_bt`/`tkl_ai_demo`（media）未适配——p2p/agent demo 需要时补；
- 仅 station 模式连接预置 AP；无配网（BLE/AP netcfg 随 netmgr 移除，如需要
  走 BLE 配网需另行接入）。
