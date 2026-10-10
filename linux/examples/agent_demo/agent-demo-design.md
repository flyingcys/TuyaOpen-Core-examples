# agent_demo 技术方案（Linux 跑通云端 agent 全链路）

> 状态：**方案草案（待评审）**　基线提交：`e4a30e8`（工作区干净）
> 目标读者：本仓开发者；开发完成后交对端会话（linux-a4）PK 评审。

## 1. 目标与范围

把上游 `apps/tuya.ai/your_chat_bot`（T5AI/ESP32 嵌入式 AI 聊天机器人）精简迁移为
`examples/agent_demo`，在 **Linux 上跑通与云端 agent 的全链路**：

- **上行**：上传一段预置语音（模拟"用户说话"）作为触发条件；
- **下行**：**只打印文字**（ASR 识别结果 + agent 回复），下行语音**不处理**（不解码不播放）；
- **无 UI**：不要屏幕/表情/LVGL/按键/低功耗/打印机/电池；
- **音频适配**：音频类硬件适配收进 port 层留空（stub），不为 Linux 做真实音频适配，
  只为代码能编译、链路能跑起来。

不在范围内：真麦克风采集、TTS 播放、唤醒词/VAD、多轮持续对话、APP 端交互。

## 2. 需求与已确认决策

| # | 事项 | 决策 | 来源 |
|---|---|---|---|
| 1 | 云端凭据 | PID 用上游 your_chat_bot 同款 AI 产品 `alon7qgyjj8yus74` + iot_demo 的授权码（`uuid3dfa17df5ff96fc6` / `92J3...`）；**已实测可激活、可建 AI 会话、可收下行** | 用户确认（2026-10-10，实测修订） |
| 2 | 触发方式 | 启动后自动上传一次，但**必须可验证**（日志/自测能确认上传发生、云端已响应） | 用户确认 |
| 3 | 语音素材 | edge-tts 在线合成"请问，现在是几点钟？"（已验证可生成） | 用户确认 |
| 4 | 下行语音 | 不处理（丢弃） | 用户确认 |
| 5 | 音频函数 | 专门一个接口函数，port 留空做适配 | 用户确认 |

**待确认项**（见 §6）：PID 配套的 uuid/authkey（授权码与 PID 绑定，现有授权码未必适用于新 PID）。

## 3. 现状摸底

### 3.1 上游 your_chat_bot 架构（参考实现）

```
app 层      tuya_main.c ──> app_chat_bot.c ──> ai_chat_init()
              │                                    │
组件层        │            ai_main（对话主控 ai_chat_main.c）
              │              ├─ ai_mode（HOLD/ONESHOT/WAKEUP/FREE 四种对话模式）
              │              ├─ ai_audio_input（麦克风采集+VAD，依赖 tdl_audio/tkl_vad）
              │              ├─ ai_audio_player（TTS 解码播放，依赖 svc_ai_player/media_src）
              │              ├─ ai_agent（应用封装：output cbs 注册、text/file/image 上行）
              │              └─ ai_ui / ai_picture / ai_mcp（UI/图片/MCP，本次不要）
协议层                       tuya_ai_service（svc_ai_agent/basic/monitor/codec）
传输                         MQTT（信令：取 token/建会话）+ TCP/TLS（音视频与文本流）
```

关键链路（上游证据）：

- **上行**：`tdl_audio(ALSA)` → `ai_audio_input` ringbuf → 80ms 切片 → `output_cb` →
  `tuya_ai_audio_input()` → 编码（Opus 40ms/16kbps）→ `tuya_ai_agent_upload_stream()` → TCP。
  （`ai_chat_main.c:201`、`ai_audio_input.c:61/101/156`、`tuya_ai_input.h:134`、`tuya_ai_agent.c:1525`）
- **下行**：TCP → `media_data_cb` → `ai_audio_play_tts_stream()` 解码播放（`ai_agent.c:130`）；
  文字走 `text_cb` → `ai_text_process()` → `ai_user_event_notify()` → 应用
  （`ai_agent.c:160/232`、`ai_skill.c:113/139/205`）。
- **会话建立**：`EVENT_MQTT_CONNECTED` → `ai_agent_init()`（`ai_chat_main.c:317/323`）→
  `tuya_ai_agent_init()`（`tuya_ai_agent.c:1232`）→ 客户端就绪发布 `EVENT_AI_CLIENT_RUN`
  （`tuya_ai_client.c:162`）→ `crt_session`（`ai_agent.c:191/239`）。
- **音频格式**：PCM 16kHz/16bit/mono（`boards/LINUX/DshanPi_A1/board_com_api.c:79-81`）；
  上行 Opus（`tuya_ai_encoder_opus.c:32-63`）；下行 TTS 默认 MP3（`ai_agent.c:49`）。
