# 移除存档：http_host / http_site / http_captive（softAP 配网 HTTP 三件套）

> 移除时间：2026-09-24。移除原因：调用方 `netcfg/` 配网模块已从 iot-core 移除，
> 三个模块在全 core 内零引用（头文件无消费者、API 零调用、最终可执行文件中
> 无对应符号），确认为死代码后清理。
>
> 三者构成一套完整的 SoftAP HTTP 服务器：底座（host）+ 路由分发（site）+
> 强制门户（captive），恢复时**应整套一起恢复**。见文末「恢复指引」。

---

## 一、总览

三者都构建在 BSD socket 之上，是 **SoftAP 配网场景的 HTTP 服务器组件**：

| 模块 | 角色 | 一句话 |
|---|---|---|
| `http_host` | **server 底座** | 监听端口、收请求、解析出结构化的 `HTTP_HOST_REQUEST_T`，带回调线程 |
| `http_site` | **路由分发** | 「方法 + 路径 → 响应」路由表，命中即回应 |
| `http_captive` | **强制门户** | 拦截系统连通性探测请求，302 重定向到配网门户 |

共同依赖：`tal_api.h`（内存/日志）。`HTTP_HOST_REQUEST_T` 定义在
`http_host.h`（含 `client_fd` / `raw` 原始报文 / `method` / `path` / `body`
/ `user_ctx`），是三件套之间的通用请求上下文。

## 二、http_host：嵌入式 HTTP 服务器底座

轻量 HTTP host：**listen → receive → dispatch → reply**，独立回调线程模型。

### 配置与请求结构

```c
typedef struct {
    uint16_t port;                    /* 监听端口 */
    uint8_t  backlog;
    uint32_t listen_recv_timeout_ms;  /* accept 超时（用于 idle 回调轮） */
    uint32_t listen_send_timeout_ms;
    uint32_t client_recv_slice_ms;    /* 收包切片窗口 */
    uint32_t client_send_timeout_ms;
    uint16_t client_wait_max_rounds;
    uint32_t max_request_size;
    uint32_t stack_depth;             /* 回调线程栈 */
    uint8_t  priority;
    const char *thread_name;
} HTTP_HOST_CFG_T;                    /* 零值字段用内置默认 */

typedef struct {
    int client_fd;
    const char *raw;                  /* 原始请求报文 */
    uint32_t raw_len;
    char method[12];
    char path[96];
    const char *body;
    uint32_t body_len;
    void *user_ctx;
} HTTP_HOST_REQUEST_T;
```

### API

| API | 作用 |
|---|---|
| `http_host_start(&host, cfg, on_request, on_idle, user_ctx)` | 启动监听线程；每收到完整请求回调 `HTTP_HOST_REQUEST_CB`；accept 空闲轮询回调 `HTTP_HOST_IDLE_CB`（用于退出检查） |
| `http_host_stop(host)` | 停止并释放全部资源 |
| `http_host_request_shutdown(host)` | 只关监听退出 accept 循环、不释放上下文——可在 idle 回调中于网络拆除前安全调用 |
| `http_host_reply(fd, status, content_type, body)` | 回 HTTP 响应（`Connection: close`） |
| `http_host_reply_not_found(fd)` | 回 404 |
| `http_host_reply_redirect(fd, location)` | 回 302 重定向（captive 依赖） |
| `http_host_find_header_end / get_content_length / get_header_value` | 报文解析辅助（头结束偏移 / Content-Length / 大小写不敏感取头） |

## 三、http_site：路由表与分发

把 HTTP server 抽象成「方法 + 路径 → 响应」的路由表，类似极简的
Express/Flask 路由。

### 数据结构

```c
typedef void *HTTP_SITE_HANDLE_T;

/* 静态路由：命中后直接回写预置内容 */
typedef struct {
    const char *method;        /* "GET" / "POST" ... */
    const char *path;          /* "/index.html" ... */
    const char *status;        /* "HTTP/1.1 200 OK" */
    const char *content_type;  /* "text/html" */
    const char *body;          /* 响应体 */
} HTTP_SITE_STATIC_ROUTE_T;

/* 动态路由：命中后回调业务 handler */
typedef OPERATE_RET (*HTTP_SITE_HANDLER_CB)(const HTTP_HOST_REQUEST_T *req, void *user_ctx);
```

路由表容量默认 16 条（`HTTP_SITE_DEFAULT_MAX_ROUTES`），创建时可指定。

### API

| API | 作用 |
|---|---|
| `http_site_create(&site, max_routes)` | 创建路由表（`max_routes=0` 用默认 16） |
| `http_site_destroy(&site)` | 销毁并释放路由表 |
| `http_site_add_static(site, method, path, status, type, body)` | 注册单条静态路由 |
| `http_site_add_static_routes(site, routes[], n)` | 批量注册静态路由（配网页的典型用法：一张静态表描述全部页面） |
| `http_site_add_handler(site, method, path, cb, user_ctx)` | 注册动态处理回调（如接收配网参数的 POST） |
| `http_site_dispatch(site, &req)` | **核心分发**（`src/http/http_site.c:202`）：按注册顺序做 method+path 精确匹配；静态路由直接 `http_host_reply()`，动态路由调 `handler`；无命中返回 `OPRT_NOT_FOUND` |

