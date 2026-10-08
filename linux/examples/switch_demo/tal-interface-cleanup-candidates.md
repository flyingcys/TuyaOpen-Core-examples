# TAL / port 低频与死接口清理候选清单

> **审计日期**：2026-09-30
> **审计范围**：linux 树（`tuyaopen-core` 子模块 + `port/` + `examples/switch_demo/`）
> **状态**：**仅记录，尚未执行删除**（用户决定先归档）
> **背景**：ops 卡注册制收编（契约头 `tuyaopen-core/iot-core/tal_system/include/tal_api_ops.h`）后，为"最小适配"持续清理死接口。零调用接口已在前几轮清理完毕（`tal_realloc`、psram 全族、tal_hash/tal_symmetry/tal_x509 自测、`tal_thread_dump_watermark`、`tkl_watchdog` 等），本清单是**剩余候选**。

## 计数口径

- 以**编译产物为准**：`examples/switch_demo/build` 下 175 个 `.o` 逐个 `objdump -r` 取重定位点（每处调用/取地址一条），再与源码 `/usr/bin/grep` 交叉。
- 宏展开已计入（`Malloc/Free/Calloc` → `tal_malloc/free/calloc`、`PR_*` → `tal_log_print_secure`、`tal_semaphore_wait_forever`、`TAL_FD_*`）；函数指针赋值已计入（cJSON hooks、mbedtls 熵源回调）。
- 174 个 `.o` 中只有 **111 个真正链接进 `switch_demo`**，其余是静态库中未被引用的成员；计数以 111 个为准。
- 未编译面（agent-core / p2p-core / tkl_wifi / tkl_bt / `port/include/init` / tuya_device_timer / tal_bluetooth）不计入调用点数，但单独标注。

---

## 一、卡成员（TAL_API_OPS_T）使用度总表（30 个）

> **注（2026-10-07 更新）**：OTA 四成员已拆为**独立卡**（契约 `tuyaopen-core/iot-core/tal_system/include/tal_ota_ops.h`，port 实现 `port/src/tkl_ota.c` 的 `tal_ota_ops_platform`），主卡 `TAL_API_OPS_T` 现为 **26 个成员**；下表 ota 行保留（仍适用于独立卡），B1 的删除清单路径已同步更新。

| # | 卡成员 | 最终调用点数 | 调用方摘要 | 结论 |
|---|---|---|---|---|
| 1 | `log_output` | 1 | `tuya_main.c:51`（日志出口 + QR 码输出，经 `tal_log_init` 注册） | **留**（唯一出口） |
| 2 | `malloc` | 140 | 全 core/cloud/lan/transport/kv | 留 |
| 3 | `free` | 313 | 同上 | 留 |
| 4 | `calloc` | 11 | workqueue、mqtt_service、http 等 | 留 |
| 5 | `get_free_heap_size` | 2 | `tal_system.c:56`（malloc 失败日志）、`tuya_main.c:98` | **可选删**（见 B6） |
| 6 | `sleep` | 26 | 全 core（重试/轮询） | 留 |
| 7 | `reset` | 1 | `tuya_main.c:131`（RESET_COMPLETE 事件） | 留（产品功能） |
| 8 | `get_millisecond` | 8 | tal_time_service、matop、mqtt wrapper | 留 |
| 9 | `get_random` | 1 | `tuya_entropy_poll.c:24`（**TLS 唯一熵源**） | **留**（务必） |
| 10-13 | `semaphore_*` ×4 | 3/3/9/3 | tal_sw_timer、tal_workqueue、tuya_iot | 留 |
| 14-17 | `mutex_*` ×4 | 17/66/78/11 | 全 core | 留 |
| 18 | `thread_create` | 5 | workqueue、sw_timer、lan_sock、tuya_ota、mqtt_bind | 留 |
| 19 | `thread_release` | 3 | workqueue、lan_sock、mqtt_bind（经 `tal_thread_delete`） | 留 |
| 20 | `thread_set_self_name` | 1 | `tal_thread.c:245`（内部，设 OS 线程名） | 留（已决策） |
| 21 | `thread_is_self` | 1 | `tal_thread.c:137`（内部，判断是否删自己） | 留（线程语义必需） |
| 22 | `thread_diagnose` | 1 | `tal_workq_service.c:194`，但入口 `tal_workq_dump` **自身 0 调用** → 死链 | **建议删**（见 B2） |
| 23-26 | `queue_*` ×4 | 1/2/1/1 | `lan_sock.c`（LAN socket 事件队列） | 留 |
| 27 | `ota_get_ability` | **0** | 全仓（含未编译面）零调用 | **建议删**（见 B1） |
| 28-30 | `ota_start/data_process/end_notify` | 各 1 | `tuya_ota.c:61/81/118`（云 OTA 全链） | 留 |

> ≤2 调用的共 12 个；其中**真正可去**的是 `thread_diagnose`、`ota_get_ability`，次选 `get_free_heap_size`。

---

## 二、建议删候选（按"改动小 + 价值低"排序）

### B1. 卡成员 `ota_get_ability`（0 调用，改动最小）

