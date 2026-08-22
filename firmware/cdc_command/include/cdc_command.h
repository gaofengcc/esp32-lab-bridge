/**
 * @file cdc_command.h
 * @brief 通用 CDC 命令注册与帧协议组件
 *
 * 帧格式保持为:
 *   [Magic 2B: 0xAB 0xCD][Cmd 1B][Length 2B LE][Payload N B][CRC8 1B]
 *
 * 这个组件只处理通用协议和命令注册，不绑定具体业务。
 */

#ifndef CDC_COMMAND_H
#define CDC_COMMAND_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================
 * 协议常量
 * ========================= */

#define CDC_CMD_MAGIC_0          0xAB
#define CDC_CMD_MAGIC_1          0xCD
#define CDC_CMD_MAX_PAYLOAD      4096
#define CDC_CMD_MAX_FRAME        (2 + 1 + 2 + CDC_CMD_MAX_PAYLOAD + 1)

/* =========================
 * 通用命令
 * ========================= */

typedef enum __attribute__((packed)) {
    CDC_CMD_SCREEN_REQ  = 0x02,
    CDC_CMD_REBOOT      = 0x08,
    CDC_CMD_LOG_PAUSE   = 0x0A,
    CDC_CMD_LOG_RESUME  = 0x0B,
} cdc_cmd_builtin_t;

typedef enum {
    CDC_CMD_STATUS_OK             = 0x00,
    CDC_CMD_STATUS_INVALID        = 0x01,
    CDC_CMD_STATUS_BUSY           = 0x02,
    CDC_CMD_STATUS_NOT_FOUND      = 0x03,
    CDC_CMD_STATUS_UNSUPPORTED    = 0x04,
    CDC_CMD_STATUS_INTERNAL       = 0x05,
} cdc_cmd_status_t;

typedef struct {
    uint8_t id;
    const char *name;
    const char *desc;
} cdc_cmd_entry_t;

typedef struct {
    uint8_t cmd;
    uint16_t payload_len;
    uint8_t payload[CDC_CMD_MAX_PAYLOAD];
} cdc_cmd_frame_t;

typedef struct {
    uint8_t state;
    uint8_t cmd;
    uint16_t payload_len;
    uint16_t received_len;
    uint8_t header[5];
    uint8_t payload[CDC_CMD_MAX_PAYLOAD];
} cdc_cmd_parser_t;

typedef void (*cdc_cmd_handler_t)(uint8_t cmd,
                                  const uint8_t *payload,
                                  uint16_t payload_len,
                                  void *user_ctx);

typedef esp_err_t (*cdc_cmd_write_fn_t)(const uint8_t *data,
                                        size_t len,
                                        void *user_ctx);

typedef esp_err_t (*cdc_cmd_dispatch_fn_t)(uint8_t cmd,
                                           const uint8_t *payload,
                                           uint16_t payload_len,
                                           void *user_ctx);

esp_err_t cdc_cmd_register(uint8_t id,
                           const char *name,
                           cdc_cmd_handler_t handler,
                           const char *desc);
esp_err_t cdc_cmd_unregister(uint8_t id);
void cdc_cmd_clear_registry(void);
const cdc_cmd_entry_t *cdc_cmd_find(uint8_t id);
esp_err_t cdc_cmd_dispatch(uint8_t cmd,
                           const uint8_t *payload,
                           uint16_t payload_len,
                           void *user_ctx);

uint8_t cdc_cmd_crc8(const uint8_t *data, size_t len);

esp_err_t cdc_cmd_build_frame(uint8_t *out_buf,
                              uint16_t *out_len,
                              uint8_t cmd,
                              const uint8_t *payload,
                              uint16_t payload_len);

esp_err_t cdc_cmd_send_frame(cdc_cmd_write_fn_t writer,
                             void *user_ctx,
                             uint8_t cmd,
                             const uint8_t *payload,
                             uint16_t payload_len);

esp_err_t cdc_cmd_send_response(cdc_cmd_write_fn_t writer,
                                void *user_ctx,
                                uint8_t cmd,
                                cdc_cmd_status_t status,
                                const uint8_t *payload,
                                uint16_t payload_len);

void cdc_cmd_parser_init(cdc_cmd_parser_t *parser);
int cdc_cmd_parser_feed(cdc_cmd_parser_t *parser,
                        uint8_t byte,
                        cdc_cmd_frame_t *out_frame);

#ifdef __cplusplus
}
#endif

#endif /* CDC_COMMAND_H */

