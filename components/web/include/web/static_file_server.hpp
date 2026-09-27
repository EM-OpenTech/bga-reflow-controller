#pragma once

#include "esp_http_server.h"

namespace web {

/**
 * @brief Sandboxed HTTP Static File Server for ESP-IDF v6.0.2.
 * 
 * Serves assets exclusively from the sandboxed LittleFS directory `/littlefs/web/`.
 * Supports automatic .gz fallback for pre-compressed assets with `Content-Encoding: gzip`.
 * Prevents Directory Traversal attacks (e.g. `..`).
 */
class StaticFileServer {
public:
    /**
     * @brief Handler function for HTTP GET requests targeting static files.
     */
    static esp_err_t fileGetHandler(httpd_req_t *req);

    /**
     * @brief Determine MIME Content-Type string from file extension.
     */
    static const char* getContentType(const char* filepath);
};

} // namespace web
