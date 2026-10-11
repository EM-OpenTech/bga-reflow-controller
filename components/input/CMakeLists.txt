# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS "input_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio esp_timer config
    PRIV_REQUIRES log
)