- **触发**：模式驱动（HOLD 按键 / VAD / 唤醒词），Linux 参考板用键盘按键（`CONFIG_BUTTON_NAME="s"`）。

### 3.2 本仓 agent-core 现状与断点

本仓 `tuyaopen-core/agent-core/` 已搬运的模块：`ai_agent` / `ai_audio` / `ai_mcp` /
`ai_skills` / `ai_video` / `utility` / `tuya_ai_service`。
**未搬运**：`ai_main` / `ai_mode` / `ai_ui` / `ai_picture`。

当前 `CONFIG_ENABLE_AI_COMPONENTS` 关闭（`linux/.config:38`），打开后编译会撞 6 处断点：

| # | 断点 | 位置 | 性质 |
|---|---|---|---|
| 1 | `add_subdirectory(ai_main)` 但目录不存在 | `agent-core/CMakeLists.txt:11` | configure 直接失败 |
| 2 | `#include "netmgr.h"`（文件已随 netmgr 删除） | `tuya_ai_service/port/src/mqc_app.c:14`、`tuya_svc_netmgr.c:10` | 编译硬依赖 |
| 3 | `http_parser.h` 缺失（nodejs http-parser） | `port/include/http_manager.h:18`、`svc_ai_basic/src/tuya_ai_protocol.c:37` | 编译硬依赖 |
| 4 | 音频 TDL/TKL 头缺失（`tdl_audio_manage.h`/`tkl_vad.h`/`svc_ai_player.h`/`media_src.h`） | `ai_audio/*`、`ai_agent.c:25` | 受 `ENABLE_COMP_AI_AUDIO` 门控 |
| 5 | 摄像头头缺失（`tdl_camera_manage.h`） | `ai_video/*` | 受 `ENABLE_COMP_AI_VIDEO` 门控 |
| 6 | `utility` 目标缺 ai_skills include 路径，但 `ai_user_event.h:18` 无条件 include `skill_emotion.h` | `utility/CMakeLists.txt:10` | 开 AI 总开关必炸 |

netmgr 真实调用点仅 2 处（其余是类型/宏）：`mqc_app.c:142`（取本机 IP 用于 socket 绑定）、
`tuya_svc_netmgr.c:39`（取链路状态）。均可替换：
- 取 IP → `tal_netif_get_ip(NW_IP_S*)`（`iot-core/tal_system/include/tal_netif.h:41`，iot_demo 已在用）；
- 链路状态 → `tuya_iot_activated()`（`iot-core/cloud/tuya_iot.h:348`）或本地状态变量。

### 3.3 port 层现状

- **完全没有音频面**：无 `tkl_audio*`、无 ALSA/PCM 采集播放、无 VAD。
- ops 卡机制：链接期全局符号 + getter（如 `tal_api_ops_platform`），非运行时注册。
- 构建模式：每 demo 独立 CMake 工程，读根 `../../.config`，GLOB `src/*.c`，
  `--start-group` 链接 core 静态库 + port（`examples/p2p_demo/CMakeLists.txt:101-186`）。
- `examples/agent_demo/` **已存在但是空目录**。

## 4. 方案设计

### 4.1 总体架构（agent_demo vs 上游）

**核心取舍：绕开 `ai_main` / `ai_mode` / `ai_audio`，demo 自己写主流程，直接复用
`ai_agent`（应用封装）+ `tuya_ai_service`（协议/传输）。**

理由：
- `ai_main`/`ai_mode` 解决的是"按键/VAD/唤醒词驱动的多轮对话状态机"——本 demo 不需要；
- `ai_audio` 依赖本仓缺失的 TDL/TKL 音频栈（`tdl_audio_manage.h` 等），整块绕开可省掉
  补 4 个头 + 播放器桩的代价；
- 上行音频有更直接的入口：`tuya_ai_input.h` 的 `tuya_ai_audio_input*()` 系列
  （svc 层公开 API，不受 `ENABLE_COMP_AI_AUDIO` 门控），**支持 PCM 直传**
  （`codec_enable=FALSE` 时 `tuya_ai_agent_upload_stream()` 直接发原始数据，见
  `tuya_ai_agent.c:1525-1544`）。

