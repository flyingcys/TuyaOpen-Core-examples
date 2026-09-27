# coreHTTP / coreMQTT 升级实操全记录与踩坑笔记

> 时间：2026-09-24。配套文档：`upgrade-analysis.md`（升级前的分析决策）、
> `diffs/`（旧版手工修改点）。本文是**实施过程的全记录**——每一步做了什么、
> 踩了什么坑、怎么定位、怎么修。下一个升级三方库的人请先读这篇。

---

## 〇、升级前后对照

| 项 | 升级前 | 升级后 |
|---|---|---|
| coreHTTP | v2.0.1 源码拷贝 + ~300 行私有状态机 + http_parser 实体化 | **v3.1.3 git submodule**（嵌套 llhttp 9.3.1），官方源码零修改 |
| coreMQTT | v1.0.1 源码拷贝 + userData/NotAuthorized 两处 patch | **v2.3.1 git submodule**，官方源码零修改，差异全在 wrapper 层 |
| transport_interface.h | v1.0.1 时代（tuya 深度定制版） | 同步 v5.0.2 官方版 + tuya 定制段（guard 内） |
| 运行状态 | — | MQTT 连接/订阅/事件全链路通，无崩溃 |

> ⚠️ 为什么 coreMQTT 不是 v5.0.2：见「坑 7」——v5.0.x 默认 MQTT 5.0 协议，
> Tuya broker（3.1.1）直接断连。**这不是技术失败，是协议不兼容**。

## 一、仓库结构陷阱（最先踩、最费时间）

### 坑 1：`linux/` 不是独立 git 仓库

**现象**：在 `tuyaopen-core/` 里跑 `git submodule add ...`，submodule 却注册到了
**外层仓库 `tuyaopen-core-examples`** 的 `.gitmodules`（路径自动带 `linux/` 前缀）。

**真相**：本工作区只有**一个 git 仓库** `tuyaopen-core-examples/`（.git 在外层），
`linux/` 只是普通目录。git 命令从任何子目录跑，都会向上找到外层仓库；
只有 `tuyaopen-core/`（submodule，自带 .git 文件）内部的 git 命令才作用于它自己。

**教训**：
```bash
# 操作 tuyaopen-core 内的 submodule 前，先确认作用仓库：
git -C tuyaopen-core rev-parse --show-toplevel   # 应输出 .../linux/tuyaopen-core
# 带明确 -C 执行，不依赖 shell 的当前目录：
git -C tuyaopen-core submodule add https://github.com/FreeRTOS/coreMQTT third_party/coreMQTT
```

### 坑 2：shell 的 cwd 会"漂移"

本次多次发生：命令 A 里 `cd 某目录`，命令 B（新 shell 调用）仍在旧 cwd，
导致 `mv`/`clone`/`cp` 落错位置（快照一度出现在 `linux/corehttp-coremqtt/`
而分析文档在 `tuyaopen-core/corehttp-coremqtt/`，后来还出现过
`rmdir` 掉当前目录后 git 报 "Unable to read current working directory"）。

**教训**：每条涉及路径的命令都用**绝对路径**开头重锚（`cd /abs/path &&`），
或者 `git -C`、`cp /abs/a /abs/b`；删目录的命令更要先确认不在其中。

## 二、submodule 操作的三个坑

### 坑 3：`git submodule add -b <tag>` 不支持 tag

`-b` 只接受**分支名**，传 tag 会报 `fatal: 'origin/vX.Y.Z' is not a commit`
（shallow clone 下尤甚）。**正确姿势**：

```bash
git submodule add <url> <path>          # 默认分支
cd <path> && git checkout vX.Y.Z        # 手动 checkout tag
cd .. && git add <path>                 # 更新宿主的 gitlink
```

### 坑 4：index 里的旧条目会卡住 `submodule add`

报错 `fatal: 'third_party/xxx' already exists in the index`——旧拷贝目录
曾被 `git add` 过，或此前失败的 add 残留。清理四件套：

```bash
git rm -r --cached third_party/xxx            # 清 index
rm -rf third_party/xxx .git/modules/third_party/xxx   # 清工作区与 gitdir
git config -f .gitmodules --remove-section submodule.third_party/xxx   # 清声明（如有）
```

