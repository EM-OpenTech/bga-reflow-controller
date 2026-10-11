# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS "storage_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES "config" "freertos" "joltwallet__littlefs"
    PRIV_REQUIRES "cjson" "nvs_flash" "log"
)
