# 双机 PK 交接文档：core 与 tkl 层解耦 + psram/EXT_RAM 全清

**终局基线（冻结）**：父仓 `0d2fe67` / core `b55b909`（两仓工作区干净）
**PK 双方**：linux-0a（本会话）/ linux-20
**日期**：2026-10-12
**双方终局判定**：无未闭环 Critical/Important

---

## 一、PK 覆盖的三轮工作

### 第一轮：core 与 tkl 层解耦验收（终局基线 bda29c8 / fae5262）

目标不变量：**core 在编代码零 tkl 符号**（调用 / 类型 / include 三口径）。

| 项 | 结论 | 关键证据 |
|---|---|---|
| 静态门禁 | 成立 | `tkl_` 调用 48 处 = 门控代码 46（ble 32 + AI 14）+ 注释 2；`TKL_` 类型 118；`tkl_` include 6 |
| **对象级门禁（最强）** | 成立 | 逐归档 `nm -u`：3 demo × 19 个 core 归档，tkl_ **未定义符号全 0**；port 归档定义 92 个 |
| 构建 | 通过 | iot/agent/p2p 三 demo，`': warning:'` 精确计数 0/0/0，`Building C` 371/377/379 行（全量重编） |
| 运行时自检 | 通过 | 自写 41 项 KAT 全 PASS（sha256/sha1/md5/HMAC/ECB/CBC/CTR/GCM/pkcs7/thread_get_id/queue 并发） |
| 变异验证 | 判别力成立 | thread_get_id→NULL（3 断言红）、ECB 模式反转（2 断言红） |
| **符号级交叉印证** | — | 合并前隔离编译：tal_hash.c 未定义 tkl_ = **15**，tal_symmetry.c = **8**，合计 **23**（与"删 23 个透传函数"吻合）；合并后 = **0** |

### 第二轮：真机前对抗性检查（esp32 侧静态风险）

| 方向 | 结论 |
|---|---|
| 1. thread_get_id 身份稳定性 | 两 port 实现函数体**逐字未变**（linux `pthread_self()` / esp32 `xTaskGetCurrentTaskHandle()`）；解耦只加主卡分发层，值域语义零变化。TCB 复用致 pjlib TLS 旧行失配属 FreeRTOS 既有属性 |
| 2. psram 删除影响 | **当前配置前后完全等价**（`# CONFIG_SPIRAM is not set`，原实现有 `#if CONFIG_SPIRAM` 回退）；即使换开 SPIRAM 的板子，常驻增量上限 2×512B=1KB |
| 3. crypto 硬件加速链 | **只改名不动加速**：`mbedtls_aes_*` 各调用点前后逐个相同；ALT 重定向在 esp-idf mbedtls 组件编译期，core 改名不触及 |
| 4. 卡成员完整性 | 24/24 差集为空；解门控后依赖仅 `freertos/task.h`，P2P=OFF 也能编 |
| 5. 假绿审查 | 点名 2 处（反向假绿 + 缺真机验证边界），已由对端在文档闭环 |

### 第三轮：psram/EXT_RAM 全清（core f261e97）+ 三项小修（core b55b909 / 父仓 0d2fe67）

- 用户指令「core 不允许这样的配置」→ 清 `ENABLE_EXT_RAM` 死块 ~18 处、`AI_*_IN_PSRAM` 守卫 + Kconfig 两项、`THREAD_CFG_T.psram_mode` 死字段（13 setter）、ringbuf `OVERFLOW_PSRAM_*` 枚举、`tal_psram_*` 死引用
- 验证分档（勿平铺成"都不被编译"）：24 = 18 `.c` + 4 `.h` + 2 Kconfig；18 `.c` 中 14 个经构建验证、4 个不被编译（`ai_mcp_server.c`/`local_store.c` 经 `-fsyntax-only` **通过**；`ai_audio_input.c`/`opus_ipc.c` **编不了**，依赖头整层不在本树，pre-existing）
- 三项小修：cipher_wrapper malloc 判空 + ret 检查（已修）、port 两个 PSRAM 枚举删除、demo/文档 psram 字样清零

---

## 二、本次 PK 发现并闭环的缺陷

### cipher_wrapper.c encrypt 路径（已修，b55b909）—— 严重性由"Minor"上调为"失败路径必崩"

