# 接入示例

下面用一个示例项目说明三步接入流程。示例中的项目名仅用于演示，实际使用时请替换成你的项目。

## 第 1 步 复制固件组件

把 `firmware/cdc_command`、`firmware/log_gate`、`firmware/lvgl_screenshot` 复制到目标 ESP32 项目的 `components/` 目录。

目标项目需要自己提供外部依赖：

- `cdc_command`：只依赖 ESP-IDF 基础组件。
- `log_gate`：依赖 `easylogger` 和 `freertos`，目标项目需要已集成 EasyLogger。
- `lvgl_screenshot`：仅在 `CONFIG_LVGL_SCREENSHOT_ENABLE=y` 时需要 `lvgl`、`lvgl_port`、`freertos`、`heap`，并且 LVGL 配置里要启用 `LV_USE_SNAPSHOT`。

目标业务组件的 `CMakeLists.txt` 可以这样写：

```cmake
idf_component_register(
    SRCS
        "app_bridge.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        cdc_command
        log_gate
        lvgl_screenshot
)
```

如果业务代码暂时不需要截图能力，可以先去掉 `lvgl_screenshot`，但截图命令就不会工作。

## 第 2 步 配置桥接服务

把 `bridge/` 复制到 Windows 机器，确保机器上已经安装 Node.js，且可以调用 `powershell.exe`。

编辑 [config.json](../bridge/config.json)：

- `projectName`：项目名
- `host` / `port`：Web 服务监听地址和端口
- `defaultPort` / `defaultBaudrate`：默认串口参数
- `defaultChip`：烧录芯片型号
- `logsDir` / `capturesDir`：日志和截图目录
- `esptoolExe`：可选的 `esptool.exe` 路径

然后在 `bridge/` 目录下启动：

```bash
cd bridge
npm start
```

服务地址来自 `config.json` 的 `host` / `port`。`host: "0.0.0.0"` 只是监听地址，不是浏览器里该直接填写的地址；本机访问通常用 `http://127.0.0.1:3000`，远程访问则用 Windows 主机实际可达的 IP 或主机名。

## 第 3 步 安装技能

在模板仓库里运行：

```bash
PROJECT=你的项目 PORT=COM5 BASE_URL=http://127.0.0.1:3000 bash skill/install_skill.sh
```

脚本会在安装时把 `skill/SKILL.md` 里的 `{PROJECT}`、`{PORT}`、`{BASE_URL}` 自动替换成环境变量对应的值；不设置环境变量时使用脚本默认值。

## app_main 最小接入骨架

下面是最小集成代码。它做了这几件事：

1. 初始化 CDC 解析器。
2. 把串口收到的每个字节喂给 `cdc_cmd_parser_feed()`。
3. 注册 `CDC_CMD_SCREEN_REQ`、`CDC_CMD_REBOOT`、`CDC_CMD_LOG_PAUSE`、`CDC_CMD_LOG_RESUME`。
4. 给 `cdc_cmd_send_response()` 提供串口写函数。
5. 在截图命令里按 `[0x5CA7E01F][len][BMP]` 输出原始截图数据。

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cdc_command.h"
#include "log_gate.h"
#include "lvgl_screenshot.h"

#define APP_CDC_UART UART_NUM_0

static const char *TAG = "app_bridge";
static cdc_cmd_parser_t s_parser;

static esp_err_t app_serial_write(const uint8_t *data, size_t len, void *user_ctx)
{
    uart_port_t port = (uart_port_t)(uintptr_t)user_ctx;
    int written = uart_write_bytes(port, (const char *)data, len);
    return (written == (int)len) ? ESP_OK : ESP_FAIL;
}

static esp_err_t app_write_u32_le(uint32_t value, void *user_ctx)
{
    uint8_t buf[4] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 24) & 0xFF),
    };
    return app_serial_write(buf, sizeof(buf), user_ctx);
}

static esp_err_t app_send_bmp(uint8_t *bmp, size_t bmp_len, void *user_ctx)
{
    const uint32_t magic = 0x5CA7E01Fu;
    esp_err_t err = app_write_u32_le(magic, user_ctx);
    if (err != ESP_OK) {
        return err;
    }
    err = app_write_u32_le((uint32_t)bmp_len, user_ctx);
    if (err != ESP_OK) {
        return err;
    }
    return app_serial_write(bmp, bmp_len, user_ctx);
}

