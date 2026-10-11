# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS "QuickPID.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer
)
