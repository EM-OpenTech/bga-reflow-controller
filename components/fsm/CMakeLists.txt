# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS "reflow_fsm.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config output pid
    PRIV_REQUIRES esp_timer log
)
