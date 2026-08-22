/**
 * @file log_gate.h
 * @brief EasyLogger 日志暂停/恢复门控组件
 */

#ifndef LOG_GATE_H
#define LOG_GATE_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t log_gate_init(void);
void log_gate_deinit(void);

esp_err_t log_gate_pause(void);
esp_err_t log_gate_resume(void);

bool log_gate_is_paused(void);
uint32_t log_gate_pause_depth(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_GATE_H */

