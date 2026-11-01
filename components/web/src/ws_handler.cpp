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
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>

#ifdef ESP_PLATFORM
#include "esp_task_wdt.h"
#include <sys/socket.h>
#endif

namespace {

constexpr auto* TAG = "WebSocketHandler";

struct LogBufferState {
    std::deque<std::string> buffer;
    std::mutex mutex;
    std::atomic<uint32_t> sequence{0};
};

LogBufferState& getLogState() {
    static LogBufferState state;
    return state;
}

std::string escapeJsonString(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.length() + 8);
    for (char c : input) {
        if (c == '"') {
            escaped += "\\\"";
        } else if (c == '\\') {
            escaped += "\\\\";
        } else if (static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) != 127) {
            escaped += c;
        }
    }
    return escaped;
}

} // anonymous namespace

namespace web {

// ============================================================================
// Circular Log Ring Buffer
// ============================================================================

void appendLogLine(const std::string& line) {
    auto& state = getLogState();
    {
        std::unique_lock<std::mutex> lock(state.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            if (state.buffer.size() >= 30) {
                state.buffer.pop_front();
            }
            state.buffer.push_back(line);
        }
    }
    state.sequence.fetch_add(1, std::memory_order_relaxed);
}

uint32_t getLogSequence() {
    return getLogState().sequence.load(std::memory_order_relaxed);
}

std::vector<std::string> getLatestLogs(size_t maxCount) {
    auto& state = getLogState();
    std::unique_lock<std::mutex> lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return {};
    }
    std::vector<std::string> result;
    size_t start = (state.buffer.size() > maxCount) ? (state.buffer.size() - maxCount) : 0;
    for (size_t i = start; i < state.buffer.size(); ++i) {
        result.push_back(state.buffer[i]);
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

WebSocketHandler::WebSocketHandler(httpd_handle_t serverHandle) : _serverHandle(serverHandle) {
    _clientFds.fill(-1);
}

WebSocketHandler::~WebSocketHandler() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void WebSocketHandler::addClient(int fd) {
    std::optional<size_t> newSlot;
    std::optional<size_t> reclaimedSlot;
    int reclaimedDeadFd = -1;
    bool maxReached = false;

    {
        std::lock_guard<std::mutex> lock(_clientsMutex);
        if (std::any_of(_clientFds.begin(), _clientFds.end(), [fd](int clientFd) { return clientFd == fd; })) {
            return;
        }
        // 1. Check for standard free slot
        for (size_t i = 0; i < _clientFds.size(); ++i) {
            if (_clientFds.at(i) == -1) {
                _clientFds.at(i) = fd;
                _lastBroadcastLogSeq = 0xFFFFFFFF; // Ensure new client receives full log history on first frame
                newSlot = i;
                break;
            }
        }
        // 2. If all slots occupied, do a lazy healthcheck to reclaim dead sockets
        if (!newSlot.has_value() && (_serverHandle != nullptr)) {
            for (size_t i = 0; i < _clientFds.size(); ++i) {
                if (_clientFds.at(i) != -1 &&
                    httpd_ws_get_fd_info(_serverHandle, _clientFds.at(i)) != HTTPD_WS_CLIENT_WEBSOCKET) {
                    reclaimedDeadFd = _clientFds.at(i);
                    _clientFds.at(i) = fd;
                    _lastBroadcastLogSeq = 0xFFFFFFFF;
                    reclaimedSlot = i;
                    break;
                }
            }
        }
        if (!newSlot.has_value() && !reclaimedSlot.has_value()) {
            maxReached = true;
        }
    }

    if (newSlot.has_value()) {
        ESP_LOGI(TAG, "New WebSocket client connected (fd: %d, slot: %zu)", fd, *newSlot);
    } else if (reclaimedSlot.has_value()) {
        ESP_LOGI(TAG, "Reclaimed stale WebSocket slot %zu (dead fd: %d) for new fd: %d", *reclaimedSlot,
                 reclaimedDeadFd, fd);
    } else if (maxReached) {
        ESP_LOGW(TAG, "Max WebSocket clients reached (%zu), rejecting fd: %d", MAX_WS_CLIENTS, fd);
    }
}

void WebSocketHandler::removeClient(int fd) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(_clientsMutex);
        auto it = std::find(_clientFds.begin(), _clientFds.end(), fd);
        if (it != _clientFds.end()) {
            *it = -1;
            removed = true;
        }
    }
    if (removed) {
        ESP_LOGI(TAG, "WebSocket client disconnected (fd: %d)", fd);
    }
}

