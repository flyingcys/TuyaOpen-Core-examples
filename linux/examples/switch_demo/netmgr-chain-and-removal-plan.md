# netmgr 链路分析与去除改造方案

> 背景：计划从 tuyaopen-core 去掉 netmgr。注释掉 `tuya_main.c` 中 `netmgr_init()` /
> `netmgr_conn_set()` 后，switch_demo 不再打印二维码。本文整理完整链路、定位断点、给出改造建议。

## 1. 关键结论（先说答案）

二维码**不是 netcfg 的产物**，而是 **mqtt_bind 直连绑定线程**的产物：
`mqtt_bind` 用 uuid/authkey 直连涂鸦 MQTT，连上后派发 `TUYA_EVENT_DIRECT_MQTT_CONNECTED`，
demo 在该事件里打印 QR 等 APP 扫码。

去掉 netmgr 后它断了 **4 处**，任何一处断了 QR 都出不来：

| # | 断点 | 位置 |
|---|------|------|
| A | 没人注册 token_get port，`tuya_iot_yield` 永久阻塞 | `tuya_iot.c:860` |
| B | mqtt_bind 线程的 `network_check()` 恒 false | `mqtt_bind.c:155` |
| C | tuya_iot 主状态机的 `network_check()` 恒 false | `tuya_iot.c:981/1079/1089` |
| D | （仅 wifi 模式相关）netcfg 启动、BLE 栈、LAN 服务没人带起 | `netconn_wifi.c:281`、`netmgr.c:383/397` |

注：linux 版 switch_demo 实为 **wired demo**（`CMakeLists.txt:29` 只开了 `CONFIG_ENABLE_WIRED`），
`netmgr_conn_set(NETCONN_WIFI, ...)` 那段原本就不参与编译，真正起作用的是 `netmgr_init()`。

## 2. 链路全景

```
tuya_main.c user_main()
 ├─ netmgr_init(NETCONN_WIRED)                          [已注释 → 全部断点根源]
 │    ├─ tal_network_card_init()
 │    ├─ __netmgr_conn_register(NETCONN_WIRED, ...)     netmgr.c:362
 │    │    └─ ★ 注册 token_get port = mqtt_bind_token_get   netconn_wired.c:76
 │    ├─ s_netmgr.inited = TRUE                         netmgr.c:375
 │    ├─ LAN 服务定时器(500ms cycle)                     netmgr.c:383
 │    └─ (ENABLE_BLUETOOTH) tuya_ble_init()             netmgr.c:397
 ├─ tuya_iot_init(&client, {..., .network_check = user_network_check})   tuya_main.c:248
 ├─ tuya_iot_start(&client)                             tuya_main.c:281
 └─ for(;;) tuya_iot_yield(&client)                     tuya_main.c:287

tuya_iot 主状态机（tuya_iot.c:899 tuya_iot_yield）
 STATE_START (:919) ──未激活──> STATE_DATA_LOAD (:932)
   ├─ 已激活 ──────────────> STATE_NETWORK_CHECK (:980)
   └─ 未激活：订阅 EVENT_LINK_ACTIVATE (:946) → STATE_TOKEN_PENDING (:950)
 STATE_TOKEN_PENDING
   ├─ 派发 TUYA_EVENT_BIND_START（demo 打印 "Device Bind Start!"）
   └─ tuya_iot_token_get_pending (:956 → :852)
        ├─ 逐个调 token_get.cb → mqtt_bind_token_get() 创建 mqtt_bind 线程
        └─ 阻塞等 token_get.sem ← tuya_iot_token_activate_evt post (:838)
                  ▲
                  │ EVENT_LINK_ACTIVATE(token)（APP 扫码下发后 mqtt_bind 发布）
                  │

mqtt_bind 线程（mqtt_bind.c:139 mqtt_bind_token_get_thread）
 STATE_MQTT_BIND_INIT  (:153) 等 network_check()=true
     └─ user_network_check → netmgr_conn_get(...)   tuya_main.c:215
        └─ !s_netmgr.inited → OPRT_RESOURCE_NOT_READY，恒返回 false  netmgr.c:469
 STATE_MQTT_BIND_START  (:160) tuya_endpoint_update_auto_region → tuya_mqtt_init(uuid/authkey)
 STATE_MQTT_BIND_CONNECT(:190) tuya_mqtt_start
 STATE_MQTT_BIND_CONNECTED_WAIT (:197) MQTT 连上
     └─ ★ 派发 TUYA_EVENT_DIRECT_MQTT_CONNECTED   mqtt_bind.c:205
          → demo 打印 QR（tuya_main.c:111-117，qrcode_string_output）
          → "Wait Tuya APP scan the Device QR code..."
 STATE_MQTT_BIND_TOKEN_WAIT  APP 扫码 → PRO_MQ_ACTIVE_TOKEN_ON → mqtt_bind_activate_token_on
     └─ publish EVENT_LINK_ACTIVATE(token)        mqtt_bind.c:128-129
```

