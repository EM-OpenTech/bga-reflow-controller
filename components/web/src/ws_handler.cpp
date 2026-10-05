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
 * @file ws_handler.cpp
 * @brief Implementation of WebSocket telemetry broadcaster and client connection manager.
 *
 * Manages WebSocket client connections, serializes telemetry JSON frames with fixed
 * decimal precision, and broadcasts async packets to connected frontends.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/ws_handler.hpp"
#include "esp_log.h"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <deque>

static const char* TAG = "WebSocketHandler";

namespace web {

// ============================================================================
// Circular Log Ring Buffer
// ============================================================================

static std::deque<std::string> s_logBuffer;
static std::mutex s_logMutex;
static std::atomic<uint32_t> s_logSequence{0};

void appendLogLine(const std::string& line)
{
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        if (s_logBuffer.size() >= 30) {
            s_logBuffer.pop_front();
        }
        s_logBuffer.push_back(line);
    }
    s_logSequence.fetch_add(1, std::memory_order_relaxed);
}

uint32_t getLogSequence()
{
    return s_logSequence.load(std::memory_order_relaxed);
}

std::vector<std::string> getLatestLogs(size_t maxCount)
{
    std::lock_guard<std::mutex> lock(s_logMutex);
    std::vector<std::string> result;
    size_t start = (s_logBuffer.size() > maxCount) ? (s_logBuffer.size() - maxCount) : 0;
    for (size_t i = start; i < s_logBuffer.size(); ++i) {
        result.push_back(s_logBuffer[i]);
    }
    return result;
}

// ============================================================================
// Client Connection Management
// ============================================================================

WebSocketHandler* WebSocketHandler::s_instance = nullptr;

void WebSocketHandler::setInstance(WebSocketHandler* instance) {
    s_instance = instance;
}

WebSocketHandler* WebSocketHandler::getInstance() {
    return s_instance;
}

WebSocketHandler::WebSocketHandler(httpd_handle_t serverHandle)
    : _serverHandle(serverHandle)
{
    for (size_t i = 0; i < MAX_WS_CLIENTS; ++i) {
        _clientFds[i] = -1;
    }
}

WebSocketHandler::~WebSocketHandler()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void WebSocketHandler::addClient(int fd)
{
    std::lock_guard<std::mutex> lock(_clientsMutex);
    for (size_t i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (_clientFds[i] == fd) {
            return;
        }
    }
    // 1. Check for standard free slot
    for (size_t i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (_clientFds[i] == -1) {
            _clientFds[i] = fd;
            _lastBroadcastLogSeq = 0xFFFFFFFF; // Ensure new client receives full log history on first frame
            ESP_LOGI(TAG, "New WebSocket client connected (fd: %d, slot: %zu)", fd, i);
            return;
        }
    }
    // 2. If all slots occupied, do a lazy healthcheck to reclaim dead sockets
    if (_serverHandle != nullptr) {
        for (size_t i = 0; i < MAX_WS_CLIENTS; ++i) {
            if (_clientFds[i] != -1 && httpd_ws_get_fd_info(_serverHandle, _clientFds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) {
                ESP_LOGI(TAG, "Reclaimed stale WebSocket slot %zu (dead fd: %d) for new fd: %d", i, _clientFds[i], fd);
                _clientFds[i] = fd;
                _lastBroadcastLogSeq = 0xFFFFFFFF;
                return;
            }
        }
    }
    ESP_LOGW(TAG, "Max WebSocket clients reached (%zu), rejecting fd: %d", MAX_WS_CLIENTS, fd);
}

void WebSocketHandler::removeClient(int fd)
{
    std::lock_guard<std::mutex> lock(_clientsMutex);
    for (size_t i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (_clientFds[i] == fd) {
            _clientFds[i] = -1;
            ESP_LOGI(TAG, "WebSocket client disconnected (fd: %d)", fd);
            return;
        }
    }
}

// ============================================================================
// WebSocket Frame Handler & Protocol Dispatcher
// ============================================================================

esp_err_t WebSocketHandler::wsHandler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Handshake request
        ESP_LOGI(TAG, "Handshake done, opening new WebSocket connection");
        if (s_instance != nullptr) {
            s_instance->addClient(httpd_req_to_sockfd(req));
        }
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = nullptr;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "httpd_ws_recv_frame failed to get frame len: %d", ret);
        return ret;
    }

    if (ws_pkt.len) {
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (buf == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate memory for WS payload");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
            free(buf);
            return ret;
        }
        if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
            httpd_ws_frame_t pong_pkt;
            memset(&pong_pkt, 0, sizeof(httpd_ws_frame_t));
            pong_pkt.payload = buf;
            pong_pkt.len     = ws_pkt.len;
            pong_pkt.type    = HTTPD_WS_TYPE_PONG;
            httpd_ws_send_frame(req, &pong_pkt);
        } else {
            ESP_LOGD(TAG, "WS Received: %s", ws_pkt.payload);
        }
        free(buf);
    }
    return ESP_OK;
}

// ============================================================================
// Telemetry JSON Serialization & Broadcasting
// ============================================================================