- 事实：全仓（编译 + 未编译）无任何调用方；port 实现是 6 行假实现（`*image_size=-1; *type=FULL|DIFF`）。
- 删除清单：
  - `tuyaopen-core/iot-core/tal_system/include/tal_ota_ops.h`（成员声明）
  - `tuyaopen-core/iot-core/tal_system/src/tal_api.c`（分发实现）
  - `tuyaopen-core/iot-core/tal_system/include/tal_ota.h`（声明 + 注释）
  - `port/src/tkl_ota.c`（static 实现 + 卡内绑定）
- 代价：4 个文件，**零调用方改动**。OTA 下载/校验/收尾三件套不受影响。

### B2. 卡成员 `thread_diagnose`（1 调用，且在死链上；port 实现是纯 stub）

- 事实：唯一调用点在 `tal_workq_service.c:194`（`tal_workq_dump` 内），而 `tal_workq_dump` 自身 0 调用；port 的 `tkl_thread_diagnose` 直接 `return OPRT_NOT_SUPPORTED`。
- 删除清单：`tal_api_ops.h` 成员 + `tal_thread.c` 实现 + `tal_thread.h` 声明 + `tkl_api_ops.c` 实现与绑定；**连带死链上游** `tal_workq_service.c` 的 `tal_workq_dump` 与 `tal_workqueue.c` 的 `tal_workqueue_traverse`（见 B5）。
- 代价：5 个文件，零调用方改动。

### B3. `tal_sha1` 全族（12 个函数，整条链零调用）

- 证据：`tal_sha1_mac` 0 调用 → 其调用的 `tal_sha1_mac_create_init/free/starts/update/finish` 各自仅 1 引用（全在死链内）；`tal_sha1_ret` 仅被 `tal_sha1_mac_starts` 调用；`tal_sha1_create_init/free/starts_ret/update_ret/finish_ret` 只被上述两者引用。
- 删除清单：`tuyaopen-core/iot-core/tuya_transport/src/tls/tal_hash.c` 的 12 个函数实现 + `include/tls/tal_hash.h` 的 12 个声明。
- ⚠️ **不要误删**：SHA256/MD5 家族是活的（`tal_sha256_mac` 3 处：LAN×2+OTA×1；`tal_md5_ret` 5 处）。
- 代价：2 个文件，零调用方改动。

### B4. `tal_time` 夏令时/设置死族（7 个函数）

- 证据：`tal_time_set`(0) → `tal_time_mktime`(1)；`tal_time_get_sum_time`(0) → `tal_time_get_sum_time_posix`(1)；`tal_time_get_sum_zone`(0)、`tal_time_set_sum_zone_tbl`(0)、`tal_time_config_update`(0，无声明)。
- 删除清单：`tal_system/src/tal_time_serivce.c` 的 7 个函数 + `include/tal_time_service.h` 的对应声明。
- ⚠️ **不要误删**：`tal_time_is_in_sum_zone` 表面 1 调用，但调用方是活的 `tal_time_get_local_time_custom`（日志时间戳链）。
- 代价：2 个文件，零调用方改动。

### B5. workqueue 死函数包（9 个函数）

- 证据（编译代码 0 调用，未编译面也无）：
  - 包装层：`tal_workq_schedule_instant`、`tal_workq_cancel`、`tal_workq_get_num`、`tal_workq_dump`
  - 底层随之死：`tal_workqueue_schedule_instant`、`tal_workqueue_traverse`、`tal_workqueue_get_num`、`tal_workqueue_get_thread`、`tal_workqueue_stop_delayed`
- 删除清单：`tal_workq_service.c` + `include/tal_workq_service.h`、`tal_workqueue.c` + `include/tal_workqueue.h`（含 `_dump_cb` 静态回调）。
- ⚠️ **不要误删**：`tal_workq_stop_delayed`（未编译的 agent-core `tuya_ai_client.c` 在用）；`tal_workqueue_cancel`（被活的 `tal_workqueue_cancel_delayed` 内部使用）。
- 代价：2 个文件，零调用方改动。

### B6. 卡成员 `get_free_heap_size`（可选，改动小但价值也低）

- 事实：仅 2 处，均为日志（malloc 失败提示、`Device Free heap`）；无功能分支依赖。
- 删除清单：`tal_api_ops.h` + `tal_system.c`（实现与失败日志改写）+ `include/tal_memory.h` + `port/src/tkl_api_ops.c` + `tuya_main.c:98`。
- ⚠️ 未编译的 agent-core `svc_ai_agent/src/tuya_ai_output.c:216` 在用 → 将来启用会断。
- 结论：只有确定要极致瘦身卡成员时再做。

### B7. AES 死链小包（3 个函数）

- 证据：`tal_aes128_ecb_encode/decode`（0 调用）→ 其 raw 助手各 1 引用；`tal_aes256_ctr_raw`（0）→ `tal_aes_crypt_ctr`（1 引用）。
- 可清：`tal_aes128_ecb_encode_raw`、`tal_aes128_ecb_decode_raw`、`tal_aes_crypt_ctr`（tal_symmetry.c）。
- ⚠️ **不要误删**：`tal_aes256_cbc_*_raw`、`tal_aes_gcm_decode`、`tal_pkcs7padding_buffer`（未编译的 agent-core 在用）。

