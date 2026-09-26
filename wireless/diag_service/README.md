# diag_service

`diag_service` 是可复用的 ESP-IDF 局域网诊断 HTTP 组件。组件只负责 HTTP
路由、Bearer 鉴权和 JSON 响应装配，业务能力通过回调注入，不绑定 WiFi、
日志、显示或 OTA 实现。

## 3 步接入

### 1. 声明依赖

将本目录作为 ESP-IDF 组件导入，并在业务组件的 `CMakeLists.txt` 中声明：

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES diag_service
)
```

### 2. 配置回调并启动

Bearer Token 应由业务侧安全保存，建议长度至少 16 个字符；服务启动前必须
确保网络接口已经获得可访问的 IP 地址。

```c
#include "diag_service.h"

static esp_err_t append_status(diag_json_writer_t *writer, void *ctx)
{
    (void)ctx;
    return diag_json_writer_kv_string(writer, "board", "demo");
}

static esp_err_t handle_reboot(httpd_req_t *req, void *ctx)
{
    (void)ctx;
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

static void start_diag(void)
{
    static const diag_service_config_t config = {
        .service_name = "demo",
        .port = 8080,
        .bearer_token = "replace-with-a-random-token",
        .on_status = append_status,
        .on_reboot = handle_reboot,
    };
    diag_service_handle_t handle = NULL;
    (void)diag_service_start(&config, &handle);
}
```

### 3. 管理生命周期

网络断开或不再提供诊断入口时调用 `diag_service_stop(handle)`；再次联网后
可用同一配置重新启动。`diag_service_is_running()` 可用于查询当前状态。

## API 清单

### 生命周期

- `diag_service_start()`：启动 HTTP 服务并注册全部内置路由。
- `diag_service_stop()`：停止服务并释放句柄。
- `diag_service_is_running()`：查询服务是否正在运行。

### JSON writer

- `diag_json_writer_init()` / `diag_json_writer_deinit()`
- `diag_json_writer_begin_object()` / `diag_json_writer_end_object()`
- `diag_json_writer_begin_array()` / `diag_json_writer_end_array()`
- `diag_json_writer_name()`、`diag_json_writer_raw()`、`diag_json_writer_string()`
- `diag_json_writer_bool()`、`diag_json_writer_i64()`、`diag_json_writer_u64()`
- `diag_json_writer_null()`
- `diag_json_writer_kv_string()`、`diag_json_writer_kv_raw()`
- `diag_json_writer_kv_bool()`、`diag_json_writer_kv_i64()`
- `diag_json_writer_kv_u64()`、`diag_json_writer_kv_null()`
- `diag_json_writer_cstr()` / `diag_json_writer_len()`

### 回调字段

- `on_status`：追加 `/api/status` 的业务字段。
- `on_selftest`：追加 `/api/selftest` 的自检字段。
- `on_logs`：处理 `/api/logs`，回调自行读取请求并发送响应。
- `on_screenshot` / `on_screenshot_free`：提供和释放 BMP 缓冲区。
- `on_ota_status`：追加 `/api/ota/status` 的业务字段。
- `on_ota_check` / `on_ota_start`：处理 OTA 请求并发送响应。
- `on_reboot`：处理重启请求并发送响应。

## 内置路由

| 方法 | 路径 | 鉴权 | 说明 |
| --- | --- | --- | --- |
| GET | `/api/health` | 否 | 返回服务存活状态和运行时间 |
| GET | `/api/status` | 是 | 状态 JSON，调用 `on_status` |
| POST | `/api/selftest` | 是 | 自检 JSON，调用 `on_selftest` |
| GET | `/api/logs` | 是 | 调用 `on_logs` |
| GET | `/api/screenshot.bmp` | 是 | 返回 BMP 截图 |
| GET | `/api/ota/status` | 是 | OTA 状态 JSON |
| POST | `/api/ota/check` | 是 | 调用 `on_ota_check` |
| POST | `/api/ota/start` | 是 | 调用 `on_ota_start` |
| POST | `/api/reboot` | 是 | 调用 `on_reboot` |

除 `/api/health` 外的路由只接受：

```http
Authorization: Bearer <token>
```

URL 查询参数中的 `token`、`auth` 和 `access_token` 会被拒绝；Token 比较使用
常量时间算法。未配置对应回调时，服务返回 `501 Not Implemented`。

## Kconfig

- `CONFIG_DIAG_SERVICE_TEST_AUTH_BYPASS`：实验室鉴权旁路，默认关闭，生产构建必须关闭。
- `CONFIG_DIAG_SERVICE_HTTPD_STACK_SIZE`：HTTP 服务任务栈大小，默认 `12288` 字节。
- `CONFIG_DIAG_SERVICE_HTTPD_MAX_URI_HANDLERS`：URI 路由槽位数，默认 `12`。

## 已知坑

- `diag_service_start()` 只能运行一个实例；业务侧应保存返回句柄并成对调用
  `diag_service_stop()`。
- 服务不会自动判断 STA、SoftAP 或 APSTA 状态，必须由业务侧在网络生命周期中启动
  和停止。
- Token 不能使用明显默认值，也不能通过 URL 参数传递；开发调试可临时打开
  `CONFIG_DIAG_SERVICE_TEST_AUTH_BYPASS`，发布前务必关闭。
- `on_logs`、`on_ota_check`、`on_ota_start` 和 `on_reboot` 回调负责自行读取请求体
  （如有需要）并调用 `httpd_resp_*` 完成响应。
- 截图回调返回的缓冲区必须保持有效到响应发送完成，释放策略由
  `on_screenshot_free` 决定；未配置时组件使用 `free()`。
- `/api/health` 不鉴权，只适合返回非敏感的存活信息。
