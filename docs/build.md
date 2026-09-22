# tuyaopen-core 手工编译指南

更新日期：2026-09-22。本指南覆盖 tuyaopen-core（五分组：`core-common` / `core-communication` / `core-iot` / `core-ai` / `core-p2p`）与本仓库（port + 示例）的整体构建；文档放在 examples 仓库，构建入口也在这里。

> **架构说明**：`tuyaopen-core` 是纯净的独立模块——模块 CMake 只含自己的源码与显式声明的依赖，不感知 port；port 与示例在本仓库（`tuyaopen-core-examples`），按目标组织（`linux/`、`esp32/`、`stm32/`...）。
>
> **配置头是静态的**：`tuya_kconfig.h` 在各 port 的 `include/common/` 下（`#ifndef` 保护，手工可改：板级宏、参数默认值、注释形式的开关）。**开关宏不由任何头承载**，编译期传入：
> - CMake 用户：`-DCONFIG_ENABLE_WIFI=ON`（core 根 CMakeLists 自动转成 `-DENABLE_WIFI=1` 注入全部模块）
> - 手工编译用户：`-DENABLE_WIFI=1`（只传要开的，OFF = 不定义）或取消 port 静态头里对应注释
>
> 宿主接线两步（完整范例见 `linux/examples/switch_demo`）：
> ```cmake
> add_subdirectory(${TUYAOPEN_CORE_DIR} ...)                        # 1. core（开关宏自动注入）
> add_subdirectory(${TUYAOPEN_PORT_DIR} ...)                        # 2. port（自建目标 + 自注入头）
> # app 链接：一个组同时包 core 库与 port（解析 TAL/云服务与 TKL 的循环引用）
> target_link_libraries(app PRIVATE
>     -Wl,--start-group ${TUYAOPEN_CORE_LIBS} $<TARGET_FILE:tuyaopen-port> -Wl,--end-group
>     tuyaopen-port)
> ```

## 环境要求

| 依赖 | 说明 |
|------|------|
| CMake ≥ 3.16 | 标准 CMake 流程，无私有协议 |
| GCC 或 Clang | GNU 工具链已验证（gcc 11.x） |
| libglib2.0-dev、libbluetooth-dev、dbus 开发包 | 仅开启 `CONFIG_ENABLE_BLUETOOTH` 时需要（BLE 走 BlueZ + GDBus） |
| wpa_supplicant、bluez | 运行时依赖（编译不需要），WiFi/BLE 实际工作时要在跑 |

Ubuntu 安装示例：

```bash
sudo apt install cmake build-essential libglib2.0-dev libbluetooth-dev
```

## 快速开始（默认配置：仅 WIRED）

入口是本仓库的示例工程（tuyaopen-core 仓库本身不含 port，无独立可执行产物）：

```bash
cd <tuyaopen-core-examples>/linux/examples/switch_demo

# 1. 配置（凭据可选：不传则用 src/tuya_config.h 默认值或 tuya_config_secrets.h）
cmake -B build

# 2. 编译
cmake --build build -j$(nproc)

# 3. 运行
./build/switch_demo
```

示例工程默认按兄弟目录推导 core（`../../../../tuyaopen-core`）与 port（`../../port`）路径，布局不同用 `-DTUYAOPEN_CORE_DIR=... -DTUYAOPEN_PORT_DIR=...` 覆盖。

假凭据也能启动并打印日志，但要真正连上涂鸦云激活设备，需要有效凭据（PID 对应产品需开通 OpenSDK）。

### 凭据的两种传法与推荐

| 方式 | 写法 | 说明 |
|------|------|------|
| CMake 变量（推荐） | `-DTUYA_PRODUCT_ID=xxx` | 进入缓存，配置一次后不再依赖环境 |
| 环境变量 | `export TUYA_PRODUCT_ID=xxx`（三个同名） | 配置时读取 |
| 都不提供 | — | 用 `src/tuya_config.h` 的 `#ifndef` 默认值（占位符凭据），或放一份 `tuya_config_secrets.h` |

凭据不是必需项（不传可正常编译运行，只是连不上真实云端）。推荐 `-D` 的原因：源文件列表用了 `CONFIGURE_DEPENDS`，增量构建时文件增删会自动触发重新 configure，环境变量可能不在而回退到占位符凭据，`-D` 进缓存则不受影响。

## 功能开关

配置时用 `-DCONFIG_xxx=ON/OFF` 控制。默认全部 OFF（`CONFIG_LITTLE_END` 默认 ON）。

