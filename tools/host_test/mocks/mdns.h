// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"

inline esp_err_t mdns_init() {
    return ESP_OK;
}
inline void mdns_free() {}
inline esp_err_t mdns_hostname_set(const char*) {
    return ESP_OK;
}
inline esp_err_t mdns_instance_name_set(const char*) {
    return ESP_OK;
}
inline esp_err_t mdns_service_add(const char*, const char*, const char*, uint16_t, void*, size_t) {
    return ESP_OK;
}
