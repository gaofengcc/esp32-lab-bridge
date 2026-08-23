# diag_service

通用无线诊断服务模板，面向 ESP-IDF 的局域网 HTTP 调试入口。

## 目标

- Bearer Token 鉴权
- 统一健康检查和状态接口
- 日志、截图、OTA、重启等能力的路由挂点
- 状态 JSON 由业务侧通过小型 writer 逐字段组装
- 截图只负责回调取 BMP 缓冲区，释放策略由调用方配置

## 路由

- `GET /api/health`
- `GET /api/status`
- `GET /api/logs`
- `GET /api/screenshot.bmp`
- `GET /api/ota/status`
- `POST /api/ota/check`
- `POST /api/ota/start`
- `POST /api/reboot`

## 鉴权

仅接受：

```http
Authorization: Bearer <token>
```

约束：

- 不接受 query token
- token 不能为空
- token 长度建议不小于 16
- 比较过程使用常量时间实现
- `GET /api/health` 免鉴权，其余路由走鉴权

## 状态 JSON

`/api/status` 和 `/api/ota/status` 采用 writer 回调模式：

```c
static esp_err_t my_status(diag_json_writer_t *w, void *ctx)
{
    (void)ctx;
    if (diag_json_writer_kv_string(w, "board", "demo") != ESP_OK) {
        return ESP_FAIL;
    }
    if (diag_json_writer_kv_u64(w, "free_heap", esp_get_free_heap_size()) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
```

## 回调契约

- `on_status` 和 `on_ota_status`：服务已打开根 JSON 对象，业务代码只追加字段。
- `on_logs`、`on_ota_check`、`on_ota_start`、`on_reboot`：业务代码负责调用 `httpd_resp_*` 发送响应。
- `on_screenshot`：返回 BMP 缓冲区和长度；释放由 `on_screenshot_free` 控制，未配置时使用 `free`。

## 说明

这个模板只提供通用 HTTP 框架，不绑定具体业务字段。业务侧可以把日志暂停/恢复、OTA manifest、截图缓冲区和重启动作接到对应回调上。
