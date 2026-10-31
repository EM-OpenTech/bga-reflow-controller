// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdint.h>

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t ssid_len;
    uint8_t channel;
    wifi_auth_mode_t authmode;
    uint8_t ssid_hidden;
    uint8_t max_connection;
    uint16_t beacon_interval;
} wifi_ap_config_t;

typedef union {
    wifi_ap_config_t ap;
} wifi_config_t;

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
    WIFI_MODE_APSTA,
} wifi_mode_t;

typedef struct {
} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT()                                                                                     \
    wifi_init_config_t {}

inline esp_err_t esp_wifi_init(const void*) {
    return ESP_OK;
}
inline esp_err_t esp_wifi_set_mode(wifi_mode_t) {
    return ESP_OK;
}
inline esp_err_t esp_wifi_set_config(int, wifi_config_t*) {
    return ESP_OK;
}
inline esp_err_t esp_wifi_start() {
    return ESP_OK;
}
inline esp_err_t esp_wifi_stop() {
    return ESP_OK;
}
inline esp_err_t esp_wifi_set_ps(int) {
    return ESP_OK;
}
