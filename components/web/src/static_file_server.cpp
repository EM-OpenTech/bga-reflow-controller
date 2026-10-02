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
 * @file static_file_server.cpp
 * @brief Implementation of Zero-Copy Flash-Embedded HTTP Static File Server.
 *
 * Serves ROM-embedded gzip-compressed HTML, CSS, and JS web assets directly
 * without RAM heap overhead.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/static_file_server.hpp"
#include "esp_log.h"
#include <cstring>
#include <string>

static const char* TAG = "StaticFileServer";

// ============================================================================
// Binary Assets Embedded in Flash ROM via CMake EMBED_FILES
// ============================================================================
extern const uint8_t index_html_gz_start[]   asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]     asm("_binary_index_html_gz_end");

extern const uint8_t style_css_gz_start[]    asm("_binary_style_css_gz_start");
extern const uint8_t style_css_gz_end[]      asm("_binary_style_css_gz_end");

extern const uint8_t app_js_gz_start[]       asm("_binary_app_js_gz_start");
extern const uint8_t app_js_gz_end[]         asm("_binary_app_js_gz_end");

extern const uint8_t chart_min_js_gz_start[] asm("_binary_chart_min_js_gz_start");
extern const uint8_t chart_min_js_gz_end[]   asm("_binary_chart_min_js_gz_end");

extern const uint8_t de_js_gz_start[]        asm("_binary_de_js_gz_start");
extern const uint8_t de_js_gz_end[]          asm("_binary_de_js_gz_end");

namespace web {

const char* StaticFileServer::getContentType(const char* filepath)
{
    std::string path(filepath);
    if (path.length() >= 3 && path.substr(path.length() - 3) == ".gz") {
        path = path.substr(0, path.length() - 3);
    }

    if (path.ends_with(".html") || path.ends_with(".htm")) return "text/html; charset=utf-8";
    if (path.ends_with(".css")) return "text/css";
    if (path.ends_with(".js")) return "application/javascript";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".svg")) return "image/svg+xml";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".ico")) return "image/x-icon";
    if (path.ends_with(".txt")) return "text/plain";
    return "application/octet-stream";
}

esp_err_t StaticFileServer::fileGetHandler(httpd_req_t *req)
{
    // Prevent Path Traversal attacks (e.g., /..)
    if (strstr(req->uri, "..") != nullptr) {
        ESP_LOGW(TAG, "Rejected suspicious path traversal URI: %s", req->uri);
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Path traversal blocked");
        return ESP_OK;
    }

// ============================================================================
// OS Connectivity & Captive Portal Probes
// ============================================================================
    // 1. Apple (iOS / macOS) — hotspot-detect.html, canonical.html
    if (strstr(req->uri, "hotspot-detect.html") != nullptr ||
        strstr(req->uri, "canonical.html") != nullptr) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store");
        httpd_resp_sendstr(req, "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
        return ESP_OK;
    }

    // 2. Android / Google — generate_204, gen_204
    if (strstr(req->uri, "generate_204") != nullptr ||
        strstr(req->uri, "gen_204") != nullptr) {
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store");
        httpd_resp_sendstr(req, "");
        return ESP_OK;
    }

    // 3. Microsoft Windows — ncsi.txt
    if (strstr(req->uri, "ncsi.txt") != nullptr) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "Microsoft NCSI");
        return ESP_OK;
    }

    // 4. Microsoft Windows — connecttest.txt, redirect.txt
    if (strstr(req->uri, "connecttest.txt") != nullptr ||
        strstr(req->uri, "redirect") != nullptr) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "Microsoft Connect Test");
        return ESP_OK;
    }

    // 5. Legacy probes
    if (strstr(req->uri, "success.txt") != nullptr) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "success\n");
        return ESP_OK;
    }

    // Strip query parameters if any (e.g. /?v=1 or /index.html?v=1)
    std::string uri_clean(req->uri);
    size_t q_pos = uri_clean.find('?');
    if (q_pos != std::string::npos) {
        uri_clean = uri_clean.substr(0, q_pos);
    }

    // Route to Embedded Flash Assets
    const uint8_t* asset_start = nullptr;
    const uint8_t* asset_end   = nullptr;
    const char*    content_type = "text/html; charset=utf-8";
    bool           is_html      = false;

    if (uri_clean == "/" || uri_clean == "/index.html") {
        asset_start  = index_html_gz_start;
        asset_end    = index_html_gz_end;
        content_type = "text/html; charset=utf-8";
        is_html      = true;
    } else if (uri_clean == "/style.css") {
        asset_start  = style_css_gz_start;
        asset_end    = style_css_gz_end;
        content_type = "text/css";
    } else if (uri_clean == "/app.js") {
        asset_start  = app_js_gz_start;
        asset_end    = app_js_gz_end;
        content_type = "application/javascript";
    } else if (uri_clean == "/chart.min.js") {
        asset_start  = chart_min_js_gz_start;
        asset_end    = chart_min_js_gz_end;
        content_type = "application/javascript";
    } else if (uri_clean == "/lang/de.js" || uri_clean == "/de.js") {
        asset_start  = de_js_gz_start;
        asset_end    = de_js_gz_end;
        content_type = "application/javascript";
    } else {
        ESP_LOGW(TAG, "404 Not Found for static asset: %s", req->uri);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "404 Not Found");
        return ESP_OK;
    }

    size_t asset_size = asset_end - asset_start;
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    if (is_html) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=300");
    }

    return httpd_resp_send(req, reinterpret_cast<const char*>(asset_start), asset_size);
}

} // namespace web
