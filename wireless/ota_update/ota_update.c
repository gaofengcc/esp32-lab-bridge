/**
 * @file ota_update.c
 * @brief 可复用 OTA 更新组件基础实现
 */

#include "ota_update.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/md.h"
#include "nvs.h"

#ifndef CONFIG_OTA_UPDATE_MANIFEST_MAX_LEN
#define CONFIG_OTA_UPDATE_MANIFEST_MAX_LEN 4096
#endif

#ifndef CONFIG_OTA_UPDATE_HTTP_TIMEOUT_MS
#define CONFIG_OTA_UPDATE_HTTP_TIMEOUT_MS 15000
#endif

#ifndef CONFIG_OTA_UPDATE_HTTP_READ_BUF_SIZE
#define CONFIG_OTA_UPDATE_HTTP_READ_BUF_SIZE 4096
#endif

#define OTA_UPDATE_TARGET_SHA256_LEN 32
#define OTA_UPDATE_MERGED_APP_OFFSET 0x10000U
#define OTA_UPDATE_NVS_NAMESPACE     "ota_ctrl"
#define OTA_UPDATE_NVS_ROLLBACK_KEY  "rollback_once"

static const char *TAG = "ota_update";

static ota_update_status_t s_status = {
    .state = OTA_UPDATE_STATE_IDLE,
    .last_error = ESP_OK,
};
static SemaphoreHandle_t s_lock = NULL;