| 开关 | 默认 | 作用 | 依赖/备注 |
|------|------|------|-----------|
| `CONFIG_ENABLE_WIRED` | OFF | `tal_wired` + cloud 的 netconn_wired | demo 里已内置 ON |
| `CONFIG_ENABLE_WIFI` | OFF | `tal_wifi` + cloud 的 netcfg/netconn_wifi + port 的 tkl_wifi 实现 | 运行时需 wpa_supplicant |
| `CONFIG_ENABLE_WIFI_ULTRA_LOWPOWER` | OFF | `tal_wifi_ulp` 低功耗管理 | 需同时开 WIFI |
| `CONFIG_ENABLE_BLUETOOTH` | OFF | `tal_bluetooth` + cloud 的 ble 源 + port 的 tkl_bt 实现 | 编译需 glib/bluez/dbus 开发包 |
| `CONFIG_ENABLE_NIMBLE` | OFF | `tal_bluetooth` 附带 NimBLE 源 | 仅蓝牙开启时有意义 |
| `CONFIG_ENABLE_CELLULAR` | OFF | 蜂窝 | **Linux 上强制 OFF**（TKL 无蜂窝支持，会打 Warning） |
| `CONFIG_ENABLE_FILE_SYSTEM` | OFF | ON 用平台 tkl_fs，OFF 编入 littlefs | |
| `CONFIG_ENABLE_PLATFORM_MBEDTLS` | OFF | ON 时不编仓库自带 mbedtls-3.1.0 | |
| `CONFIG_ENABLE_DEVICE_TIMER` | OFF | cloud 的 tuya_device_timer.c | |
| `CONFIG_ENABLE_TUYA_P2P` | OFF | core-p2p 全部基础子库 | |
| `CONFIG_ENABLE_LOCAL_STORE` | OFF | core-p2p/local_store | 需开 P2P |
| `CONFIG_ENABLE_IPC_RING_BUFFER` | OFF | core-p2p/svc_ring_buffer | 需开 P2P |
| `CONFIG_ENABLE_AI_COMPONENTS` | OFF | core-ai 组件层 | **当前编不过**：缺 tkl_kws.h 等 4 个头 |
| `CONFIG_ENABLE_COMP_AI_AUDIO/MCP/VIDEO` | OFF | 各 AI 组件 | 需开总开关，同样受上条限制 |
| `CONFIG_ENABLE_TUYA_CODEC(_OPUS/_OPUS_IPC/_SPEEX)` | OFF | tuya_ai_service 编码器 | |
| `CONFIG_ENABLE_QRCODE` | OFF | core-common/common/qrcode 模块（APP 绑定二维码输出） | 有线绑定的 demo 必开（demo 里 `ENABLE_QRCODE` 宏联动） |
| `CONFIG_LITTLE_END` | ON | pjproject 字节序 | |

开关值支持 `y/n/1/0/yes/no/on/off` 归一化（兼容 Kconfig 习惯），但推荐直接用 `ON/OFF`。

## port 来源与多芯片

port（TKL）与 core 分仓库，按目标芯片放在 examples 仓库：

```
tuyaopen-core-examples/
  linux/port/       # LINUX 实现：tkl_*.c + tkl_wifi/ + tkl_bt/ + include/ + CMakeLists.txt
  esp32/port/       # ESP32 实现（骨架，待补）
  stm32/port/       # STM32 实现（骨架）
```

每个 port 目录**自包含**：源码 + 头 + 自己的 `CMakeLists.txt`（收集源码、按 core 开关裁剪 tkl_wifi/tkl_bt、把 port 头注入 `tuyaopen-headers`），宿主 `add_subdirectory(port)` 即得 `tuyaopen-port` 目标。port 无源码时为空 INTERFACE + WARNING。

新增芯片：建 `<目标>/port/`，放该芯片的 TKL `.c`（头文件可从 `linux/port/include/` 同步）+ 参考任一 port 写 `CMakeLists.txt` + 在 `include/common/` 放静态 `tuya_kconfig.h`（板级宏），示例工程里 `-DTUYAOPEN_PORT_DIR` 指过去即可。

## 用 Kconfig / menuconfig 配置（可选）

tuyaopen-core 仓库根提供了统一 Kconfig 入口 `Kconfig`，可作为子模块并入宿主的 Kconfig 树。所有布尔开关符号**不带** `CONFIG_` 前缀（与 tuyaopen 一致），kconfiglib 生成 `.config` 时自动加前缀，符号名与 CMake 开关一一对应、默认值一致。

### 宿主接入（一行）

宿主自己的 Kconfig 里 rsource tuyaopen-core 的入口即可：

```
mainmenu "my application"

rsource "third_party/tuyaopen-core/Kconfig"   # 路径相对宿主 Kconfig 所在目录
```

### 生成配置并喂给 CMake

