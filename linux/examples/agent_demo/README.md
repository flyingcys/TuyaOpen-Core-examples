# agent_demo

## 简介

上游 TuyaOpen `apps/tuya.ai/your_chat_bot`（T5AI/ESP32 的 AI 聊天机器人）的 Linux 精简移植：
跑通**与云端 AI agent 的全链路**——上传一段预置语音触发，云端 ASR 识别 + agent 回复，
下行**只打印文字**（下行语音不处理）。无 UI、无按键/唤醒词/麦克风。

- **上行**：内置语音素材 `assets/voice_16k.pcm`（16kHz/16bit/单声道，edge-tts 合成
  "请问，现在是几点钟？"，2.592s），启动后自动上传一次（模拟"用户说了一句话"）；
- **下行**：`[AGENT][ASR]` 打印识别结果、`[AGENT][NLG]` 打印 agent 回复文字；
  云端下发的语音包直接丢弃（AI 音频模块整体关闭，不解码不播放）；
- **音频适配**：麦克风采集收进 port 层的最小契约（`tkl_ai_demo_*`），Linux 实现为
  **PCM 文件回放**——不做真实音频硬件适配，只为链路能跑通；真实设备接入时替换该实现。

## 构建与运行

```bash
cd examples/agent_demo
cmake -B build
cmake --build build -j$(nproc)
cd build && ./agent_demo
```

> 构建必须在本仓库树内进行：agent_demo 复用 `../iot_demo/qrcode` 组件（不复制副本），
> 单独拷贝本目录会因找不到 qrcode 而 configure 失败（CMake 会给出明确提示）。

凭据可选：默认取 `src/tuya_config.h` 的 PID/UUID/AuthKey（PID 为上游 your_chat_bot
同款 AI 产品 `alon7qgyjj8yus74`，授权码与 iot_demo 相同），可用
`-DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..` 覆盖，或在
`src/tuya_config_secrets.h`（参考 `.example` 模板）里配置，也支持同名环境变量。
凭据优先级：**`-D` / 环境变量 > `tuya_config_secrets.h` > `tuya_config.h` 默认值**
（secrets 模板的 `#define` 带 `#ifndef` 守卫，不会覆盖命令行注入；若自行去掉守卫，
secrets 会静默覆盖 `-D`）。语音素材在构建时复制到 `build/voice_16k.pcm`，从 `build/`
目录运行即可。

运行后行为：

1. 初始化日志/KV/时间服务/工作队列，使用宿主网络连云；
2. 设备未激活时进入绑定模式：直连 MQTT 成功后打印绑定二维码；
3. 用涂鸦 App 扫描二维码，绑定并激活设备（**需要该 PID 已开通 AI 服务**）；
4. 激活后 `MQTT_CONNECTED` 事件拉起 AI 链路：`ai_agent_init()` → 等 AI 客户端就绪 →
   自动上传一次语音（日志 `[AGENT] trigger: upload one utterance from ...`）；
5. 云端返回：`[AGENT][ASR] 请问，现在是几点钟`（识别结果）→
   `[AGENT][NLG] ...`（agent 回复文字，全链路通的最终判据）。

## Kconfig 开关

| 符号 | 含义 | 默认 |
|---|---|---|
| `AGENT_DEMO` | 构建本 example；select `ENABLE_AI_COMPONENTS`、`ENABLE_QRCODE`、`ENABLE_TUYA_CODEC` | y（无 prompt） |
| `AGENT_DEMO_AUDIO_FILE` | 上行语音素材路径（16k/16bit/mono PCM）；留空用内置素材 | "" |

menuconfig 用法（`AGENT_DEMO` 无 prompt，不构建该 example 即可，无需关闭）：

```bash
pip3 install --user kconfiglib          # 一次性
cd linux
KCONFIG_CONFIG=./.config menuconfig     # 交互配置
KCONFIG_CONFIG=./.config olddefconfig   # 按默认值生成
```

优先级：**`-D` 命令行 > `.config` > 工程内置默认**。也可直接用 `-D` 覆盖：

```bash
cmake -B build -DCONFIG_AGENT_DEMO_AUDIO_FILE=/path/to/your_16k.pcm
```

注意 `.config` 是 `linux/` 下所有 example 共享的，一旦存在即以它为准；删除可回到
内置默认（AI 组件 / QRCODE / WIRED 开；与现有 .config 相比：少 `ENABLE_AI_MONITOR`
与整个 P2P 子树）。

## 自测

三个独立可执行文件，无需连云、无需 App、无需凭据（在 build 目录执行——素材
`voice_16k.pcm` 由 CMake 复制到该目录，从别处执行会提示 `run from build/`）：

