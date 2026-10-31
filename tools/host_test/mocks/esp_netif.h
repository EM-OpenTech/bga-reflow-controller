// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"

typedef void* esp_netif_t;

inline esp_netif_t* esp_netif_create_default_wifi_ap() {
    return nullptr;
}
inline void esp_netif_destroy_default_wifi_ap(esp_netif_t*) {}
