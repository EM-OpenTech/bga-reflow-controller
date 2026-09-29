/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file web_server.cpp
 * @brief Implementation of HTTP Server and WebSocket Manager.
 *
 * Configures ESP-IDF HTTP server instance, registers REST/WebSocket/static route
 * callbacks, and manages telemetry broadcasts.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/web_server.hpp"
#include "web/static_file_server.hpp"
#include "web/rest_api.hpp"
#include "system_context.hpp"
#include "esp_log.h"

static const char* TAG = "WebServer";

static vprintf_like_t s_origVprintf = nullptr;

static int customLogHook(const char *fmt, va_list args)
{
    int ret = 0;
    if (s_origVprintf) {
        va_list args_copy;
        va_copy(args_copy, args);
        ret = s_origVprintf(fmt, args_copy);
        va_end(args_copy);
    } else {
        ret = vprintf(fmt, args);
    }

    char buf[192];
    vsnprintf(buf, sizeof(buf), fmt, args);
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    if (!line.empty()) {
        web::appendLogLine(line);
    }

    return ret;
}


namespace web {

WebServer::WebServer(storage::StorageManager& storage,
                     config::MachineSettings& settings,
                     fsm::ReflowFSM&         fsm,
                     WifiManager&            wifi,
                     app::SystemContext&     context)
    : _storage(storage)
    , _settings(settings)
    , _fsm(fsm)
    , _wifi(wifi)
    , _context(context)
{
}

WebServer::~WebServer()
{
    stop();
}

esp_err_t WebServer::begin()
{
    ESP_LOGI(TAG, "Starting HTTP Web Server on port 80...");

    RestApi::init(&_storage, &_settings, &_fsm, &_wifi, &_context);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 25;
    config.stack_size = 8192;
    config.max_open_sockets = 10;          // Up to 10 HTTP/WS sockets
    config.backlog_conn = 8;               // Buffer incoming connect requests
    config.lru_purge_enable = true;        // Automatically close oldest idle socket when full
    config.recv_wait_timeout = 3;          // 3s recv timeout for faster stale socket cleanup
    config.send_wait_timeout = 3;          // 3s send timeout
    config.enable_so_linger = true;        // Close sockets immediately without dangling TIME_WAIT
    config.linger_timeout = 0;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.close_fn = [](httpd_handle_t hd, int sockfd) {
        if (WebSocketHandler::getInstance() != nullptr) {
            WebSocketHandler::getInstance()->removeClient(sockfd);
        }
        close(sockfd);
    };

    if (s_origVprintf == nullptr) {
        s_origVprintf = esp_log_set_vprintf(customLogHook);
    }

    esp_err_t ret = httpd_start(&_serverHandle, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    _wsHandler = std::make_unique<WebSocketHandler>(_serverHandle);
    WebSocketHandler::setInstance(_wsHandler.get());

    registerRoutes();

    ESP_LOGI(TAG, "HTTP & WebSocket server started successfully.");
    return ESP_OK;
}

void WebServer::stop()
{
    if (_serverHandle != nullptr) {
        httpd_stop(_serverHandle);
        _serverHandle = nullptr;
        ESP_LOGI(TAG, "HTTP server stopped.");
    }
}

void WebServer::broadcastTelemetry(const TelemetryData& telemetry)
{
    if (_wsHandler != nullptr) {
        _wsHandler->broadcast(telemetry);
    }
}

void WebServer::registerRoutes()
{
    // ── 1. WebSocket Endpoint /ws ────────────────────────────────────────────
    httpd_uri_t ws_uri = {};
    ws_uri.uri          = "/ws";
    ws_uri.method       = HTTP_GET;
    ws_uri.handler      = WebSocketHandler::wsHandler;
    ws_uri.user_ctx     = nullptr;
    ws_uri.is_websocket = true;
    httpd_register_uri_handler(_serverHandle, &ws_uri);

    // ── 2. REST API: Status (lightweight sync for WS reconnect) ─────────────
    httpd_uri_t get_status = {};
    get_status.uri      = "/api/status";
    get_status.method   = HTTP_GET;
    get_status.handler  = RestApi::getStatusHandler;
    get_status.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &get_status);

    // ── 3. REST API: Settings ────────────────────────────────────────────────
    httpd_uri_t get_settings = {};
    get_settings.uri      = "/api/settings";
    get_settings.method   = HTTP_GET;
    get_settings.handler  = RestApi::getSettingsHandler;
    get_settings.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &get_settings);

