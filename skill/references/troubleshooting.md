# Troubleshooting

## Flash

- `9009`: 通常表示 Win10 主机侧命令不存在、可执行文件路径错误，或桥脚本调用的工具没有加入 PATH。
- `serviceRevision` 过旧：重启 Win10 桥服务，并确认访问的是 `{BASE_URL}` 对应的当前实例。
- 烧录一直等待：检查 `GET {BASE_URL}/api/state`、`GET {BASE_URL}/api/tools`，确认 `{PORT}` 存在且未被其他进程占用。
- 烧录后无启动日志：重新 `POST {BASE_URL}/api/session/start`，再 `POST {BASE_URL}/api/reboot`，确认事件流中是否有 ROM boot 或应用启动标记。

## Logs

- 如果 `GET {BASE_URL}/api/state` 显示 `running`，说明桥服务已经附着串口会话。
- 如果日志为空，先检查 `{PORT}` 是否配置正确，再查 Win10 桥脚本和 USB Serial/JTAG 设备枚举状态。
- 如果日志中断，确认没有第二个进程打开 `{PORT}`，并查看桥服务控制台是否报错。

## Screenshot

- `screenshot timeout` 不证明串口错误。
- 优先检查设备是否打印：
  - `SCREENSHOT_BUSY`
  - `SCREENSHOT_ERROR`
  - `SCREENSHOT_NOT_FOUND`
- 如果设备已收到 `!SCN` 但无图片，检查 {PROJECT} 的 CDC 命令处理路径和 LVGL 截图模块。
- 如果设备没有收到 `!SCN`，检查桥服务是否仍拥有 `{PORT}`，以及 `GET {BASE_URL}/api/events` 是否仍在输出串口日志。
- 如果最新截图为空或旧图重复，检查 `GET {BASE_URL}/api/screenshot/latest` 返回的文件名、时间戳和桥服务截图目录。

## Flow Control

- Win10 桥拥有串口；不要在桥运行时从 IDE、PowerShell、Python 脚本或第二个服务直接打开 `{PORT}`。
- 排查时先收集 API 状态和事件流，再改动固件或桥脚本。
- 只有在 API 明确显示桥离线、工具缺失或串口不可用时，才优先处理 Win10 侧环境问题。
