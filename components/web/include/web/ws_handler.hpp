#pragma once

#include "esp_http_server.h"
#include "fsm/reflow_fsm.hpp"
#include <mutex>
#include <vector>
#include <string>

namespace web {

/// Capture ESP-IDF log line into ring buffer for Web UI
void appendLogLine(const std::string& line);

/// Get copy of latest log lines for Web UI
std::vector<std::string> getLatestLogs(size_t maxCount = 20);

/// Maximum concurrent WebSocket browser connections
constexpr size_t MAX_WS_CLIENTS = 4;

/**
 * @brief Telemetry snapshot structure sent to connected WebSocket clients.
 */
struct TelemetryData {
    uint8_t  stateEnum        = 0;
    std::string stateStr      = "IDLE";
    bool     preheatDone      = false;
    float    topTemp          = 0.0f;
    float    bottomTemp       = 0.0f;
    float    topSet           = 0.0f;
    float    bottomSet        = 0.0f;
    float    topPower         = 0.0f;
    float    bottomPower      = 0.0f;
    uint32_t elapsedSec       = 0;
    uint32_t talSec           = 0;
    bool     fan              = false;
    bool     lamp             = false;
    uint8_t  topStep          = 0;
    uint8_t  bottomStep       = 0;
    bool     topSettling      = false;
    bool     bottomSettling   = false;
    bool     topHolding       = false;
    bool     bottomHolding    = false;
    uint32_t topSettleRemain    = 0;
    uint32_t bottomSettleRemain = 0;
    uint32_t topHoldRemain      = 0;
    uint32_t bottomHoldRemain   = 0;
    std::string profileFile   = "";
    std::vector<fsm::StepMarker> stepMarkers;

    // Active PID Gains
    float    topPidKp            = 0.0f;
    float    topPidKi            = 0.0f;
    float    topPidKd            = 0.0f;
    float    bottomPidKp         = 0.0f;
    float    bottomPidKi         = 0.0f;
    float    bottomPidKd         = 0.0f;

    // Autotuner Telemetry
    bool     autotuneActive      = false;
    bool     autotuneFinished    = false;
    bool     autotuneIsTop       = true;
    float    autotuneProgress    = 0.0f;
    float    autotuneTargetTemp  = 0.0f;
    float    autotuneKp          = 0.0f;
    float    autotuneKi          = 0.0f;
    float    autotuneKd          = 0.0f;
};

/**
 * @brief Manages WebSocket client connections and asynchronous broadcasts (ESP-IDF v6.0.2).
 */
class WebSocketHandler {
public:
    explicit WebSocketHandler(httpd_handle_t serverHandle);
    ~WebSocketHandler();

    /**
     * @brief HTTPD WebSocket Request Handler for URI `/ws`.
     */
    static esp_err_t wsHandler(httpd_req_t *req);

    /**
     * @brief Serialize TelemetryData struct into clean, standard-precision JSON string.
     * @param telemetry Telemetry data snapshot.
     * @return Formatted JSON string.
     */
    static std::string serializeTelemetry(const TelemetryData& telemetry);

    /**
     * @brief Broadcast telemetry data JSON frame to all active WebSocket clients.
     * @param telemetry Telemetry data snapshot.
     */
    void broadcast(const TelemetryData& telemetry);

    /**
     * @brief Helper to register WebSocket singleton pointer.
     */
    static void setInstance(WebSocketHandler* instance);
    static WebSocketHandler* getInstance();

    /**
     * @brief Client registration and disconnection management.
     */
    void addClient(int fd);
    void removeClient(int fd);

private:
    httpd_handle_t _serverHandle;
    std::mutex     _clientsMutex;
    int            _clientFds[MAX_WS_CLIENTS] = {-1, -1, -1, -1};

    static WebSocketHandler* s_instance;
};

} // namespace web