```
agent_demo（本方案）                          上游 your_chat_bot（对照）
┌──────────────────────────────┐              ┌──────────────────────────────┐
│ src/tuya_main.c              │              │ tuya_main.c + app_chat_bot.c │
│  iot 激活/连云（抄 iot_demo） │              │  + ai_chat_main（ai_main）   │
│  ├─ ai_agent_init()          │              │  ├─ ai_chat_init()           │
│  ├─ 上行任务：读 PCM 文件     │              │  ├─ ai_mode（按键/VAD/唤醒） │
│  │    → tuya_ai_audio_input()│              │  ├─ ai_audio_input（麦克风） │
│  └─ 下行：text_cb 打印文字    │              │  └─ ai_audio_player（TTS）   │
│       media_data_cb 丢弃      │              │                              │
├──────────────────────────────┤              ├──────────────────────────────┤
│ port: tkl_ai_*（文件回放）    │              │ tdl_audio + tkl_audio(ALSA)  │
└──────────────────────────────┘              └──────────────────────────────┘
        ↓ 复用（core 零结构改动）                      ↓
   ai_agent / tuya_ai_service / tuya_iot（iot-core）
```

### 4.2 数据流设计

**上行（触发）**：
```
启动 → iot 激活+MQTT 连接 → ai_agent_init()（注册 5 个 output cbs + 会话订阅）
  → EVENT_AI_CLIENT_RUN（客户端就绪）→ 自动建会话（core 内既有逻辑）
  → demo 等就绪（tuya_ai_agent_is_ready()）
  → 触发一次上传：
       tuya_ai_input_start(true)
       循环：tkl_ai_read(80ms 帧 @16kHz/16bit/mono = 2560 B) 
             → tuya_ai_audio_input(ts, pts, data, len, len)
       tuya_ai_input_stop()
  → 云端 ASR → agent 处理 → 下行
```

- 帧长取上游同款 **80ms 切片**（`ai_chat_main.c:41 AI_AUDIO_SLICE_TIME 80`），16k/16bit/mono
  → 2560 B/帧；2.59s 素材约 33 帧。
- 用 `tuya_ai_audio_input()`（ringbuf 版）而非 `_direct` 版：与上游调用形态一致，
  ringbuf 有背压保护（写满等待），无需自己控速。
- **PCM vs Opus**：先按 PCM 直传（`codec_enable=FALSE`）——最小改动。若云端拒绝 PCM
  上行，再评估引入 Opus 编码（core 有 `tuya_ai_encoder_opus.c`，需 libopus + 开
  `ENABLE_TUYA_CODEC_OPUS`）。**此风险在 §6 单列，实施时先验证。**

**下行（文字 + 音频）**：
```
云端 → TCP → svc_ai_agent 分发
  ├─ text_cb → ai_text_process()（ai_skills）→ ai_user_event_notify()
  │      → demo 注册的回调：打印 [ASR] / [NLG] 文字
  └─ media_data_cb(AI_PT_AUDIO) → 丢弃（只计数 + 日志），不解码不播放
```

- 下行文字**零成本复用**：`ai_user_event_notify_register()`（`utility/include/ai_user_event.h:108`），
  监听 `AI_USER_EVT_TEXT_STREAM_START/DATA/STOP`（枚举 `:44-47`，负载 `AI_NOTIFY_TEXT_T` `:82-86`）。
- `ai_agent.c` 已有 `ai_agent_init()` 注册全部 output cbs（`:231-235`），demo 不需要自己接协议层。

### 4.3 音频适配接口（port 留空，用户要求）

**新增最小音频输入契约**（形态参照官方 `tkl_audio.h` 的 `tkl_ai_*` 命名，便于将来替换为
官方全栈实现）：

```
port/include/media/tkl_ai_demo.h        ← 契约（本 demo 专用，非官方 tkl_audio.h）
port/src/tkl_ai_demo.c                  ← Linux 实现：从 PCM 文件回放
```

接口（最小 3 函数）：

```c
/* 打开音频源。Linux: 打开 PCM 文件；真实设备: 启动麦克风（适配点） */
OPERATE_RET tkl_ai_demo_open(const char *src_path);
/* 读一帧 PCM 数据，返回实际字节数；0 = 数据结束(EOF)；<0 = 错误 */
INT_T       tkl_ai_demo_read(UINT8_T *buf, UINT_T len);
/* 关闭音频源 */
OPERATE_RET tkl_ai_demo_close(VOID);
```

- **"留空"的含义**：不做真实 Linux 音频适配（不接 ALSA/麦克风）；实现为**文件回放**，
  只为链路能跑。真实设备接入时替换本文件实现（麦克风采集/VAD），接口不变。
- 文件路径可配：Kconfig `AGENT_DEMO_AUDIO_FILE`（默认 `assets/voice_16k.pcm`，
  构建期 `configure_file` 拷到构建目录，同 p2p_demo 的 `demo_video.264` 做法）。

