/**
 * @file lvgl_screenshot.h
 * @brief LVGL 屏幕截图组件
 *
 * 依赖 LVGL 和 lvgl_port。
 */

#ifndef LVGL_SCREENSHOT_H
#define LVGL_SCREENSHOT_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_LVGL_SCREENSHOT_ENABLE) && CONFIG_LVGL_SCREENSHOT_ENABLE

esp_err_t lvgl_screenshot_init(void);
bool lvgl_screenshot_capture(uint8_t **bmp_buf, size_t *bmp_len);
void lvgl_screenshot_free(uint8_t *bmp_buf);

#else

static inline esp_err_t lvgl_screenshot_init(void)
{
    return ESP_OK;
}

static inline bool lvgl_screenshot_capture(uint8_t **bmp_buf, size_t *bmp_len)
{
    if (bmp_buf) {
        *bmp_buf = NULL;
    }
    if (bmp_len) {
        *bmp_len = 0;
    }
    return false;
}

static inline void lvgl_screenshot_free(uint8_t *bmp_buf)
{
    (void)bmp_buf;
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* LVGL_SCREENSHOT_H */

