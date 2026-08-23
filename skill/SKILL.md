---
name: {PROJECT}-win10-serial-lab
description: 用于 {PROJECT} 通过 Win10 Web 串口桥完成 ESP32 烧录、日志、重启和截图的技能。
---

# {PROJECT} ESP32 Win10 串口实验室

## 适用场景

当 Ubuntu 侧负责编译，但 ESP32 设备连接在 Win10 主机，需要通过 Web 串口桥闭环完成这些动作时使用：

- 检查 Win10 桥状态和工具能力
- 启动串口会话
- 通过 `{PORT}` 烧录固件
- 读取实时日志和事件流
- 发送重启命令并验证启动日志
- 抓取 LVGL 或设备侧截图
- 排查 `!SCN`、`screenshot timeout`、`SCREENSHOT_*`、`esptool` 退出码等问题

## 变量

将下面占位符替换成你的项目值：

- `{PROJECT}`: 项目名
- `{PORT}`: Win10 侧 ESP32 串口号
- `{BASE_URL}`: Win10 Web 串口桥地址

这些占位符由 `skill/install_skill.sh` 在安装时自动替换。

## 工作流

1. 先读取当前实验室状态。
   - `GET {BASE_URL}/api/state`
   - `GET {BASE_URL}/api/tools`
   - 如果烧录或截图失败，先查看最近事件和日志，不要直接假设串口错误
2. 在 Ubuntu 侧编译固件。
   - 优先使用项目已有构建脚本，例如 `./build_esp32.sh`
   - 单文件烧录产物通常放在 `output/{PROJECT}_merged.bin`
3. 通过 Win10 桥烧录。
   - `POST {BASE_URL}/api/flash?port={PORT}&baudrate=921600`
4. 重新打开串口会话并读取实时日志。
   - `POST {BASE_URL}/api/session/start`
   - `GET {BASE_URL}/api/events`
5. 重启并验证启动。
   - `POST {BASE_URL}/api/reboot`
   - 等待项目约定的启动标记、版本号或业务初始化日志
6. 抓取截图。
   - `POST {BASE_URL}/api/screenshot`
   - `GET {BASE_URL}/api/screenshot/latest`
   - 如果截图超时，先检查设备是否打印 `SCREENSHOT_BUSY`、`SCREENSHOT_ERROR` 或 `SCREENSHOT_NOT_FOUND`
7. 收尾并保存证据。
   - 记录烧录结果、首条启动标记、截图文件名和关键日志
   - 需要交接时写入 `.agent-sync/`
   - 结束后可关闭会话：`POST {BASE_URL}/api/session/stop`

## 命令清单

- 状态：`curl -s {BASE_URL}/api/state`
- 工具：`curl -s {BASE_URL}/api/tools`
- 启动会话：`curl -s -X POST {BASE_URL}/api/session/start`
- 关闭会话：`curl -s -X POST {BASE_URL}/api/session/stop`
- 烧录：`curl -s -X POST "{BASE_URL}/api/flash?port={PORT}&baudrate=921600"`
- 事件流：`curl -N {BASE_URL}/api/events`
- 截图：`curl -s -X POST {BASE_URL}/api/screenshot`
- 最新截图：`curl -s {BASE_URL}/api/screenshot/latest`
- 重启：`curl -s -X POST {BASE_URL}/api/reboot`

## 排错速查

- `esptool` 退出码 `9009`：通常是 Win10 侧命令不存在或可执行文件路径错误，先查 `GET {BASE_URL}/api/tools`。
- `serviceRevision` 过旧：重启 Win10 桥服务，确认运行的是当前脚本。
- 日志为空：检查 `GET {BASE_URL}/api/state` 是否显示会话已运行，再确认 `{PORT}` 和桥配置。
- `screenshot timeout`：不能直接证明串口错误，优先看设备日志里的 `SCREENSHOT_BUSY`、`SCREENSHOT_ERROR`、`SCREENSHOT_NOT_FOUND`。
- 串口占用：Win10 桥是串口唯一拥有者；桥运行时不要从第二个进程直接打开 `{PORT}`。

## 工作规则

- 把 Win10 Web 服务视为串口的单一拥有者。
- 优先使用 Web API，不在 Ubuntu 侧直接访问 Win10 串口。
- 硬件验证必须有命令输出、日志、截图或用户提供的证据支撑。
- 截图路径不清楚时，检查项目中的 CDC 命令处理和 LVGL 截图实现，例如 `source/idf/*cdc*`、`source/idf/*screenshot*` 或同类模块。

## 无线调试

当设备已经通过 Wi-Fi 获取到局域网地址时，优先使用 `http://<device-ip>:8080`
进行无线诊断。无线接口清单、token 规则和操作顺序见 `references/wireless.md`。
串口桥只作为无线不可用、设备未联网或需要救援时的兜底路径。

## 关键文件

- [bridge/server.js](../bridge/server.js)
- [bridge/serial_bridge.ps1](../bridge/serial_bridge.ps1)
- [bridge/config.json](../bridge/config.json)
- [tools/lvgl_screenshot.py](../tools/lvgl_screenshot.py)

## References

- [workflow.md](references/workflow.md)
- [api-map.md](references/api-map.md)
- [troubleshooting.md](references/troubleshooting.md)
- [artifacts.md](references/artifacts.md)
- [wireless.md](references/wireless.md)
