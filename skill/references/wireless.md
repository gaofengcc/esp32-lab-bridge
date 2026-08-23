# Wireless Debug

## 基础地址

- `http://<device-ip>:8080`

## 常用接口

- `GET /api/health`
  - 轻量探活，不需要 token。
- `GET /api/status`
  - 读取设备状态 JSON，需要 `Authorization: Bearer <token>`。
- `GET /api/logs`
  - 读取最近日志文本，需要 token。
- `GET /api/screenshot.bmp`
  - 读取 BMP 截图，需要 token。
- `GET /api/ota/status`
  - 读取 OTA 状态，需要 token。
- `POST /api/ota/check`
  - 触发 OTA 检查，需要 token。
- `POST /api/ota/start`
  - 触发 OTA 下载和写入，需要 token。
- `POST /api/reboot`
  - 请求设备重启，需要 token。

## token 规则

- 只接受 `Authorization: Bearer <token>`
- 不接受 `token` 或 `auth` query 参数
- 弱 token 直接拒绝，例如 `password`、`token`、`123456`
- 生产环境必须使用强 token

## 推荐执行顺序

1. `GET /api/health`
2. `GET /api/status`
3. `GET /api/logs`
4. `GET /api/screenshot.bmp`
5. `GET /api/ota/status`
6. `POST /api/ota/check`
7. `POST /api/ota/start`
8. `POST /api/reboot`

## 示例

```bash
export DIAG_TOKEN='replace-with-strong-token'
curl -H "Authorization: Bearer $DIAG_TOKEN" http://<device-ip>:8080/api/status
curl -H "Authorization: Bearer $DIAG_TOKEN" http://<device-ip>:8080/api/logs
curl -H "Authorization: Bearer $DIAG_TOKEN" http://<device-ip>:8080/api/screenshot.bmp -o shot.bmp
```

## 工作原则

- 无线优先，串口兜底
- 先探活，再看状态，再看日志，再看截图
- OTA 失败时优先保留 `status`、`logs` 和 `ota/status` 的返回值
