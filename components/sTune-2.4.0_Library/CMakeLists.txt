# SPDX-FileCopyrightText: 2022 Dlloydev <dlloydev@testcor.ca>
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: MIT AND AGPL-3.0-or-later

idf_component_register(
    SRCS "sTan.cpp" "sTune.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer esp_driver_gpio log
)

target_enable_strict_warnings(${COMPONENT_LIB})