### 坑 5：嵌套 submodule 用"官方声明 + 初始化"引入

coreHTTP v3.1.3 的解析器换成 llhttp，且官方 `.gitmodules` 里已声明
（`source/dependency/3rdparty/llhttp`）。**不要自己拷贝实体、也不要在新位置
重新 add**（都会偏离官方 pin），直接：

```bash
cd third_party/coreHTTP
git submodule update --init source/dependency/3rdparty/llhttp
# 自动按官方 pin 的 commit 检出（本次为 llhttp 9.3.1）
```

`test/unit-test/CMock` 这类纯测试 submodule **不初始化**（不参与编译）。

## 三、代码适配的坑（按崩溃/报错顺序）

### 坑 6：主版本升级可能改变"默认协议"——抓包才看得见

**现象**：升级后 MQTT 连接被服务器断连，transport 层报
`MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY`（-30848）。

**排查**：在 wrapper 的 send 回调里 dump 包字节：
```c
log_debug("mqtt tx %zu bytes:", len);
for (size_t i = 0; i < len && i < 24; i++) { /* hex dump */ }
```
看到 CONNECT 第 9 字节（protocol level）是 **0x05（MQTT 5.0）**——
Tuya broker 只支持 3.1.1（level 4），直接断连。

**根因**：coreMQTT v5.0.0 起"Added MQTT v5.0 protocol support"，
且 `core_mqtt_serializer_private.c` 里 `#define MQTT_VERSION_5 (5U)` **硬编码、
无版本选择机制**。

**教训**：跨大版本升级通信库，先查 CHANGELOG 里的**协议/线上格式变化**；
对不透明升级（不知道发什么包），在 transport 回调加 dump 是最快的定位手段。

### 坑 7：结构体新增成员 + 局部变量未初始化 = 垃圾函数指针

**现象**：修完协议问题后段错误，bt 停在 `sendMessageVector` 的野地址。

**根因**：v5（及 v2.x 的 TransportOutVector_t 相关演进）给
`TransportInterface_t` 新增了 `writev` 成员。wrapper 里：

```c
TransportInterface_t transport;          // 未初始化！
transport.pNetworkContext = ...;
transport.send = ...; transport.recv = ...;
```

`writev` 是**垃圾值非 NULL** → 库判 `writev != NULL` 就调用 → 野指针。

**修复**：`TransportInterface_t transport = {0};`（指定初始化器把其余成员置零）。
结构体成员（如 session ctx 内的 transport）不受影响——前提是 ctx 分配后有 memset。

**教训**：**给"回调结构体"加成员是上游升级的常见暗坑**——旧代码只初始化
自己认识的字段。凡是局部定义的接口结构体，一律 `= {0}`。

### 坑 8：内存所有权变化——双重释放

**现象**：`http_client_free → tal_free(response->body)` 段错误。

**根因**：旧版私有实现里 `body`/`headers` 是**独立 malloc**（可各自 free）；
v3.1.3 的 `body`/`headers` 只是**响应缓冲 `buffer` 内部的偏移指针**。
`http_client_free` 先 free(buffer) 再 free(body) = 释放 buffer 内部地址 → 崩。

**修复**：只 free `buffer` 一份；body/headers 不再单独 free。
（`http_client_free` 全局 grep 过：body 的所有权只在这一个出口。）

**教训**：升级后凡是"指针字段"，先弄清**指向堆还是指向另一个缓冲的内部**。
所有权模型的文档（谁分配谁释放）要跟着版本走。

### 坑 9：官方 API 拆分后，"一步式"要自己组合

v3.1.3 把 `HTTPClient_Send` 拆成 `SendHttpHeaders` / `SendHttpData` /
`ReceiveAndParseHttpResponse` 三段，且 Send 内部按 `pResponse->respOptionFlags`
决定是否解析 body：

- 只收头：`pResponse->respOptionFlags = HTTP_RESPONSE_DO_NOT_PARSE_BODY_FLAG;`
  （Send 返回后 pBuffer 里**可能已预收部分 body**——要先消费这段，再走裸读）
- 收完头后 body 是纯数据流：直接 `pTransport->recv(pNetworkContext, buf, len)`
  裸读（等价旧 `HTTPClient_Recv` 的内部行为）

