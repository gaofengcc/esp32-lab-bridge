---
name: esp32-win10-serial-lab-template
description: 用于 ESP32 项目通过 Win10 串口桥完成烧录、日志、重启和截图的技能模板。
---

# ESP32 Win10 串口桥模板

## 适用场景

当项目需要通过 Win10 串口代理完成这些动作时使用：

- 启动串口会话
- 烧录固件
- 读取日志
- 发送重启命令
- 抓取 LVGL 截图

## 变量

将下面占位符替换成你的项目值：

- `{PROJECT}`: 项目名
- `{PORT}`: 串口号，例如 `COM5`
- `{BASE_URL}`: 桥接服务地址，例如 `http://127.0.0.1:3000`

## 工作流

1. 先读取当前状态。
   - `GET {BASE_URL}/api/state`
   - `GET {BASE_URL}/api/tools`
2. 启动串口会话。
   - `POST {BASE_URL}/api/session/start`
3. 必要时烧录。
   - `POST {BASE_URL}/api/flash?port={PORT}&baudrate=921600`
4. 截图或重启。
   - `POST {BASE_URL}/api/screenshot`
   - `POST {BASE_URL}/api/reboot`
5. 结束后关闭会话。
   - `POST {BASE_URL}/api/session/stop`

## 关键文件

- [bridge/server.js](../bridge/server.js)
- [bridge/serial_bridge.ps1](../bridge/serial_bridge.ps1)
- [bridge/config.json](../bridge/config.json)

