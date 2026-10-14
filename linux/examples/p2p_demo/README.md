# p2p_demo

## 简介

上游 TuyaOpen `apps/tuya_cloud/ipc_p2p_demo`（T5AI IPC 摄像头 demo）的 Linux 移植：通过
P2P 通道把设备视频推给涂鸦 App（App 侧看直播画面），可选支持本地录像与日历回放。Linux 上
没有摄像头，视频源是**上游 demo 自带的视频** `assets/demo_video.264`（2304x1296@15fps、
36 秒循环，源自上游 `apps/tuya_cloud/camera_demo/demo_video.264`），由虚拟摄像头
`demo_vcam` 按帧率循环喂给媒体链路；音频不做（上游默认关，Linux 无音频）。

## 构建与运行

```bash
cd examples/p2p_demo
cmake -B build
cmake --build build -j$(nproc)
cd build && ./p2p_demo
```

> 构建必须在本仓库树内进行：p2p_demo 复用 examples 公共组件 `../qrcode`（不复制副本），
> 单独拷贝本目录会因找不到 qrcode 而 configure 失败（CMake 会给出明确提示）。

凭据可选：默认取 `src/tuya_config.h` 的 PID/UUID/AuthKey，可用
`-DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..` 覆盖，或在
`src/tuya_config_secrets.h`（参考 `.example` 模板）里配置，也支持同名环境变量。
凭据优先级：**`-D` / 环境变量 > `tuya_config_secrets.h` > `tuya_config.h` 默认值**
（secrets 模板的 `#define` 带 `#ifndef` 守卫，不会覆盖命令行注入）。视频源在构建时
复制到 `build/demo_video.264`，从 `build/` 目录运行即可（在别处运行按可执行文件所在目录解析）。

运行后行为：

1. 初始化日志/KV/时间服务/工作队列，使用宿主网络连云；
2. 设备未激活时进入绑定模式：直连 MQTT 成功后打印绑定二维码；
3. 用涂鸦 App 扫描二维码，绑定并激活设备；
4. 激活后 `MQTT_CONNECTED` 事件拉起媒体线程（`TUYA_APP_Start` → P2P listen），App 端即可打开
   直播画面（需 IPC 类目产品，见"已知限制"）。

## Kconfig 开关

| 符号 | 含义 | 默认 |
|---|---|---|
| `P2P_DEMO` | 构建本 example；select `ENABLE_TUYA_P2P`、`ENABLE_IPC_RING_BUFFER` | y（无 prompt） |
| `P2P_DEMO_LOCAL_STORE` | 本地录像与日历回放；select `ENABLE_LOCAL_STORE` | n |
| `P2P_DEMO_WIDTH` | 上报给 App 的视频宽度（像素），需与视频源一致 | 2304 |
| `P2P_DEMO_HEIGHT` | 上报给 App 的视频高度（像素），需与视频源一致 | 1296 |
| `P2P_DEMO_FPS` | 虚拟摄像头喂帧帧率，同时上报给 App | 15 |
| `P2P_DEMO_KBPS` | 上报给 App 的目标码率（视频源不参与实际码控） | 960 |
| `P2P_DEMO_GOP` | 上报给 App 的 GOP 长度（内置视频为 60 帧 GOP） | 60 |
| `P2P_DEMO_H264_FILE` | 直播视频源路径；留空用内置视频 | "" |
| `P2P_DEMO_LIVE_RECORD` | 直播期间持续写 local_store（按段切分） | n |
| `P2P_DEMO_RECORD_MAX_SEC` | 单个录像分段时长上限（秒，30-3600） | 120 |
| `P2P_DEMO_PB_SEED` | 启动时把直播源文件灌进当天索引，免录像体验回放 | n |

menuconfig 用法（`P2P_DEMO` 无 prompt，不构建该 example 即可，无需关闭）：

```bash
pip3 install --user kconfiglib          # 一次性
cd linux
KCONFIG_CONFIG=./.config menuconfig     # 交互配置
KCONFIG_CONFIG=./.config olddefconfig   # 按默认值生成
```

优先级：**`-D` 命令行 > `.config` > 工程内置默认**。也可直接用 `-D` 覆盖：

```bash
cmake -B build -DCONFIG_P2P_DEMO_FPS=15
cmake -B build -DCONFIG_P2P_DEMO_H264_FILE=/path/to/your.h264
```

注意 `.config` 是 `linux/` 下所有 example 共享的，一旦存在即以它为准；删除可回到内置默认。

## 自测

两个独立可执行文件，无需连云、无需 App（在 example 目录执行）：

