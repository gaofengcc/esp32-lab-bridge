/**
 * @file cdc_command.c
 * @brief 通用 CDC 帧协议、命令注册表和解析器
 */

#include "cdc_command.h"

#include <string.h>

#define CDC_CMD_REGISTRY_MAX   16

typedef struct {
    bool used;
    cdc_cmd_entry_t entry;
    cdc_cmd_handler_t handler;
} cdc_registry_item_t;

static cdc_registry_item_t s_registry[CDC_CMD_REGISTRY_MAX];

static cdc_registry_item_t *find_slot(uint8_t id)
{
    for (size_t i = 0; i < CDC_CMD_REGISTRY_MAX; i++) {
        if (s_registry[i].used && s_registry[i].entry.id == id) {
            return &s_registry[i];
        }
    }
    return NULL;
}

static cdc_registry_item_t *find_free_slot(void)
{
    for (size_t i = 0; i < CDC_CMD_REGISTRY_MAX; i++) {
        if (!s_registry[i].used) {
            return &s_registry[i];
        }
    }
    return NULL;
}

esp_err_t cdc_cmd_register(uint8_t id,
                           const char *name,
                           cdc_cmd_handler_t handler,
                           const char *desc)
{
    if (!name || !handler) {
        return ESP_ERR_INVALID_ARG;
    }

    cdc_registry_item_t *slot = find_slot(id);
    if (!slot) {
        slot = find_free_slot();
    }
    if (!slot) {
        return ESP_ERR_NO_MEM;
    }

    slot->used = true;
    slot->entry.id = id;
    slot->entry.name = name;
    slot->entry.desc = desc ? desc : "";
    slot->handler = handler;
    return ESP_OK;
}

esp_err_t cdc_cmd_unregister(uint8_t id)
{
    cdc_registry_item_t *slot = find_slot(id);
    if (!slot) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(slot, 0, sizeof(*slot));
    return ESP_OK;
}

void cdc_cmd_clear_registry(void)
{
    memset(s_registry, 0, sizeof(s_registry));
}

const cdc_cmd_entry_t *cdc_cmd_find(uint8_t id)
{
    cdc_registry_item_t *slot = find_slot(id);
    return slot ? &slot->entry : NULL;
}

esp_err_t cdc_cmd_dispatch(uint8_t cmd,
                           const uint8_t *payload,
                           uint16_t payload_len,
                           void *user_ctx)
{
    cdc_registry_item_t *slot = find_slot(cmd);
    if (!slot || !slot->handler) {
        return ESP_ERR_NOT_FOUND;
    }

    slot->handler(cmd, payload, payload_len, user_ctx);
    return ESP_OK;
}

uint8_t cdc_cmd_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    if (!data) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

