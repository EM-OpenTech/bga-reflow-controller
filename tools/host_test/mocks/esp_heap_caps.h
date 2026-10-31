// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_DMA      (1 << 3)
#define MALLOC_CAP_INTERNAL (1 << 11)

inline size_t heap_caps_get_largest_free_block(uint32_t) {
    return 131072;
}

inline void* heap_caps_malloc(size_t size, uint32_t) {
    return malloc(size);
}
