// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <stdint.h>

typedef struct {
    char label[17];
    uint32_t address;
    uint32_t size;
} esp_partition_t;
