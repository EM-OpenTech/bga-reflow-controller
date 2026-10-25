// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef const char* esp_event_base_t;
typedef void* esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

#define ESP_EVENT_ANY_BASE nullptr
#define ESP_EVENT_ANY_ID -1

inline esp_err_t esp_event_handler_instance_register(esp_event_base_t, int32_t, esp_event_handler_t, void*, esp_event_handler_instance_t*) {
    return ESP_OK;
}

inline esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t, esp_event_handler_instance_t) {
    return ESP_OK;
}
