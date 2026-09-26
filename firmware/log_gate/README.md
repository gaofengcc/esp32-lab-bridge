# log_gate

EasyLogger 输出门控组件，提供可嵌套的暂停/恢复、异步队列清空和状态查询。具体日志后端由目标工程提供。

## 3 步接入

1. 将 `firmware/log_gate` 复制到目标工程的 `components/log_gate`，并确保目标工程已经集成 `easylogger`。
2. 在业务组件的 `CMakeLists.txt` 中加入 `REQUIRES log_gate`，系统启动后调用 `log_gate_init()`。
3. 在截图、升级或其他需要安静日志的临界区调用 `log_gate_pause()`，结束后调用 `log_gate_resume()`；可用查询 API 做状态监控。

## API 清单

- `log_gate_init()` / `log_gate_deinit()`：初始化或清理门控状态。
- `log_gate_pause()` / `log_gate_resume()`：嵌套暂停与恢复日志输出。
- `log_gate_is_paused()`：查询当前是否处于暂停状态。
- `log_gate_pause_depth()`：查询当前嵌套暂停深度。

## Kconfig

本组件没有额外 Kconfig 项。若目标工程定义了 `ELOG_ASYNC_OUTPUT_ENABLE`，暂停时会调用 EasyLogger 的异步队列清理接口。

## 已知坑

- 组件依赖 EasyLogger 的 `elog_get_output_enabled()`、`elog_set_output_enabled()`、`elog_set_output_paused()` 接口，目标工程必须使用兼容版本。
- 暂停/恢复应成对调用；多次暂停会增加深度，只有最后一次恢复才真正恢复输出。
- 临界区保护使用 FreeRTOS 自旋锁，不应在持锁期间扩展业务逻辑或调用可能阻塞的接口。
