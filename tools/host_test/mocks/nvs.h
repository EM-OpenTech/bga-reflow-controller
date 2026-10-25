// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY,
    NVS_READWRITE
} nvs_open_mode_t;

inline esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t*) { return ESP_OK; }
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_get_str(nvs_handle_t, const char*, char*, size_t*) { return ESP_ERR_NOT_FOUND; }
inline esp_err_t nvs_set_str(nvs_handle_t, const char*, const char*) { return ESP_OK; }
inline esp_err_t nvs_get_u8(nvs_handle_t, const char*, uint8_t*) { return ESP_ERR_NOT_FOUND; }
inline esp_err_t nvs_set_u8(nvs_handle_t, const char*, uint8_t) { return ESP_OK; }
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
