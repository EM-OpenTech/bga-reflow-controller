// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include "esp_partition.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t esp_ota_handle_t;
#define OTA_WITH_SEQUENTIAL_WRITES 0

inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
    static const esp_partition_t part = {"ota_1", 0x10000, 0x200000};
    return &part;
}

inline esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* out_handle) {
    if (out_handle != nullptr) {
        *out_handle = 1;
    }
    return ESP_OK;
}

inline esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t) {
    return ESP_OK;
}

inline esp_err_t esp_ota_end(esp_ota_handle_t) {
    return ESP_OK;
}

inline esp_err_t esp_ota_abort(esp_ota_handle_t) {
    return ESP_OK;
}

inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) {
    return ESP_OK;
}
