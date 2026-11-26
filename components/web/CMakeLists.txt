# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS 
        "src/api_context.cpp"
        "src/control_routes.cpp"
        "src/profile_routes.cpp"
        "src/ota_routes.cpp"
        "src/system_routes.cpp"
        "src/wifi_manager.cpp"
        "src/usb_network.cpp"
        "src/web_server.cpp"
        "src/rest_api.cpp"
        "src/static_file_server.cpp"
        "src/ws_handler.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES 
        config
        storage
        fsm
        esp_wifi
        esp_netif
        esp_http_server
        mdns
        nvs_flash
        freertos
        log
        cjson
        lwip
        esp_app_format
        app_update
        bootloader_support
    EMBED_FILES
        "build_assets/index.html.gz"
        "build_assets/style.css.gz"
        "build_assets/app.js.gz"
        "build_assets/chart.min.js.gz"
        "build_assets/de.js.gz"
)

target_include_directories(${COMPONENT_LIB} PRIVATE "../../main/include")

target_enable_strict_warnings(${COMPONENT_LIB})
