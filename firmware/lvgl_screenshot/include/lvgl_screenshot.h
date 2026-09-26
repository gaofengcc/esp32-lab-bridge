/**
 * @file lvgl_screenshot.h
 * @brief LVGL 屏幕截图与 BMP 编码接口
 *
 * 启用 CONFIG_LVGL_SCREENSHOT_ENABLE 后，组件会在 LVGL 任务中捕获
 * 当前活动屏幕，并返回 24 位 BGR bottom-up BMP 数据。
 *
 * 未启用配置时，接口仍然可安全调用：初始化返回 ESP_OK，截图返回
 * false，释放函数不执行任何操作。这样业务代码无需增加条件编译。
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

/**
 * @brief 初始化截图模块。
 *
 * 应在 lvgl_port_init() 成功后调用一次。
 *
 * @return ESP_OK 表示初始化成功，ESP_ERR_NO_MEM 表示互斥量创建失败。
 */
esp_err_t lvgl_screenshot_init(void);

/**
 * @brief 捕获当前 LVGL 活动屏幕并编码为 BMP。
 *
 * 捕获操作会通过 lvgl_port_call() 投递到 LVGL 任务中执行，避免跨任务
 * 直接访问 LVGL。成功后由组件分配 BMP 缓冲区，调用者必须使用
 * lvgl_screenshot_free() 释放。
 *
 * @param[out] bmp_buf 成功时返回 BMP 缓冲区地址，失败时置为 NULL。
 * @param[out] bmp_len 成功时返回 BMP 字节数，失败时置为 0。
 * @return true 表示成功，false 表示参数、调度、内存或快照失败。
 */
bool lvgl_screenshot_capture(uint8_t **bmp_buf, size_t *bmp_len);

/**
 * @brief 释放 lvgl_screenshot_capture() 返回的 BMP 缓冲区。
 *
 * @param[in] bmp_buf 由截图接口返回的缓冲区，可以传入 NULL。
 */
void lvgl_screenshot_free(uint8_t *bmp_buf);

#else

/* 配置关闭时提供空桩，便于上层代码保持统一调用路径。 */
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