### 4.4 core 侧改造清单（最小集）

| # | 文件 | 改动 | 说明 |
|---|---|---|---|
| 1 | `agent-core/CMakeLists.txt:11` | 删 `add_subdirectory(ai_main)` | ai_main 不迁移；同步更新 :1-3 注释（门控原因已解决） |
| 2 | `tuya_ai_service/port/src/mqc_app.c` | 删 `:14` include；`:142` `netmgr_conn_get(...IP)` → `tal_netif_get_ip()` | 去 netmgr |
| 3 | `tuya_ai_service/port/src/tuya_svc_netmgr.c` | 删 `:10` include；`:39` 状态 → `tuya_iot_activated()` | 去 netmgr |
| 4 | `tuya_ai_service/port/src/http_manager.c` | `:15` 删 include；`:277-279` 自写 URL 解析（~20 行，拆 host/port/path） | 替代 http_parser |
| 5 | `svc_ai_basic/src/tuya_ai_protocol.c:37` | 删 `#include "http_parser.h"`（只 include 未用符号） | — |
| 6 | `agent-core/utility/CMakeLists.txt` | 补 ai_skills include 路径（或把 `ai_user_event.h:18` 的 `skill_emotion.h` 依赖解耦） | 开总开关必炸项 |
| 7 | `agent-core/ai_agent/src/ai_agent.c:209-229` | 音频 attr 设置增加 `#else` 分支：`AUDIO_CODEC_PCM / 16000 / 1ch / 16bit`，`codec_enable=FALSE` | 关 AI_AUDIO 时 attr 不再全 0 |

**裁剪开关**（agent_demo 档）：`ENABLE_AI_COMPONENTS=y`，
`ENABLE_COMP_AI_AUDIO=n`、`ENABLE_COMP_AI_MCP=n`、`ENABLE_COMP_AI_VIDEO=n`。
（audio 关掉后 `ai_audio`/`ai_mcp`/`ai_video` 不参与编译，断点 4/5 自然消失。）

### 4.5 demo 文件清单

```
examples/agent_demo/
├── CMakeLists.txt                 # 抄 p2p_demo 骨架（.config 解析/凭据注入/start-group 链接/自测目标）
├── Kconfig                        # AGENT_DEMO（select ENABLE_AI_COMPONENTS）+ 音频文件路径参数
├── README.md / README_CN.md       # 构建/运行/配置/自测/与上游差异/已知限制
├── agent-demo-design.md           # 本文档
├── assets/
│   └── voice_16k.pcm              # edge-tts "请问，现在是几点钟？" 16k/16bit/mono（2.59s，81KB）
└── src/
    ├── tuya_main.c                # 主流程（§4.2）
    ├── tuya_config.h              # PID/uuid/authkey（#ifndef 默认 + secrets 覆盖，抄 iot_demo）
    ├── tuya_config_secrets.h.example
    ├── agent_audio.c/.h           # 上行任务：文件→帧→tuya_ai_audio_input（调 port 的 tkl_ai_demo_*）
    ├── agent_downlink.c/.h        # 下行：ai_user_event_notify_register 回调打印（ASR/NLG）
    └── agent_chain_selftest.c     # 自测（离线：切帧/边界；见 §5）
```

### 4.6 构建与 Kconfig

- `linux/Kconfig` 追加 `rsource "examples/agent_demo/Kconfig"`（现有三条 rsource 在 :9/:12/:15）。
- `examples/agent_demo/Kconfig`：
  ```
  config AGENT_DEMO
      bool "agent_demo"
      select ENABLE_AI_COMPONENTS
      ...
  config AGENT_DEMO_AUDIO_FILE
      string "PCM 语音素材路径"
      default "assets/voice_16k.pcm"
  ```
- `.config` 当前是 p2p 档（`CONFIG_P2P_DEMO=y` 等）。agent_demo 构建时需 AI 档：
  `CONFIG_AGENT_DEMO=y` + `CONFIG_ENABLE_AI_COMPONENTS=y` + 关 P2P/本地存储等。
  README 说明切换方式；CMakeLists 内置默认开关兜底（同 p2p_demo 模式）。
- 链接必须用 `--start-group`（ops 卡是链接期符号，见 `tuyaopen-core.cmake:99-109`）。

### 4.7 凭据

- PID `alon7qgyjj8yus74`（上游 your_chat_bot 同款 AI 产品）+ 授权码 `uuid3dfa17df5ff96fc6` /
  `92J3...`（与 iot_demo 相同）。**用户决策 + 实测**（2026-10-10）：
  - 先用 `qhivvyqawogv04e4`（iot_demo PID）实测：激活成功但 **AI 会话建立失败**
    （`create ai agent session failed` / `mq token ack timeout`）——该 PID 未开通 AI 服务；
  - 换 `alon7qgyjj8yus74` 后：激活成功 → `create session:...success` → 33 帧上传 →
    ASR + NLG 下行全通。