**教训**：官方"新增等价 API"的语义细节（预收片段、暂停态、缓冲归属）要读
实现确认，不能只看名字。

### 坑 10：URL 解析器没了 → 自己补一个小的

llhttp 没有独立的 `parse_url`（旧 http_parser 有）。OTA/HTTP 的 URL 格式
固定为 `http(s)://host[:port]/path`，写 30 行本地解析即可
（见 `http_session.c` 的 `http_url_parse()`，已被 `http_download.c` 与
`cloud/tuya_http.c` 共用）。**不要为一个简单场景引一个库**。

### 坑 11：QoS1/QoS2 订阅前必须 InitStatefulQoS

v2.x 起 `MQTT_Subscribe`（qos>0）校验 `incomingPublishRecords` 是否初始化，
未调 `MQTT_InitStatefulQoS` 直接 `MQTTBadParameter`。wrapper 的 context 里补了
outgoing/incoming 记录表各 10 条并在 `mqtt_client_init` 调用。

**教训**：新版库的**运行时前置条件**（不只是编译期 API 变化）要读校验代码。

### 坑 12：transport_interface.h 的 tuya 定制段必须在 include guard 内

该头是 core 侧适配拷贝（非 submodule 文件），tuya 定制为
`NetworkContext_t = tuya_transporter_t` + 内联收发辅助。第一版把定制段追加到了
**guard `#endif` 之外**——同 TU 内第二次 include 该头时定制段重复展开，
报 `redefinition of 'NetworkTransportSend'`。

**教训**：改公共头时，**任何定义（哪怕 static inline）都必须在 guard 内**；
定制段与官方体的边界用注释标清。

## 四、调试方法论（本次用到的四板斧）

### 1. 段错误：gdb 批处理抓 backtrace

```bash
gdb -batch -ex run -ex bt --args ./switch_demo 2>/dev/null | tail -20
```
不需要交互式 IDE；一次拿到崩溃线程、模块、行号。本次两处崩溃都是这样定位的
（第一处 `http_client_free`，第二处 `sendMessageVector`）。

### 2. 协议包透视：transport 回调 dump（定位"服务器断连"的关键）

**场景**：CONNECT 发出后服务器主动断连（`MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY`），
但看不到发出去的包长什么样—— CONNECT 包在 TLS 记录层**内部**，wireshark 抓到
的是密文（要看明文得配 TLS keylog）；而 TLS 握手前没有抓包意义。

**正解**：在 transport 的 send/recv 回调里 dump 字节——这是所有协议包的
**必经之路，且在加密之前（明文视角）**：

```c
/* coreMQTT/coreHTTP 的 TransportInterface.send 指向的回调，临时加 dump */
static int network_write(NetworkContext_t *pNetwork, const unsigned char *pMsg, size_t len)
{
    log_debug("mqtt tx %zu bytes:", len);
    for (size_t i = 0; i < len && i < 24; i++) {
        log_debug("  [%02zu] %02x '%c'", i, pMsg[i],
                  (pMsg[i] >= 0x20 && pMsg[i] < 0x7f) ? pMsg[i] : '.');
    }
    return tuya_transporter_write(transporter, (uint8_t *)pMsg, len, 0);
}
```

**本次 dump 的解读（CONNECT 前 12 字节）**：

```
10            ← 包类型 0x10 = CONNECT
5f            ← 剩余长度 95
00 04 'MQTT'  ← 协议名（长度 4）
05            ← ⚠️ protocol level = 5（MQTT 5.0！）—— 正常应为 04（3.1.1）
c2            ← 连接标志（clean session + username + password）
00 78         ← keepalive 120s
```

第 9 字节 `05` 直接暴露了问题：coreMQTT v5.0.0 起默认用 MQTT 5.0 协议，
而 broker 只支持 3.1.1——**一行字节比任何日志都直观**。

**要点**：
- dump 点选在 **transport 回调**（协议栈的必经窄口），一个函数覆盖所有包；
- recv 方向同样可以 dump（响应/CONNAACK），对拍请求与响应的时序；
- dump 用完**记得删**（本次调试后已移除 network_write 的 dump 段）；
- 补充手段：非 TLS 流量可直接 tcpdump/wireshark；TLS 流量要抓密文外的
  明文可设 `SSL_KEYLOGFILE` 环境变量配合 wireshark（需要库支持 keylog 导出）。

