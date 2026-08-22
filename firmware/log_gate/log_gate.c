/**
 * @file log_gate.c
 * @brief EasyLogger 暂停/恢复与异步队列清空封装
 */

#include "log_gate.h"

#include "elog.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_pause_depth = 0U;
static bool s_output_enabled_before_pause = true;
static bool s_paused = false;

static inline void gate_lock(void)
{
    taskENTER_CRITICAL(&s_lock);
}

static inline void gate_unlock(void)
{
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t log_gate_init(void)
{
    gate_lock();
    s_pause_depth = 0U;
    s_output_enabled_before_pause = true;
    s_paused = false;
    gate_unlock();
    return ESP_OK;
}

void log_gate_deinit(void)
{
    gate_lock();
    s_pause_depth = 0U;
    s_paused = false;
    gate_unlock();
}

esp_err_t log_gate_pause(void)
{
    gate_lock();
    if (s_pause_depth == 0U) {
        s_output_enabled_before_pause = elog_get_output_enabled();
        elog_set_output_paused(true);
        if (s_output_enabled_before_pause) {
            elog_set_output_enabled(false);
        }
#ifdef ELOG_ASYNC_OUTPUT_ENABLE
        elog_async_clear();
#endif
        s_paused = true;
    }
    s_pause_depth++;
    gate_unlock();
    return ESP_OK;
}

esp_err_t log_gate_resume(void)
{
    gate_lock();
    if (s_pause_depth == 0U) {
        gate_unlock();
        return ESP_OK;
    }

    s_pause_depth--;
    if (s_pause_depth == 0U) {
        if (s_output_enabled_before_pause) {
            elog_set_output_enabled(true);
        }
        elog_set_output_paused(false);
        s_paused = false;
    }
    gate_unlock();
    return ESP_OK;
}

bool log_gate_is_paused(void)
{
    bool paused = false;
    gate_lock();
    paused = s_paused;
    gate_unlock();
    return paused;
}

uint32_t log_gate_pause_depth(void)
{
    uint32_t depth = 0U;
    gate_lock();
    depth = s_pause_depth;
    gate_unlock();
    return depth;
}