// ============================================================================
// WebSocket Frame Handler & Protocol Dispatcher
// ============================================================================

esp_err_t WebSocketHandler::wsHandler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        // Handshake request
        const int sockfd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Handshake done, opening new WebSocket connection (fd: %d)", sockfd);
#ifdef ESP_PLATFORM
        // Set short send timeout (150 ms) to prevent dead sockets from blocking the broadcast loop
        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 150000;
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
        if (s_instance != nullptr) {
            s_instance->addClient(sockfd);
        }
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt{};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "httpd_ws_recv_frame failed to get frame len: %d", ret);
        return ret;
    }

    if (ws_pkt.len > 0) {
        std::vector<uint8_t> buf(ws_pkt.len + 1, 0);
        ws_pkt.payload = buf.data();
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
            return ret;
        }

        if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
            httpd_ws_frame_t pong_pkt{};
            pong_pkt.payload = buf.data();
            pong_pkt.len = ws_pkt.len;
            pong_pkt.type = HTTPD_WS_TYPE_PONG;
            httpd_ws_send_frame(req, &pong_pkt);
        } else {
            ESP_LOGD(TAG, "WS Received: %s", ws_pkt.payload);
        }
    }
    return ESP_OK;
}

// ============================================================================
// Telemetry JSON Serialization & Broadcasting
// ============================================================================

std::string WebSocketHandler::serializeTelemetry(const TelemetryData& t, bool includeLogs) {
    auto f1 = [](float v) -> std::string {
        if (std::isnan(v) || std::isinf(v)) {
            return "null";
        }
        std::array<char, 24> buf{};
        int len = snprintf(buf.data(), buf.size(), "%.1f", static_cast<double>(v));
        return (len > 0) ? std::string(buf.data()) : "null";
    };
    auto f2 = [](float v) -> std::string {
        if (std::isnan(v) || std::isinf(v)) {
            return "null";
        }
        std::array<char, 24> buf{};
        int len = snprintf(buf.data(), buf.size(), "%.2f", static_cast<double>(v));
        return (len > 0) ? std::string(buf.data()) : "null";
    };
    auto f3 = [](float v) -> std::string {
        if (std::isnan(v) || std::isinf(v)) {
            return "null";
        }
        std::array<char, 24> buf{};
        int len = snprintf(buf.data(), buf.size(), "%.3f", static_cast<double>(v));
        return (len > 0) ? std::string(buf.data()) : "null";
    };
    auto fi = [](float v) -> int {
        if (std::isnan(v) || std::isinf(v)) {
            return 0;
        }
        return static_cast<int>(std::round(v));
    };

    std::ostringstream ss;
    ss << "{"
       << "\"stateEnum\":" << static_cast<int>(t.stateEnum) << ","
       << R"("state":")" << t.stateStr << "\","
       << "\"preheatDone\":" << (t.preheatDone ? "true" : "false") << ","
       << "\"topTemp\":" << (t.topSensorOk ? f1(t.topTemp) : "null") << ","
       << "\"bottomTemp\":" << (t.bottomSensorOk ? f1(t.bottomTemp) : "null") << ","
       << "\"topSensorOk\":" << (t.topSensorOk ? "true" : "false") << ","
       << "\"bottomSensorOk\":" << (t.bottomSensorOk ? "true" : "false") << ","
       << "\"topSet\":" << f1(t.topSet) << ","
       << "\"bottomSet\":" << f1(t.bottomSet) << ","
       << "\"topPower\":" << fi(t.topPower) << ","
       << "\"bottomPower\":" << fi(t.bottomPower) << ","
       << "\"elapsed\":" << t.elapsedSec << ","
       << "\"talSec\":" << t.talSec << ","
       << "\"fan\":" << (t.fan ? "true" : "false") << ","
       << "\"lamp\":" << (t.lamp ? "true" : "false") << ","
       << "\"topStep\":" << static_cast<int>(t.topStep) << ","
       << "\"bottomStep\":" << static_cast<int>(t.bottomStep) << ","
       << "\"topSettling\":" << (t.topSettling ? "true" : "false") << ","
       << "\"bottomSettling\":" << (t.bottomSettling ? "true" : "false") << ","
       << "\"topHolding\":" << (t.topHolding ? "true" : "false") << ","
       << "\"bottomHolding\":" << (t.bottomHolding ? "true" : "false") << ","
       << "\"topSettleRemainSec\":" << t.topSettleRemain << ","
       << "\"bottomSettleRemainSec\":" << t.bottomSettleRemain << ","
       << "\"topHoldRemainSec\":" << t.topHoldRemain << ","
       << "\"bottomHoldRemainSec\":" << t.bottomHoldRemain << ","
       << R"("profileFile":")" << t.profileFile << "\","
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
        if (i > 0) {
            ss << ",";
        }
        ss << "{"
           << "\"time\":" << m.timeS << ","
           << "\"step\":" << static_cast<int>(m.stepIndex) << ","
           << "\"temp\":" << f1(m.targetTemp) << ","
           << "\"isTop\":" << (m.isTop ? "true" : "false") << ","
           << R"("label":"Step )" << static_cast<int>(m.stepIndex + 1) << "\\n"
           << (m.isTop ? "Top: " : "Bottom: ") << fi(m.targetTemp) << "°C\""
           << "}";
    }
    ss << "]";

    // 1:1 Live CLI Log streaming into Web UI (only serialized when log sequence changes)
    if (includeLogs) {
        ss << ",\"logs\":[";
        auto logs = getLatestLogs(20);
        for (size_t i = 0; i < logs.size(); ++i) {
            if (i > 0) {
                ss << ",";
            }
            ss << "\"" << escapeJsonString(logs[i]) << "\"";
        }
        ss << "]";
    }
    ss << "}";

    return ss.str();
}

