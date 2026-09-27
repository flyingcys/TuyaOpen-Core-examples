# 段错误排查实战记录——从"运行就崩"到"全链路打通"

> 时间：2026-09-24。背景：coreHTTP v2.0.1→v3.1.3、coreMQTT v1.0.1→v5.0.2(后回退
> v2.3.1) 升级完成后，运行 `switch_demo` 出现**段错误**。
> 本文档完整记录当时的命令、原始输出、推理过程和弯路。
> 配套：`upgrade-analysis.md`（升级决策）、`upgrade-walkthrough.md`（升级过程）、
> `diffs/`（修改点）。

---

## 一、排查总览（先看结论）

整个排查是一条"**范围逐步收窄、每步一个证据**"的链：

```
退出码定性(崩溃/失败)  →  运行时长(哪个阶段)  →  gdb bt(哪个函数)
      →  指针来历(哪行代码)  →  字节 dump(什么协议内容)  →  源码/CHANGELOG(为什么)
```

| 步骤 | 动作 | 证据 | 结论/动作 |
|---|---|---|---|
| 0 | 干净构建跑 10s | exit=124 正常 | 排除"代码必然崩"，指向环境/产物差异 |
| 1 | 用户 build 增量编译后跑 8s | exit=124，走到 iotdns | 与源码对齐后不崩——需要更长运行 |
| 2 | 跑 40s | exit=124，但 iotdns 报 `HTTPInvalidParameter` | **第一个功能 bug**（非崩溃） |
| 3 | 修 pBuffer 后跑 45s | **exit=139** | 段错误复现！范围：iotdns 之后 |
| 4 | gdb bt（第 1 次） | #4 `http_client_free` | 双重释放 → 修所有权 |
| 5 | 再跑 | **exit=139 又崩** | 崩溃是串联的，修一个暴露下一个 |
| 6 | gdb bt（第 2 次） | #2 `sendMessageVector`，#0 野地址 | 垃圾函数指针 → `= {0}` 修复 |
| 7 | 再跑 | 不崩了，但 `MQTTRecvFailed` | 从崩溃调试切到协议调试 |
| 8 | network_read 加日志 | `result=-30848` | = `MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY`，服务器断连 |
| 9 | network_write 加字节 dump | CONNECT 第 9 字节 `05` | **MQTT 5.0 协议**！broker 只支持 3.1.1 |
| 10 | 查库源码 | `#define MQTT_VERSION_5 (5U)` 硬编码 | v5.0.x 无法兼容 → 回退 v2.3.1 |
| 11 | 再跑 | `MQTTBadParameter`（订阅） | 读库校验代码 → 补 `MQTT_InitStatefulQoS` |
| 12 | 最终验证 | 45s exit=124，订阅成功，无错误 | **全链路打通** |

## 二、Step 0：先定性——是崩溃还是功能失败？

```bash
cd /tmp/switch_demo_cfg_test && timeout 10 ./switch_demo > /tmp/sd_out.log 2>&1
echo "exit=$?"      # 输出 exit=124
tail -8 /tmp/sd_out.log
```

**退出码速查表**（第一阶段唯一要看的东西）：

| exit | 含义 |
|---|---|
| 0 | 正常退出 |
| 124 | `timeout` 截停——**程序还活着**，不是崩溃 |
| 139 | SIGSEGV，段错误（128+11） |
| 134 | SIGABRT（assert/abort） |

这一步 10 秒钟确定问题的**类别**。本次干净构建 exit=124——**不崩**！
说明"段错误"不是代码必然行为，与运行环境/产物相关。

## 三、Step 1：干净构建对照——排除脏产物

升级动了大量头文件/目录，**增量编译的产物可能是陈旧混合物**。
先把用户的 build 目录增量编到与源码一致，再复现：

```bash
cmake --build examples/switch_demo/build -j$(nproc)
cd examples/switch_demo/build && timeout 8 ./switch_demo > /tmp/sd_user.log 2>&1
```

8 秒版本跑到 `iotdns.c:250 http request send!`（exit=124）——没崩但没走完。
**决策**：程序是分阶段的（iotdns → atop activate → MQTT 连接），
8 秒只走到第一阶段——拉长时间才能看到崩溃点。

## 四、Step 2：拉长运行时间，圈定崩溃阶段

```bash
timeout 40 ./switch_demo > /tmp/sd_user2.log 2>&1
```

**第一次长跑的收获不是段错误，而是第一个功能 bug**：

