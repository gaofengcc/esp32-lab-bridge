/**
 * @file ota_update.h
 * @brief 可复用的 OTA 更新组件接口。
 *
 * 支持 manifest -> bin 下载、SHA256 校验、双分区写入、merged 镜像识别、
 * pending verify 和一次性回滚控制。直接上传接口作为兼容占位保留，
 * 默认返回 ESP_ERR_NOT_SUPPORTED。
 */

#ifndef WIRELESS_OTA_UPDATE_H
#define WIRELESS_OTA_UPDATE_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_UPDATE_PARTITION_LABEL_LEN     17
#define OTA_UPDATE_MANIFEST_URL_MAX_LEN    256
#define OTA_UPDATE_ERROR_NAME_MAX_LEN      32

typedef enum {
    OTA_UPDATE_STATE_IDLE = 0,
    OTA_UPDATE_STATE_CHECKING,
    OTA_UPDATE_STATE_DOWNLOADING,
    OTA_UPDATE_STATE_WRITING,
    OTA_UPDATE_STATE_VERIFYING,
    OTA_UPDATE_STATE_READY_TO_REBOOT,
    OTA_UPDATE_STATE_FAILED,
    OTA_UPDATE_STATE_UNSUPPORTED,
} ota_update_state_t;

typedef struct {
    char running_partition[OTA_UPDATE_PARTITION_LABEL_LEN];
    char boot_partition[OTA_UPDATE_PARTITION_LABEL_LEN];
    char update_partition[OTA_UPDATE_PARTITION_LABEL_LEN];
    char manifest_url[OTA_UPDATE_MANIFEST_URL_MAX_LEN];
    char last_error_name[OTA_UPDATE_ERROR_NAME_MAX_LEN];
    bool pending_verify;
    bool rollback_enabled;
    ota_update_state_t state;
    uint8_t progress;
    size_t downloaded_bytes;
    size_t expected_bytes;
    esp_err_t last_error;
} ota_update_status_t;

const char *ota_update_state_name(ota_update_state_t state);
esp_err_t ota_update_status_json(const ota_update_status_t *status,
                                 char *out_json, size_t out_len);

esp_err_t ota_update_boot_guard_init(void);
esp_err_t ota_update_mark_app_valid_after_selftest(void);
esp_err_t ota_update_mark_app_invalid_and_reboot(void);
/** 在 NVS 中安排下一次启动执行一次回滚自检。 */
esp_err_t ota_update_schedule_rollback_once(void);
esp_err_t ota_update_service_init(void);
esp_err_t ota_update_get_status(ota_update_status_t *out);

/** 从 manifest URL 下载并执行 OTA，可选 Bearer 鉴权。 */
esp_err_t ota_update_start_from_manifest(const char *manifest_url);
esp_err_t ota_update_start_from_manifest_auth(const char *manifest_url,
                                              const char *bearer_token);
/** 直接使用 manifest JSON 执行 OTA，可选 Bearer 鉴权。 */
esp_err_t ota_update_start_from_manifest_json(const char *manifest_json);
esp_err_t ota_update_start_from_manifest_json_auth(const char *manifest_json,
                                                   const char *bearer_token);
/** 直接上传接口暂未实现，保留用于上层兼容。 */
esp_err_t ota_update_begin_upload(size_t total_size, const char *sha256);
esp_err_t ota_update_write_chunk(uint32_t offset, const uint8_t *data, size_t len);
esp_err_t ota_update_finish_upload(void);
esp_err_t ota_update_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* WIRELESS_OTA_UPDATE_H */