void WebSocketHandler::broadcast(const TelemetryData& t) {
    if (_serverHandle == nullptr) {
        return;
    }

    std::array<int, MAX_HTTP_SOCKETS> clientFds{};
    size_t numClients = clientFds.size();
    if (httpd_get_client_list(_serverHandle, &numClients, clientFds.data()) != ESP_OK) {
        return;
    }

    bool hasWsClient = false;
    for (size_t i = 0; i < numClients; ++i) {
#ifdef ESP_PLATFORM
        esp_task_wdt_reset();
#endif
        if (httpd_ws_get_fd_info(_serverHandle, clientFds.at(i)) == HTTPD_WS_CLIENT_WEBSOCKET) {
            hasWsClient = true;
            break;
        }
    }
    if (!hasWsClient) {
        return;
    }

    uint32_t currentLogSeq = getLogSequence();
    bool includeLogs = (currentLogSeq != _lastBroadcastLogSeq);
    if (includeLogs) {
        _lastBroadcastLogSeq = currentLogSeq;
    }

    std::string jsonPayload = serializeTelemetry(t, includeLogs);

    for (size_t i = 0; i < numClients; ++i) {
#ifdef ESP_PLATFORM
        esp_task_wdt_reset();
#endif
        if (httpd_ws_get_fd_info(_serverHandle, clientFds.at(i)) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_frame_t ws_pkt{};
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            ws_pkt.payload = reinterpret_cast<uint8_t*>(jsonPayload.data());
            ws_pkt.len = jsonPayload.length();
            ws_pkt.type = HTTPD_WS_TYPE_TEXT;
            ws_pkt.final = true;

            esp_err_t ret = httpd_ws_send_frame_async(_serverHandle, clientFds.at(i), &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send async WS frame to fd: %d (ret: %d). Closing session.", clientFds.at(i),
                         ret);
                removeClient(clientFds.at(i));
                httpd_sess_trigger_close(_serverHandle, clientFds.at(i));
            }
        }
    }
}

} // namespace web