```
[ty D][http_client_wrapper.c:51] Sending HTTP POST request to h6.iot-dns.com
[ty E][http_client_wrapper.c:62] Failed to send HTTP POST request: Error=HTTPInvalidParameter.
[ty E][iotdns.c:271] http_request_send error:2
```

`HTTPInvalidParameter`——查 v3.1.3 的 `HTTPClient_Send` 校验代码：
**`pResponse->pBuffer == NULL` 会被拒**。而 v3.1.3 的语义是"应用提供
响应缓冲（头+体共用）"，wrapper 升级时漏了分配（`http_response = {0}`）。

**对照旧版实现确认分配策略**（在 `diffs/` 里 grep）：
旧版私有实现是 `HTTP_RECV_INIT` 时分配头缓冲 + Recv 时单独 malloc body——
v3.1.3 变成了"一个缓冲收到底 + 所有权交给调用者"。

修复：wrapper 里分配响应缓冲、`response->buffer` 传出、由 `http_client_free` 释放。

> 此时还没崩——但这个 bug 修完后，45 秒的运行就复现出了段错误
> （exit=139）。**功能 bug 的修复常会"解锁"后面的崩溃**。

## 五、Step 3：gdb 抓第一次崩溃（http_client_free）

```bash
timeout 60 gdb -batch -ex run -ex bt --args ./switch_demo 2>/dev/null | tail -14
```

**原始输出**：

```
Thread 7 "mqtt_bind" received signal SIGSEGV, Segmentation fault.
[Switching to Thread 0x7fffd53f9640 (LWP 1580905)]
0x000055555559d7bc in MEM_Deallocate ()
#0  0x000055555559d7bc in MEM_Deallocate ()
#1  0x000055555559dfe6 in tuya_mem_heap_free ()
#2  0x0000555555599acc in tkl_system_free ()
#3  0x00005555555651cc in tal_free ()
#4  0x00005555555651cc in http_client_free ()
#5  0x000055555558f381 in iotdns_cloud_endpoint_get ()
#6  0x000055555557918a in tuya_endpoint_update_auto_region ()
#7  0x0000555555576233 in mqtt_bind_token_get_thread ()
...
```

**bt 读法（三个要点）**：

1. **哪个线程**：`Thread 7 "mqtt_bind"`——连云线程（不是主线程/网络线程）；
2. **向下找第一个"我们的代码"帧**：#0~#3 是内存库内部帧，#4 `http_client_free`
   才是我们的函数——崩溃点是 `tal_free(response->body)`；
3. **追指针来历**：`response->body` 指向哪？查 v3.1.3 的语义——
   body/headers 只是**响应缓冲 buffer 内部的偏移指针**（旧版是独立 malloc）。
   旧 `http_client_free` 先 free(buffer) 再 free(body) = **释放 buffer 内部地址**。

**修复**：只释放 buffer 一份。

**教训**：崩溃的"物理位置"（MEM_Deallocate）和"逻辑位置"（所有权设计）经常
差好几帧——bt 要读到第一个业务帧，再去查那个指针的一生。

## 六、Step 4：第二次崩溃（sendMessageVector）——崩溃常是串联的

修完再跑，**又崩了，但位置变了**：

```
#0  0x00001388000022b3 in ?? ()                    ← 野地址，无符号
#1  0x00005555555bcea8 in sendMessageVector ()
#2  0x00005555555c0d64 in sendConnectWithoutCopy ()
#3  0x00005555555c2415 in MQTT_Connect ()
#4  0x00005555555a3217 in mqtt_client_connect ()
```

**读法**：#0 野地址 + #1 是库函数——典型的**调用了垃圾函数指针**。
联想：v5 给 `TransportInterface_t` 新增了 `writev` 成员（向量写），库的逻辑是
`writev != NULL 就用 writev`。而 wrapper 里：

```c
TransportInterface_t transport;        // ← 未初始化！writev 是垃圾值
transport.pNetworkContext = ...;
transport.send = ...; transport.recv = ...;
```

**修复**：`TransportInterface_t transport = {0};`（指定初始化器把其余成员置零）。

> **教训**：给"回调结构体"加成员是上游升级的常见暗坑——旧代码只初始化
> 自己认识的字段。凡是局部定义的接口结构体，一律 `= {0}`。
> （结构体**成员**不受影响——前提是宿主分配后有 memset。）

## 七、Step 5：从"崩溃调试"切换到"协议调试"

