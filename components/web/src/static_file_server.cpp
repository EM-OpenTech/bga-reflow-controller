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

// ============================================================================
// Binary Assets Embedded in Flash ROM via CMake EMBED_FILES
// ============================================================================
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

extern const uint8_t style_css_gz_start[] asm("_binary_style_css_gz_start");
extern const uint8_t style_css_gz_end[] asm("_binary_style_css_gz_end");

extern const uint8_t app_js_gz_start[] asm("_binary_app_js_gz_start");
extern const uint8_t app_js_gz_end[] asm("_binary_app_js_gz_end");

extern const uint8_t chart_min_js_gz_start[] asm("_binary_chart_min_js_gz_start");
extern const uint8_t chart_min_js_gz_end[] asm("_binary_chart_min_js_gz_end");

extern const uint8_t de_js_gz_start[] asm("_binary_de_js_gz_start");
extern const uint8_t de_js_gz_end[] asm("_binary_de_js_gz_end");
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

namespace {

constexpr const char* TAG = "StaticFileServer";

bool handleCaptivePortalProbe(httpd_req_t* req) {
    // 1. Apple (iOS / macOS) — hotspot-detect.html, canonical.html
    if (strstr(req->uri, "hotspot-detect.html") != nullptr || strstr(req->uri, "canonical.html") != nullptr) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store");
        httpd_resp_sendstr(req, "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
        return true;
    }

    // 2. Android / Google — generate_204, gen_204
    if (strstr(req->uri, "generate_204") != nullptr || strstr(req->uri, "gen_204") != nullptr) {
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store");
        httpd_resp_sendstr(req, "");
        return true;
    }

    // 3. Microsoft Windows — ncsi.txt
    if (strstr(req->uri, "ncsi.txt") != nullptr) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "Microsoft NCSI");
        return true;
    }

    // 4. Microsoft Windows — connecttest.txt, redirect.txt
    if (strstr(req->uri, "connecttest.txt") != nullptr || strstr(req->uri, "redirect") != nullptr) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "Microsoft Connect Test");
        return true;
    }

    // 5. Legacy probes
    if (strstr(req->uri, "success.txt") != nullptr) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "success\n");
        return true;
    }

    return false;
}

struct StaticAsset {
    const uint8_t* start{nullptr};
    const uint8_t* end{nullptr};
    const char* contentType{nullptr};
    bool isHtml{false};
};

bool lookupStaticAsset(const std::string& uri, StaticAsset& asset) {
    if (uri == "/" || uri == "/index.html") {
        asset = {.start = index_html_gz_start,
                 .end = index_html_gz_end,
                 .contentType = "text/html; charset=utf-8",
                 .isHtml = true};
        return true;
    }
    if (uri == "/style.css") {
        asset = {.start = style_css_gz_start, .end = style_css_gz_end, .contentType = "text/css", .isHtml = false};
        return true;
    }
    if (uri == "/app.js") {
        asset = {
            .start = app_js_gz_start, .end = app_js_gz_end, .contentType = "application/javascript", .isHtml = false};
        return true;
    }
    if (uri == "/chart.min.js") {
        asset = {.start = chart_min_js_gz_start,
                 .end = chart_min_js_gz_end,
                 .contentType = "application/javascript",
                 .isHtml = false};
        return true;
    }
    if (uri == "/lang/de.js" || uri == "/de.js") {
        asset = {
            .start = de_js_gz_start, .end = de_js_gz_end, .contentType = "application/javascript", .isHtml = false};
        return true;
    }
    return false;
}

} // anonymous namespace

namespace web {

const char* StaticFileServer::getContentType(const char* filepath) {
    if (filepath == nullptr) {
        return "application/octet-stream";
    }

    std::string_view path(filepath);
    if (path.ends_with(".gz")) {
        path.remove_suffix(3);
    }

    struct MimeMapping {
        std::string_view ext;
        const char* mime;
    };

    static constexpr std::array<MimeMapping, 11> kMimeMap = {{
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".ico", "image/x-icon"},
        {".txt", "text/plain"},
    }};

    for (const auto& entry : kMimeMap) {
        if (path.ends_with(entry.ext)) {
            return entry.mime;
        }
    }
    return "application/octet-stream";
}

esp_err_t StaticFileServer::fileGetHandler(httpd_req_t* req) {
    // Prevent Path Traversal attacks (e.g., /..)
    if (strstr(req->uri, "..") != nullptr) {
        ESP_LOGW(TAG, "Rejected suspicious path traversal URI: %s", req->uri);
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Path traversal blocked");
        return ESP_OK;
    }

    if (handleCaptivePortalProbe(req)) {
        return ESP_OK;
    }

    // Strip query parameters if any (e.g. /?v=1 or /index.html?v=1)
    std::string uriClean(req->uri);
    size_t qPos = uriClean.find('?');
    if (qPos != std::string::npos) {
        uriClean = uriClean.substr(0, qPos);
    }

    StaticAsset asset;
    if (!lookupStaticAsset(uriClean, asset)) {
        ESP_LOGW(TAG, "404 Not Found for static asset: %s", req->uri);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "404 Not Found");
        return ESP_OK;
    }

    size_t assetSize = asset.end - asset.start;
    httpd_resp_set_type(req, asset.contentType);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    if (asset.isHtml) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=604800, immutable");
    }

    auto assetLen = static_cast<ssize_t>(assetSize);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return httpd_resp_send(req, reinterpret_cast<const char*>(asset.start), assetLen);
}

} // namespace web
