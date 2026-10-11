# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS "max31856.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos driver esp_driver_spi esp_driver_gpio esp_timer
    PRIV_REQUIRES log
)
