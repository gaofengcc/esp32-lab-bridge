# Artifacts

每次完整烧录、日志、重启或截图闭环后，尽量收集这些输出：

- `output/{PROJECT}_merged.bin`
- `GET {BASE_URL}/api/state` 返回值
- `GET {BASE_URL}/api/tools` 返回值
- `GET {BASE_URL}/api/events` 关键日志片段
- `GET {BASE_URL}/api/screenshot/latest` 返回值
- `output/lab_session/` 或桥服务配置的截图、日志、抓包目录
- `.agent-sync/` 交接说明

最终交接优先记录：

- 烧录是否成功，以及 API 返回的关键字段
- 使用的串口 `{PORT}` 和波特率
- 首条启动标记、版本号或关键应用日志
- 截图文件名、时间戳和截图 API 返回值
- 如果失败，记录首个明确错误，而不是只写 timeout
