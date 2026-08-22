/**
 * @file lvgl_screenshot.c
 * @brief LVGL 屏幕截图与 BMP 编码
 */

#include "lvgl_screenshot.h"

#if defined(CONFIG_LVGL_SCREENSHOT_ENABLE) && CONFIG_LVGL_SCREENSHOT_ENABLE

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "lvgl_port.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "lvgl_screenshot";
static SemaphoreHandle_t s_capture_mutex = NULL;

typedef struct {
    uint8_t *fb;
    size_t fb_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    bool ok;
    const char *error;
} screenshot_request_t;

static uint8_t expand5(uint16_t v)
{
    v &= 0x1F;
    return (uint8_t)((v << 3) | (v >> 2));
}

static uint8_t expand6(uint16_t v)
{
    v &= 0x3F;
    return (uint8_t)((v << 2) | (v >> 4));
}

static void encode_row_bgr(const uint16_t *src, uint8_t *dst, uint32_t w)
{
    for (uint32_t x = 0; x < w; x++) {
        uint16_t c = src[x];
        uint8_t r = expand5(c >> 11);
        uint8_t g = expand6(c >> 5);
        uint8_t b = expand5(c);

        dst[x * 3 + 0] = b;
        dst[x * 3 + 1] = g;
        dst[x * 3 + 2] = r;
    }
}

static void build_bmp_header(uint8_t *hdr,
                             uint32_t w,
                             uint32_t h,
                             uint32_t pixel_data_size)
{
    uint32_t total = 54 + pixel_data_size;
    memset(hdr, 0, 54);
    hdr[0] = 'B';
    hdr[1] = 'M';
    hdr[2] = (uint8_t)(total);
    hdr[3] = (uint8_t)(total >> 8);
    hdr[4] = (uint8_t)(total >> 16);
    hdr[5] = (uint8_t)(total >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (uint8_t)(w);
    hdr[19] = (uint8_t)(w >> 8);
    hdr[20] = (uint8_t)(w >> 16);
    hdr[21] = (uint8_t)(w >> 24);
    hdr[22] = (uint8_t)(h);
    hdr[23] = (uint8_t)(h >> 8);
    hdr[24] = (uint8_t)(h >> 16);
    hdr[25] = (uint8_t)(h >> 24);
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = (uint8_t)(pixel_data_size);
    hdr[35] = (uint8_t)(pixel_data_size >> 8);
    hdr[36] = (uint8_t)(pixel_data_size >> 16);
    hdr[37] = (uint8_t)(pixel_data_size >> 24);
}

static bool encode_bmp_from_rgb565(const uint8_t *fb,
                                   uint32_t w,
                                   uint32_t h,
                                   uint32_t stride,
                                   uint8_t **bmp_buf,
                                   size_t *bmp_len)
{
    uint32_t row_bytes_raw = w * 3;
    uint32_t row_pad = (row_bytes_raw + 3) & ~3u;
    uint32_t pixel_data_size = row_pad * h;
    size_t bmp_total = 54 + pixel_data_size;

    uint8_t *bmp_out = (uint8_t *)heap_caps_malloc(bmp_total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!bmp_out) {
        bmp_out = (uint8_t *)heap_caps_malloc(bmp_total, MALLOC_CAP_8BIT);
    }
    if (!bmp_out) {
        bmp_out = (uint8_t *)malloc(bmp_total);
    }
    if (!bmp_out) {
        ESP_LOGE(TAG, "BMP 缓冲区分配失败: %u 字节", (unsigned)bmp_total);
        return false;
    }

    build_bmp_header(bmp_out, w, h, pixel_data_size);

    uint8_t *pixel_start = bmp_out + 54;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = h - 1 - y;
        const uint16_t *src_row = (const uint16_t *)(fb + src_y * stride);
        uint8_t *dst_row = pixel_start + y * row_pad;
        encode_row_bgr(src_row, dst_row, w);
        if (row_pad > row_bytes_raw) {
            memset(dst_row + row_bytes_raw, 0, row_pad - row_bytes_raw);
        }
    }

    *bmp_buf = bmp_out;
    *bmp_len = bmp_total;
    return true;
}