### 3. 死代码判定：链接产物 + 源码引用 双重确认

- **链接级**：对每个 `.o` 取一个特征符号，查最终可执行：
  ```bash
  nm --defined-only xxx.c.o | awk '$2=="T"{print $3; exit}'   # 特征符号
  nm switch_demo | grep -qE " ${sym}$"                        # 是否被拉入
  ```
  ⚠️ 特征符号要选**模块特有**的——本次误判过一次
  （`mbedtls_pk_ec` 是 mbedtls 库自己的符号，不是 tal_asymmetrical 的）。
- **源码级**：对头的每个对外函数逐个 grep 调用者（排除自身与头）。
- 静态库语义：**.o 级拉入**——没被引用的 .o 根本不进可执行，
  所以"编译过"≠"被使用"；反过来"链接进产物"也要求至少一个符号被引用。

### 4. 使用面调查：不要截断输出

本次误判 `tal_network_card_set_active_ip` "零调用"——根因是
`grep ... | head -12` 把 `tuya_main.c:223`（应用层的真实推送者）截掉了。
**查调用面用 `grep -c` 先看总量，或输出重定向到文件全量阅读**；
`examples/`（应用层）和 `port/` 也要纳入搜索范围。

### 5. 段错误排查完整复盘：现象 → 证据 → 结论 的时间线

> **本节是概要。完整的实战记录（实际命令、原始 gdb 输出、字节 dump 解读、
> 误判与弯路）见独立文档 `crash-debugging-walkthrough.md`。**

**Step 0｜先区分"崩溃"还是"功能失败"——只看退出码**

| exit code | 含义 |
|---|---|
| 0 | 正常退出 |
| **124** | `timeout` 截停——**程序还活着**，不是崩溃 |
| **139** | SIGSEGV，段错误 |
| 134 | SIGABRT（assert/abort） |

跑法固定为 `timeout N ./switch_demo > log 2>&1; echo exit=$?`，
退出码加日志尾部，5 秒钟确定问题的类别。

**Step 1｜干净构建对照——排除"脏产物"干扰**

同一个源码，`/tmp` 全新构建跑不崩，用户的旧 build 目录崩/异常——
说明增量编译产物有陈旧成分（升级动了大量头文件/目录，增量编译不总是可靠）。
对策：先把用户的 build 增量编到与源码一致再复现，否则后面全白查。

**Step 2｜逐步拉长运行时间，圈定崩溃阶段**

程序是分阶段的：iotdns（HTTP）→ atop activate（HTTP）→ MQTT 连接。
8 秒跑到 iotdns 就截停了——**崩溃在更后面但没走到**；
拉到 40 秒，日志尾部停在 `tuya_mqtt_start` 后、
`exit=139`——**范围圈定：MQTT 连接阶段**。
（这就是"怎么定位到是 MQTT 的"——不是猜的，是别的阶段都活着。）

**Step 3｜gdb 拿 backtrace——让程序自己告诉你崩在哪**

```bash
gdb -batch -ex run -ex bt --args ./switch_demo 2>/dev/null | tail -14
```

读 bt 的三个要点：
- `Thread 7 "mqtt_bind" received signal SIGSEGV` → **哪个线程**（连云线程）；
- 帧 #0 是崩溃点（可能是库内部/野地址），**向下找第一个"我们的代码"帧**：
  本次 #4 是 `http_client_free`——范围进一步收窄到这函数；
- 加 `-ex 'info locals'` 可看局部变量（若二进制保留符号）。

**第一处**：`tal_free(response->body)` 崩 → 追问 body 指针的来历 →
v3.1.3 的 body 指向响应缓冲内部 → 旧 free 逻辑变成双重释放 → 修所有权。

**Step 4｜崩溃常是串联的——修一个会暴露下一个**

修完第一处再跑，崩在 `MQTT_Connect → sendMessageVector` 野函数指针 →
联想 v5 给 `TransportInterface_t` 新增了 `writev` 成员、而 wrapper 的局部
transport 变量没初始化 → `= {0}` 修复。
（修完 MQTT 崩溃不等于结束，每次修复后**必须再跑**，直到 exit=124。）

**Step 5｜不崩了但连不上——从"崩溃调试"切到"协议调试"**

