// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON,
    ESP_RST_EXT,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_BROWNOUT,
    ESP_RST_SDIO,
    ESP_RST_USB,
    ESP_RST_JTAG,
} esp_reset_reason_t;

typedef void (*shutdown_handler_t)(void);

inline esp_err_t esp_register_shutdown_handler(shutdown_handler_t) {
    return ESP_OK;
}

inline esp_err_t esp_unregister_shutdown_handler(shutdown_handler_t) {
    return ESP_OK;
}

inline esp_reset_reason_t esp_reset_reason() {
    return ESP_RST_POWERON;
}

inline uint32_t esp_get_free_heap_size() {
    return 250000;
}

inline uint32_t esp_get_minimum_free_heap_size() {
    return 200000;
}

inline void esp_restart() {}
