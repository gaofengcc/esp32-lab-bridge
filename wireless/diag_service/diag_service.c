/**
 * @file diag_service.c
 * @brief 通用无线诊断服务模板
 */

#include "diag_service.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#define DIAG_JSON_MIN_CAPACITY        256U

typedef struct diag_service_s {
    httpd_handle_t server;
    uint16_t port;
    char *service_name;
    char *bearer_token;
    size_t token_len;
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
    diag_service_status_cb_t on_selftest;
} diag_service_t;

static const char *TAG = "diag_service";
static diag_service_t *s_service = NULL;

#ifndef CONFIG_DIAG_SERVICE_HTTPD_STACK_SIZE
#define CONFIG_DIAG_SERVICE_HTTPD_STACK_SIZE 12288
#endif

#ifndef CONFIG_DIAG_SERVICE_HTTPD_MAX_URI_HANDLERS
#define CONFIG_DIAG_SERVICE_HTTPD_MAX_URI_HANDLERS 12
#endif

static const char *diag_status_line(int code)
{
    switch (code) {
    case 200:
        return "200 OK";
    case 401:
        return "401 Unauthorized";
    case 500:
        return "500 Internal Server Error";
    case 501:
        return "501 Not Implemented";
    default:
        return "200 OK";
    }
}

