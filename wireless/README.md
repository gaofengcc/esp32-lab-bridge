# 无线调试

`wireless/` 目录沉淀的是 ESP32 的局域网诊断模板。它适合在设备已连上
Wi-Fi、并能被局域网主机访问时使用，默认端口为 `8080`。

## 架构

- 设备侧提供 HTTP 服务
- 局域网主机通过 `http://<device-ip>:8080` 访问
- `diag_service` 负责路由、token 鉴权和响应装配
- `ota_update` 负责 manifest OTA
- `firmware/log_gate` 和 `firmware/lvgl_screenshot` 仍然负责日志门控和截图数据源

## API 清单

- `GET /api/health`
- `GET /api/status`
- `GET /api/logs`
- `GET /api/screenshot.bmp`
- `GET /api/ota/status`
- `POST /api/ota/check`
- `POST /api/ota/start`
- `POST /api/reboot`

## token 配置

- 只接受 `Authorization: Bearer <token>`
- `token` 或 `auth` query 参数直接拒绝
- 弱 token 拒绝，例如 `123456`、`password`、`token`
- 生产构建必须配置强 token

示例：

```bash
export DIAG_TOKEN='replace-with-strong-token'
curl -H "Authorization: Bearer $DIAG_TOKEN" http://<device-ip>:8080/api/status
```

## 分阶段实施

- P0: 重启 + 日志
- P1: OTA
- P2: 截屏
- P3: 业务项目自定义的扩展路由

## 与串口桥的关系

- 无线优先：局域网可访问时优先走 HTTP
- 串口兜底：网络不可用、设备未拿到 IP、或 OTA 后需要救援时，再切回串口桥
- 两套能力可以并存，但诊断入口应优先统一到无线 HTTP 服务

## 模板定位

- 模板内不保留具体项目的专属字符串
- 状态 JSON 只保留业务无关的最小字段集
- 业务项目通过 provider 注入自己的状态、日志和 OTA 策略
