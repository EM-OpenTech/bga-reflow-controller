// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include <stddef.h>

typedef struct {
    const char* base_path;
    const char* partition_label;
    int format_if_mount_failed;
    int dont_mount;
    int grow_on_mount;
} esp_vfs_littlefs_conf_t;

inline size_t g_mockLittleFsTotal = 1048576; // 1 MB
inline size_t g_mockLittleFsUsed = 262144;   // 256 KB

inline void mockLittleFsReset(size_t total = 1048576, size_t used = 262144) {
    g_mockLittleFsTotal = total;
    g_mockLittleFsUsed = used;
}

inline esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t*) {
    return ESP_OK;
}
inline esp_err_t esp_vfs_littlefs_unregister(const char*) {
    return ESP_OK;
}
inline esp_err_t esp_littlefs_info(const char*, size_t* total, size_t* used) {
    if (total)
        *total = g_mockLittleFsTotal;
    if (used)
        *used = g_mockLittleFsUsed;
    return ESP_OK;
}
inline esp_err_t esp_littlefs_format(const char*) {
    return ESP_OK;
}