```bash
cd build && ./agent_chain_selftest
cd build && ./agent_nlg_selftest
cd build && ./agent_http_selftest
```

> 验证纪律（1c 评审 R3）：以 `cmake --build build`（**全目标**）通过为准——单跑某个
> selftest 不构成完整验证（如 `CONFIG_ENABLE_TUYA_CODEC=OFF` 时三个自测均能链接通过、
> 但 agent_demo 主目标会因 `tuya_ai_get_encoder` 缺失而链接失败）。

- `agent_chain_selftest`：上行音频源契约与素材帧切分口径——总字节 82944、按 80ms
  帧（2560B）切分 33 帧（32 整帧 + 末帧 1024B）、EOF 语义、关闭重开、坏路径拒绝、
  **I/O 错误与 EOF 区分**（目录路径 fopen 可成功但 read 必须 <0 而非 0/EOF）。
  素材路径与 app 同源（`CONFIG_AGENT_DEMO_AUDIO_FILE` 非空则测该文件）。
  期望输出 `AGENT CHAIN SELFTEST PASSED`（exit 0）。
- `agent_nlg_selftest`：下行消费契约——多段 NLG 流的末段（eof=1）经
  `AI_USER_EVT_TEXT_STREAM_STOP` 携带非空内容时必须打印；空包不误打。用公开 API
  构造事件（不依赖云端分包）。期望输出 `AGENT NLG SELFTEST PASSED`（exit 0）。
- `agent_http_selftest`：http_manager URL 解析边界——端口越界（回绕样本）与显式
  `:0` 必须被 parser 拒绝；边界内端口不被拒绝；空 host 回归（不依赖网络）。
  期望输出 `AGENT HTTP SELFTEST PASSED`（exit 0）。

## 语音素材

内置素材 `assets/voice_16k.pcm`（构建时复制为 `build/voice_16k.pcm`）：

- **内容**："请问，现在是几点钟？"（edge-tts 合成，zh-CN-XiaoxiaoNeural）
- **格式**：16kHz / 16bit / 单声道 / 小端 PCM（raw），2.592 秒，82944 字节
- **生成方式**（需联网）：
  ```bash
  pip3 install --user edge-tts
  edge-tts --voice zh-CN-XiaoxiaoNeural --text "请问，现在是几点钟？" --write-media voice.mp3
  ffmpeg -y -i voice.mp3 -ar 16000 -ac 1 -f s16le voice_16k.pcm
  ```

换成自己的语音：按同样格式生成 16k/16bit/mono PCM，用 `-DCONFIG_AGENT_DEMO_AUDIO_FILE`
指定，或替换 `assets/voice_16k.pcm`。素材长度决定上传时长（帧数 = 时长 × 12.5 帧/秒）。

## 与上游 your_chat_bot 的差异

| 项 | 上游（T5AI/ESP32 板） | 本 demo（Linux） |
|---|---|---|
| 交互 | 按键/唤醒词/VAD 触发，回合制对话 | 启动后自动上传一段预置语音（一次性） |
| 上行音频 | TDL/TKL 麦克风采集（`ai_audio_input`）+ Opus 编码 | port 契约 `tkl_ai_demo_*` 文件回放 + **PCM 直传**（不开编码器） |
| 下行音频 | 解码播放（`ai_audio_player` + `svc_ai_player`，MP3/Opus） | **整体关闭**（`ENABLE_COMP_AI_AUDIO=n`），语音包丢弃 |
| UI | LVGL 聊天界面 + 表情 + 通知音 | 无（仅日志/stdio 打印文字） |
| 对话主控 | `ai_main`（`ai_chat_main`）+ `ai_mode`（四种对话模式） | 不移植；demo 自己组织主流程（直接复用 `ai_agent` 应用封装） |
| 附加能力 | 低功耗/电池/打印机/MCP/摄像头/图片/APP 角色切换 | 全部不移植 |
| 联网 | `netmgr_*` / lwip / 蓝牙配网 | `tal_netif` + `mqtt_bind` 直连（同 iot_demo） |
| 激活与凭据 | `tuya_authorize_init/read`（license 授权） | 编译期凭据 `tuya_config.h`（同 iot_demo） |
| 板级初始化 | `board_register_hardware()`、PSRAM、CLI | 删除（Linux 无对应实现） |

保留：`ai_agent`（应用封装：output 回调注册、text/file/image 上行）、`ai_skills`（下行
文字解析：ASR/NLG/skill）、`tuya_ai_service`（AI 协议与传输：MQTT 信令 + TCP/TLS 流）、
`ai_user_event`（事件总线）、二维码绑定链路（`mqtt_bind`）。

## 已知限制