修完两处崩溃后程序**不崩了**，但 MQTT 连接失败：`MQTTRecvFailed`。
此时崩溃调试手段用完——**不是内存问题，是通信问题**。换武器：
在 transport 的 send 回调里 dump 包字节（TLS 加密前，明文视角）：

```c
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

**dump 输出与逐字段解读**（CONNECT 前 12 字节）：

```
[00] 10        ← 包类型 0x10 = CONNECT              ✓
[01] 5f        ← 剩余长度 95                        ✓
[02-03] 00 04  ← 协议名长度 4                       ✓
[04-07] MQTT   ← 协议名                             ✓
[08] 05        ← ⚠️ protocol level = 5（MQTT 5.0）  ✗ 正常应为 04（3.1.1）
[09] c2        ← 连接标志                            ✓
[10-11] 00 78  ← keepalive 120s                     ✓
```

**第 9 字节 `05` 一眼暴露问题**：coreMQTT v5.0.0 起默认使用 MQTT 5.0 协议
（`core_mqtt_serializer_private.c: #define MQTT_VERSION_5 (5U)` 硬编码、
无版本选择机制），而 Tuya broker 只支持 3.1.1——收到 level 5 的 CONNECT
直接断连（`MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY` = -30848）。

## 八、Step 6：最后的缺口——读库的校验代码

协议通了后 QoS1 订阅报 `MQTTBadParameter`。读 v2.3.1 的校验函数：

```c
if( pContext->incomingPublishRecords == NULL )
{
    if( pSubscriptionList->qos > MQTTQoS0 )
    {
        LogError( "The incoming publish record list is not initialised for
                   QoS1/QoS2 records. Please call MQTT_InitStatefulQoS ..." );
        status = MQTTBadParameter;
    }
}
```

**v2.x 起 QoS1/QoS2 需要先调 `MQTT_InitStatefulQoS`**（运行时前置条件，
编译期完全看不出来）。wrapper 的 context 补两组记录表（各 10 条）并在
`mqtt_client_init` 调用即可。

## 九、误判与弯路记录（这些同样重要）

1. **grep 输出被 head 截断导致误判**：第一版方案误判
   `tal_network_card_set_active_ip` "零调用、恒为 0"——实际
   `tuya_main.c:223`（应用层 wired 链路回调）就是推送者，只是输出被
   `head -12` 截掉。**错误删除了 tuya_lan.c 的活功能，后来发现并恢复**。
   → 纪律：查调用面用 `grep -c` 先看总量，输出进文件全量阅读。

2. **替换静默未命中 ×3**：`ENABLE_LIBLWIP` 被手写成 `ENABLE_LIBWIP`，
   sed/python 替换不报错但也没改任何东西（python 脚本里 "done" 照样打印）。
   → 纪律：python 里 `assert old in s`；替换后 grep 验证；编译当验收员。

3. **Edit 的 old_string 手抄错误 ×2**：重排定制段时结构体分节注释的星号数量
   抄错、`ENABLE_LIBWIP` 又错一次。
   → 纪律：old_string 用**复制粘贴**或更短的唯一锚点，不手抄长段。

4. **sed 误改结构体成员**：把 `TransportInterface_t transport;`（http_session
   结构体成员）全局替换成 `= {0}`——C 结构体成员不支持该语法，编译报一片
   连锁错误。
   → 纪律：全局 sed 前先 `grep -n` 看所有命中点是否语义相同。

5. **干净构建的意义差点被忽略**：第一反应是"用户环境问题"，实际上
   "干净构建不崩 + 用户 build 崩"这组对照，本身就把排查范围从
   "全代码"缩小到"增量产物 + 长时运行路径"。

## 十、复盘检查清单

- [ ] 退出码定性（崩溃 139 / 截停 124 / abort 134 / 正常 0）
- [ ] 运行时长递增，圈定崩溃阶段（程序是分阶段的）
- [ ] 干净构建对照，排除脏产物
- [ ] `gdb -batch -ex run -ex bt` 拿调用栈，读到第一个业务帧
- [ ] 修复后**必须再跑**——崩溃常是串联的
- [ ] 不是内存错误时切协议调试：transport 回调 dump 字节
- [ ] 错误码查表（-30848 这类魔数对着 mbedtls/库错误码头文件翻译）
- [ ] 读库的参数校验代码找运行时前置条件
- [ ] 修复后跑全链路回归，最后删调试代码