std::string WebSocketHandler::serializeTelemetry(const TelemetryData& t, bool includeLogs)
{
    auto f1 = [](float v) -> std::string {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.1f", v);
        return std::string(buf);
    };
    auto f2 = [](float v) -> std::string {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    };
    auto f3 = [](float v) -> std::string {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.3f", v);
        return std::string(buf);
    };
    auto fi = [](float v) -> int {
        return static_cast<int>(std::round(v));
    };

    std::ostringstream ss;
    ss << "{"
       << "\"stateEnum\":" << (int)t.stateEnum << ","
       << "\"state\":\"" << t.stateStr << "\","
       << "\"preheatDone\":" << (t.preheatDone ? "true" : "false") << ","
       << "\"topTemp\":" << f1(t.topTemp) << ","
       << "\"bottomTemp\":" << f1(t.bottomTemp) << ","
       << "\"topSet\":" << f1(t.topSet) << ","
       << "\"bottomSet\":" << f1(t.bottomSet) << ","
       << "\"topPower\":" << fi(t.topPower) << ","
       << "\"bottomPower\":" << fi(t.bottomPower) << ","
       << "\"elapsed\":" << t.elapsedSec << ","
       << "\"talSec\":" << t.talSec << ","
       << "\"fan\":" << (t.fan ? "true" : "false") << ","
       << "\"lamp\":" << (t.lamp ? "true" : "false") << ","
       << "\"topStep\":" << (int)t.topStep << ","
       << "\"bottomStep\":" << (int)t.bottomStep << ","
       << "\"topSettling\":" << (t.topSettling ? "true" : "false") << ","
       << "\"bottomSettling\":" << (t.bottomSettling ? "true" : "false") << ","
       << "\"topHolding\":" << (t.topHolding ? "true" : "false") << ","
       << "\"bottomHolding\":" << (t.bottomHolding ? "true" : "false") << ","
       << "\"topSettleRemainSec\":" << t.topSettleRemain << ","
       << "\"bottomSettleRemainSec\":" << t.bottomSettleRemain << ","
       << "\"topHoldRemainSec\":" << t.topHoldRemain << ","
       << "\"bottomHoldRemainSec\":" << t.bottomHoldRemain << ","
       << "\"profileFile\":\"" << t.profileFile << "\","
       << "\"topPidKp\":" << f2(t.topPidKp) << ","
       << "\"topPidKi\":" << f3(t.topPidKi) << ","
       << "\"topPidKd\":" << f2(t.topPidKd) << ","
       << "\"bottomPidKp\":" << f2(t.bottomPidKp) << ","
       << "\"bottomPidKi\":" << f3(t.bottomPidKi) << ","
       << "\"bottomPidKd\":" << f2(t.bottomPidKd) << ","
       << "\"tuneActive\":" << (t.autotuneActive ? "true" : "false") << ","
       << "\"tuneFinished\":" << (t.autotuneFinished ? "true" : "false") << ","
       << "\"tuneIsTop\":" << (t.autotuneIsTop ? "true" : "false") << ","
       << "\"tuneProgress\":" << fi(t.autotuneProgress) << ","
       << "\"tuneTargetTemp\":" << f1(t.autotuneTargetTemp) << ","
       << "\"tuneKp\":" << f2(t.autotuneKp) << ","
       << "\"tuneKi\":" << f3(t.autotuneKi) << ","
       << "\"tuneKd\":" << f2(t.autotuneKd) << ","
       << "\"stepMarkers\":[";

    for (size_t i = 0; i < t.stepMarkers.size(); ++i) {
        const auto& m = t.stepMarkers[i];
        if (i > 0) ss << ",";
        ss << "{"
           << "\"time\":" << m.timeS << ","
           << "\"step\":" << (int)m.stepIndex << ","
           << "\"temp\":" << f1(m.targetTemp) << ","
           << "\"isTop\":" << (m.isTop ? "true" : "false") << ","
           << "\"label\":\"Step " << (int)(m.stepIndex + 1) << "\\n"
           << (m.isTop ? "Top: " : "Bottom: ") << (int)roundf(m.targetTemp) << "°C\""
           << "}";
    }
    ss << "]";

    // 1:1 Live CLI Log streaming into Web UI (only serialized when log sequence changes)
    if (includeLogs) {
        ss << ",\"logs\":[";
        auto logs = getLatestLogs(20);
        for (size_t i = 0; i < logs.size(); ++i) {
            if (i > 0) ss << ",";
            std::string escaped;
            for (char c : logs[i]) {
                if (c == '"') escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else if (c >= 32 && c <= 126) escaped += c;
            }
            ss << "\"" << escaped << "\"";
        }
        ss << "]";
    }
    ss << "}";

    return ss.str();
}

void WebSocketHandler::broadcast(const TelemetryData& t)
{
    if (_serverHandle == nullptr) return;

    size_t numClients = 8;
    int clientFds[8] = {0};
    if (httpd_get_client_list(_serverHandle, &numClients, clientFds) != ESP_OK) {
        return;
    }

    bool hasWsClient = false;
    for (size_t i = 0; i < numClients; ++i) {
        if (httpd_ws_get_fd_info(_serverHandle, clientFds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            hasWsClient = true;
            break;
        }
    }
    if (!hasWsClient) return;

    uint32_t currentLogSeq = getLogSequence();
    bool includeLogs = (currentLogSeq != _lastBroadcastLogSeq);
    if (includeLogs) {
        _lastBroadcastLogSeq = currentLogSeq;
    }

    std::string jsonPayload = serializeTelemetry(t, includeLogs);

    for (size_t i = 0; i < numClients; ++i) {
        if (httpd_ws_get_fd_info(_serverHandle, clientFds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_frame_t ws_pkt;
            memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
            ws_pkt.payload = (uint8_t*)jsonPayload.c_str();
            ws_pkt.len = jsonPayload.length();
            ws_pkt.type = HTTPD_WS_TYPE_TEXT;
            ws_pkt.final = true;

            esp_err_t ret = httpd_ws_send_frame_async(_serverHandle, clientFds[i], &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send async WS frame to fd: %d (ret: %d). Closing session.", clientFds[i], ret);
                removeClient(clientFds[i]);
                httpd_sess_trigger_close(_serverHandle, clientFds[i]);
            }
        }
    }
}

} // namespace web