static void on_screen_req(uint8_t cmd,
                          const uint8_t *payload,
                          uint16_t payload_len,
                          void *user_ctx)
{
    (void)cmd;
    (void)payload;
    (void)payload_len;

    uint8_t *bmp = NULL;
    size_t bmp_len = 0;
    if (!lvgl_screenshot_capture(&bmp, &bmp_len)) {
        cdc_cmd_send_response(app_serial_write, user_ctx,
                              CDC_CMD_SCREEN_REQ,
                              CDC_CMD_STATUS_INTERNAL,
                              NULL,
                              0);
        return;
    }

    if (app_send_bmp(bmp, bmp_len, user_ctx) != ESP_OK) {
        ESP_LOGE(TAG, "截图输出失败");
    }
    lvgl_screenshot_free(bmp);
}

static void on_reboot(uint8_t cmd,
                      const uint8_t *payload,
                      uint16_t payload_len,
                      void *user_ctx)
{
    (void)cmd;
    (void)payload;
    (void)payload_len;

    cdc_cmd_send_response(app_serial_write, user_ctx,
                          CDC_CMD_REBOOT,
                          CDC_CMD_STATUS_OK,
                          NULL,
                          0);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static void on_log_pause(uint8_t cmd,
                         const uint8_t *payload,
                         uint16_t payload_len,
                         void *user_ctx)
{
    (void)cmd;
    (void)payload;
    (void)payload_len;

    esp_err_t err = log_gate_pause();
    cdc_cmd_send_response(app_serial_write, user_ctx,
                          CDC_CMD_LOG_PAUSE,
                          (err == ESP_OK) ? CDC_CMD_STATUS_OK : CDC_CMD_STATUS_INTERNAL,
                          NULL,
                          0);
}

static void on_log_resume(uint8_t cmd,
                          const uint8_t *payload,
                          uint16_t payload_len,
                          void *user_ctx)
{
    (void)cmd;
    (void)payload;
    (void)payload_len;

    esp_err_t err = log_gate_resume();
    cdc_cmd_send_response(app_serial_write, user_ctx,
                          CDC_CMD_LOG_RESUME,
                          (err == ESP_OK) ? CDC_CMD_STATUS_OK : CDC_CMD_STATUS_INTERNAL,
                          NULL,
                          0);
}

static void app_feed_cdc_rx(const uint8_t *data, size_t len, void *user_ctx)
{
    cdc_cmd_frame_t frame;
    for (size_t i = 0; i < len; i++) {
        int ret = cdc_cmd_parser_feed(&s_parser, data[i], &frame);
        if (ret == 1) {
            esp_err_t err = cdc_cmd_dispatch(frame.cmd,
                                             frame.payload,
                                             frame.payload_len,
                                             user_ctx);
            if (err != ESP_OK) {
                cdc_cmd_send_response(app_serial_write, user_ctx,
                                      frame.cmd,
                                      CDC_CMD_STATUS_NOT_FOUND,
                                      NULL,
                                      0);
            }
        }
    }
}

static void app_cdc_rx_task(void *arg)
{
    uart_port_t port = (uart_port_t)(uintptr_t)arg;
    uint8_t rx[128];

    while (1) {
        int n = uart_read_bytes(port, rx, sizeof(rx), portMAX_DELAY);
        if (n > 0) {
            app_feed_cdc_rx(rx, (size_t)n, arg);
        }
    }
}

void app_main(void)
{
    cdc_cmd_parser_init(&s_parser);
    cdc_cmd_clear_registry();

    log_gate_init();
    lvgl_screenshot_init();

    cdc_cmd_register(CDC_CMD_SCREEN_REQ, "screen_req", on_screen_req, "抓取 LVGL 截图");
    cdc_cmd_register(CDC_CMD_REBOOT, "reboot", on_reboot, "重启设备");
    cdc_cmd_register(CDC_CMD_LOG_PAUSE, "log_pause", on_log_pause, "暂停日志");
    cdc_cmd_register(CDC_CMD_LOG_RESUME, "log_resume", on_log_resume, "恢复日志");

    // 这里省略串口初始化。目标项目只要把收到的原始字节喂给 app_feed_cdc_rx() 即可。
    xTaskCreate(app_cdc_rx_task, "cdc_rx", 4096, (void *)(uintptr_t)APP_CDC_UART, 10, NULL);
}
```

要点只有两个：

- CDC 命令走 `[0xAB 0xCD][cmd][len][payload][crc8]`。
- 截图成功后不要再包 CDC 帧，直接输出 `[0x5CA7E01F][len][BMP]` 原始字节流。

如果项目没有 LVGL，或者 `LV_USE_SNAPSHOT` 没开，就保持 `CONFIG_LVGL_SCREENSHOT_ENABLE=n`，并且不要接入截图命令。