    httpd_uri_t post_settings = {};
    post_settings.uri      = "/api/settings";
    post_settings.method   = HTTP_POST;
    post_settings.handler  = RestApi::postSettingsHandler;
    post_settings.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_settings);

    // ── 3. REST API: Profiles ────────────────────────────────────────────────
    httpd_uri_t get_profiles = {};
    get_profiles.uri      = "/api/profiles*";
    get_profiles.method   = HTTP_GET;
    get_profiles.handler  = RestApi::getProfilesHandler;
    get_profiles.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &get_profiles);

    httpd_uri_t get_profile_legacy = {};
    get_profile_legacy.uri      = "/api/profile";
    get_profile_legacy.method   = HTTP_GET;
    get_profile_legacy.handler  = RestApi::getProfileHandler;
    get_profile_legacy.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &get_profile_legacy);

    httpd_uri_t post_profiles = {};
    post_profiles.uri      = "/api/profiles";
    post_profiles.method   = HTTP_POST;
    post_profiles.handler  = RestApi::postProfileHandler;
    post_profiles.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_profiles);

    httpd_uri_t post_profile_legacy = {};
    post_profile_legacy.uri      = "/api/profile";
    post_profile_legacy.method   = HTTP_POST;
    post_profile_legacy.handler  = RestApi::postProfileHandler;
    post_profile_legacy.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_profile_legacy);

    httpd_uri_t delete_profiles = {};
    delete_profiles.uri      = "/api/profiles*";
    delete_profiles.method   = HTTP_DELETE;
    delete_profiles.handler  = RestApi::deleteProfileHandler;
    delete_profiles.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &delete_profiles);

    httpd_uri_t delete_profile_legacy = {};
    delete_profile_legacy.uri      = "/api/profile";
    delete_profile_legacy.method   = HTTP_DELETE;
    delete_profile_legacy.handler  = RestApi::deleteProfileHandler;
    delete_profile_legacy.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &delete_profile_legacy);

    // ── 4. REST API: Control & Overrides ─────────────────────────────────────
    httpd_uri_t post_control = {};
    post_control.uri      = "/api/control";
    post_control.method   = HTTP_POST;
    post_control.handler  = RestApi::postControlHandler;
    post_control.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_control);

    httpd_uri_t post_overrides = {};
    post_overrides.uri      = "/api/overrides";
    post_overrides.method   = HTTP_POST;
    post_overrides.handler  = RestApi::postOverridesHandler;
    post_overrides.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_overrides);

    // ── 5. REST API: Theme (thin wrapper around settings.theme) ───────────────
    httpd_uri_t get_theme = {};
    get_theme.uri      = "/api/theme";
    get_theme.method   = HTTP_GET;
    get_theme.handler  = RestApi::getThemeHandler;
    get_theme.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &get_theme);

    httpd_uri_t post_theme = {};
    post_theme.uri      = "/api/theme";
    post_theme.method   = HTTP_POST;
    post_theme.handler  = RestApi::postThemeHandler;
    post_theme.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_theme);

    // ── 6. REST API: PID Library (/littlefs/config/pid_library.json) ─────────
    httpd_uri_t get_pidlib = {};
    get_pidlib.uri      = "/api/pidlibrary";
    get_pidlib.method   = HTTP_GET;
    get_pidlib.handler  = RestApi::getPidLibraryHandler;
    get_pidlib.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &get_pidlib);

    httpd_uri_t post_pidlib = {};
    post_pidlib.uri      = "/api/pidlibrary";
    post_pidlib.method   = HTTP_POST;
    post_pidlib.handler  = RestApi::postPidLibraryHandler;
    post_pidlib.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_pidlib);

    // ── 7. REST API: History & Security ──────────────────────────────────────
    httpd_uri_t get_history = {};
    get_history.uri      = "/api/history";
    get_history.method   = HTTP_GET;
    get_history.handler  = RestApi::getHistoryHandler;
    get_history.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &get_history);

    httpd_uri_t get_sec_status = {};
    get_sec_status.uri      = "/api/security/status";
    get_sec_status.method   = HTTP_GET;
    get_sec_status.handler  = RestApi::getSecurityStatusHandler;
    get_sec_status.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &get_sec_status);

    httpd_uri_t post_sec_wifi = {};
    post_sec_wifi.uri      = "/api/security/wifi";
    post_sec_wifi.method   = HTTP_POST;
    post_sec_wifi.handler  = RestApi::postSecurityWifiHandler;
    post_sec_wifi.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_sec_wifi);

    // ── 8. REST API: Backup Tracking ─────────────────────────────────────────
    httpd_uri_t post_backup_complete = {};
    post_backup_complete.uri      = "/api/backup/complete";
    post_backup_complete.method   = HTTP_POST;
    post_backup_complete.handler  = RestApi::postBackupCompleteHandler;
    post_backup_complete.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_backup_complete);

    // ── 9. REST API: 1-Click Web OTA Firmware Update ─────────────────────────
    httpd_uri_t post_ota = {};
    post_ota.uri      = "/api/ota";
    post_ota.method   = HTTP_POST;
    post_ota.handler  = RestApi::postOtaUpdateHandler;
    post_ota.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &post_ota);

    // ── 10. Static File Server (Wildcard fallback for / and all files) ────────
    httpd_uri_t static_files = {};
    static_files.uri      = "/*";
    static_files.method   = HTTP_GET;
    static_files.handler  = StaticFileServer::fileGetHandler;
    static_files.user_ctx = nullptr;
    httpd_register_uri_handler(_serverHandle, &static_files);
}

} // namespace web
