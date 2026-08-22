# Workflow

## 闭环步骤

1. 确认 Win10 Web 串口桥在线。
   - `GET {BASE_URL}/api/state`
   - `GET {BASE_URL}/api/tools`
   - 如果后续截图或烧录失败，先保留状态返回值和最近事件日志
2. 在 Ubuntu 侧编译 {PROJECT} 固件。
   - 优先使用项目已有构建入口，例如 `./build_esp32.sh`
   - 如项目产出合并镜像，默认记录 `output/{PROJECT}_merged.bin`
3. 通过 Win10 桥烧录合并镜像或桥服务配置的固件。
   - `POST {BASE_URL}/api/flash?port={PORT}&baudrate=921600`
4. 重新打开串口会话读取实时日志。
   - `POST {BASE_URL}/api/session/start`
   - `GET {BASE_URL}/api/events`
5. 重启设备并验证启动。
   - `POST {BASE_URL}/api/reboot`
   - 等待项目约定的启动标记、版本号、任务启动或 UI 初始化日志
6. 抓取截图。
   - `POST {BASE_URL}/api/screenshot`
   - `GET {BASE_URL}/api/screenshot/latest`
7. 如果截图失败，按设备侧日志优先排查。
   - 检查 `SCREENSHOT_BUSY`
   - 检查 `SCREENSHOT_ERROR`
   - 检查 `SCREENSHOT_NOT_FOUND`
   - 检查 CDC 命令处理文件，例如 `source/idf/*cdc*`
   - 检查 LVGL 或项目截图模块，例如 `source/idf/*screenshot*`
8. 保存交接证据。
   - 烧录 API 返回值
   - 首条启动标记或关键启动日志
   - 截图文件名或失败日志
   - `.agent-sync/` 交接说明

## 执行原则

- Win10 桥服务是 `{PORT}` 的唯一拥有者。
- 串口会话、烧录、重启和截图都优先走 `{BASE_URL}` 的 Web API。
- `screenshot timeout` 只是结果，不是根因；必须结合 `GET {BASE_URL}/api/events` 和设备侧错误文本判断。