`iot-core/tuya_transport/src/tls/cipher_wrapper.c`：
- `mbedtls_cipher_auth_encrypt_wrapper` 第 47 行 `tal_malloc` **不查返回值**，decrypt 侧对称位置却查了
- 更重成因（对端 linux-20 走完调用方语义后定级，我独立复现确认）：调用方 `mbedtls_symmetry.c:253` 传 `size_t olen = 0` → wrapper 内 mbedtls 失败**不写 `*olen`** → 无符号 `*olen -= tag_len` 下溢为 ~1.8e19 → `memcpy` 巨长拷贝**必崩**
- 可达路径：`tuya_ai_biz.c:186/313`（AI 视频流/图像上传）→ `tal_aes_gcm_encode` → wrapper
- 归属：`fee5118 整理目录`，**pre-existing**，非解耦引入
- 修复：补 malloc 判空 + `if (ret != 0) goto EXIT`

### 仍开放的欠账（待用户拍板，非本次引入）

- `tal_aes_create_init` 不查 `tal_malloc` 返回值——**2026-10-12 用户裁决"修复"，
  已落地**（镜像 hash 版写法：`if (ctx) init else return OPRT_MALLOC_FAILED`；
  linux 三 demo + esp32 构建通过）。至此 PK 记录的两条同类欠账全部关闭。

---

## 三、PK 中双向纠错（记录在案）

**对端采纳并落地的（3 处）**：
1. psram 行表述改写（我第一轮漏核原实现的 `#if CONFIG_SPIRAM` 回退分支与 sdkconfig → 反向假绿）
2. 验证分母口径明确（24/18/4/2/22 各指什么，勿平铺）
3. opus/speex 缺头不对称记入文档

**我认领的自身问题（3 处）**：
1. 测试自身 3 次假红（GCM 传 NULL 违反契约 / 队列消费类型与 item_size 不配 / 隐式声明 tal_malloc）——均非被测代码缺陷
2. 只在 core 仓搜 `tkl_hash.h`（实为在 port）得出"合并前不可编译"的错误结论，经对端反质询撤回
3. 工具报 `call Bash instead` 我误读为工具不存在，白等数轮并误告对端"环境故障"

---

## 四、工具恢复后的三项待办（已完成）

1. ✅ 方向 3 换锚：`tuya_tls.c` 只调 `mbedtls_ssl_*`，EXT_RAM 删除与底层加速链正交
2. ✅ 方向 4 重核：`THREAD_CFG_T` 缩小后卡成员仍 24/24
3. ✅ esp32 ALT 细节：ROM 为头路径注入（`main/CMakeLists.txt:30-36`）；`ENABLE_PLATFORM_MBEDTLS=y`（`sdkconfig.defaults:5`）；ALT 宏清单由对端在其有 IDF 树的机器实测补齐（`$IDF_PATH/components/mbedtls/port/include/mbedtls/esp_config.h`，3067 行，ALT 段 150-239）

---

## 五、验证边界（如实保留，不阻塞）

- **esp32 真机未烧录**：本轮全部为静态/链接期判据；对端已在 §2.3/§8 写明"勿把门禁全绿当真机可用"
- **esp32"去组必失败"实验**：对端本机实测，我无 idf 工具链不能本地闭环（环境边界）
- **esp_config.h ALT 宏内容**：对端本机实测（其有 IDF 树），我方依文档 §4.5 + sdkconfig 实证
- **p2p 运行时路径未实测**：需 App 激活；`tal_thread_get_id` 运行时证据仅在 linux 自检

---

## 六、复现命令（可复核）

```bash
cd /home/share/samba/open-components/tuyaopen-core-examples

# 基线与干净度
/usr/bin/git rev-parse HEAD                                    # 0d2fe67
/usr/bin/git -C linux/tuyaopen-core rev-parse HEAD              # b55b909
/usr/bin/git status --porcelain | /usr/bin/grep -v corehttp-coremqtt   # 空
/usr/bin/git -C linux/tuyaopen-core status --porcelain         # 空

# psram/EXT_RAM 清零（预期 0 / 0）
/usr/bin/grep -rni 'psram\|ext_ram' linux/tuyaopen-core --include='*.c' --include='*.h' | /usr/bin/wc -l
/usr/bin/grep -rni 'psram\|ext_ram' linux/port esp32/port --include='*.c' --include='*.h' | /usr/bin/wc -l

# 两树同步（预期仅 CMakeLists.txt）
/usr/bin/diff -rq linux/tuyaopen-core esp32/examples/iot_demo/components/tuyaopen-core | /usr/bin/grep -v '\.git\|mbedtls'

# 对象级 tkl 门禁（需构建产物；预期全 0）
for a in $(/usr/bin/find <build> -name '*.a' -path '*tuyaopen-core*'); do nm -u "$a" | /usr/bin/grep -c '\btkl_'; done
```