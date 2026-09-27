# coreHTTP / coreMQTT 升级分析（v2.0.1→v3.1.3、v1.0.1→v5.0.2）

> 分析时间：2026-09-24。材料：本目录下四个官方版本快照 + `diffs/` 里
> 我们手工修改的完整 diff。结论先行：**两库都可以升级到最新**，
> coreHTTP 收益极大（私有状态机可整体删除），coreMQTT 的差异在
> wrapper 层消化（官方源码可保持零修改）。

---

## 一、现状与手工修改点

### coreHTTP（原 v2.0.1，官方最新 v3.1.3）

| # | 修改点 | 位置 | 性质 |
|---|---|---|---|
| 1 | `HTTPClient_Request()` 一步式请求 + `HTTP_RECV_*` 接收状态机 + `HTTPClient_Recv()` + `HTTP_SEND_DISABLE_RECV_BODY_FLAG`（约 300 行） | `core_http_client.c/.h` | 私有扩展：OTA 分块下载需要"先收头、再分块收 body" |
| 2 | 日志宏接 TAL（`LogError→PR_ERR` 等） | `core_http_config_defaults.h` | 可移到用户配置头（官方允许覆盖） |
| 3 | `HTTPResponse_t.pHeaders` 去掉 `const` | `core_http_client.h` | 伴随私有状态机产生，升级后回退 |
| 4 | http_parser **submodule 实体化**；剔除 `source/interface` | 目录/CMake | 迁移性处理 |

**消费面（关键）**：私有 API 的调用者**全部在连接层内**——
`http_download.c`（OTA 分块下载）、`http_session.c`（streaming）、
`http_client_wrapper.c`（一步式）。外部（cloud/p2p/ai）只依赖
`http_client_interface.h` wrapper 接口，不受升级影响。

### coreMQTT（原 v1.0.1，官方最新 v5.0.2）

| # | 修改点 | 位置 | 性质 |
|---|---|---|---|
| 1 | `MQTT_Init()` 加第 6 参数 `void *userData`；`MQTTContext_t` 加字段 | `core_mqtt.c/.h` | 私有扩展：事件回调里反查 wrapper 上下文 |
| 2 | `MQTTNotAuthorized` 枚举 + CONNACK code 5 映射（官方归 `MQTTServerRefused`） | `core_mqtt_serializer.c/.h` | 私有扩展：云侧未授权场景的精确错误 |

**消费面**：两者均只在 `mqtt_client_wrapper.c`（26/220 行）——云层不直接触碰。

## 二、目标版本收益

### coreHTTP v3.1.3

| 我们的私有实现 | v3.1.3 官方等价物 | 结论 |
|---|---|---|
| `HTTPClient_Recv()` 分块收 body | **`HTTPClient_ReceiveAndParseHttpResponse()`**（可分步调用，`HTTPParserPaused` + `HTTP_RESPONSE_DO_NOT_PARSE_BODY_FLAG`） | 可整体替换 |
| `HTTP_SEND_DISABLE_RECV_BODY_FLAG` | `HTTPResponse_t.respOptionFlags` flag 体系 | 可整体替换 |
| chunked 解析（自写） | **官方 chunked 回调**（每 chunk 触发） | 可整体替换 |
| 一步式 `HTTPClient_Request()` | `HTTPClient_SendHttpHeaders` + `SendHttpData` + `ReceiveAndParseHttpResponse` 组合 | 可整体替换 |

其他变化：解析器 http_parser → **llhttp**（nodejs，嵌套 submodule，官方 pin
release 9.3.1）；`HTTPClient_Send` 拆分为细粒度三段式；`pHeaders` 恢复 `const`。

**升级后约 300 行私有状态机整体删除，官方源码修改面只剩 Log 宏
（挪进 `core_http_config.h`，官方允许的覆盖机制）**。

### coreMQTT v5.0.2

- 新增：`MQTT_InitStatefulQoS`（QoS 状态化重传）、`MQTT_CancelCallback`、
  向量 IO、`MQTT_CheckConnectStatus` 等；
- `MQTT_Init` 签名与 v1 相同（**userData 官方至今未提供**）；
- `MQTTNotAuthorized` 官方 v5.0.2 仍未提供（CONNACK 拒绝统一报 `MQTTServerRefused`）；
- `MQTTContext_t` 结构大改（transport 内嵌值、publish 记录指针化、回调返回 bool）。

