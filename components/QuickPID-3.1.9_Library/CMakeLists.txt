# SPDX-FileCopyrightText: 2023 David Lloyd <dlloydev@testcor.ca>
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: MIT AND AGPL-3.0-or-later

idf_component_register(
    SRCS "QuickPID.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer
)

target_enable_strict_warnings(${COMPONENT_LIB})