```bash
# 需要 kconfiglib：pip3 install --user kconfiglib
export KCONFIG_CONFIG=./.config          # 可选，默认就是 ./.config
cd <宿主目录> && olddefconfig             # 全默认值生成 .config
guiconfig                               # 或 menuconfig 交互配置

# 把布尔开关转成 CMake 参数（core 的 TuyaOpenOptions.cmake 接受 y/1/on 并归一化）
FLAGS=$(grep -E '^CONFIG_ENABLE_.*=y|^CONFIG_LITTLE_END=y' .config | sed 's/^/-D/' | tr '\n' ' ')
cmake -B build $FLAGS -DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..
```

纯手工编译（不走 CMake）的用户：kconfiglib 以空前缀（`config_prefix=""`）生成的头与源码守卫风格直接匹配，可整体替代 port 的静态 `tuya_kconfig.h`。

### 入口结构

```
Kconfig                        # 统一入口：rsource 五个分组 + LITTLE_END
├── core-common/Kconfig        # libtls（含 ENABLE_PLATFORM_MBEDTLS）、tal_kv、tal_system 参数、qrcode
├── core-communication/Kconfig # ENABLE_WIRED/WIFI/BLUETOOTH/NIMBLE + tal_wifi_ulp
├── core-iot/Kconfig           # 安全等级、DEVICE_TIMER、CELLULAR(+APN)、BT_SERVICE 参数
├── core-ai/Kconfig            # AI_COMPONENTS 总开关 → ai_audio/ai_mcp/ai_video；tuya_ai_service 参数与 TUYA_CODEC 系列
└── core-p2p/Kconfig           # TUYA_P2P → LOCAL_STORE、IPC_RING_BUFFER
```

依赖关系已在 Kconfig 内表达（如 `ENABLE_WIFI_ULTRA_LOWPOWER` depends on `ENABLE_WIFI`，只开 ULP 不开 WIFI 会被自动压回）。

## 常用配置组合

```bash
# 最小（有线）
cmake -B build -DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..

# 网络全开（WiFi + 低功耗 + 蓝牙；cellular 在 Linux 上无效）
cmake -B build -DCONFIG_ENABLE_WIFI=ON -DCONFIG_ENABLE_WIFI_ULTRA_LOWPOWER=ON \
    -DCONFIG_ENABLE_BLUETOOTH=ON -DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..

# P2P（视频流）
cmake -B build -DCONFIG_ENABLE_TUYA_P2P=ON -DCONFIG_ENABLE_LOCAL_STORE=ON \
    -DCONFIG_ENABLE_IPC_RING_BUFFER=ON -DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..

# 内存调试
cmake -B build -DENABLE_ASAN=ON -DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..
```

以上组合均已在 Linux（gcc 11）验证可完整构建并启动运行。

## 日常操作

| 操作 | 命令 |
|------|------|
| 增量编译 | `cmake --build build -j$(nproc)` |
| 源文件增删后 | 同上（glob 自动触发重配置，凭据用 -D 则无需额外操作） |
| 修改开关组合 | `rm -rf build` 后按新参数重新配置 |
| 彻底清理 | `rm -rf build` |
| 查看实际生效的开关 | `cmake -B build -LH | grep CONFIG_` |

## 只配置 tuyaopen-core（可选）

tuyaopen-core 仓库根作为顶层工程时可以直接配置（纯净独立，零 WARNING——配置头与 TKL 头属于宿主职责，core 不感知）：

```bash
cmake -S <tuyaopen-core> -B /tmp/core-libs       # 配置 OK（模块静态库目标全部生成）
```

注意：脱离宿主接线时不要实际编译（`cmake --build`）——模块源码 include TKL 头与 `tuya_kconfig.h`，需要按文首两步接线后才可编译。完整流程参考本仓库的 switch_demo。

## 产物说明

- `build/switch_demo`：可执行文件
- `build/tuyaopen-core/core-*/lib*.a`：各模块静态库（libcjson、libtls、tal_*、tuya_cloud_service、pjproject 等）
- `<port>/include/common/tuya_kconfig.h`：静态配置头（板级宏 + 参数默认值，手工可改）；开关宏由编译期注入（`-DCONFIG_*` 或 `-DENABLE_*`）

## 已知限制（2026-09-22）

1. `CONFIG_ENABLE_CELLULAR` 在 Linux 上强制 OFF（port 无蜂窝 TKL 实现）。
2. `CONFIG_ENABLE_AI_COMPONENTS=ON` 编译不过：仓库缺 `tkl_kws.h`、`svc_ai_player.h`、`ai_manage_mode.h`、`tdl_camera_manage.h`，需要时从 tuyaopen 上游补齐。
3. WiFi/BLE 编译期不需要系统服务，但**运行时**分别依赖 wpa_supplicant 与 bluez/D-Bus 在运行。
4. 开关关闭时 port 里对应的 TKL 实现（tkl_wifi / tkl_bt）不参与编译；开启后需要相应的系统开发包（见环境要求）。
