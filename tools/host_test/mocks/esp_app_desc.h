/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t magic_word;
    uint32_t secure_version;
    uint32_t reserv1[2];
    char version[32];
    char project_name[32];
    char time[16];
    char date[16];
    char idf_ver[32];
    uint8_t app_elf_sha256[32];
    uint32_t reserv2[20];
} esp_app_desc_t;

inline const esp_app_desc_t* esp_app_get_description(void) {
    static const esp_app_desc_t desc = {
        0xABCD, 0, {0, 0}, "1.0.0-beta.1", "bga_reflow_controller", "12:00:00", "2026-10-27", "v6.0.2", {0}, {0}};
    return &desc;
}

#ifdef __cplusplus
}
#endif