## 四、http_captive：Captive Portal 重定向

解决「手机/电脑连上设备热点后，怎么让用户打开配网页」的问题。

### 工作原理

手机/电脑连 Wi-Fi 后，系统后台会请求固定的**连通性探测 URL**（如苹果的
`/hotspot-detect.html`、安卓的 `/generate_204`、Windows 的 `/ncsi.txt`）。
本模块维护这份探测路径清单（内置 8 条，可自定义），对命中探测路径的 GET
请求回 **302 重定向**到配网门户 URL——各操作系统检测到探测被重定向后，会
自动弹出配网页。

### 数据与 API

```c
typedef struct {
    const char *ap_ip;              /* AP IP，用于拼默认门户 URL */
    const char *portal_url;         /* 显式指定门户 URL；NULL 则用 http://<ap_ip>/ */
    const char *const *probe_paths; /* 自定义探测列表；NULL 用内置默认 */
    BOOL_T redirect_unmatched_get;  /* 是否把未命中路由的 GET 也重定向到门户 */
} HTTP_CAPTIVE_CFG_T;
```

| API | 作用 |
|---|---|
| `http_captive_start(cfg)` | 初始化（记录门户 URL 与开关），幂等 |
| `http_captive_stop()` | 复位状态 |
| `http_captive_handle_http(req)` | **在站点分发前调用**：命中探测路径则回 302，返回 `OPRT_OK`；否则 `OPRT_NOT_FOUND` 交给 `http_site_dispatch` |
| `http_captive_redirect_to_portal(req)` | 站点分发未命中后调用：`redirect_unmatched_get` 开启时把任意 GET 重定向到门户 |

### 重要局限（设计注释明示）

`http_captive` **只做 HTTP 层重定向，不做 DNS 劫持**。没有 DNS 劫持时，手机
的自动弹窗在部分系统上不可靠（探测请求可能根本不发出或走缓存），用户可能
需要**手动打开浏览器访问 `http://<ap_ip>/`**。这是有意为之的简化——若未来
重做配网要求可靠的自动弹窗，需补充 DNS 服务器（将所有域名解析到 AP IP）。

## 五、三件套协作模型（原 netcfg 中的完整链路）

```
SoftAP 工作站连入
        │
        ▼
http_host_start(on_request=配网请求回调)     ← server 底座：收请求、解析出 req
        │  每个请求进入 on_request：
        ▼
http_captive_handle_http(req)               ← 1) 先拦连通性探测
   ├─ 命中探测路径 → 302 门户 URL（系统弹配网页）
   └─ 未命中 ↓
http_site_dispatch(site, req)               ← 2) 再走路由表
   ├─ 静态路由 → http_host_reply 回配网页面内容
   ├─ 动态路由 → handler（收 SSID/密码等参数）
   └─ 未命中 ↓
http_captive_redirect_to_portal(req)        ← 3) 可选兜底：任意 GET 引导到门户
```

## 六、仍然存活的相邻组件（勿混淆）

| 文件 | 状态 | 消费者 |
|---|---|---|
| `src/http/http_client_wrapper.c` | **活跃** | `cloud/tuya_http.c`、`atop_base.c`、`iotdns.c`（设备侧 HTTP **客户端**，上云主通道） |
| `src/http/http_download.c` | **活跃** | OTA 分块下载（基于 `http_client_interface.h`） |
| `src/http/http_session.c` | **存活（条件参与）** | `agent-core/tuya_ai_http.c`（AI 上云，开 `CONFIG_ENABLE_AI_COMPONENTS` 后参与链接） |

三件套是 HTTP **服务端**（配网期对外服务）；上述组件是 HTTP **客户端**
（业务期对云通信），方向不同、互不依赖。

## 七、恢复指引

1. **源码**：从 git 历史恢复（本文件记录于删除当次提交之前）：
   - `git show HEAD:third_party/libhttp/src/http_host.c`（同理 `http_site.c`、`http_captive.c`）
   - 头文件：`third_party/libhttp/include/http_host.h`、`http_site.h`、`http_captive.h`
   （更早历史在 `core-common/libhttp/` 路径下）
2. **放置位置**：`.c` 放回 `iot-core/tuya_transport/src/http/`，`.h` 放回
   `iot-core/tuya_transport/include/`。
3. **构建**：`tuya_transport` 目标用 `GLOB_RECURSE src/*.c` 收源、`include/`
   整目录导出头——**恢复文件后无需改任何 CMake**，重新编译即可。
4. **依赖**：仅依赖 `tal_api.h` 与 BSD socket / tal_network，无新增依赖。
5. **注意**：三件套配套使用（host 的 `HTTP_HOST_REQUEST_T` 是 site/captive
   的入参类型），单独恢复 site/captive 而没有 host 无法编译。
