/**
 * @file diag_service.h
 * @brief 通用无线诊断服务模板
 *
 * 目标是把 LAN HTTP 诊断能力做成可复用底座：
 * - Bearer 鉴权
 * - 健康检查、状态、日志、截图、OTA、重启路由
 * - 状态 JSON 由业务侧通过小型 writer 逐字段组装
 * - 截图只负责回调获取 BMP 缓冲区，释放策略由调用方配置
 */

#ifndef DIAG_SERVICE_H
#define DIAG_SERVICE_H

#include "esp_err.h"
#include "esp_http_server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIAG_SERVICE_DEFAULT_PORT         8080
#define DIAG_SERVICE_DEFAULT_MIN_TOKEN    16U
#define DIAG_SERVICE_DEFAULT_JSON_CAP     512U
#define DIAG_SERVICE_MAX_JSON_DEPTH       8U

typedef struct diag_service_s *diag_service_handle_t;

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    uint8_t depth;
    bool pending_name;
    bool is_object[DIAG_SERVICE_MAX_JSON_DEPTH];
    bool need_comma[DIAG_SERVICE_MAX_JSON_DEPTH];
} diag_json_writer_t;

/* 在服务已打开的 JSON 对象中追加业务字段，不需要 begin/end 根对象。 */
typedef esp_err_t (*diag_service_status_cb_t)(diag_json_writer_t *writer,
                                              void *user_ctx);

/* HTTP 路由回调负责自行调用 httpd_resp_* 发送响应。 */
typedef esp_err_t (*diag_service_http_cb_t)(httpd_req_t *req,
                                            void *user_ctx);

/* 返回 BMP 内存缓冲区；释放方式由 on_screenshot_free 控制，未配置时使用 free。 */
typedef esp_err_t (*diag_service_screenshot_cb_t)(uint8_t **bmp_buf,
                                                  size_t *bmp_len,
                                                  void *user_ctx);
typedef void (*diag_service_free_cb_t)(uint8_t *bmp_buf,
                                       void *user_ctx);

typedef struct {
    const char *service_name;
    uint16_t port;
    const char *bearer_token;
    size_t min_token_len;
    void *user_ctx;

    diag_service_status_cb_t on_status;
    diag_service_http_cb_t on_logs;
    diag_service_screenshot_cb_t on_screenshot;
    diag_service_free_cb_t on_screenshot_free;
    diag_service_status_cb_t on_ota_status;
    diag_service_http_cb_t on_ota_check;
    diag_service_http_cb_t on_ota_start;
    diag_service_http_cb_t on_reboot;
    /* 自检路由复用状态 writer，业务侧只追加自检字段。 */
    diag_service_status_cb_t on_selftest;
} diag_service_config_t;

esp_err_t diag_json_writer_init(diag_json_writer_t *writer, size_t initial_cap);
void diag_json_writer_deinit(diag_json_writer_t *writer);
const char *diag_json_writer_cstr(const diag_json_writer_t *writer);
size_t diag_json_writer_len(const diag_json_writer_t *writer);

esp_err_t diag_json_writer_begin_object(diag_json_writer_t *writer);
esp_err_t diag_json_writer_end_object(diag_json_writer_t *writer);
esp_err_t diag_json_writer_begin_array(diag_json_writer_t *writer);
esp_err_t diag_json_writer_end_array(diag_json_writer_t *writer);
esp_err_t diag_json_writer_name(diag_json_writer_t *writer, const char *name);
esp_err_t diag_json_writer_string(diag_json_writer_t *writer, const char *value);
esp_err_t diag_json_writer_raw(diag_json_writer_t *writer, const char *json);
esp_err_t diag_json_writer_bool(diag_json_writer_t *writer, bool value);
esp_err_t diag_json_writer_i64(diag_json_writer_t *writer, int64_t value);
esp_err_t diag_json_writer_u64(diag_json_writer_t *writer, uint64_t value);
esp_err_t diag_json_writer_null(diag_json_writer_t *writer);
esp_err_t diag_json_writer_kv_string(diag_json_writer_t *writer,
                                    const char *name,
                                    const char *value);
esp_err_t diag_json_writer_kv_raw(diag_json_writer_t *writer,
                                  const char *name,
                                  const char *json);
esp_err_t diag_json_writer_kv_bool(diag_json_writer_t *writer,
                                   const char *name,
                                   bool value);
esp_err_t diag_json_writer_kv_i64(diag_json_writer_t *writer,
                                  const char *name,
                                  int64_t value);
esp_err_t diag_json_writer_kv_u64(diag_json_writer_t *writer,
                                  const char *name,
                                  uint64_t value);
esp_err_t diag_json_writer_kv_null(diag_json_writer_t *writer,
                                   const char *name);

esp_err_t diag_service_start(const diag_service_config_t *config,
                             diag_service_handle_t *out_handle);
esp_err_t diag_service_stop(diag_service_handle_t handle);
bool diag_service_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_SERVICE_H */
