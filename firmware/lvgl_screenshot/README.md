# LVGL 屏幕截图组件

本组件把当前 LVGL 活动屏幕捕获为标准 24 位 BMP。截图在 LVGL 任务中执行，避免业务任务直接访问 LVGL 内部状态；组件关闭时保留空桩接口，不会引入 LVGL 编译依赖。

## 三步接入

### 第 1 步：加入组件

把 `firmware/lvgl_screenshot` 放入目标项目的 `components/lvgl_screenshot`，或通过组件管理器引入本仓库路径。组件关闭时只注册头文件，不编译 `lvgl_screenshot.c`。

目标项目需要自行提供以下外部组件：

- `lvgl`：建议使用与项目一致的 LVGL 9.x 版本。
- `lvgl_port`：提供 `lvgl_port_call()`，负责把回调投递到 LVGL 任务。
- `freertos`、`heap`：ESP-IDF 内置组件。

### 第 2 步：打开配置

在 `menuconfig` 中打开：

```text
Component config
  -> LVGL 屏幕截图
     -> 启用 LVGL 屏幕截图 (CONFIG_LVGL_SCREENSHOT_ENABLE)
```

同时确认 LVGL 配置启用了 `LV_USE_SNAPSHOT`。没有 LVGL、`lvgl_port` 或快照支持时，请保持 `CONFIG_LVGL_SCREENSHOT_ENABLE=n`。

### 第 3 步：初始化并获取 BMP

在 `lvgl_port_init()` 成功后调用初始化接口；需要截图时调用捕获接口，输出完成后释放缓冲区：

```c
#include "lvgl_screenshot.h"
#include "lvgl_port.h"

void app_init(void)
{
    /* 先完成目标项目自己的 LVGL 初始化。 */
    lvgl_port_init();
    lvgl_screenshot_init();
}

bool app_capture(uint8_t **data, size_t *length)
{
    if (!lvgl_screenshot_capture(data, length)) {
        return false;
    }

    /* 这里发送 *data / *length；发送完成后释放。 */
    lvgl_screenshot_free(*data);
    *data = NULL;
    *length = 0;
    return true;
}
```

## API 清单

| API | 说明 |
| --- | --- |
| `esp_err_t lvgl_screenshot_init(void)` | 创建截图互斥量；应在 `lvgl_port_init()` 后调用。 |
| `bool lvgl_screenshot_capture(uint8_t **bmp_buf, size_t *bmp_len)` | 在 LVGL 任务中捕获当前活动屏幕并返回 BMP 缓冲区。 |
| `void lvgl_screenshot_free(uint8_t *bmp_buf)` | 释放截图接口分配的 BMP 缓冲区。 |

BMP 为 24 位 BGR、行顺序 bottom-up，行按 4 字节对齐。捕获接口会串行化并发请求，避免同一时间进行多次截图。

## Kconfig 项

| 配置项 | 默认值 | 作用 |
| --- | --- | --- |
| `CONFIG_LVGL_SCREENSHOT_ENABLE` | `n` | 开启截图源文件和 LVGL 相关私有依赖；关闭时仅保留安全空桩。 |

## 已知坑

- `lvgl_screenshot_init()` 必须在 `lvgl_port_init()` 成功后调用，否则截图请求无法投递到 LVGL 任务。
- 必须启用 LVGL 的 `LV_USE_SNAPSHOT`；未启用时捕获会返回失败并保留空桩行为。
- `lvgl_port` 不是本组件内置的显示驱动，目标项目需要提供与自身 LVGL 任务模型匹配的实现。
- `lvgl_screenshot_capture()` 返回的缓冲区由组件分配，发送或保存完成后必须调用 `lvgl_screenshot_free()`。
- 截图需要一块与屏幕分辨率相关的 RGB565 帧缓冲和 BMP 输出缓冲；内存不足时接口返回 `false`。
- 组件没有进行真机导入验证；本次发布仅做静态检查，未执行编译。
