#pragma once

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage/storage_manager.hpp"
#include "fsm/reflow_fsm.hpp"
#include "web/wifi_manager.hpp"

// Forward declaration – full type available via system_context.hpp in main/include
namespace app { class SystemContext; }

namespace web {

/**
 * @brief FreeRTOS Task Handles for runtime stack & health monitoring.
 */
struct TaskHandles {
    TaskHandle_t safetyTask    = nullptr;
    TaskHandle_t burstfireTask = nullptr;
    TaskHandle_t controlTask   = nullptr;
    TaskHandle_t inputTask     = nullptr;
    TaskHandle_t webTask       = nullptr;
};

/**
 * @brief REST API Dispatcher and Route Handlers for Reflow Controller (ESP-IDF v6.0.2).
 */
class RestApi {
public:
    /**
     * @brief Initialise static dependencies. Call once before httpd_start().
     * @param context  Thread-safe SystemContext – used to read activeProfileFile on Core 0.
     */
    static void init(storage::StorageManager* storage,
                     config::MachineSettings* settings,
                     fsm::ReflowFSM*         fsm,
                     WifiManager*            wifi,
                     app::SystemContext*     context);

    /**
     * @brief Register running FreeRTOS task handles for stack high-watermark diagnostics.
     */
    static void setTaskHandles(const TaskHandles& handles);

    // ========================================================================
    // REST ROUTE HANDLERS
    // ========================================================================
    static esp_err_t getStatusHandler(httpd_req_t *req);
    static esp_err_t getSettingsHandler(httpd_req_t *req);
    static esp_err_t postSettingsHandler(httpd_req_t *req);
    static esp_err_t getProfilesHandler(httpd_req_t *req);
    static esp_err_t getProfileHandler(httpd_req_t *req);
    static esp_err_t postProfileHandler(httpd_req_t *req);
    static esp_err_t deleteProfileHandler(httpd_req_t *req);
    static esp_err_t postControlHandler(httpd_req_t *req);
    static esp_err_t postOverridesHandler(httpd_req_t *req);
    static esp_err_t getHistoryHandler(httpd_req_t *req);
    static esp_err_t getSecurityStatusHandler(httpd_req_t *req);
    static esp_err_t postSecurityWifiHandler(httpd_req_t *req);
    // Theme (thin wrapper — theme lives inside settings.json)
    static esp_err_t getThemeHandler(httpd_req_t *req);
    static esp_err_t postThemeHandler(httpd_req_t *req);
    // PID Library (/littlefs/config/pid_library.json)
    static esp_err_t getPidLibraryHandler(httpd_req_t *req);
    static esp_err_t postPidLibraryHandler(httpd_req_t *req);
    // Backup State & Completion Tracking
    static esp_err_t postBackupCompleteHandler(httpd_req_t *req);
    // 1-Click Web OTA Firmware Update
    static esp_err_t postOtaUpdateHandler(httpd_req_t *req);

    // URI / Path parameter parsing helper
    static std::string extractProfileFilenameFromUri(const std::string& uri, const std::string& query = "");

private:
    static storage::StorageManager* s_storage;
    static config::MachineSettings* s_settings;
    static fsm::ReflowFSM*          s_fsm;
    static WifiManager*             s_wifi;
    static app::SystemContext*      s_context;  ///< For safe Core-0 reads of profile name
    static bool                     s_backupTaken; ///< Instance runtime tracking (resets to false on reboot)
    static TaskHandles              s_taskHandles; ///< Captured task handles for stack monitoring

    static std::string readRequestBody(httpd_req_t *req);
};

} // namespace web