static esp_err_t diag_send_json_response(httpd_req_t *req,
                                         int status_code,
                                         const char *json)
{
    httpd_resp_set_status(req, diag_status_line(status_code));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t diag_send_json_error(httpd_req_t *req,
                                      int status_code,
                                      const char *code,
                                      const char *message)
{
    diag_json_writer_t writer = {0};
    esp_err_t err = diag_json_writer_init(&writer, DIAG_JSON_MIN_CAPACITY);
    if (err != ESP_OK) {
        return err;
    }

    err = diag_json_writer_begin_object(&writer);
    if (err == ESP_OK) {
        err = diag_json_writer_kv_bool(&writer, "ok", false);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_string(&writer, "code", code ? code : "error");
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_string(&writer, "message", message ? message : "");
    }
    if (err == ESP_OK) {
        err = diag_json_writer_end_object(&writer);
    }
    if (err == ESP_OK) {
        err = diag_send_json_response(req, status_code, diag_json_writer_cstr(&writer));
    }
    diag_json_writer_deinit(&writer);
    return err;
}

static bool diag_query_has_forbidden_token(const httpd_req_t *req)
{
    int qlen = httpd_req_get_url_query_len(req);
    if (qlen <= 0) {
        return false;
    }

    char *query = (char *)calloc((size_t)qlen + 1U, 1U);
    if (!query) {
        return true;
    }

    bool forbidden = false;
    if (httpd_req_get_url_query_str((httpd_req_t *)req, query, (size_t)qlen + 1U) == ESP_OK) {
        if (strstr(query, "token=") != NULL ||
            strstr(query, "auth=") != NULL ||
            strstr(query, "access_token=") != NULL) {
            forbidden = true;
        }
    }
    free(query);
    return forbidden;
}

static bool diag_secure_memeq(const uint8_t *a, size_t a_len,
                              const uint8_t *b, size_t b_len)
{
    uint8_t diff = (uint8_t)(a_len ^ b_len);
    size_t max_len = (a_len > b_len) ? a_len : b_len;

    for (size_t i = 0; i < max_len; i++) {
        uint8_t av = (i < a_len && a) ? a[i] : 0U;
        uint8_t bv = (i < b_len && b) ? b[i] : 0U;
        diff |= (uint8_t)(av ^ bv);
    }
    return diff == 0U;
}

static bool diag_token_is_weak(const char *token, size_t len, size_t min_len)
{
    if (!token || len < min_len || min_len == 0U) {
        return true;
    }

    bool has_non_space = false;
    for (size_t i = 0; i < len; i++) {
        if (token[i] != ' ' && token[i] != '\t' && token[i] != '\r' && token[i] != '\n') {
            has_non_space = true;
            break;
        }
    }
    if (!has_non_space) {
        return true;
    }

    static const char *const denylist[] = {
        "default", "changeme", "replace-me", "your-token", "token",
        "diag-token", "password", "secret", "admin", "test",
        "123456", "12345678",
    };
    for (size_t i = 0; i < sizeof(denylist) / sizeof(denylist[0]); i++) {
        if (strcmp(token, denylist[i]) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t diag_check_bearer_auth(httpd_req_t *req)
{
#ifdef CONFIG_DIAG_SERVICE_TEST_AUTH_BYPASS
    /* 仅供本地实验室临时调试，生产构建不得开启。 */
    (void)req;
    return ESP_OK;
#else
    if (!s_service || !s_service->bearer_token) {
        return ESP_ERR_INVALID_STATE;
    }

    if (diag_query_has_forbidden_token(req)) {
        return ESP_ERR_INVALID_ARG;
    }

    char auth_header[192] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization",
                                    auth_header, sizeof(auth_header)) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    static const char bearer_prefix[] = "Bearer ";
    size_t prefix_len = sizeof(bearer_prefix) - 1U;
    if (strncmp(auth_header, bearer_prefix, prefix_len) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *token = auth_header + prefix_len;
    size_t token_len = strlen(token);
    if (token_len == 0U || diag_token_is_weak(token, token_len, s_service->min_token_len)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (token_len != s_service->token_len) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!diag_secure_memeq((const uint8_t *)token, token_len,
                           (const uint8_t *)s_service->bearer_token,
                           s_service->token_len)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
#endif
}

static esp_err_t diag_require_auth(httpd_req_t *req)
{
    esp_err_t err = diag_check_bearer_auth(req);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer realm=\"diag_service\"");
    return diag_send_json_error(req, 401, "unauthorized",
                                "需要有效的 Bearer Token");
}

static esp_err_t diag_send_ok_object(httpd_req_t *req,
                                     int status_code,
                                     const char *service_name,
                                     uint64_t uptime_ms)
{
    diag_json_writer_t writer = {0};
    esp_err_t err = diag_json_writer_init(&writer, DIAG_JSON_MIN_CAPACITY);
    if (err != ESP_OK) {
        return err;
    }

    err = diag_json_writer_begin_object(&writer);
    if (err == ESP_OK) {
        err = diag_json_writer_kv_bool(&writer, "ok", true);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_string(&writer, "service", service_name);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(&writer, "uptime_ms", uptime_ms);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_end_object(&writer);
    }
    if (err == ESP_OK) {
        err = diag_send_json_response(req, status_code, diag_json_writer_cstr(&writer));
    }
    diag_json_writer_deinit(&writer);
    return err;
}

static esp_err_t diag_health_handler(httpd_req_t *req)
{
    return diag_send_ok_object(req, 200,
                               s_service->service_name,
                               (uint64_t)(esp_timer_get_time() / 1000LL));
}

static esp_err_t diag_status_handler(httpd_req_t *req)
{
    esp_err_t err = diag_require_auth(req);
    if (err != ESP_OK) {
        return err;
    }

    diag_json_writer_t writer = {0};
    err = diag_json_writer_init(&writer, DIAG_JSON_MIN_CAPACITY);
    if (err != ESP_OK) {
        return err;
    }

    err = diag_json_writer_begin_object(&writer);
    if (err == ESP_OK) {
        err = diag_json_writer_kv_bool(&writer, "ok", true);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_string(&writer, "service", s_service->service_name);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(&writer, "uptime_ms",
                                     (uint64_t)(esp_timer_get_time() / 1000LL));
    }
    if (err == ESP_OK && s_service->on_status) {
        err = s_service->on_status(&writer, s_service->user_ctx);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_end_object(&writer);
    }
    if (err == ESP_OK) {
        err = diag_send_json_response(req, 200, diag_json_writer_cstr(&writer));
    }
    diag_json_writer_deinit(&writer);
    return err;
}

static esp_err_t diag_logs_handler(httpd_req_t *req)
{
    esp_err_t err = diag_require_auth(req);
    if (err != ESP_OK) {
        return err;
    }

    if (s_service->on_logs) {
        return s_service->on_logs(req, s_service->user_ctx);
    }
    return diag_send_json_error(req, 501,
                                "not_implemented",
                                "日志路由尚未接入业务实现");
}

static esp_err_t diag_screenshot_handler(httpd_req_t *req)
{
    esp_err_t err = diag_require_auth(req);
    if (err != ESP_OK) {
        return err;
    }

    if (!s_service->on_screenshot) {
        return diag_send_json_error(req, 501,
                                    "not_implemented",
                                    "截图路由尚未接入业务实现");
    }

    uint8_t *bmp_buf = NULL;
    size_t bmp_len = 0;
    err = s_service->on_screenshot(&bmp_buf, &bmp_len, s_service->user_ctx);
    if (err != ESP_OK || !bmp_buf || bmp_len == 0U) {
        if (bmp_buf && s_service->on_screenshot_free) {
            s_service->on_screenshot_free(bmp_buf, s_service->user_ctx);
        } else if (bmp_buf) {
            free(bmp_buf);
        }
        return diag_send_json_error(req, 500,
                                    "capture_failed",
                                    "截图捕获失败");
    }

    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = httpd_resp_send(req, (const char *)bmp_buf, bmp_len);
    if (s_service->on_screenshot_free) {
        s_service->on_screenshot_free(bmp_buf, s_service->user_ctx);
    } else {
        free(bmp_buf);
    }
    return err;
}

static esp_err_t diag_selftest_handler(httpd_req_t *req)
{
    esp_err_t err = diag_require_auth(req);
    if (err != ESP_OK) {
        return err;
    }

    if (!s_service->on_selftest) {
        return diag_send_json_error(req, 501,
                                    "not_implemented",
                                    "自检路由尚未接入业务实现");
    }

    diag_json_writer_t writer = {0};
    err = diag_json_writer_init(&writer, DIAG_JSON_MIN_CAPACITY);
    if (err != ESP_OK) {
        return err;
    }

    err = diag_json_writer_begin_object(&writer);
    if (err == ESP_OK) {
        err = diag_json_writer_kv_bool(&writer, "ok", true);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_string(&writer, "service", s_service->service_name);
    }
    if (err == ESP_OK) {
        err = s_service->on_selftest(&writer, s_service->user_ctx);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_end_object(&writer);
    }
    if (err == ESP_OK) {
        err = diag_send_json_response(req, 200, diag_json_writer_cstr(&writer));
    }
    diag_json_writer_deinit(&writer);
    return err;
}

static esp_err_t diag_ota_status_handler(httpd_req_t *req)
{
    esp_err_t err = diag_require_auth(req);
    if (err != ESP_OK) {
        return err;
    }

    diag_json_writer_t writer = {0};
    err = diag_json_writer_init(&writer, DIAG_JSON_MIN_CAPACITY);
    if (err != ESP_OK) {
        return err;
    }

    err = diag_json_writer_begin_object(&writer);
    if (err == ESP_OK) {
        err = diag_json_writer_kv_bool(&writer, "ok", true);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_string(&writer, "service", s_service->service_name);
    }
    if (err == ESP_OK && s_service->on_ota_status) {
        err = s_service->on_ota_status(&writer, s_service->user_ctx);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_end_object(&writer);
    }
    if (err == ESP_OK) {
        err = diag_send_json_response(req, 200, diag_json_writer_cstr(&writer));
    }
    diag_json_writer_deinit(&writer);
    return err;
}

static esp_err_t diag_ota_check_handler(httpd_req_t *req)
{
    esp_err_t err = diag_require_auth(req);
    if (err != ESP_OK) {
        return err;
    }

    if (s_service->on_ota_check) {
        return s_service->on_ota_check(req, s_service->user_ctx);
    }
    return diag_send_json_error(req, 501,
                                "not_implemented",
                                "OTA 检查路由尚未接入业务实现");
}

static esp_err_t diag_ota_start_handler(httpd_req_t *req)
{
    esp_err_t err = diag_require_auth(req);
    if (err != ESP_OK) {
        return err;
    }

    if (s_service->on_ota_start) {
        return s_service->on_ota_start(req, s_service->user_ctx);
    }
    return diag_send_json_error(req, 501,
                                "not_implemented",
                                "OTA 启动路由尚未接入业务实现");
}

static esp_err_t diag_reboot_handler(httpd_req_t *req)
{
    esp_err_t err = diag_require_auth(req);
    if (err != ESP_OK) {
        return err;
    }

    if (s_service->on_reboot) {
        return s_service->on_reboot(req, s_service->user_ctx);
    }
    return diag_send_json_error(req, 501,
                                "not_implemented",
                                "重启路由尚未接入业务实现");
}

static esp_err_t diag_register_routes(httpd_handle_t server)
{
    static const httpd_uri_t routes[] = {
        { .uri = "/api/health", .method = HTTP_GET, .handler = diag_health_handler },
        { .uri = "/api/status", .method = HTTP_GET, .handler = diag_status_handler },
        { .uri = "/api/logs", .method = HTTP_GET, .handler = diag_logs_handler },
        { .uri = "/api/screenshot.bmp", .method = HTTP_GET, .handler = diag_screenshot_handler },
        { .uri = "/api/selftest", .method = HTTP_POST, .handler = diag_selftest_handler },
        { .uri = "/api/ota/status", .method = HTTP_GET, .handler = diag_ota_status_handler },
        { .uri = "/api/ota/check", .method = HTTP_POST, .handler = diag_ota_check_handler },
        { .uri = "/api/ota/start", .method = HTTP_POST, .handler = diag_ota_start_handler },
        { .uri = "/api/reboot", .method = HTTP_POST, .handler = diag_reboot_handler },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static char *diag_strdup_or_default(const char *src, const char *fallback)
{
    const char *use = src ? src : fallback;
    size_t len = strlen(use);
    char *copy = (char *)calloc(len + 1U, 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, use, len);
    return copy;
}

esp_err_t diag_json_writer_init(diag_json_writer_t *writer, size_t initial_cap)
{
    if (!writer) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(writer, 0, sizeof(*writer));
    if (initial_cap < DIAG_JSON_MIN_CAPACITY) {
        initial_cap = DIAG_JSON_MIN_CAPACITY;
    }

    writer->buf = (char *)calloc(initial_cap + 1U, 1U);
    if (!writer->buf) {
        return ESP_ERR_NO_MEM;
    }
    writer->cap = initial_cap;
    return ESP_OK;
}

void diag_json_writer_deinit(diag_json_writer_t *writer)
{
    if (!writer) {
        return;
    }
    free(writer->buf);
    memset(writer, 0, sizeof(*writer));
}

const char *diag_json_writer_cstr(const diag_json_writer_t *writer)
{
    return (writer && writer->buf) ? writer->buf : "";
}

size_t diag_json_writer_len(const diag_json_writer_t *writer)
{
    return writer ? writer->len : 0U;
}

static esp_err_t diag_json_writer_reserve(diag_json_writer_t *writer, size_t extra)
{
    if (!writer || !writer->buf) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t need = writer->len + extra + 1U;
    if (need <= writer->cap + 1U) {
        return ESP_OK;
    }

    size_t new_cap = writer->cap;
    while (new_cap + 1U < need) {
        new_cap = (new_cap == 0U) ? DIAG_JSON_MIN_CAPACITY : (new_cap * 2U);
    }

    char *next = (char *)realloc(writer->buf, new_cap + 1U);
    if (!next) {
        return ESP_ERR_NO_MEM;
    }
    writer->buf = next;
    writer->cap = new_cap;
    return ESP_OK;
}

static esp_err_t diag_json_writer_append_mem(diag_json_writer_t *writer,
                                             const char *data,
                                             size_t len)
{
    esp_err_t err = diag_json_writer_reserve(writer, len);
    if (err != ESP_OK) {
        return err;
    }

    memcpy(writer->buf + writer->len, data, len);
    writer->len += len;
    writer->buf[writer->len] = '\0';
    return ESP_OK;
}

static esp_err_t diag_json_writer_append_char(diag_json_writer_t *writer, char c)
{
    return diag_json_writer_append_mem(writer, &c, 1U);
}

static esp_err_t diag_json_writer_escape_string(diag_json_writer_t *writer,
                                                const char *value)
{
    const unsigned char *src = (const unsigned char *)(value ? value : "");
    esp_err_t err = diag_json_writer_append_char(writer, '"');
    if (err != ESP_OK) {
        return err;
    }

    for (; *src != '\0'; src++) {
        char esc[7];
        switch (*src) {
        case '\"':
            err = diag_json_writer_append_mem(writer, "\\\"", 2U);
            break;
        case '\\':
            err = diag_json_writer_append_mem(writer, "\\\\", 2U);
            break;
        case '\b':
            err = diag_json_writer_append_mem(writer, "\\b", 2U);
            break;
        case '\f':
            err = diag_json_writer_append_mem(writer, "\\f", 2U);
            break;
        case '\n':
            err = diag_json_writer_append_mem(writer, "\\n", 2U);
            break;
        case '\r':
            err = diag_json_writer_append_mem(writer, "\\r", 2U);
            break;
        case '\t':
            err = diag_json_writer_append_mem(writer, "\\t", 2U);
            break;
        default:
            if (*src < 0x20U) {
                (void)snprintf(esc, sizeof(esc), "\\u%04x", (unsigned int)*src);
                err = diag_json_writer_append_mem(writer, esc, 6U);
            } else {
                err = diag_json_writer_append_char(writer, (char)*src);
            }
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
    }

    return diag_json_writer_append_char(writer, '"');
}

static esp_err_t diag_json_writer_before_value(diag_json_writer_t *writer)
{
    if (!writer) {
        return ESP_ERR_INVALID_ARG;
    }

    if (writer->depth == 0U) {
        return ESP_OK;
    }

    uint8_t idx = (uint8_t)(writer->depth - 1U);
    if (writer->is_object[idx]) {
        if (!writer->pending_name) {
            return ESP_ERR_INVALID_STATE;
        }
        writer->pending_name = false;
        writer->need_comma[idx] = true;
        return ESP_OK;
    }

    if (writer->need_comma[idx]) {
        return diag_json_writer_append_char(writer, ',');
    }
    writer->need_comma[idx] = true;
    return ESP_OK;
}

static esp_err_t diag_json_writer_push_frame(diag_json_writer_t *writer, bool is_object)
{
    if (writer->depth >= DIAG_SERVICE_MAX_JSON_DEPTH) {
        return ESP_ERR_INVALID_STATE;
    }

    writer->is_object[writer->depth] = is_object;
    writer->need_comma[writer->depth] = false;
    writer->depth++;
    return ESP_OK;
}

static esp_err_t diag_json_writer_pop_frame(diag_json_writer_t *writer, bool expect_object)
{
    if (!writer || writer->depth == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t idx = (uint8_t)(writer->depth - 1U);
    if (writer->is_object[idx] != expect_object) {
        return ESP_ERR_INVALID_STATE;
    }
    if (writer->is_object[idx] && writer->pending_name) {
        return ESP_ERR_INVALID_STATE;
    }

    writer->depth--;
    if (writer->depth > 0U) {
        writer->need_comma[writer->depth - 1U] = true;
    }
    return ESP_OK;
}

esp_err_t diag_json_writer_begin_object(diag_json_writer_t *writer)
{
    esp_err_t err = diag_json_writer_before_value(writer);
    if (err != ESP_OK) {
        return err;
    }
    err = diag_json_writer_append_char(writer, '{');
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_push_frame(writer, true);
}

esp_err_t diag_json_writer_end_object(diag_json_writer_t *writer)
{
    esp_err_t err = diag_json_writer_pop_frame(writer, true);
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_append_char(writer, '}');
}

esp_err_t diag_json_writer_begin_array(diag_json_writer_t *writer)
{
    esp_err_t err = diag_json_writer_before_value(writer);
    if (err != ESP_OK) {
        return err;
    }
    err = diag_json_writer_append_char(writer, '[');
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_push_frame(writer, false);
}

esp_err_t diag_json_writer_end_array(diag_json_writer_t *writer)
{
    esp_err_t err = diag_json_writer_pop_frame(writer, false);
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_append_char(writer, ']');
}

esp_err_t diag_json_writer_name(diag_json_writer_t *writer, const char *name)
{
    if (!writer || !name || writer->depth == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t idx = (uint8_t)(writer->depth - 1U);
    if (!writer->is_object[idx] || writer->pending_name) {
        return ESP_ERR_INVALID_STATE;
    }

    if (writer->need_comma[idx]) {
        esp_err_t err = diag_json_writer_append_char(writer, ',');
        if (err != ESP_OK) {
            return err;
        }
    }
    esp_err_t err = diag_json_writer_escape_string(writer, name);
    if (err != ESP_OK) {
        return err;
    }
    err = diag_json_writer_append_char(writer, ':');
    if (err != ESP_OK) {
        return err;
    }
    writer->pending_name = true;
    return ESP_OK;
}

esp_err_t diag_json_writer_raw(diag_json_writer_t *writer, const char *json)
{
    esp_err_t err = diag_json_writer_before_value(writer);
    if (err != ESP_OK) {
        return err;
    }
    err = diag_json_writer_append_mem(writer, json ? json : "null", strlen(json ? json : "null"));
    if (err != ESP_OK) {
        return err;
    }
    if (writer->depth > 0U && !writer->is_object[writer->depth - 1U]) {
        writer->need_comma[writer->depth - 1U] = true;
    }
    return ESP_OK;
}

esp_err_t diag_json_writer_string(diag_json_writer_t *writer, const char *value)
{
    esp_err_t err = diag_json_writer_before_value(writer);
    if (err != ESP_OK) {
        return err;
    }
    err = diag_json_writer_escape_string(writer, value);
    if (err != ESP_OK) {
        return err;
    }
    if (writer->depth > 0U && !writer->is_object[writer->depth - 1U]) {
        writer->need_comma[writer->depth - 1U] = true;
    }
    return ESP_OK;
}

esp_err_t diag_json_writer_bool(diag_json_writer_t *writer, bool value)
{
    return diag_json_writer_raw(writer, value ? "true" : "false");
}

esp_err_t diag_json_writer_i64(diag_json_writer_t *writer, int64_t value)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%" PRId64, value);
    if (n < 0) {
        return ESP_FAIL;
    }
    return diag_json_writer_raw(writer, tmp);
}

esp_err_t diag_json_writer_u64(diag_json_writer_t *writer, uint64_t value)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%" PRIu64, value);
    if (n < 0) {
        return ESP_FAIL;
    }
    return diag_json_writer_raw(writer, tmp);
}

esp_err_t diag_json_writer_null(diag_json_writer_t *writer)
{
    return diag_json_writer_raw(writer, "null");
}

esp_err_t diag_json_writer_kv_string(diag_json_writer_t *writer,
                                     const char *name,
                                     const char *value)
{
    esp_err_t err = diag_json_writer_name(writer, name);
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_string(writer, value);
}

esp_err_t diag_json_writer_kv_raw(diag_json_writer_t *writer,
                                  const char *name,
                                  const char *json)
{
    esp_err_t err = diag_json_writer_name(writer, name);
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_raw(writer, json);
}

esp_err_t diag_json_writer_kv_bool(diag_json_writer_t *writer,
                                   const char *name,
                                   bool value)
{
    esp_err_t err = diag_json_writer_name(writer, name);
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_bool(writer, value);
}

esp_err_t diag_json_writer_kv_i64(diag_json_writer_t *writer,
                                  const char *name,
                                  int64_t value)
{
    esp_err_t err = diag_json_writer_name(writer, name);
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_i64(writer, value);
}

esp_err_t diag_json_writer_kv_u64(diag_json_writer_t *writer,
                                  const char *name,
                                  uint64_t value)
{
    esp_err_t err = diag_json_writer_name(writer, name);
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_u64(writer, value);
}

esp_err_t diag_json_writer_kv_null(diag_json_writer_t *writer,
                                   const char *name)
{
    esp_err_t err = diag_json_writer_name(writer, name);
    if (err != ESP_OK) {
        return err;
    }
    return diag_json_writer_null(writer);
}

esp_err_t diag_service_start(const diag_service_config_t *config,
                             diag_service_handle_t *out_handle)
{
    if (!config || !config->bearer_token) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_service) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t token_len = strlen(config->bearer_token);
    size_t min_token_len = config->min_token_len > 0U
        ? config->min_token_len
        : DIAG_SERVICE_DEFAULT_MIN_TOKEN;
    if (diag_token_is_weak(config->bearer_token, token_len, min_token_len)) {
        return ESP_ERR_INVALID_ARG;
    }

    diag_service_t *svc = (diag_service_t *)calloc(1, sizeof(*svc));
    if (!svc) {
        return ESP_ERR_NO_MEM;
    }

    svc->port = config->port ? config->port : DIAG_SERVICE_DEFAULT_PORT;
    svc->service_name = diag_strdup_or_default(config->service_name, "diag_service");
    svc->bearer_token = diag_strdup_or_default(config->bearer_token, "");
    if (!svc->service_name || !svc->bearer_token) {
        diag_service_stop(svc);
        return ESP_ERR_NO_MEM;
    }
    svc->token_len = token_len;
    svc->min_token_len = min_token_len;
    svc->user_ctx = config->user_ctx;
    svc->on_status = config->on_status;
    svc->on_logs = config->on_logs;
    svc->on_screenshot = config->on_screenshot;
    svc->on_screenshot_free = config->on_screenshot_free;
    svc->on_selftest = config->on_selftest;
    svc->on_ota_status = config->on_ota_status;
    svc->on_ota_check = config->on_ota_check;
    svc->on_ota_start = config->on_ota_start;
    svc->on_reboot = config->on_reboot;

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.server_port = svc->port;
    http_cfg.ctrl_port = (uint16_t)(svc->port + 1U);
    http_cfg.max_open_sockets = 4;
    http_cfg.max_uri_handlers = CONFIG_DIAG_SERVICE_HTTPD_MAX_URI_HANDLERS;
    http_cfg.lru_purge_enable = true;
    http_cfg.stack_size = CONFIG_DIAG_SERVICE_HTTPD_STACK_SIZE;

    esp_err_t err = httpd_start(&svc->server, &http_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务器启动失败: %s", esp_err_to_name(err));
        diag_service_stop(svc);
        return err;
    }

    s_service = svc;
    err = diag_register_routes(svc->server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "路由注册失败: %s", esp_err_to_name(err));
        diag_service_stop(svc);
        return err;
    }

    if (out_handle) {
        *out_handle = (diag_service_handle_t)svc;
    }

    ESP_LOGI(TAG, "无线诊断服务已启动: %s:%u",
             svc->service_name, (unsigned)svc->port);
    return ESP_OK;
}

esp_err_t diag_service_stop(diag_service_handle_t handle)
{
    diag_service_t *svc = (diag_service_t *)handle;
    if (!svc) {
        return ESP_ERR_INVALID_ARG;
    }

    if (svc->server) {
        httpd_stop(svc->server);
        svc->server = NULL;
    }

    if (s_service == svc) {
        s_service = NULL;
    }

    free(svc->service_name);
    free(svc->bearer_token);
    free(svc);
    return ESP_OK;
}

bool diag_service_is_running(void)
{
    return s_service && s_service->server;
}
