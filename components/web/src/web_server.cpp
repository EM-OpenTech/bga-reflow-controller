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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_context.hpp"
#include "web/rest_api.hpp"
#include "web/static_file_server.hpp"
#include <atomic>

namespace {

constexpr const char* TAG = "WebServer";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
vprintf_like_t s_origVprintf = nullptr;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> s_inHook{false};

std::string stripAnsiEscapeCodes(const char* buf, int len) {
    // Strip ANSI escape codes (e.g. \033[0;31m, \033[0m) so web logs stay clean & readable
    std::string clean;
    if (len <= 0 || buf == nullptr) {
        return clean;
    }
    clean.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) {
        if (buf[i] == '\033' && i + 1 < len && buf[i + 1] == '[') {
            i += 2;
            while (i < len && buf[i] != 'm') {
                ++i;
            }
            continue; // Skip the terminating 'm'
        }
        clean.push_back(buf[i]);
    }
    return clean;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
int customLogHook(const char* fmt, va_list args) {
    // 1. Output to default UART console first
    va_list args_console;
    va_copy(args_console, args);
    int ret = (s_origVprintf != nullptr) ? s_origVprintf(fmt, args_console) : vprintf(fmt, args_console);
    va_end(args_console);

    // 2. If in ISR or already inside hook, skip Web buffer to protect FreeRTOS interrupt state
    if (xPortInIsrContext() != 0) {
        return ret;
    }

    bool expected = false;
    if (!s_inHook.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
        return ret;
    }

    // 3. Format into buffer for Web UI log streaming using a fresh va_list copy
    std::array<char, 384> buf{};
    va_list args_buf;
    va_copy(args_buf, args);
    int len = vsnprintf(buf.data(), buf.size(), fmt, args_buf);
    va_end(args_buf);

    if (len > 0) {
        while (len > 0 &&
               (buf.at(static_cast<size_t>(len - 1)) == '\n' || buf.at(static_cast<size_t>(len - 1)) == '\r')) {
            buf.at(static_cast<size_t>(--len)) = '\0';
        }
        if (len > 0) {
            std::string clean = stripAnsiEscapeCodes(buf.data(), len);
            if (!clean.empty()) {
                web::appendLogLine(clean);
            }
        }
    }

    s_inHook.store(false, std::memory_order_release);
    return ret;
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // anonymous namespace

namespace web {

WebServer::WebServer(storage::StorageManager& storage, config::MachineSettings& settings, fsm::ReflowFSM& fsm,
                     WifiManager& wifi, app::SystemContext& context)
    : _storage(storage), _settings(settings), _fsm(fsm), _wifi(wifi), _context(context) {}

WebServer::~WebServer() {
    stop();
}

esp_err_t WebServer::begin() {
    ESP_LOGI(TAG, "Starting HTTP Web Server on port 80...");

    RestApi::init(&_storage, &_settings, &_fsm, &_wifi, &_context);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 25;
    config.stack_size = 8192;
    config.max_open_sockets = MAX_HTTP_SOCKETS; // Up to 10 HTTP/WS sockets
    config.backlog_conn = 8;                    // Buffer incoming connect requests
    config.lru_purge_enable = true; // Auto-purge stale/idle HTTP sockets to accept new connections immediately
    config.recv_wait_timeout = 2;   // 2s recv timeout for faster stale socket cleanup
    config.send_wait_timeout = 2;   // 2s send timeout
    config.enable_so_linger = true; // Close sockets immediately without dangling TIME_WAIT
    config.linger_timeout = 0;
    config.uri_match_fn = httpd_uri_match_wildcard;

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

void WebServer::stop() {
    if (_serverHandle != nullptr) {
        httpd_stop(_serverHandle);
        _serverHandle = nullptr;
        ESP_LOGI(TAG, "HTTP server stopped.");
    }
}

void WebServer::broadcastTelemetry(const TelemetryData& telemetry) {
    if (_wsHandler != nullptr) {
        _wsHandler->broadcast(telemetry);
    }
}

void WebServer::registerRoutes() {
    auto registerRoute = [this](const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*),
                                bool isWs = false) {
        httpd_uri_t config = {};
        config.uri = uri;
        config.method = method;
        config.handler = handler;
        config.user_ctx = nullptr;
        config.is_websocket = isWs;
        httpd_register_uri_handler(_serverHandle, &config);
    };

    // 1. WebSocket Endpoint /ws
    registerRoute("/ws", HTTP_GET, WebSocketHandler::wsHandler, true);

    // 2. REST API: Status (lightweight sync for WS reconnect)
    registerRoute("/api/status", HTTP_GET, RestApi::getStatusHandler);

    // 3. REST API: Settings
    registerRoute("/api/settings", HTTP_GET, RestApi::getSettingsHandler);
    registerRoute("/api/settings", HTTP_POST, RestApi::postSettingsHandler);

    // 4. REST API: Profiles
    registerRoute("/api/profiles*", HTTP_GET, RestApi::getProfilesHandler);
    registerRoute("/api/profile", HTTP_GET, RestApi::getProfileHandler);
    registerRoute("/api/profiles", HTTP_POST, RestApi::postProfileHandler);
    registerRoute("/api/profile", HTTP_POST, RestApi::postProfileHandler);
    registerRoute("/api/profiles*", HTTP_DELETE, RestApi::deleteProfileHandler);
    registerRoute("/api/profile", HTTP_DELETE, RestApi::deleteProfileHandler);

    // 5. REST API: Control & Overrides
    registerRoute("/api/control", HTTP_POST, RestApi::postControlHandler);
    registerRoute("/api/overrides", HTTP_POST, RestApi::postOverridesHandler);

    // 6. REST API: Theme (thin wrapper around settings.theme)
    registerRoute("/api/theme", HTTP_GET, RestApi::getThemeHandler);
    registerRoute("/api/theme", HTTP_POST, RestApi::postThemeHandler);

    // 7. REST API: PID Library (/littlefs/config/pid_library.json)
    registerRoute("/api/pidlibrary", HTTP_GET, RestApi::getPidLibraryHandler);
    registerRoute("/api/pidlibrary", HTTP_POST, RestApi::postPidLibraryHandler);

    // 8. REST API: History & Security
    registerRoute("/api/history", HTTP_GET, RestApi::getHistoryHandler);
    registerRoute("/api/security/status", HTTP_GET, RestApi::getSecurityStatusHandler);
    registerRoute("/api/security/wifi", HTTP_POST, RestApi::postSecurityWifiHandler);

    // 9. REST API: Backup Tracking
    registerRoute("/api/backup/complete", HTTP_POST, RestApi::postBackupCompleteHandler);

    // 10. REST API: 1-Click Web OTA Firmware Update
    registerRoute("/api/ota", HTTP_POST, RestApi::postOtaUpdateHandler);

    // 11. Static File Server (Wildcard fallback for / and all files)
    registerRoute("/*", HTTP_GET, StaticFileServer::fileGetHandler);
}

} // namespace web