- uuid/authkey 支持三级覆盖：`-D` 编译宏 > 环境变量 > `tuya_config_secrets.h` > 默认值。

## 5. 验证方案

### 5.1 离线自测（不连云，`agent_chain_selftest`）

| 用例 | 断言 |
|---|---|
| 素材加载 | 打开 `voice_16k.pcm` 成功，字节数 = 采样数×2，时长 2.59s |
| 切帧 | 80ms 帧 = 2560 B；总帧数 = ceil(总字节/2560)；最后一帧按实际长度 |
| 边界 | EOF 返回 0；读空缓冲区/重开正常 |
| 变异验证 | 把切帧长度改错，断言必须失败（防假绿） |

### 5.2 在线实跑（全链路判据）

```
cd examples/agent_demo && cmake -B build && cmake --build build -j
cd build && timeout 60 ./agent_demo
```

日志判定序列（缺一不可）：
1. `Activation data read fail, go activation mode...` 或已激活直连（同 iot_demo）；
2. `Device Bind Start!` → 二维码（未激活时）→ 扫码激活；已激活则直达 MQTT；
3. `TUYA_EVENT_MQTT_CONNECTED` → `ai agent init`；
4. 会话建立（`EVENT_AI_CLIENT_RUN` 后 crt_session 成功日志）；
5. **上行**：`[AGENT] upload start, N frames, X bytes` → 逐帧上传（或汇总日志）；
6. **下行 ASR**：`[AGENT][ASR] 请问，现在是几点钟` （识别结果，验证语音真被云端识别）；
7. **下行 NLG**：`[AGENT][NLG] ...`（agent 回复文字，全链路通的最终判据）。

### 5.3 运行方式

- 启动后自动上传一次（用户确认），进程保持常驻（yield 循环），文字打印到 stdout。
- 可加 `AGENT_DEMO_EXIT_AFTER_REPLY`（编译期可选）用于自动化：收到 NLG 后退出码 0。

## 6. 风险与待确认项

| # | 风险/待确认 | 影响 | 处置 |
|---|---|---|---|
| 1 | ~~PID 是否开通 AI Agent 服务~~ | ~~未开通则 agent 会话建不起来~~ | **已实测闭环**：`qhivvyqawogv04e4` 建会话失败，换 `alon7qgyjj8yus74` 全通（见 §4.7） |
| 2 | **PCM 上行云端是否接受**（上游用 Opus） | 云端可能拒收/不识别 | 实施时先验证；不行则启用 Opus（libopus 依赖需解决） |
| 3 | AI 协议版本：`.config` 为 `CONFIG_AI_VERSION=2`，上游默认 0x01 | v2 分支行为差异 | 跟随本仓配置（v2 已实现）；异常时回退 v1 对比 |
| 4 | 云端 agent 智能体配置（默认角色）是否就绪 | 无回复 | 激活后在 APP/平台侧确认默认智能体 |
| 5 | `enable_serv_vad=TRUE`（`tuya_ai_agent.c:1249`）对单次上传的影响 | 服务端 VAD 可能改变结束判定 | 实测；必要时 demo 侧 `tuya_ai_agent_server_vad_ctrl(false)` |
| 6 | 下行音频包体量（不解码直接丢） | 网络流量浪费 | 可接受（验证阶段）；后续可协商不发音频 |

## 7. 任务分解（实施顺序）

1. **T1 core 断点修复**（§4.4 第 1-6 项）→ `CONFIG_ENABLE_AI_COMPONENTS=y` 能 configure + 编译；
2. **T2 port 音频契约**（§4.3）`tkl_ai_demo.c/h` + 素材就位（voice_16k.pcm）；
3. **T3 demo 骨架**（CMakeLists/Kconfig/凭据/README/离线自测）；
4. **T4 链路接线**（tuya_main 主流程 + 上行任务 + 下行打印 + `ai_agent.c` attr 补丁）；
5. **T5 实跑验证**（§5.2 判据全序列）；
6. **T6 文档收尾**（README 已知限制、与上游差异表）→ 交付对端 PK 评审。

## 8. 实施记录（2026-10-10，实测修订）

### 8.1 实测发现、超出 §4.4 清单的断点（已修）

