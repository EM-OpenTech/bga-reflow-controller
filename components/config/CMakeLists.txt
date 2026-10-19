# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS "machine_config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio
)

target_enable_strict_warnings(${COMPONENT_LIB})
