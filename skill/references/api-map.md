# API Map

## 基础地址

- `{BASE_URL}`: Win10 Web 串口桥地址
- `{PORT}`: Win10 侧 ESP32 串口号

## 端点

- `GET {BASE_URL}/api/state`
  - 查看桥服务、串口会话、当前运行状态和服务版本。
- `GET {BASE_URL}/api/tools`
  - 查看 Win10 侧可用工具，例如 Python、esptool、Node.js 或脚本路径。
- `GET {BASE_URL}/api/events`
  - 通过 SSE 读取实时日志、烧录状态、截图状态和设备串口输出。
- `POST {BASE_URL}/api/session/start`
  - 打开 `{PORT}` 的串口会话并开始采集日志。
- `POST {BASE_URL}/api/session/stop`
  - 关闭串口会话，释放桥服务中的当前会话状态。
- `POST {BASE_URL}/api/flash?port={PORT}&baudrate=921600`
  - 通过 Win10 桥烧录 ESP32。固件路径通常由桥服务配置或项目脚本约定。
- `POST {BASE_URL}/api/screenshot`
  - 触发 ESP32 设备截图。常见实现是在同一 USB Serial/JTAG 线上发送 `!SCN`。
- `GET {BASE_URL}/api/screenshot/latest`
  - 获取最近一次截图结果或文件信息。
- `POST {BASE_URL}/api/reboot`
  - 通过桥服务请求设备重启。

## 串口所有权

桥服务保持 Win10 侧 COM 口所有权。桥服务运行时，不要让第二个终端、IDE 串口监视器或脚本同时打开 `{PORT}`。