| # | 断点 | 修复 |
|---|---|---|
| 1 | `tuya_ai_agent.h` / `tuya_ai_input.h` **循环包含**：展开顺序不同时 `AI_ATTR_BASE_T`/`AI_INPUT_SEND_T` 未定义（上游靠 include 顺序侥幸） | `tuya_ai_input.h` 改前置声明 `tuya_ai_agent_get_scode` |
| 2 | `http_manager.c` 用**旧 coreHTTP API**（`HTTPClient_Request` + `HTTP_SEND_DISABLE_RECV_BODY_FLAG`），本仓官方 v3.1.3 无此二者 | 改 `HTTPClient_Send` + `respOptionFlags = HTTP_RESPONSE_DO_NOT_PARSE_BODY_FLAG`（与 iot-core `http_session.c` 同款用法） |
| 3 | `http_manager.h` 依赖 nodejs http-parser 的 `enum http_method`（HTTP_GET 等） | 本地定义同名枚举（顺序与 http-parser 一致） |
| 4 | `ai_skills` 缺 `utility` / `tuya_cloud_service` 依赖（target 化后 include 不传递） | CMakeLists 显式补 |
| 5 | `tuya_ai_get_encoder` 链接缺失（`svc_ai_agent` 无条件引用；编码器注册表随 `ENABLE_TUYA_CODEC` 门控） | 开 `CONFIG_ENABLE_TUYA_CODEC`（注册表纯实现，无 opus 依赖） |
| 6 | §4.4 第 7 项（`ai_agent.c` attr 分支）**实测证实必需**：不开音频模块时 attr 全 0，云端拿不到格式、上传后不回 ASR/NLG | 补 `#else` 分支：PCM/16k/mono/16bit + `codec_enable=FALSE` |

### 8.2 实测结论（全链路证据）

- 构建：`cmake -B build && cmake --build build -j` 通过（exit 0）；
- 自测：`./build/agent_chain_selftest` → `AGENT CHAIN SELFTEST PASSED`；
  **变异验证**：`tkl_ai_demo_read` 读长减半 → `frame count` 断言红（exit 1），还原后绿；
- 实跑（PID `alon7qgyjj8yus74`）：扫码激活 → `create session:...success` →
  `upload done: frames=33, bytes=82944, last_frame_bytes=1024` →
  `ASR text: 请问现在是几点钟？` → `[AGENT][ASR] 请问现在是几点钟？` →
  `[AGENT][NLG] >> 现在的时间是无法通过工具获取，/ 我直接告诉你吧，/ 具体时间你可以看看手机或者时钟哦。` → stream end。

### 8.3 已知无害告警

- 上传起始处一条 `tuya_ai_event.c:36 event or session id was null`（会话刚建立时的
  事件关联时序），不影响后续（`use session ...` 正常、下行正常）；
- 首启一条 `load tls_ca_cnt fail. no ca in local db`（CA 首次拉取前提示，iot_demo 同样存在）。

## 9. 能力边界（本 demo 明确不做的项）

> **定位声明**：agent_demo 是"跑通云端 AI agent 全链路的**验证载体**"，不是功能完整的产品。
> 它的价值在于把链路走通、把判据做真（见 §5.2 与三个自测）；功能完整性不是目标。
>
> 本节把"**有意不做**"与"**做到一半/未实现**"分开记录，目的是防止后人把刻意边界当遗漏
> "顺手补上"——前者补了是偏离定位，后者补了需要有明确的实现方案与验证。

### 9.1 规模口径（先建立量感，避免误读）

对比上游 `/home/share/samba/open-components/tuyaopen/apps/tuya.ai/your_chat_bot`：

| 口径 | 上游 | 本 demo |
|---|---|---|
| 应用层 `.c/.h` 总行数 | 112,831 | 1,034 |
| 其中 **UI 资源**（点阵字模/PNG/GIF 数组） | **105,777** | 0 |
| **实际逻辑代码** | **7,054** | **1,034** |
| core 侧组件 | ai_agent 460 / ai_skills 1509 / ai_audio 1342 / ai_mcp 2208 / ai_video 383 / **ai_mode 2228** / **ai_main 830** | 前三项与 ai_mcp/ai_video **逐行同源**；**无 ai_mode / ai_main** |

**关键结论**：AI 协议、会话、技能、传输这些硬骨头两侧同源（行数差 0~14，差异仅
Linux PCM 属性分支与注释）；本 demo 真正没移植的应用侧能力集中在
**`ai_mode`（2228 行）+ `ai_main`（830 行）= 3058 行的"对话主控"**。
所以"上游 11 万行"里绝大部分是界面资源，**别把它当逻辑差距**。

### 9.2 有意不做（平台/定位决定，补了即偏离）