static void capture_snapshot_in_lvgl_task(void *user_data)
{
    screenshot_request_t *req = (screenshot_request_t *)user_data;
    lv_display_t *disp = lv_display_get_default();
    lv_obj_t *screen = lv_screen_active();

    if (!req || !disp || !screen) {
        if (req) {
            req->error = "no_display_or_screen";
        }
        return;
    }

#if LV_USE_SNAPSHOT
    uint32_t w = (uint32_t)lv_display_get_horizontal_resolution(disp);
    uint32_t h = (uint32_t)lv_display_get_vertical_resolution(disp);
    uint32_t stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
    if (stride == 0) {
        stride = w * 2;
    }

    size_t fb_size = (size_t)stride * h;
    uint8_t *fb = (uint8_t *)heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb) {
        fb = (uint8_t *)heap_caps_malloc(fb_size, MALLOC_CAP_8BIT);
    }
    if (!fb) {
        fb = (uint8_t *)malloc(fb_size);
    }
    if (!fb) {
        req->error = "fb_alloc_failed";
        ESP_LOGE(TAG, "RGB565 缓冲区分配失败: %u 字节", (unsigned)fb_size);
        return;
    }
    memset(fb, 0, fb_size);

    lv_draw_buf_t draw_buf;
    lv_result_t init_res = lv_draw_buf_init(&draw_buf, w, h, LV_COLOR_FORMAT_RGB565,
                                            stride, fb, (uint32_t)fb_size);
    if (init_res != LV_RESULT_OK) {
        free(fb);
        req->error = "draw_buf_init_failed";
        return;
    }
    lv_draw_buf_set_flag(&draw_buf, LV_IMAGE_FLAGS_MODIFIABLE);

    lv_result_t snap_res = lv_snapshot_take_to_draw_buf(screen,
                                                        LV_COLOR_FORMAT_RGB565,
                                                        &draw_buf);
    if (snap_res != LV_RESULT_OK) {
        free(fb);
        req->error = "snapshot_failed";
        return;
    }

    req->fb = fb;
    req->fb_size = fb_size;
    req->width = draw_buf.header.w;
    req->height = draw_buf.header.h;
    req->stride = draw_buf.header.stride;
    req->ok = true;
#else
    req->error = "snapshot_disabled";
#endif
}

esp_err_t lvgl_screenshot_init(void)
{
    if (!s_capture_mutex) {
        s_capture_mutex = xSemaphoreCreateMutex();
        if (!s_capture_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "LVGL 截图模块已初始化");
    return ESP_OK;
}

bool lvgl_screenshot_capture(uint8_t **bmp_buf, size_t *bmp_len)
{
    if (!bmp_buf || !bmp_len || !s_capture_mutex) {
        return false;
    }

    *bmp_buf = NULL;
    *bmp_len = 0;

    if (xSemaphoreTake(s_capture_mutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGW(TAG, "截图正在进行中");
        return false;
    }

    screenshot_request_t req = {0};
    esp_err_t call_res = lvgl_port_call(capture_snapshot_in_lvgl_task, &req);
    if (call_res != ESP_OK || !req.ok) {
        ESP_LOGE(TAG, "截图失败: call=%s reason=%s",
                 esp_err_to_name(call_res), req.error ? req.error : "unknown");
        if (req.fb) {
            free(req.fb);
        }
        xSemaphoreGive(s_capture_mutex);
        return false;
    }

    bool encoded = encode_bmp_from_rgb565(req.fb, req.width, req.height,
                                          req.stride, bmp_buf, bmp_len);
    free(req.fb);
    xSemaphoreGive(s_capture_mutex);
    return encoded;
}

void lvgl_screenshot_free(uint8_t *bmp_buf)
{
    if (bmp_buf) {
        free(bmp_buf);
    }
}

#endif /* CONFIG_LVGL_SCREENSHOT_ENABLE */