esp_err_t cdc_cmd_build_frame(uint8_t *out_buf,
                              uint16_t *out_len,
                              uint8_t cmd,
                              const uint8_t *payload,
                              uint16_t payload_len)
{
    if (!out_buf || !out_len || payload_len > CDC_CMD_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t pos = 0;
    out_buf[pos++] = CDC_CMD_MAGIC_0;
    out_buf[pos++] = CDC_CMD_MAGIC_1;
    out_buf[pos++] = cmd;
    out_buf[pos++] = (uint8_t)(payload_len & 0xFF);
    out_buf[pos++] = (uint8_t)((payload_len >> 8) & 0xFF);

    if (payload && payload_len > 0) {
        memcpy(out_buf + pos, payload, payload_len);
        pos += payload_len;
    }

    out_buf[pos++] = cdc_cmd_crc8(out_buf, pos);
    *out_len = pos;
    return ESP_OK;
}

esp_err_t cdc_cmd_send_frame(cdc_cmd_write_fn_t writer,
                             void *user_ctx,
                             uint8_t cmd,
                             const uint8_t *payload,
                             uint16_t payload_len)
{
    uint8_t frame[CDC_CMD_MAX_FRAME];
    uint16_t frame_len = 0;

    if (!writer) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = cdc_cmd_build_frame(frame, &frame_len, cmd, payload, payload_len);
    if (err != ESP_OK) {
        return err;
    }

    return writer(frame, frame_len, user_ctx);
}

esp_err_t cdc_cmd_send_response(cdc_cmd_write_fn_t writer,
                                void *user_ctx,
                                uint8_t cmd,
                                cdc_cmd_status_t status,
                                const uint8_t *payload,
                                uint16_t payload_len)
{
    uint8_t frame[CDC_CMD_MAX_FRAME];
    uint16_t pos = 0;
    uint16_t total_len = (uint16_t)(payload_len + 1);

    if (!writer || payload_len > (CDC_CMD_MAX_PAYLOAD - 1)) {
        return ESP_ERR_INVALID_ARG;
    }

    frame[pos++] = CDC_CMD_MAGIC_0;
    frame[pos++] = CDC_CMD_MAGIC_1;
    frame[pos++] = cmd;
    frame[pos++] = (uint8_t)(total_len & 0xFF);
    frame[pos++] = (uint8_t)((total_len >> 8) & 0xFF);
    frame[pos++] = (uint8_t)status;

    if (payload && payload_len > 0) {
        memcpy(frame + pos, payload, payload_len);
        pos += payload_len;
    }

    frame[pos++] = cdc_cmd_crc8(frame, pos);
    return writer(frame, pos, user_ctx);
}

void cdc_cmd_parser_init(cdc_cmd_parser_t *parser)
{
    if (!parser) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser->state = 0;
}

int cdc_cmd_parser_feed(cdc_cmd_parser_t *parser,
                        uint8_t byte,
                        cdc_cmd_frame_t *out_frame)
{
    if (!parser) {
        return -1;
    }

    enum {
        S_MAGIC_0 = 0,
        S_MAGIC_1,
        S_CMD,
        S_LEN_0,
        S_LEN_1,
        S_PAYLOAD,
        S_CRC,
    };

    switch (parser->state) {
    case S_MAGIC_0:
        if (byte == CDC_CMD_MAGIC_0) {
            parser->header[0] = byte;
            parser->state = S_MAGIC_1;
        }
        break;

    case S_MAGIC_1:
        if (byte == CDC_CMD_MAGIC_1) {
            parser->header[1] = byte;
            parser->state = S_CMD;
        } else {
            parser->state = S_MAGIC_0;
            if (byte == CDC_CMD_MAGIC_0) {
                parser->header[0] = byte;
                parser->state = S_MAGIC_1;
            }
        }
        break;

    case S_CMD:
        parser->cmd = byte;
        parser->header[2] = byte;
        parser->payload_len = 0;
        parser->received_len = 0;
        parser->state = S_LEN_0;
        break;

    case S_LEN_0:
        parser->payload_len = byte;
        parser->header[3] = byte;
        parser->state = S_LEN_1;
        break;

    case S_LEN_1:
        parser->payload_len |= (uint16_t)((uint16_t)byte << 8);
        parser->header[4] = byte;
        parser->received_len = 0;
        if (parser->payload_len > CDC_CMD_MAX_PAYLOAD) {
            cdc_cmd_parser_init(parser);
            return -1;
        }
        parser->state = (parser->payload_len == 0) ? S_CRC : S_PAYLOAD;
        break;

    case S_PAYLOAD:
        parser->payload[parser->received_len++] = byte;
        if (parser->received_len >= parser->payload_len) {
            parser->state = S_CRC;
        }
        break;

    case S_CRC: {
        uint8_t crc_buf[5 + CDC_CMD_MAX_PAYLOAD];
        memcpy(crc_buf, parser->header, 5);
        if (parser->payload_len > 0) {
            memcpy(crc_buf + 5, parser->payload, parser->payload_len);
        }

        uint8_t expected = cdc_cmd_crc8(crc_buf, 5 + parser->payload_len);
        if (byte != expected) {
            cdc_cmd_parser_init(parser);
            return -1;
        }

        if (out_frame) {
            out_frame->cmd = parser->cmd;
            out_frame->payload_len = parser->payload_len;
            if (parser->payload_len > 0) {
                memcpy(out_frame->payload, parser->payload, parser->payload_len);
            }
        }

        cdc_cmd_parser_init(parser);
        return 1;
    }

    default:
        cdc_cmd_parser_init(parser);
        return -1;
    }

    return 0;
}