| 能力 | 上游证据 | 不做原因 |
|---|---|---|
| **UI 全部**（屏幕/表情/LVGL/聊天界面/主题/亮度/波形） | `src/display2/ui_chatbot` + `ui_wechat` 两套 + core `ai_ui` 4 套（10360 行，实测） | Linux 无屏；文字已打 stdout，数据面本就通。上游 `ui_wechat` 才是有气泡的功能套，`ui_chatbot` 的 `ui_set_user_msg/assistant_msg/system_msg` 全是空 `return;` |
| **按键 / 唤醒词 / VAD** | `ai_mode_hold.c:314` 长按、`ai_mode_wakeup.c:169` `tkl_kws_reg_wakeup_cb`、`ai_audio_input.c:146` `EVENT_AUDIO_VAD` | 依赖 `tdl_button_manage.h`/`tkl_kws.h`/`tkl_vad.h`，Linux 无对应硬件 |
| **真实麦克风采集** | `ai_audio_input.c:360/367` `tdl_audio_find`+`tdl_audio_open`；平台是 **push 模式**（`tkl_audio.c:75-88` `voice_read_callback` 回调），`tkl_audio.c:655` 的 `tkl_ai_get_frame()` 直接返回 `OPRT_NOT_SUPPORTED` | port 契约 `tkl_ai_demo_*` 已留替换点（`port/include/media/tkl_ai_demo.h` 注明适配点），Linux 只做 PCM 文件回放、且是 **poll 模式**（`agent_audio.c:64` 主动 read） |
| **摄像头 / 视频** | `board_com_api.c:283` camera 注册、`ai_agent.c:141` VIDEO `/* TBD */` | 无硬件；且上游该分支本身也是空实现 |
| **打印机** | `app_printer.c`（206 行，JPEG→单色抖动位图） | 无硬件 |
| **电池 / 低功耗** | `app_battery.c` 74 行、`app_lowpower.c` 260 行（`tuya_pm` 两级链） | 无硬件；上游 30 个板 config 也仅 2 个开启 |
| **音量 / 亮度调节 + DP 上报** | `app_ui_helper.c:35-49` 去抖 + `ai_audio_player_set_vol`；`tuya_main.c:71` `DPID_VOLUME=3` | 无播放器可调音量 |
| **多轮持续对话 / 语音打断 / 服务端 VAD 交互** | `ai_mode_free.c` 等四模式状态机（`ai_manage_mode.h:48-57`：INIT/IDLE/LISTEN/UPLOAD/THINK/SPEAK + INVALID 哨兵） | 定位为"一次触发"验证；多轮需先移植 ai_mode |

### 9.3 未实现/做到一半（属半成品，补前需方案 + 验证）

| 能力 | 现状 | 补它需要什么 |
|---|---|---|
| **上行 Opus 编码** | PCM 直传（`codec_enable=FALSE`，`ai_agent.c` 的 `#else` 分支硬编码 16k/mono/16bit） | 开 `ENABLE_COMP_AI_AUDIO_CODEC_OPUS`（引 libopus）+ 把属性改回从编码器协商。**注意：上游默认板同样是 PCM 裸发，这不是我们独有的简化**——`svc_ai_codec/Kconfig:5-7` 的 `ENABLE_TUYA_CODEC_OPUS` **default n** → `__ai_agent_reg_encoder`（`tuya_ai_agent.c:1195-1204`）一个编码器都不注册 → `tuya_ai_get_encoder()` 返回 NULL（`:1172`）→ 不 create、走原始数据；`your_chat_bot` 28 个板 config 仅 8 个显式开 Opus、`app_default.config` 无 codec 项。即默认口径上下游一致，Opus 是少数板的选择 |
| **下行 TTS 播放** | AI 音频模块整体关闭，语音包在 core 内丢弃（`ai_agent.c:137-139` 被预处理掉） | 开 `ENABLE_COMP_AI_AUDIO`，但 Linux port 无 `tdl_audio_manage`/`svc_ai_player`/`media_src`（§3.2 断点 4），需先补 port 侧 |
| **媒体 URL 流式下载** | 6ba0506 改安全失败（`PR_ERR("http media download not implemented")`）。上游 `tuya_ai_http.c` 有真实下载循环 | 用本 manager 的 transport 直读 body 实现；**必须先处理 README 已知限制里的三个前置**：IPv6 字面量/userinfo 不支持、>1023B URL 被 `fill_url_head` 静默截断 |
| **图片 / 文件 / 视频下行** | `ai_agent.c:140-148` image 走 `ai_picture_output`（模块未搬），file(`:150`)/video(`:141`) 上游同为 `/* TBD */` | 图片需搬 `ai_picture`（892 行）+ 媒体下载；file/video 上游也未实现，**不属于我们的欠账** |
| **表情/情绪驱动** | `agent_downlink.c` default 分支忽略（core `ai_skill.c:183-190` 会发 `AI_USER_EVT_EMOTION`） | 只需在消费端加 case；终端可直接打 emoji 字符，成本极低 |
| **`ai_agent_send_text/send_file/send_image/cloud_alert/role_switch`** | core 函数都在（`ai_agent.c:260-372`），**应用层一次没调** | 纯调用，无依赖 |
| **MQTT_DISCONNECT / TIMESTAMP_SYNC 事件处理** | `tuya_main.c` 只处理 6 个事件，无这两个 case | 纯 tal 层逻辑（上游 `tuya_main.c:222/233`） |
| **重置配网（连按重置 3 次）** | 无。上游 `reset_netcfg.c` 111 行用 KV 计数 + 5s 定时器归零 | **已有可搬样板**：同仓 `linux/examples/p2p_demo/src/reset_netcfg.c`（129 行）只依赖 `tal_api.h`+`tuya_iot.h`，板级无关、已在 Linux 落地——低风险可搬迁项 |

