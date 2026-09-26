# v0.2.0 交接记录

## 发布目标

本次把 `firmware/` 与 `wireless/` 下的五个 ESP-IDF 组件整理为可被新项目导入的发布态 `v0.2.0`。目录布局保持不变，未修改 `bridge/`、`skill/` 的既有实现，也未修改只读参考仓库。

## 合并明细

### `wireless/ota_update`

- 回移 Nas 版 `manifest_is_merged_flash_image()`：当 manifest 指向 `_merged.bin` 时，完整载荷仍按原文件校验 SHA256，但写入 OTA app 分区时跳过 merged 包内的 `0x10000` 前置镜像头。
- 回移 Nas 版 `schedule_rollback_once()` 与 `consume_rollback_once_flag()`：使用 NVS 保存一次性回滚标志；公开 `ota_update_schedule_rollback_once()` 供业务触发。
- 回移 `ota_update_start_from_manifest_json_auth()`，并补齐对应的非鉴权 JSON 入口。
- 保留模板版 `ota_update_status_json()`、`ota_update_state_name()`、`status_is_busy()`、状态扩展字段、manifest 字段别名和直接上传兼容占位 API。
- URL 入口保留 Bearer 鉴权，并兼容把 `_merged.bin` URL 归一化到同目录 `manifest.json`。
- 未原样合入 Nas 的 `app_log/easylogger` 日志依赖、独立 operation mutex/10 秒状态缓存、测试 URL Kconfig；分别由模板的 `esp_log`、状态锁与忙态拒绝、通用 manifest API 替代。Nas 内部可关闭回滚标志的分支保留为静态能力，但发布 API 只公开启用一次性回滚。

### `wireless/diag_service`

- 以模板版回调注入式、JSON writer、句柄生命周期、路由和 Bearer 鉴权为主干。
- 回移 Nas Kconfig：`DIAG_SERVICE_TEST_AUTH_BYPASS`、`DIAG_SERVICE_HTTPD_STACK_SIZE`、`DIAG_SERVICE_HTTPD_MAX_URI_HANDLERS`，并让 HTTP 服务真正使用栈大小与 URI 槽位配置。
- 回移 Nas 的自检能力：新增 `/api/selftest` 与 `on_selftest` writer 回调。
- 保留模板原有状态、日志、截图、OTA、重启回调及截图自定义释放策略。
- 未合入 Nas 的 `diag_service_init/start/stop` 分离 API、固定 char-buffer provider、额外 mutex 和 `read_request_body()`，因为它们与模板句柄式回调契约冲突；请求体仍由 OTA/重启等业务回调自行读取。

### `firmware/lvgl_screenshot`

- 配置统一为 `CONFIG_LVGL_SCREENSHOT_ENABLE`，不再使用 Nas 版 `LVGL_SCREENSHOT_DEBUG`。
- 保留模板版可选依赖：关闭配置时不编译截图源文件、不引入 LVGL 相关依赖，并在头文件提供安全空桩。
- 保留并核对截图函数集合：RGB565 位扩展、BGR24 编码、BMP 头、LVGL 任务内快照、互斥串行化、内存失败清理和释放 API。
- 未合入 Nas 的 `app_log` 与看门狗 heartbeat 调用，避免引入业务耦合；如未来需要，可通过通用可选回调重新设计。

### `firmware/cdc_command` 与 `firmware/log_gate`

- 按任务约束保持模板版现状，没有从参考工程的项目专用 CDC 实现或设备配置耦合实现回移。
- 两个组件补齐发布元数据、中文头文件说明、API/Kconfig/已知坑 README，并升级到 `0.2.0`。

## 发布态文件

- 每个组件均包含 `CMakeLists.txt`、`idf_component.yml`、`include/*.h`、`README.md`。
- 五个 manifest 版本统一为 `0.2.0`，顶层 README 增加组件管理器和 submodule 两种导入方式。
- `tools/check_components.sh` 检查文件完整性、版本一致性、中文头注释和业务字符串残留。
- `.gitignore` 忽略 `.vscode/` 与 `bridge/logs/`；有效的 Windows 启动脚本已纳入版本控制。

## 实测证据

以下命令均为静态检查，遵守本次“不编译、不联网、不 push”约束：

```text
bash tools/check_components.sh
git diff --check
rg -n -i 'nas_|nas-01|ext/|int/|mqtt|device_config|lcd35|LCD35' firmware wireless
```

`check_components.sh` 五个组件的必备文件、`0.2.0` 版本、中文头注释和业务字符串扫描全部 PASS；本次未执行 ESP-IDF 编译、组件管理器真实拉取或真机验证。

## 已知缺口与遗留风险

- 没有 Wi-Fi 组件、配网页或网络生命周期管理；`diag_service` 需要业务侧先完成网络接入并注入回调。
- 没有真实的组件管理器导入验证，也没有在目标项目中编译验证。
- 没有 OTA 真机写入、merged 包重启、rollback 和断电恢复验证；这些仍需在双 app 分区设备上复测。
- merged bin 识别依赖文件名包含 `_merged.bin`；manifest 的 size/SHA256 必须对应完整下载文件。
- `ota_update_schedule_rollback_once()` 依赖应用先完成 NVS 初始化，且目标工程必须启用 rollback 与双 app 分区。
- `diag_service` 的实验室鉴权旁路默认关闭，发布构建必须继续保持关闭。
- LVGL 截图需要目标工程提供兼容的 `lvgl_port_call()` 和 `LV_USE_SNAPSHOT`；关闭 Kconfig 时仅保证安全空桩。

## 下一步

1. 在一个干净的新 ESP-IDF 工程中用顶层 README 的 A/B 两种方式分别导入并完成一次构建。
2. 在双 app 分区设备上验证 app-only 与 merged bin 两条 OTA 路径、Bearer manifest、pending verify 和一次性 rollback。
3. 根据目标项目实际的日志库、LVGL 端口和网络栈，补充集成级测试与硬件台架记录。
