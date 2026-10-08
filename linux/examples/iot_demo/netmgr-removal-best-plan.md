# 去 netmgr 最佳方案（linux 跑通版）

> 原则：core 不改架构，只做**两处接线 + 一处清理**。
> "port 反馈状态"采用**拉式**（port 存状态、core 轮询），不做推式回调。
> 链路细节见同目录 `netmgr-chain-and-removal-plan.md`。

## 改造后的结构

```
port 层（linux）                     core（tuya_iot，零改动）
┌─────────────────────┐
│ s_link_up 状态标志    │◄── user_network_check() 拉   STATE_NETWORK_CHECK
│ (wired: 初始即 true)  │                               └─ 未激活 → STATE_TOKEN_PENDING
└─────────────────────┘                                   └─ 调 token_get.cb
demo 层                                                    ▲
└─ tuya_iot_token_get_port_register(&client,               │
     mqtt_bind_token_get) ─────────────────────────────────┘
     → mqtt_bind 线程直连涂鸦 MQTT → TUYA_EVENT_DIRECT_MQTT_CONNECTED → 二维码
```

## 三步改动

### 1. port 层：维护链路状态（core 拉取）

```c
/* port 层，如 linux/port/src/tkl_... 或 demo 内： */
static volatile bool s_link_up = false;

void port_link_status_set(bool up) { s_link_up = up; }  /* 将来 esp32 由 esp_netif 事件调用 */
bool port_link_status_get(void)    { return s_link_up; }
```

linux wired 场景网卡由 OS 管理，初始化后直接 `port_link_status_set(true)`。
（如需主动探测，可用 netlink/SIOCGIFFLAGS 检查 IFF_UP；第一版不需要。）

### 2. demo 层：换掉 network_check + 注册 token port

```c
#include "mqtt_bind.h"   /* 同时保证 mqtt_bind_token_get 被链接进来 */

bool user_network_check(void)
{
    return port_link_status_get();   /* 不再调 netmgr_conn_get */
}

/* tuya_iot_init() 成功之后、tuya_iot_start() 之前： */
tuya_iot_token_get_port_register(&client, mqtt_bind_token_get);
```

### 3. core 清理：拆掉 3 处 netmgr 悬空耦合（tuyaopen-core submodule 内）

| 位置 | 内容 | 处理 |
|---|---|---|
| tuya_iot.c:43 | `#include "netmgr.h"` | 删 |
| tuya_iot.c:798-800 | `tuya_iot_destroy` 里 3 次 `netmgr_conn_set(...CLOSE)` | 删 |
| tuya_iot.c:929 + 865-886 | 订阅 netmgr 的 `EVENT_LINK_TYPE_CHG`、`__tuya_iot_link_type_change_cb` 整个函数 | 删 |

> 注：netmgr 组件本身可以先留着不编（CMake 裁剪），物理删除等 linux 验证通过后做。

## 验证清单

1. 编译运行，日志顺序：`go activation mode` → `Device Bind Start!` → `MQTT direct connected!` → 二维码
2. QR 内容 `https://smartapp.tuya.com/s/p?p=<pid>&uuid=<uuid>&v=2.0`
3. APP 扫码：token 打印 → 激活 → `TUYA_EVENT_MQTT_CONNECTED`
4. 断网重连：拔网线再插，走 `STATE_NETWORK_RECONNECT` 恢复

## 以后扩到 esp32 / wifi 配网时再加

- port 的 `port_link_status_set` 改由 esp_netif/IP 事件驱动
- wifi 配网需自己带起 netcfg：`netcfg_init()` → `ap_netcfg_init()/ble_netcfg_init()` → `netcfg_start(...)`（参照 netconn_wifi.c:281-308），BLE 场景需先 `tuya_ble_init()`