主状态机拿到 token 后：`STATE_NETWORK_CHECK (:981)` → `STATE_ENDPOINT_GET/UPDATE` → 激活 →
`STATE_MQTT_CONNECTING` → `STATE_MQTT_YIELD`。断线重连路径 `STATE_MQTT_RECONNECT /
STATE_NETWORK_RECONNECT`（:1077/:1088）同样依赖 `network_check()`。

## 3. 改造建议

`tuya_iot` 本身**不依赖 netmgr**，它只要求外部提供两样东西。去掉 netmgr = 把这两样
改由 demo/port 层直接提供（mqtt_bind 模块只依赖 network_check 和 tuya_mqtt，可独立存活）：

### 方案 A：最小改动（推荐）

在 `tuya_main.c` 补两行级别的改动：

```c
#include "mqtt_bind.h"   // core-iot/cloud/mqtt_bind.h

// 1) 替代 netconn_wired 原本注册的 token_get port（修断点 A）
tuya_iot_token_get_port_register(&client, mqtt_bind_token_get);

// 2) 换掉 user_network_check 里对 netmgr_conn_get 的依赖（修断点 B/C）
static bool user_network_check(void)
{
    /* wired/linux：网卡常在，可直接 return true；
       esp32：建议在 esp_netif 事件里置 volatile 标志，这里读标志 */
    return true;
}
```

3) 仅当以后启用 **wifi + 配网** 时，才需要自己带起 netcfg（修断点 D）。参照
   `netconn_wifi.c:281-308`：`netcfg_init()` → `ap_netcfg_init()/ble_netcfg_init()` →
   `netcfg_start(NETCFG_TUYA_xxx, finish_cb, NULL)`；且 BLE 配网需先 `tuya_ble_init()`
   （原来藏在 `netmgr_init` 里，`netmgr.c:397`）。wired demo 不需要这一步。

### 方案 B：长期结构化

- 把「链路就绪检测」(network_check，已是 config 回调) 与「token 获取方式」
  (`tuya_iot_token_get_port_register`，已是现成扩展点) 明确为 tuya_iot 的 port 接口，
  netmgr/netconn/netcfg 整体降级为可选组件。
- 清理 core 内残留耦合（netmgr 组件移除时**必须**处理，否则编译/链接失败）：
  - `tuya_iot.c:43` `#include "netmgr.h"`
  - `tuya_iot.c:798-800` `tuya_iot_destroy()` 直接调 `netmgr_conn_set(...NETCONN_CMD_CLOSE)`
  - `tuya_iot.c:865-886` `__tuya_iot_link_type_change_cb` 订阅 netmgr 发布的
    `EVENT_LINK_TYPE_CHG`（netmgr.c:243/258 发布），订阅点在 `tuya_iot.c:929`，使用 `netmgr_type_e`

## 5. ESP32 侧注意事项

- esp32 的 `switch_demo/main/tuya_main.c` 目前 netmgr 调用**尚未注释**（untracked 新文件），
  改造时与 linux 侧同步。
- 当前 ESP32 构建 `sdkconfig` 为 WiFi-only（`CONFIG_ENABLE_WIFI=y`，WIRED/BLUETOOTH 均关；
  `sdkconfig.defaults` 的 diff 显示原本是 WIRED demo，注释明言"有线绑定的 demo 需要二维码输出"）。
  WiFi-only 下 `mqtt_bind_token_get` 的两个注册者（wired/cellular netconn）都不编，
  该函数**已被链接器裁掉**（ELF 中无符号）——按方案 A 显式 `#include "mqtt_bind.h"` 并注册后
  才会被链入。
- 全仓库**不存在 `netcfg_api_init`**；netcfg 的等价入口是 `netcfg_init()` + 
  `netcfg_start(type, finish_cb, args)`（netcfg.h:193 / netcfg.c:269）。

## 4. 验证清单

1. 编译运行，日志应依次出现：
   `Activation data read fail, go activation mode...` → `Device Bind Start!` →
   `MQTT direct connected!` → 二维码 ASCII 图
2. QR 内容应为 `https://smartapp.tuya.com/s/p?p=<pid>&uuid=<uuid>&v=2.0`
3. APP 扫码后：token 打印（tuya_iot.c:963）→ 激活成功 → `TUYA_EVENT_MQTT_CONNECTED`
4. 回归：拔网线/断网 → 走 `STATE_NETWORK_RECONNECT`，确认新 network_check 生效
