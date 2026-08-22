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

1. 复制 `firmware/` 下需要的组件到目标 ESP32 项目，并在 `idf_component.yml` 或 `CMakeLists.txt` 中声明依赖。
2. 修改 `bridge/config.json`，填入项目名、串口号、日志目录、端口和烧录参数，然后启动 Win10 串口桥。
3. 执行 `skill/install_skill.sh` 安装 Codex 技能模板，把 `{PROJECT}`、`{PORT}`、`{BASE_URL}` 参数换成你的项目值。

## 模板约束

- 模板代码和文档保持中文。
- 模板目录内不包含某个具体项目的专属字符串。
- `examples/` 只用于演示接入流程，可以出现示例项目名。

