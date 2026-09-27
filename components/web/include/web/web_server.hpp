#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "storage/storage_manager.hpp"
#include "fsm/reflow_fsm.hpp"
#include "web/wifi_manager.hpp"
#include "web/ws_handler.hpp"
#include <memory>

// Forward declaration – full type available via system_context.hpp in main/include
namespace app { class SystemContext; }

namespace web {

/**
 * @brief Main HTTP and WebSocket WebServer Component (ESP-IDF v6.0.2).
 */
class WebServer {
public:
    WebServer(storage::StorageManager& storage,
              config::MachineSettings& settings,
              fsm::ReflowFSM&         fsm,
              WifiManager&            wifi,
              app::SystemContext&     context);
    ~WebServer();

    /**
     * @brief Start HTTPD server and register all static file, REST, and WebSocket handlers.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t begin();

    /**
     * @brief Stop the running HTTPD server.
     */
    void stop();

    /**
     * @brief Broadcast telemetry data frame to connected WebSocket clients.
     */
    void broadcastTelemetry(const TelemetryData& telemetry);

private:
    storage::StorageManager& _storage;
    config::MachineSettings& _settings;
    fsm::ReflowFSM&         _fsm;
    WifiManager&            _wifi;
    app::SystemContext&     _context;  ///< Passed to RestApi for thread-safe Core-0 reads

    httpd_handle_t                 _serverHandle = nullptr;
    std::unique_ptr<WebSocketHandler> _wsHandler;

    void registerRoutes();
};

} // namespace web
