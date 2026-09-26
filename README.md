# ESP32 Lab Bridge

这是一个可复用的 ESP32 远程自动化调试模板仓库。

目标是把 USB CDC 命令通道、日志暂停/恢复、LVGL 截图、局域网无线诊断、Win10 串口桥和 Codex 技能打包成通用骨架，方便其他 ESP32 项目直接接入。

## 目录

```text
firmware/
wireless/
bridge/
skill/
tools/
examples/
```

## 快速开始

1. 复制 `firmware/` 下需要的组件到目标 ESP32 项目的 `components/`，在业务组件 `CMakeLists.txt` 中声明 `REQUIRES cdc_command log_gate lvgl_screenshot`，并按 [examples/README.md](examples/README.md) 接好 CDC 初始化、RX 喂入、命令注册、串口写函数和截图输出。
2. 如果项目已经有可访问的局域网 IP，再复制 `wireless/` 下需要的组件到目标 ESP32 项目的 `components/`，在业务组件 `CMakeLists.txt` 中声明 `REQUIRES diag_service ota_update`，并按 [wireless/README.md](wireless/README.md) 接好 HTTP 服务、token、日志、重启和 OTA 流程。
3. 在 Windows 10/Windows 侧安装 Node.js，确保 PowerShell 可用；修改 `bridge/config.json` 的项目名、串口号、日志目录、`host`/`port` 和烧录参数，然后执行 `cd bridge && npm start` 启动串口桥。桥接服务地址来自 `config.json` 的 `host`/`port`；`host: "0.0.0.0"` 是监听地址，给浏览器或技能使用时请填实际可访问地址，例如 `http://127.0.0.1:3000` 或 Windows 主机 LAN IP。
4. 执行 `PROJECT=你的项目 PORT=COM5 BASE_URL=http://127.0.0.1:3000 bash skill/install_skill.sh` 安装 Codex 技能。脚本会在安装时替换 `{PROJECT}`、`{PORT}`、`{BASE_URL}`；不设置环境变量时使用脚本默认值。

## 导入到新项目

发布态版本为 `v0.2.0`。五个组件都可以作为独立 ESP-IDF 组件导入，API 与目录布局保持向后兼容。

### 方式 A：组件管理器

在新项目的 `main/idf_component.yml`（或业务组件自己的 manifest）中加入需要的依赖。下面是全部五个组件的可直接复制示例：

```yaml
dependencies:
  gaofengcc/lab_bridge_cdc_command:
    git: https://github.com/gaofengcc/esp32-lab-bridge.git
    path: firmware/cdc_command
    version: "v0.2.0"
  gaofengcc/lab_bridge_log_gate:
    git: https://github.com/gaofengcc/esp32-lab-bridge.git
    path: firmware/log_gate
    version: "v0.2.0"
  gaofengcc/lab_bridge_lvgl_screenshot:
    git: https://github.com/gaofengcc/esp32-lab-bridge.git
    path: firmware/lvgl_screenshot
    version: "v0.2.0"
  gaofengcc/lab_bridge_diag_service:
    git: https://github.com/gaofengcc/esp32-lab-bridge.git
    path: wireless/diag_service
    version: "v0.2.0"
  gaofengcc/lab_bridge_ota_update:
    git: https://github.com/gaofengcc/esp32-lab-bridge.git
    path: wireless/ota_update
    version: "v0.2.0"
```

然后在业务组件中按需写入 `REQUIRES cdc_command log_gate lvgl_screenshot diag_service ota_update`。`log_gate` 需要目标工程提供 EasyLogger；`lvgl_screenshot` 只有在 `LVGL_SCREENSHOT_ENABLE=y` 时才启用 LVGL 相关源文件和私有依赖。

### 方式 B：离线或国内网络推荐

把仓库作为 submodule 放到新项目中，再用 `EXTRA_COMPONENT_DIRS` 指向需要的组件目录：

```bash
cd /path/to/your-esp32-project
git submodule add https://github.com/gaofengcc/esp32-lab-bridge.git third_party/esp32-lab-bridge
git submodule update --init --depth 1
```

在项目根目录的 `CMakeLists.txt` 中加入：

```cmake
set(LAB_BRIDGE_ROOT "${CMAKE_CURRENT_LIST_DIR}/third_party/esp32-lab-bridge")
set(EXTRA_COMPONENT_DIRS
    "${LAB_BRIDGE_ROOT}/firmware/cdc_command"
    "${LAB_BRIDGE_ROOT}/firmware/log_gate"
    "${LAB_BRIDGE_ROOT}/firmware/lvgl_screenshot"
    "${LAB_BRIDGE_ROOT}/wireless/diag_service"
    "${LAB_BRIDGE_ROOT}/wireless/ota_update"
)
```

随后执行 `idf.py reconfigure`，在业务组件的 `CMakeLists.txt` 中按需声明依赖即可。该方式不依赖组件管理器缓存，适合无法稳定访问外部组件仓库的环境。

## 固件组件依赖

外部依赖由目标项目自行提供；模板只提供可复制组件和接入骨架。

| 组件 | 必需依赖 | 说明 |
| --- | --- | --- |
| `cdc_command` | ESP-IDF 基础组件 | CDC 帧解析、命令注册、响应帧封装，不绑定具体串口驱动。 |
| `log_gate` | `easylogger`、`freertos` | 使用 `elog.h` 暂停/恢复日志输出，目标项目需要已经集成 EasyLogger。 |
| `lvgl_screenshot` | 仅在 `CONFIG_LVGL_SCREENSHOT_ENABLE=y` 时需要 `lvgl`、`lvgl_port`、`freertos`、`heap` | 无 LVGL 或未启用 `LV_USE_SNAPSHOT` 的项目保持关闭并跳过截图能力。 |
| `wireless/diag_service` | `esp_http_server`、`freertos` | 局域网诊断 HTTP 服务，负责路由、鉴权和状态 JSON 装配。 |
| `wireless/ota_update` | `esp_http_client`、`esp_partition`、`app_update`、`bootloader_support`、`cjson`、`mbedtls`、`heap` | manifest → bin 下载、SHA256 校验、双分区写入和回滚状态。 |

每个组件目录带有最小 `idf_component.yml`，便于目标项目用 Component Manager 或目录复制方式接入；EasyLogger、LVGL、`lvgl_port` 等业务外部依赖仍由目标项目自己的 manifest 或组件目录提供。

## 模板约束

- 模板代码和文档保持中文。
- 模板目录内不包含某个具体项目的专属字符串。
- `examples/` 只用于演示接入流程，可以出现示例项目名。

## 无线调试

`wireless/` 提供局域网优先的诊断模板，默认面向 `http://<device-ip>:8080`。
它和 `bridge/` 的关系是无线优先、串口兜底：设备可被局域网访问时，优先用
HTTP API 完成状态、日志、截屏、OTA 和重启；网络不可用或设备恢复阶段，再回到
串口桥。
