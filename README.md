# ESP32 Lab Bridge

这是一个可复用的 ESP32 远程自动化调试模板仓库。

目标是把 USB CDC 命令通道、日志暂停/恢复、LVGL 截图、Win10 串口桥和 Codex 技能打包成通用骨架，方便其他 ESP32 项目直接接入。

## 目录

```text
firmware/
bridge/
skill/
tools/
examples/
```

## 快速开始

1. 复制 `firmware/` 下需要的组件到目标 ESP32 项目的 `components/`，在业务组件 `CMakeLists.txt` 中声明 `REQUIRES cdc_command log_gate lvgl_screenshot`，并按 [examples/README.md](examples/README.md) 接好 CDC 初始化、RX 喂入、命令注册、串口写函数和截图输出。
2. 在 Windows 10/Windows 侧安装 Node.js，确保 PowerShell 可用；修改 `bridge/config.json` 的项目名、串口号、日志目录、`host`/`port` 和烧录参数，然后执行 `cd bridge && npm start` 启动串口桥。桥接服务地址来自 `config.json` 的 `host`/`port`；`host: "0.0.0.0"` 是监听地址，给浏览器或技能使用时请填实际可访问地址，例如 `http://127.0.0.1:3000` 或 Windows 主机 LAN IP。
3. 执行 `PROJECT=你的项目 PORT=COM5 BASE_URL=http://127.0.0.1:3000 bash skill/install_skill.sh` 安装 Codex 技能。脚本会在安装时替换 `{PROJECT}`、`{PORT}`、`{BASE_URL}`；不设置环境变量时使用脚本默认值。

## 固件组件依赖

外部依赖由目标项目自行提供；模板只提供可复制组件和接入骨架。

| 组件 | 必需依赖 | 说明 |
| --- | --- | --- |
| `cdc_command` | ESP-IDF 基础组件 | CDC 帧解析、命令注册、响应帧封装，不绑定具体串口驱动。 |
| `log_gate` | `easylogger`、`freertos` | 使用 `elog.h` 暂停/恢复日志输出，目标项目需要已经集成 EasyLogger。 |
| `lvgl_screenshot` | 仅在 `CONFIG_LVGL_SCREENSHOT_ENABLE=y` 时需要 `lvgl`、`lvgl_port`、`freertos`、`heap` | 无 LVGL 或未启用 `LV_USE_SNAPSHOT` 的项目保持关闭并跳过截图能力。 |

每个组件目录带有最小 `idf_component.yml`，便于目标项目用 Component Manager 或目录复制方式接入；EasyLogger、LVGL、`lvgl_port` 等业务外部依赖仍由目标项目自己的 manifest 或组件目录提供。

## 模板约束

- 模板代码和文档保持中文。
- 模板目录内不包含某个具体项目的专属字符串。
- `examples/` 只用于演示接入流程，可以出现示例项目名。
