# cdc_command

通用 USB CDC 命令帧组件，只负责帧编解码、CRC8、命令注册和回调分发，不绑定具体串口驱动或业务协议。

## 3 步接入

1. 将 `firmware/cdc_command` 复制到目标工程的 `components/cdc_command`，或按顶层 README 的组件管理器/submodule 方式引入。
2. 在业务组件的 `CMakeLists.txt` 中加入 `REQUIRES cdc_command`，初始化 `cdc_cmd_parser_t` 并注册业务命令。
3. 串口收到的每个字节调用 `cdc_cmd_parser_feed()`；发送时通过 `cdc_cmd_send_frame()` 或 `cdc_cmd_send_response()` 注入底层写回调。

## API 清单

- `cdc_cmd_register()` / `cdc_cmd_unregister()` / `cdc_cmd_clear_registry()`：维护命令表。
- `cdc_cmd_find()` / `cdc_cmd_dispatch()`：查询并分发命令。
- `cdc_cmd_crc8()`：计算 CRC8。
- `cdc_cmd_build_frame()`：构造 `[0xAB 0xCD][cmd][len LE][payload][crc8]` 帧。
- `cdc_cmd_send_frame()` / `cdc_cmd_send_response()`：通过调用方提供的写回调发送帧。
- `cdc_cmd_parser_init()` / `cdc_cmd_parser_feed()`：增量解析输入字节流。

## Kconfig

本组件没有额外 Kconfig 项。最大 payload 为 `CDC_CMD_MAX_PAYLOAD`（默认 4096 字节），命令注册表容量为 16。

## 已知坑

- 组件不会创建 UART/USB 任务，必须由业务侧保证串口驱动、线程安全和写回调生命周期。
- `cdc_cmd_parser_feed()` 返回 `1` 才表示完成一帧，返回 `0` 表示继续等待，返回负数表示帧格式或 CRC 错误。
- 响应帧会把状态码作为 payload 第一个字节，业务协议解析端需要按此约定处理。