static void lock_status(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock_status(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static esp_err_t ensure_lock(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

/* 通过 NVS 保存一次性回滚请求，避免把项目业务状态耦合进组件。 */
static esp_err_t schedule_rollback_once(bool enable)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(OTA_UPDATE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    if (enable) {
        err = nvs_set_u8(handle, OTA_UPDATE_NVS_ROLLBACK_KEY, 1);
    } else {
        err = nvs_erase_key(handle, OTA_UPDATE_NVS_ROLLBACK_KEY);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static bool consume_rollback_once_flag(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(OTA_UPDATE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t flag = 0;
    err = nvs_get_u8(handle, OTA_UPDATE_NVS_ROLLBACK_KEY, &flag);
    bool enabled = (err == ESP_OK && flag != 0);
    if (enabled) {
        (void)nvs_erase_key(handle, OTA_UPDATE_NVS_ROLLBACK_KEY);
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
    return enabled;
}

static void copy_string(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static void copy_partition_label(char *dst, size_t dst_len, const esp_partition_t *part)
{
    copy_string(dst, dst_len, part ? part->label : NULL);
}

static void set_last_error_locked(esp_err_t err)
{
    s_status.last_error = err;
    esp_err_to_name_r(err, s_status.last_error_name, sizeof(s_status.last_error_name));
}

const char *ota_update_state_name(ota_update_state_t state)
{
    switch (state) {
    case OTA_UPDATE_STATE_IDLE:
        return "idle";
    case OTA_UPDATE_STATE_CHECKING:
        return "checking";
    case OTA_UPDATE_STATE_DOWNLOADING:
        return "downloading";
    case OTA_UPDATE_STATE_WRITING:
        return "writing";
    case OTA_UPDATE_STATE_VERIFYING:
        return "verifying";
    case OTA_UPDATE_STATE_READY_TO_REBOOT:
        return "ready_to_reboot";
    case OTA_UPDATE_STATE_FAILED:
        return "failed";
    case OTA_UPDATE_STATE_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown";
    }
}

static bool status_is_busy(ota_update_state_t state)
{
    return state == OTA_UPDATE_STATE_CHECKING ||
           state == OTA_UPDATE_STATE_DOWNLOADING ||
           state == OTA_UPDATE_STATE_WRITING ||
           state == OTA_UPDATE_STATE_VERIFYING ||
           state == OTA_UPDATE_STATE_READY_TO_REBOOT;
}

static bool is_pending_verify(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        return false;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return false;
    }
    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

static esp_err_t refresh_status_snapshot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = running ? esp_ota_get_next_update_partition(NULL) : NULL;

    lock_status();
    copy_partition_label(s_status.running_partition, sizeof(s_status.running_partition), running);
    copy_partition_label(s_status.boot_partition, sizeof(s_status.boot_partition), boot);
    copy_partition_label(s_status.update_partition, sizeof(s_status.update_partition), next);
    s_status.pending_verify = is_pending_verify();
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    s_status.rollback_enabled = true;
#else
    s_status.rollback_enabled = false;
#endif
    set_last_error_locked(s_status.last_error);
    unlock_status();

    return running ? ESP_OK : ESP_FAIL;
}

static esp_err_t set_idle_snapshot(void)
{
    esp_err_t err = refresh_status_snapshot();
    lock_status();
    if (err == ESP_OK) {
        s_status.state = s_status.pending_verify ? OTA_UPDATE_STATE_VERIFYING
                                                 : OTA_UPDATE_STATE_IDLE;
    } else {
        s_status.state = OTA_UPDATE_STATE_FAILED;
        set_last_error_locked(err);
    }
    unlock_status();
    return err;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_sha256_hex(const char *hex, uint8_t out[OTA_UPDATE_TARGET_SHA256_LEN])
{
    if (!hex || strlen(hex) != 64 || !out) {
        return false;
    }

    for (int i = 0; i < 32; i++) {
        int hi = hex_value(hex[i * 2]);
        int lo = hex_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static const char *json_get_string_any(cJSON *root, const char *const *keys)
{
    if (!root || !keys) {
        return NULL;
    }
    for (size_t i = 0; keys[i]; i++) {
        cJSON *item = cJSON_GetObjectItem(root, keys[i]);
        if (cJSON_IsString(item) && item->valuestring) {
            return item->valuestring;
        }
    }
    return NULL;
}

static esp_err_t json_get_size_any(cJSON *root, const char *const *keys, size_t *out)
{
    if (!root || !keys || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; keys[i]; i++) {
        cJSON *item = cJSON_GetObjectItem(root, keys[i]);
        if (cJSON_IsNumber(item) && item->valuedouble > 0) {
            *out = (size_t)item->valuedouble;
            return ESP_OK;
        }
        if (cJSON_IsString(item) && item->valuestring && item->valuestring[0] != '\0') {
            errno = 0;
            char *end = NULL;
            unsigned long long value = strtoull(item->valuestring, &end, 10);
            if (errno == 0 && end && *end == '\0' && value > 0) {
                *out = (size_t)value;
                return ESP_OK;
            }
        }
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_manifest(const char *json,
                                char *bin_url, size_t bin_url_len,
                                uint8_t sha256[OTA_UPDATE_TARGET_SHA256_LEN],
                                size_t *expected_size)
{
    if (!json || !bin_url || !sha256 || !expected_size) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const char *url_keys[] = { "url", "bin_url", "image_url", "download_url", NULL };
    const char *sha_keys[] = { "sha256", "sha256_hex", "digest_sha256", NULL };
    const char *size_keys[] = { "size", "length", "image_size", "content_length", NULL };

    const char *url = json_get_string_any(root, url_keys);
    const char *sha = json_get_string_any(root, sha_keys);
    esp_err_t err = json_get_size_any(root, size_keys, expected_size);
    if (!url || !sha || err != ESP_OK) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (strlen(url) >= bin_url_len || !parse_sha256_hex(sha, sha256)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    copy_string(bin_url, bin_url_len, url);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t set_bearer_token(esp_http_client_handle_t client, const char *token)
{
    if (!client || !token || token[0] == '\0') {
        return ESP_OK;
    }

    char header[96] = {0};
    int len = snprintf(header, sizeof(header), "Bearer %s", token);
    if (len <= 0 || len >= (int)sizeof(header)) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_http_client_set_header(client, "Authorization", header);
}

static esp_err_t http_read_all(const char *url, const char *bearer_token,
                               char *out, size_t out_len, size_t *read_len)
{
    if (!url || !out || out_len == 0 || !read_len) {
        return ESP_ERR_INVALID_ARG;
    }
    *read_len = 0;

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = CONFIG_OTA_UPDATE_HTTP_TIMEOUT_MS,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = set_bearer_token(client, bearer_token);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int64_t content_len = esp_http_client_fetch_headers(client);
    if (content_len < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_HTTP_FETCH_HEADER;
    }

    int status = 0;
    int total = 0;
    while (true) {
        if ((size_t)total + 1 >= out_len) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        int ret = esp_http_client_read(client, out + total, out_len - total - 1);
        if (ret < 0) {
            err = ESP_FAIL;
            break;
        }
        if (ret == 0) {
            status = esp_http_client_get_status_code(client);
            break;
        }
        total += ret;
    }
    out[total] = '\0';
    *read_len = (size_t)total;

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        return err;
    }
    if (status < 200 || status >= 300) {
        return ESP_FAIL;
    }
    (void)content_len;
    return ESP_OK;
}

static bool manifest_is_merged_flash_image(const char *url)
{
    return url && strstr(url, "_merged.bin") != NULL;
}

static bool normalize_manifest_url(const char *input, char *out, size_t out_len)
{
    if (!input || !out || out_len == 0) {
        return false;
    }

    const char *basename = strrchr(input, '/');
    const char *filename = basename ? basename + 1 : input;
    if (strcmp(filename, "manifest.json") == 0 ||
        !manifest_is_merged_flash_image(filename)) {
        copy_string(out, out_len, input);
        return true;
    }

    size_t prefix_len = basename ? (size_t)(basename - input + 1) : 0;
    const char *manifest_name = "manifest.json";
    if (prefix_len + strlen(manifest_name) + 1 > out_len) {
        return false;
    }

    if (prefix_len > 0) {
        memcpy(out, input, prefix_len);
    }
    copy_string(out + prefix_len, out_len - prefix_len, manifest_name);
    return true;
}

static esp_err_t minimal_boot_selftest(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!running || !next) {
        return ESP_FAIL;
    }

    if (consume_rollback_once_flag()) {
        ESP_LOGW(TAG, "一次性回滚标记已消费，故意让启动自检失败");
        return ESP_FAIL;
    }

    if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < 64 * 1024) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_update_status_json(const ota_update_status_t *status,
                                 char *out_json, size_t out_len)
{
    if (!status || !out_json || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char err_name[OTA_UPDATE_ERROR_NAME_MAX_LEN] = {0};
    esp_err_to_name_r(status->last_error, err_name, sizeof(err_name));

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "state", ota_update_state_name(status->state));
    cJSON_AddNumberToObject(root, "progress", status->progress);
    cJSON_AddBoolToObject(root, "pending_verify", status->pending_verify);
    cJSON_AddBoolToObject(root, "rollback_enabled", status->rollback_enabled);
    cJSON_AddStringToObject(root, "running_partition", status->running_partition);
    cJSON_AddStringToObject(root, "boot_partition", status->boot_partition);
    cJSON_AddStringToObject(root, "update_partition", status->update_partition);
    cJSON_AddStringToObject(root, "manifest_url", status->manifest_url);
    cJSON_AddNumberToObject(root, "downloaded_bytes", (double)status->downloaded_bytes);
    cJSON_AddNumberToObject(root, "expected_bytes", (double)status->expected_bytes);
    cJSON_AddNumberToObject(root, "last_error", (double)status->last_error);
    cJSON_AddStringToObject(root, "last_error_name", err_name);

    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ESP_OK;
    if (strlen(printed) >= out_len) {
        err = ESP_ERR_INVALID_SIZE;
    } else {
        copy_string(out_json, out_len, printed);
    }
    free(printed);
    return err;
}

esp_err_t ota_update_boot_guard_init(void)
{
    esp_err_t err = refresh_status_snapshot();
    lock_status();
    if (err != ESP_OK) {
        s_status.state = OTA_UPDATE_STATE_FAILED;
        set_last_error_locked(err);
        unlock_status();
        return err;
    }

    if (!s_status.pending_verify) {
        s_status.state = OTA_UPDATE_STATE_IDLE;
        set_last_error_locked(ESP_OK);
        unlock_status();
        return ESP_OK;
    }

    s_status.state = OTA_UPDATE_STATE_VERIFYING;
    set_last_error_locked(ESP_OK);
    unlock_status();

    err = minimal_boot_selftest();
    lock_status();
    set_last_error_locked(err);
    s_status.state = (err == ESP_OK) ? OTA_UPDATE_STATE_VERIFYING
                                     : OTA_UPDATE_STATE_FAILED;
    unlock_status();
    return err;
}

esp_err_t ota_update_mark_app_valid_after_selftest(void)
{
    esp_err_t err = refresh_status_snapshot();
    lock_status();
    if (err != ESP_OK) {
        s_status.state = OTA_UPDATE_STATE_FAILED;
        set_last_error_locked(err);
        unlock_status();
        return err;
    }

    if (!s_status.pending_verify) {
        set_last_error_locked(ESP_OK);
        unlock_status();
        return ESP_OK;
    }
    s_status.state = OTA_UPDATE_STATE_VERIFYING;
    unlock_status();

    err = esp_ota_mark_app_valid_cancel_rollback();
    lock_status();
    if (err == ESP_OK) {
        s_status.pending_verify = false;
        s_status.state = OTA_UPDATE_STATE_IDLE;
    } else {
        s_status.state = OTA_UPDATE_STATE_FAILED;
    }
    set_last_error_locked(err);
    unlock_status();
    return err;
}

esp_err_t ota_update_mark_app_invalid_and_reboot(void)
{
    (void)ensure_lock();
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    lock_status();
    s_status.state = OTA_UPDATE_STATE_FAILED;
    set_last_error_locked(err);
    unlock_status();
    return err;
}

esp_err_t ota_update_schedule_rollback_once(void)
{
    return schedule_rollback_once(true);
}

esp_err_t ota_update_service_init(void)
{
    esp_err_t err = ensure_lock();
    if (err != ESP_OK) {
        return err;
    }

    lock_status();
    s_status.progress = 0;
    s_status.downloaded_bytes = 0;
    s_status.expected_bytes = 0;
    s_status.manifest_url[0] = '\0';
    unlock_status();

    return set_idle_snapshot();
}

esp_err_t ota_update_get_status(ota_update_status_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = refresh_status_snapshot();
    lock_status();
    *out = s_status;
    unlock_status();
    return err;
}

static esp_err_t start_operation(const char *manifest_url)
{
    esp_err_t err = ensure_lock();
    if (err != ESP_OK) {
        return err;
    }

    lock_status();
    if (status_is_busy(s_status.state)) {
        unlock_status();
        return ESP_ERR_INVALID_STATE;
    }

    s_status.state = OTA_UPDATE_STATE_CHECKING;
    s_status.progress = 0;
    s_status.downloaded_bytes = 0;
    s_status.expected_bytes = 0;
    s_status.last_error = ESP_OK;
    s_status.last_error_name[0] = '\0';
    copy_string(s_status.manifest_url, sizeof(s_status.manifest_url), manifest_url);
    unlock_status();
    return ESP_OK;
}

static void finish_operation_failure(esp_err_t err)
{
    lock_status();
    s_status.state = OTA_UPDATE_STATE_FAILED;
    set_last_error_locked(err);
    unlock_status();
}

static esp_err_t run_manifest_json_ota(const char *manifest_json,
                                       const char *manifest_url,
                                       const char *bearer_token)
{
    char bin_url[OTA_UPDATE_MANIFEST_URL_MAX_LEN] = {0};
    uint8_t expected_sha[OTA_UPDATE_TARGET_SHA256_LEN] = {0};
    size_t expected_size = 0;
    esp_err_t err = parse_manifest(manifest_json, bin_url, sizeof(bin_url),
                                   expected_sha, &expected_size);
    if (err != ESP_OK) {
        finish_operation_failure(err);
        return err;
    }

    /* merged 包前 0x10000 是 flash 映射前缀，不能写进 app 分区。 */
    bool merged_flash_image = manifest_is_merged_flash_image(bin_url);
    size_t ota_payload_offset = merged_flash_image ? OTA_UPDATE_MERGED_APP_OFFSET : 0;
    size_t ota_image_size = expected_size;
    if (merged_flash_image) {
        if (expected_size <= OTA_UPDATE_MERGED_APP_OFFSET) {
            finish_operation_failure(ESP_ERR_INVALID_SIZE);
            return ESP_ERR_INVALID_SIZE;
        }
        ota_image_size = expected_size - OTA_UPDATE_MERGED_APP_OFFSET;
        ESP_LOGI(TAG, "检测到 merged bin，跳过前置 0x%05x 字节写入 OTA",
                 (unsigned)OTA_UPDATE_MERGED_APP_OFFSET);
    }

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update || ota_image_size == 0 || ota_image_size > update->size) {
        finish_operation_failure(ESP_ERR_INVALID_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t cfg = {
        .url = bin_url,
        .timeout_ms = CONFIG_OTA_UPDATE_HTTP_TIMEOUT_MS,
        .buffer_size = CONFIG_OTA_UPDATE_HTTP_READ_BUF_SIZE,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        finish_operation_failure(ESP_FAIL);
        return ESP_FAIL;
    }

    err = set_bearer_token(client, bearer_token);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        finish_operation_failure(err);
        return err;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        finish_operation_failure(err);
        return err;
    }

    int64_t content_len = esp_http_client_fetch_headers(client);
    if (content_len < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        finish_operation_failure(ESP_ERR_HTTP_FETCH_HEADER);
        return ESP_ERR_HTTP_FETCH_HEADER;
    }
    if (content_len > 0 && (size_t)content_len != expected_size) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        finish_operation_failure(ESP_ERR_INVALID_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }
    int image_status = esp_http_client_get_status_code(client);
    if (image_status < 200 || image_status >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        finish_operation_failure(ESP_FAIL);
        return ESP_FAIL;
    }

    esp_ota_handle_t ota = 0;
    err = esp_ota_begin(update, ota_image_size, &ota);
    if (err != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        finish_operation_failure(err);
        return err;
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(CONFIG_OTA_UPDATE_HTTP_READ_BUF_SIZE,
                                               MALLOC_CAP_8BIT);
    if (!buf) {
        (void)esp_ota_abort(ota);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        finish_operation_failure(ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    mbedtls_md_context_t sha_ctx;
    mbedtls_md_init(&sha_ctx);
    const mbedtls_md_info_t *sha_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!sha_info) {
        heap_caps_free(buf);
        mbedtls_md_free(&sha_ctx);
        (void)esp_ota_abort(ota);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        finish_operation_failure(ESP_FAIL);
        return ESP_FAIL;
    }

    int md_ret = mbedtls_md_setup(&sha_ctx, sha_info, 0);
    if (md_ret == 0) {
        md_ret = mbedtls_md_starts(&sha_ctx);
    }
    if (md_ret != 0) {
        mbedtls_md_free(&sha_ctx);
        heap_caps_free(buf);
        (void)esp_ota_abort(ota);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        finish_operation_failure(ESP_FAIL);
        return ESP_FAIL;
    }

    size_t downloaded = 0;
    size_t payload_written = 0;
    uint8_t last_reported_progress = 0;
    lock_status();
    s_status.state = OTA_UPDATE_STATE_DOWNLOADING;
    s_status.expected_bytes = expected_size;
    unlock_status();

    while (downloaded < expected_size) {
        int ret = esp_http_client_read(client, (char *)buf,
                                       CONFIG_OTA_UPDATE_HTTP_READ_BUF_SIZE);
        if (ret < 0) {
            err = ESP_FAIL;
            break;
        }
        if (ret == 0) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }

        size_t chunk = (size_t)ret;
        if (downloaded + chunk > expected_size) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }

        md_ret = mbedtls_md_update(&sha_ctx, buf, chunk);
        if (md_ret != 0) {
            err = ESP_FAIL;
            break;
        }

        size_t chunk_start = downloaded;
        size_t chunk_end = downloaded + chunk;
        if (chunk_end > ota_payload_offset) {
            size_t write_offset = chunk_start < ota_payload_offset
                                  ? ota_payload_offset - chunk_start
                                  : 0;
            size_t write_len = chunk - write_offset;
            if (write_len > 0) {
                lock_status();
                s_status.state = OTA_UPDATE_STATE_WRITING;
                unlock_status();
                err = esp_ota_write(ota, buf + write_offset, write_len);
                if (err != ESP_OK) {
                    break;
                }
                payload_written += write_len;
            }
        }

        downloaded += chunk;
        lock_status();
        s_status.downloaded_bytes = downloaded;
        s_status.progress = (uint8_t)((downloaded * 100U) / expected_size);
        if (s_status.progress >= (uint8_t)(last_reported_progress + 10U) ||
            s_status.progress == 100U) {
            last_reported_progress = s_status.progress;
        }
        s_status.state = OTA_UPDATE_STATE_DOWNLOADING;
        unlock_status();
    }

    uint8_t actual_sha[OTA_UPDATE_TARGET_SHA256_LEN] = {0};
    if (err == ESP_OK) {
        md_ret = mbedtls_md_finish(&sha_ctx, actual_sha);
        if (md_ret != 0) {
            err = ESP_FAIL;
        }
    }
    mbedtls_md_free(&sha_ctx);
    heap_caps_free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && downloaded != expected_size) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK && payload_written != ota_image_size) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK &&
        memcmp(actual_sha, expected_sha, sizeof(actual_sha)) != 0) {
        err = ESP_ERR_INVALID_CRC;
    }
    if (err != ESP_OK) {
        (void)esp_ota_abort(ota);
        finish_operation_failure(err);
        return err;
    }

    lock_status();
    s_status.state = OTA_UPDATE_STATE_VERIFYING;
    unlock_status();
    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        finish_operation_failure(err);
        return err;
    }
    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        finish_operation_failure(err);
        return err;
    }

    (void)refresh_status_snapshot();
    lock_status();
    copy_string(s_status.manifest_url, sizeof(s_status.manifest_url), manifest_url);
    s_status.downloaded_bytes = downloaded;
    s_status.expected_bytes = expected_size;
    s_status.progress = 100;
    s_status.state = OTA_UPDATE_STATE_READY_TO_REBOOT;
    set_last_error_locked(ESP_OK);
    unlock_status();
    return ESP_OK;
}

esp_err_t ota_update_start_from_manifest(const char *manifest_url)
{
    return ota_update_start_from_manifest_auth(manifest_url, NULL);
}

esp_err_t ota_update_start_from_manifest_json(const char *manifest_json)
{
    return ota_update_start_from_manifest_json_auth(manifest_json, NULL);
}

esp_err_t ota_update_start_from_manifest_json_auth(const char *manifest_json,
                                                   const char *bearer_token)
{
    if (!manifest_json || manifest_json[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = start_operation(NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = run_manifest_json_ota(manifest_json, NULL, bearer_token);
    return err;
}

esp_err_t ota_update_start_from_manifest_auth(const char *manifest_url,
                                              const char *bearer_token)
{
    if (!manifest_url || manifest_url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = start_operation(manifest_url);
    if (err != ESP_OK) {
        return err;
    }

    char manifest_fetch_url[OTA_UPDATE_MANIFEST_URL_MAX_LEN] = {0};
    const char *fetch_url = manifest_url;
    if (normalize_manifest_url(manifest_url, manifest_fetch_url,
                               sizeof(manifest_fetch_url)) &&
        strcmp(manifest_fetch_url, manifest_url) != 0) {
        fetch_url = manifest_fetch_url;
    }

    char *manifest = (char *)heap_caps_malloc(CONFIG_OTA_UPDATE_MANIFEST_MAX_LEN,
                                              MALLOC_CAP_8BIT);
    if (!manifest) {
        finish_operation_failure(ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    size_t manifest_len = 0;
    err = http_read_all(fetch_url, bearer_token, manifest,
                        CONFIG_OTA_UPDATE_MANIFEST_MAX_LEN, &manifest_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "manifest 下载完成: %u bytes", (unsigned)manifest_len);
        err = run_manifest_json_ota(manifest, manifest_url, bearer_token);
    } else {
        finish_operation_failure(err);
    }
    heap_caps_free(manifest);
    return err;
}

esp_err_t ota_update_begin_upload(size_t total_size, const char *sha256)
{
    (void)total_size;
    (void)sha256;
    lock_status();
    s_status.state = OTA_UPDATE_STATE_UNSUPPORTED;
    set_last_error_locked(ESP_ERR_NOT_SUPPORTED);
    unlock_status();
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_update_write_chunk(uint32_t offset, const uint8_t *data, size_t len)
{
    (void)offset;
    (void)data;
    (void)len;
    lock_status();
    s_status.state = OTA_UPDATE_STATE_UNSUPPORTED;
    set_last_error_locked(ESP_ERR_NOT_SUPPORTED);
    unlock_status();
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_update_finish_upload(void)
{
    lock_status();
    s_status.state = OTA_UPDATE_STATE_UNSUPPORTED;
    set_last_error_locked(ESP_ERR_NOT_SUPPORTED);
    unlock_status();
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_update_abort(void)
{
    lock_status();
    s_status.state = OTA_UPDATE_STATE_IDLE;
    s_status.progress = 0;
    s_status.downloaded_bytes = 0;
    s_status.expected_bytes = 0;
    s_status.manifest_url[0] = '\0';
    set_last_error_locked(ESP_OK);
    unlock_status();
    return ESP_OK;
}