### B8. 死事件枚举（3 个宏）

- `tuyaopen-core/iot-core/tal_system/include/tal_event_info.h`：`EVENT_REBOOT_REQ`、`EVENT_REBOOT_ACK`、`EVENT_HEALTH_ALERT`。
- 唯一引用是 `tal_event.c:345-346` 里**被注释掉的**两行；`EVENT_HEALTH_ALERT` 的订阅者（tuya_health）已删。

---

## 三、务必保留（防误删）

| 接口（调用点数） | 位置 | 保留理由 |
|---|---|---|
| `tal_system_get_random` (1) | `tuya_entropy_poll.c:24` | **TLS 唯一熵源**（`MBEDTLS_ENTROPY_HARDWARE_ALT` + `MBEDTLS_NO_PLATFORM_ENTROPY`），删了 TLS 起不来 |
| `log_output` 卡成员 (1) | `tuya_main.c:51` | 全量日志 + **QR 绑定码输出**的唯一出口 |
| `tal_system_reset` (1) | `tuya_main.c:131` | 设备复位（产品功能） |
| `tal_ota_*` 通知三件套 (各 1) | `tuya_ota.c` | 云 OTA 完整功能链 |
| `tal_queue_*` (1/2/1/1) | `lan_sock.c` | LAN socket 事件队列 |
| `tal_wired_get_ip` / `set_status_cb` (各 1) | `tuya_main.c` | 有线链路状态与 IP（demo 核心） |
| `tal_kv_init` / `serialize_get/set` (各 1) | `tuya_main.c`、`tuya_endpoint.c` | 设备激活数据与 endpoint 缓存 |
| `tal_net_*` ≤2 的一批 | tcp_transporter、tuya_tls、tuya_lan | 云 MQTT/HTTP/TLS 与 LAN 的 socket 底座 |
| `tuya_tls_*` / `tuya_x509_*` ≤2 的一批 | tls_transporter、tuya_cert_manager | TLS 传输层与证书管理 |
| `tal_sha256_mac` (3) / `tal_sha256_ret` (4) / `tal_md5_ret` (5) | LAN、OTA、tal_kv | LAN 会话密钥、OTA HMAC、KV 完整性 |
| `tal_aes128_cbc_encode/decode` (各 1) | `tal_kv.c` | KV 存储加密 |
| `tal_time_get_local_time_custom` (2) / `tal_time_is_in_sum_zone` (1) | `tal_log.c` | 日志时间戳链（表面低频但链路是活的） |
| `tal_sw_timer_*` / `tal_workq_*`（活路径） | app、iot_dp、cert_manager | 软定时器与云服务工作队列 |
| `tal_fs` 被 tal_kv 使用的子集 | `tal_kv.c` | KV 文件后端 |

### 仅未编译代码（agent-core / p2p-core）调用的，**不能删**

`tal_aes256_cbc_encode_raw/decode_raw`、`tal_aes_gcm_decode`、`tal_pkcs7padding_buffer`、`tal_log_del_output_term`、`tal_net_fd_clear/getpeername/getsockname/set_bufsize/shutdown`、`tal_time_get`、`tal_time_check_time_sync`、`tal_workq_stop_delayed`。

---

## 四、备注

1. **卡成员 `thread_set_self_name` / `thread_is_self`** 各只有 1 处内部调用，但属线程生命周期语义（设 OS 线程名 / 判断是否删自身）；`set_self_name` 已由用户决策保留，不建议以"低频"为由删除。
2. **死目录 `port/include/init/`**：已于 2026-10-08 整体删除（CMake 早已排除不编译；唯一障碍是 `tal_wired.c` 的两行死代码，已随之清理）。原备注 3（未编译的旧 init 路径引用已删符号）随目录删除一并消失。
3. **存疑未评（已处置）**：`tal_wired_get_status/set_ip/get_mac/set_mac` 已随 `tkl_wired → tkl_netif` 改名一并裁剪（3 个零调用 tkl 接口 + 4 个死包装）；`tal_kv_cmd` 已随 tal_kv 重构删除。
4. **计数口径**：表中"调用点数"为链接进 `switch_demo` 的 111 个 `.o` 中的重定位点数；按全部 174 个 `.o` 口径个别项会 +1~2，不影响结论。
6. **tal_kv 重构影响（2026-10-08，本表为审计时点快照）**：tal_kv 已改为"core 4 接口透传 + 实现全在 port"（子模块 `ba58b24` / 外层 `9961ff8`）——`serialize_get/set` 接口已删除、`tal_aes128_cbc_encode/decode` 与 `tal_sha256_ret` 的 KV 侧使用方变为 `port/src/tkl_kv.c`、`tal_fs` 不再被 KV 使用、`tal_kv_cmd` 已删（见备注 4）。表中相应行按新事实理解。