**按既定决策：差异在 wrapper 层消化，官方源码零修改**：
- `userData` → wrapper 自建注册表（4 槽），回调按 `pContext` 指针反查上下文；
- 未授权判定：`MQTTServerRefused` 映射（v2.3.1 CONNACK 拒绝统一报此码）。

## 三、风险清单与对策

| # | 风险 | 等级 | 对策 |
|---|---|---|---|
| 1 | OTA 分块下载改造：`http_download.c` 从"Recv 循环"改为"ReceiveAndParse 循环"，断点/偏移语义等价性 | **高** | 逐行对照旧状态机行为映射；改造后真实 OTA 下载回归 |
| 2 | `http_session.c` streaming 模式标志替换 | 中 | 仅标志替换 + 读取路径改官方 API |
| 3 | llhttp 与原 http_parser 行为差异（头部解析边界） | 中 | 按官方 pin 版本（9.3.1）引入；现有 HTTP 用例回归 |
| 4 | `pHeaders` 恢复 const：消费方若写该指针需排查 | 低 | 消费方目前仅读取 |
| 5 | coreMQTT v5 `MQTTContext_t` 大改：wrapper 字段适配 | 中 | 编译驱动逐个适配 |
| 6 | coreMQTT v5 行为变化：重传/QoS/keepalive 语义升级 | 中 | wrapper 层回归：连接→订阅→发布→断开 |
| 7 | 配置头与新版 config_defaults 的宏集合变化 | 低 | 编译驱动补齐 |
| 8 | 官方 `source/interface` 与 tuya_transport 的 transport_interface.h 同名冲突 | 低 | include 路径不含 `source/interface`（既有做法） |

## 四、实施方案

### Phase 1：coreHTTP v2.0.1 → v3.1.3（已开始）
1. `third_party/coreHTTP` 替换为 v3.1.3 **git submodule**（嵌套 llhttp 9.3.1 已初始化）✓；
2. `core_http_config.h`（tuya 配置头）接管 TAL Log 宏；
3. 适配三个调用文件（编译驱动）：
   - `http_client_wrapper.c`：`HTTPClient_Request` → 官方三段式组合；
   - `http_download.c`：`DISABLE_RECV_BODY` + `Recv` 循环 → `respOptionFlags` + `ReceiveAndParseHttpResponse` 循环；
   - `http_session.c`：同上标志替换；
4. 编译 + 现有功能回归。

### Phase 2：coreMQTT v1.0.1 → v2.3.1（**v5.0.2 经验证不可行，已改定 v2.3.1**）

> 实施中的关键发现：v5.0.0 起 coreMQTT 默认使用 **MQTT v5.0 协议**
> （`core_mqtt_serializer_private.c: #define MQTT_VERSION_5 (5U)`，硬编码、
> 无版本选择机制），CONNECT 包 protocol level=5，Tuya broker（3.1.1）收到后
> 直接断连（`MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY`）。故回退至 v2.3.1
> （v5.0.0 前最后版本，仍为 MQTT 3.1.1 协议）。
>
> v2.3.1 相比 v1.0.1 的适配点：QoS1/QoS2 订阅需先调
> `MQTT_InitStatefulQoS()`（wrapper 已补，含 outgoing/incoming 记录表各 10 条）。

（官方源码零修改，已完成）
1. `third_party/coreMQTT` 替换为 v5.0.2 **git submodule** ✓；
2. `mqtt_client_wrapper.c` 适配（差异全部在 wrapper 层消化）：
   - 回调返回 `bool`；`pContext->userData` → 注册表反查；
   - `MQTT_Connect` 补 `timeoutMs` 语义位 + 两个 `NULL` props；
   - `MQTT_Disconnect(pContext, NULL, NULL)`、`Subscribe/Unsubscribe/Publish` 补 `NULL` props；
   - `MQTT_ProcessLoop` 去掉超时参数（收包超时由 transport recv 控制）；
   - `MQTTNotAuthorized` → `MQTTServerRefused` 映射；
3. 编译 + 连接回归。

### 回滚方案
- 替换前的旧副本与全部 diff 已存档于本目录（`diffs/` 与四份官方快照）；
- git 侧 `third_party/coreHTTP|coreMQTT` 的变更可直接回滚。

## 五、后续 submodule 化路径

升级完成、差异消化后：
- coreHTTP：剩余差异 = `core_http_config.h`（用户配置头，放 wrapper 目录）+
  llhttp 走官方嵌套 submodule（已就位）→ 当前已是 submodule；
- coreMQTT：wrapper 层消化彻底 → **官方源码零修改**，当前已是 submodule。