```bash
./build/vcam_selftest
./build/media_chain_selftest
```

- `vcam_selftest`：打开视频源，验证 AU 索引与喂帧节拍，期望输出 `VCAM SELFTEST PASSED`
  （80 帧、时间戳以 66ms 步进、含 2 个 I 帧）。
- `media_chain_selftest`：跑通完整媒体链路（vcam → 帧校验/队列/ring buffer → P2P 取帧回调）
  并验证"请求 I 帧"，期望 `MEDIA CHAIN SELFTEST PASSED`（60 帧/4 秒、时间戳单调递增、
  请求后到下一个 I 帧之间 ≤ 8 帧——正常 1~2 帧，请求被忽略时要等 ~59 个 P 帧）；找不到视频源
  时报错 `sample not found: run from build/ ...`。

## 视频源

内置视频 `assets/demo_video.264`（构建时复制为 `build/demo_video.264`）：

- **出处**：上游 TuyaOpen 的 `apps/tuya_cloud/camera_demo/demo_video.264`（`camera_demo` 是
  `ipc_p2p_demo` 的前身，自带视频用于"文件模式"P2P 演示；`fce79c22` 用 ipc_p2p_demo 重写时
  该文件被删除，本移植从上游 git 历史取回）。
- **参数**：Annex-B H.264、High profile、2304x1296@15fps、540 帧（36 秒循环）、9 个 IDR
  （GOP=60）、无 AUD（AU 边界按"已有 VCL 再遇 VCL"切分）、最大帧约 56KB、整文件 4.3MB。
- `demo_vcam` 打开时解析为 AU 索引，按帧率循环喂帧；`request_i_frame` 跳到下一个 IDR。

换成自己的 H.264：

```bash
cmake -B build -DCONFIG_P2P_DEMO_H264_FILE=/path/to/your.h264
```

或在 menuconfig 里填 `P2P_DEMO_H264_FILE`（也可直接改 `.config`）。要求 Annex-B 格式；分辨率/
帧率/GOP 变化时必须同步改 `P2P_DEMO_WIDTH/HEIGHT/FPS/GOP`（建议 `KBPS` 一并改），否则上报给
App 的 AV 能力与实际码流不一致。单个 AU 需小于 256KB（demo 帧队列槽上限，超出会告警丢帧）。

## 录像与回放（默认关）

默认只做 P2P 直播。打开本地录像与日历回放：

```bash
cmake -B build-ls -DCONFIG_P2P_DEMO_LOCAL_STORE=ON -DCONFIG_ENABLE_LOCAL_STORE=ON
cmake --build build-ls -j$(nproc)
```

- `P2P_DEMO_LOCAL_STORE` 会 select core 的 `ENABLE_LOCAL_STORE`；menuconfig 里勾前者即可，用 `-D` 时两个都要给。
- 录像落在 `./media`（相对**运行目录**，从 `build/` 运行即 `build/media`），分段文件在
  `media/rec/<YYYY-MM-DD>/` 下（`live_N.media` + 索引）；日期取设备本地时间，云时间同步前
  （刚启动、未激活）为 `1970-01-01`。
- 直播期间持续录像需再开 `P2P_DEMO_LIVE_RECORD`（默认 n），分段时长由 `P2P_DEMO_RECORD_MAX_SEC` 控制（默认 120 秒）。
- `P2P_DEMO_PB_SEED`（默认 n）启动时把直播源文件灌进当天索引，不录像也能让 App 体验
  日历回放（对应上游的 `CAMERA_DEMO_PB_SEED`）。

## 与上游 ipc_p2p_demo 的差异

| 项 | 上游（T5AI 板） | 本 demo（Linux） |
|---|---|---|
| 视频源 | TDL 摄像头 + T5 硬件编码器（`tdl_camera_*`） | 内置样片虚拟摄像头 `demo_vcam` |
| 音频 | `CAMERA_DEMO_AUDIO`（默认关）+ TDL 音频 | 整体不移植，音频回调保留空实现 |
| 联网 | `netmgr_*` / lwip | `tal_netif`（同 iot_demo） |
| 激活与凭据 | `tuya_authorize_init/read`（license 授权） | 编译期凭据 `tuya_config.h`（同 iot_demo） |
| 录像存储 | 挂载 SD 卡（`tkl_fs_mount("/sdcard")`） | 直接写宿主目录 `./media`，无挂载语义 |
| 板级初始化 | `board_register_hardware()`、堆打印、CLI | 删除（Linux 无对应实现） |
| 录像与回放 | `CAMERA_DEMO_LOCAL_STORE`，默认关 | `P2P_DEMO_LOCAL_STORE`，默认关（保留） |
| 回放灌样片 | 读板子上的 `/sdcard/pb_seed.h264`（用户自备） | 用本 demo 的直播源文件（内置样片） |