此时 gdb 没用了（不是内存错误）。在 transport 的 send 回调加**字节 dump**，
看到 CONNECT 包 protocol level = 0x05（MQTT 5.0）→ 查 coreMQTT 源码确认
`MQTT_VERSION_5` 硬编码、无版本选择 → 结论：v5.0.x 与 Tuya broker（3.1.1）
协议不兼容 → 回退 v2.3.1（证据链闭环：字节 → 源码 → 官方 CHANGELOG）。

**Step 6｜最后的功能缺口**

协议通了但 QoS1 订阅报 `MQTTBadParameter` → 读库的参数校验代码 →
v2.x 要求先调 `MQTT_InitStatefulQoS` → wrapper 补上 → 全链路通。

**复盘总结**：整个排查是一条"范围逐步收窄"的链：
退出码（崩溃/失败）→ 运行时长（哪个阶段）→ gdb bt（哪个函数）→
指针来历（哪行代码）→ 字节 dump（什么协议内容）→ 源码/CHANGELOG（为什么）。
每一步的结论都来自一个可复现的证据，而不是猜测。

## 五、文本替换的纪律

本次连续三次因 `ENABLE_LIBLWIP`/`ENABLE_LIBWIP` 拼写差异导致 sed/python
替换"静默未命中"（原文件里两种拼写都出现过——上游的历史笔误）。

**纪律**：
1. 替换前先 `grep` 确认原文**精确拼写**（复制粘贴，不手敲）；
2. `sed -i` 后立刻 `grep` 验证目标串消失/新串出现；
3. python 脚本用 `assert old in s`，不匹配就中止而不是静默继续；
4. 批量替换后跑一次编译，让编译器当验收员。

## 六、最终文件清单（升级涉及）

```
tuyaopen-core/
├── third_party/
│   ├── coreHTTP/            # submodule v3.1.3（嵌套 llhttp 9.3.1）
│   ├── coreMQTT/            # submodule v2.3.1
│   └── CMakeLists.txt       # corehttp 编 llhttp 三文件；coremqtt GLOB source/*.c
├── iot-core/tuya_transport/
│   ├── include/transport/transport_interface.h
│   │                        # 同步 v5.0.2 官方版 + tuya 定制段（guard 内）
│   ├── include/transport/tal_network.h      # 网络卡/ops 类型（自 register.h 并入）
│   ├── include/http/http_session.h          # + http_method_t、http_url_parse 声明
│   ├── include/http/http_client_interface.h # + HTTP_CLIENT_RESPONSE_BUFFER_SIZE
│   ├── src/mqtt/mqtt_client_wrapper.c       # 注册表反查 userData、props/ProcessLoop 适配、InitStatefulQoS
│   ├── src/http/http_session.c              # Send + respOptionFlags、body 预收段+裸读、http_url_parse 实现
│   ├── src/http/http_download.c             # 同上 + URL 解析改公共函数
│   ├── src/http/http_client_wrapper.c       # Request→Send、响应缓冲分配、http_client_free 单份释放
│   └── src/transport/tal_platform.c         # （已删，卡定义并入 port）
└── port/
    ├── include/network/tkl_network.h        # 补齐 8 个声明
    └── src/tkl_network.c                    # 原 tal_posix.c：tkl_net_* 实现 + 标准网卡定义
```

## 七、复盘检查清单（下次升级三方库时过一遍）

- [ ] 读官方 CHANGELOG 的**协议/线上格式变化**（不只是 API 列表）
- [ ] 确认仓库结构：git 命令作用于哪个仓库（`rev-parse --show-toplevel`）
- [ ] submodule：tag 用 checkout 不用 -b；嵌套 submodule 用官方声明 + update --init
- [ ] 用 git 快照/对比目录先跑一遍 diff，列出**修改点清单**再动手
- [ ] 回调/接口结构体局部变量一律 `= {0}`
- [ ] 指针字段的**所有权模型**（谁分配、谁释放、是否指向别的缓冲）
- [ ] 官方新增的"运行时前置条件"（如 InitStatefulQoS）——读参数校验代码
- [ ] 编译驱动迭代 + 真实链路回归（连接→订阅→发布→下载）
- [ ] 全程文档化：分析（upgrade-analysis.md）+ 实操（本文）