- **PID 需开通 AI 服务**：默认 PID `alon7qgyjj8yus74`（上游 your_chat_bot 同款 AI 产品，
  已验证可建会话/可收下行）；授权码沿用 iot_demo 的测试 license。生产使用请换成自己的
  AI 产品 + 授权码（`-D` 或 `src/tuya_config_secrets.h`）。
- **默认 UUID/AuthKey 是仓库内的测试 license**（与 iot_demo/p2p_demo 相同）：同一 license
  不能多处同时在线，与其他 demo 并行跑会互相踢下线。
- **激活数据存在运行目录的 `tuyadb/`**：首次运行需用涂鸦 App 扫码激活；换目录运行等于换
  设备身份，需要重新扫码（或把已激活的 `tuyadb/` 一起复制过去）。
- **上行音频为 PCM 直传**（`codec_enable=FALSE`）：不启用 Opus 编码。若云端对上行编码有
  要求，需打开 `ENABLE_COMP_AI_AUDIO_CODEC_OPUS`（引入 libopus 依赖）并改
  `ai_agent.c` 的音频属性分支。
- **只上传一次**：触发为一次性（用户确认口径）；如需多轮，重新运行或扩展触发线程。
- **下行语音被丢弃**：不解码不播放；云端仍会下发 TTS 音频流（占用流量但不处理）。
- **NLG 末段补打的验证边界**：多段 NLG 流的末段（eof=1）经 STOP 携带时会补打，该消费
  契约由 `agent_nlg_selftest` 覆盖（变异验证有效）；但**云端真实分包形态未实测**——三次
  实跑的末包恰好均为空，未触发该分支。契约层通过 ≠ 端到端验证。
- **URL parser 边界（媒体下载相关，1c 评审 R5）**：不支持 IPv6 字面量（`[::1]`）与含
  userinfo 的 URL（`user:pass@host`）——两者均属"误报性质的安全失败"：host 段在第一个
  `:` 截断后，端口段读到非数字、命中显式 `:0` 非法检查报 INVALID_PARM，**并非显式的
  IPv6/userinfo 检测**（将来若要支持，需在 host 段显式处理括号/userinfo）；另 >1023 字符
  的媒体 URL 会被 `fill_url_head` 静默截断（`DEF_URL_LEN=1024`，实测 1439B URL 的
  `&token=` 段丢失；TTS URL 有 128 长度限制兜底、图片 URL 未见长度校验）。媒体下载整体
  未实现（6ba0506 安全失败），故当前无实际影响；将来实现下载时必须处理上述三点。
- **媒体 URL 下载未实现**：云端若下发图片/文件/音频的下载 URL（`tuya_ai_agent.c` 的
  image/file dld 路径），agent-core 侧当前**安全失败**（错误日志
  `http media download not implemented`），不会下载成功。原因：原实现调用 iot-core 的
  `http_read_content`，与 agent-core 的 session context 布局不兼容（类型混淆，实测段
  错误），已替换为明确失败；完整流式下载待按需实现。当前文字链路（ASR/NLG/emoji）不
  触发该路径。
- **AI monitor（TCP 5055）**：core 的 `ai_monitor` 会监听本机 5055 端口输出 AI 协议流供
  调试（`ENABLE_AI_MONITOR`，默认开）。端口被占用时 monitor 自身降级重试（日志可见
  `create server socket failed`），**不阻断 AI 链路**（实测：占用 5055 后 ASR/NLG 正常）。

## 目录结构

```sh
+- agent_demo
    +- assets
    |   -- voice_16k.pcm            # 上行语音素材（edge-tts 合成，16k/16bit/mono）
    +- src
    |   -- tuya_main.c              # 入口：初始化、连云、事件处理、AI 链路启动与触发
    |   -- agent_audio.c/.h         # 上行：读素材 → 按帧喂 tuya_ai_audio_input
    |   -- agent_downlink.c/.h      # 下行：注册事件回调，打印 ASR/NLG 文字
    |   -- agent_chain_selftest.c   # 链路自测（目标 agent_chain_selftest）
    |   -- tuya_config.h            # PID/UUID/AuthKey（可被 secrets 或 -D 覆盖）
    |   -- tuya_config_secrets.h.example  # 凭据覆盖模板
    -- CMakeLists.txt
    -- Kconfig                      # AGENT_DEMO* 开关
    -- agent-demo-design.md         # 技术方案文档
```

音频适配契约（port 层）：

```sh
+- port
    +- include/media/tkl_ai_demo.h  # 音频输入最小契约（open/read/close）
    +- src/tkl_ai_demo.c            # Linux 实现：PCM 文件回放（真实设备替换点）
```