保留：P2P 启动链（`TUYA_APP_Start`/`p2p_listen`）、媒体回调契约、ring buffer、连续复位清配网（`reset_netcfg`）、`skill_param` 上报。

## 已知限制

- **需要 IPC 类目产品**：默认 PID 用上游 ipc_p2p_demo 的 IPC 产品（`sjzw8nclwoitx0rv`）。若换成
  通用产品，设备能连云、能被 App 绑定、skill 上报也成功，但云侧 `tuya.device.ipc.p2p.config.get`
  会返回 `IP Camera Not exist`——非 IPC 类目产品没有 P2P 配置，`TUYA_APP_Start` 取不到 p2p auth，
  P2P 服务起不来。生产使用请换成自己的 IPC 产品 + license（`-D` 或 `src/tuya_config_secrets.h`）。
- **默认 UUID/AuthKey 是仓库内的测试 license**（与 iot_demo 相同）：同一 license 不能两处同时在线，
  与 iot_demo 并行跑会互相踢下线。
- **激活数据存在运行目录的 `tuyadb/`**：首次运行需用涂鸦 App 扫码激活；换目录运行等于换设备身份，
  需要重新扫码（或把已激活的 `tuyadb/` 一起复制过去）。
- **连续复位 3 次会清配网**：`reset_netcfg` 计数在 5 秒内未被清零时累加，达到 3 次即触发
  `tuya_iot_reset`（factory reset，随后 `tal_system_reset` 重启）。调试时反复快速启动 3 次
  就会丢激活数据、需要重新扫码绑定。
- **未激活设备停在绑定等待态**：MQTT 已连、二维码已打印，等待 App 扫码；激活前 P2P 服务不会启动。
- **未激活设备上调用 `TUYA_APP_Start` 会阻塞**：上游 `p2p_init` 取不到 p2p auth 时以 1 秒间隔
  永久重试（`while(1)`，持续打印 `get p2p auth failed`）。正常流程由激活后的 `MQTT_CONNECTED`
  事件拉起，不会遇到；仅在未激活时误调用才会触发。
- App 调整码率不生效（视频源无编码器，回调返回不支持），真实会话下可能每秒打印一条码率调整日志（与上游实现一致）。
- **长跑注意（上游属性）**：pjproject 的 TLS 行表是 24 行定长、按线程分配且不回收
  （`os_core_tuyaos.c`，p2p-core 上游代码）；每建立一次 P2P 会话会新增一个会话线程
  （`rtc_wrk`）。表满后 `pj_thread_local_set` 返回 `PJ_ENOMEM`、`get` 返回 NULL（优雅
  降级、不崩溃），表现为该会话的部分 p2p 上下文不可用。极端频繁的连断（数十次量级）
  才会触达；如需彻底解决需改 p2p-core（随上游一并决策）。

## 目录结构

```sh
+- p2p_demo
    +- assets                       # demo_video.264（上游 camera_demo 自带视频）
    +- src
    |   -- tuya_main.c              # 入口：初始化、连云、事件处理、媒体线程
    |   -- reset_netcfg.c/.h        # 连续复位 3 次清配网数据
    |   -- tuya_ipc_demo.c/.h       # 媒体链路（取帧/队列/ring buffer/录制）与回调契约
    |   -- demo_vcam.c/.h           # 虚拟摄像头：样片 AU 索引 + 按帧率循环喂帧
    |   -- demo_media_event.c/.h    # local_store 回放（日历查询/回放送帧）
    |   -- fs_card.c                # 文件系统卡（tal_fs_ops；local_store 使用）
    |   -- tuya_config.h            # PID/UUID/AuthKey（可被 secrets 或 -D 覆盖）
    |   -- tuya_config_secrets.h.example  # 凭据覆盖模板
    +- selftest                      # 离线自测（独立目标，不连云；见「自测」章节）
    |   -- demo_vcam_selftest.c     # vcam 自测（目标 vcam_selftest）
    |   -- media_chain_selftest.c   # 媒体链路自测（目标 media_chain_selftest）
    -- CMakeLists.txt
    -- Kconfig                      # P2P_DEMO* 开关
```

> `fs_card.c` 在 p2p_demo 与 agent_demo 各有一份副本，**须逐字节同步**：修改任
> 一份后运行 `diff examples/p2p_demo/src/fs_card.c examples/agent_demo/src/fs_card.c`
> 校验（不一致时把改动同步到另一份）。