### 9.4 为什么"对话主控"3058 行不能直接搬（重要判断）

`ai_mode` / `ai_main` **不是"打开宏就能用"**：

- `ai_mode` 直接 include `tdl_button_manage.h`、`tdl_led_manage.h`、`tkl_kws.h`、`tkl_vad.h`、
  `ai_audio_player.h`、`ai_picture_input.h`（实测 include 列表）；
- `ai_main` 的 UI 调用有 `#if ENABLE_COMP_AI_DISPLAY` 门控（3 处），但**按键创建
  （`ai_chat_main.c:293-306` 的 `tdl_button_create` 等 7 处）与唤醒词无门控、直接编译**。

⇒ 移植前需先做按键/唤醒词/UI 的依赖剥离或宏隔离，**属中等工作量，不是零成本**。
若将来要补，建议路径：先按 `ai_mode_free.c`（行为最接近本 demo）做单模式移植，
按键/VAD/唤醒词改由**参数注入**（模式接口本就支持 `handle_key` / `vad_change`），
再决定是否引其余三态。

### 9.5 接真实麦克风时，"只换 port 实现"补不了的四件事（重要）

常见误解是"把 `tkl_ai_demo.c` 换成麦克风采集就能接真机"。**不够**——上游的上行策略
不在可替换的 port 层，而在**未搬运的 `ai_main`/`ai_mode`/`ai_audio` 三块里**：

| 上游行为 | 证据 | 换 port 能补上吗 |
|---|---|---|
| **播放中不上传**（半双工，防把自己 TTS 录进去） | `ai_chat_main.c:215-219`：AEC 关时 `if(false == ai_audio_player_is_playing())` 才 `tuya_ai_audio_input` | **不能**——判断在 `ai_chat_main.c`，我们侧 `agent_audio.c` 零处该判断 |
| **AEC 开则无脑上传**（支持语音打断） | `ai_chat_main.c:211-214`，板级 `zectrix_t5ai_note_4.c:173` `cfg.aec_enable=1` → `tkl_vad.c:62-64` `speex_aes_process` | **不能**（同左） |
| **本地 VAD 定轮次边界**（按键抬起才 stop） | `ai_mode_hold.c:282-290`：`PRESS_UP` → `__ai_mode_enter_upload` 内 `tuya_ai_input_stop()`；`:165` init 时 `tuya_ai_agent_server_vad_ctrl(false)` | **不能**——我们 `agent_audio.c:89` 是"读完 EOF 即 stop"；且 core 默认 `enable_serv_vad=TRUE`（`tuya_ai_agent.c:1249`）而 app 从未调用 `tuya_ai_agent_server_vad_ctrl(false)` 去关它 ⇒ **当前是"服务端 VAD 定边界"**，与上游 HOLD 模式的本地 VAD 口径不同 |
| **push 采集语义** | `tkl_audio.c:75-88` 平台读线程回调 `voice_read_callback`；`tkl_audio.c:655` `tkl_ai_get_frame()` 返回 `OPRT_NOT_SUPPORTED`（证明非拉模式） | 半能——契约可改成 push，但需自己解决"何时起止一轮" |

**结论**：接真麦克风的正确顺序是**先搬 `ai_mode`/`ai_main`（含按键/VAD/AEC 决策），
再替换 port 采集实现**；反过来会把半双工/AEC/边界决策的责任丢给应用层从零实现。
这也是本 demo 只做"文件回放 + 一次触发"的原因：它验证的是**协议链路**，不是**音频策略**。
