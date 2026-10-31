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
 * @file api_context.cpp
 * @brief Implementation of shared HTTP utility helpers for REST API controllers.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/api_context.hpp"

namespace web {

std::string ApiContext::readRequestBody(httpd_req_t* req) {
    if (req == nullptr) {
        return "";
    }

    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > config::Limits::MAX_JSON_PAYLOAD_BYTES) {
        return "";
    }

    std::string body;
    body.resize(total_len);

    size_t cur_len = 0;
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, &body[cur_len], total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return "";
        }
        cur_len += static_cast<size_t>(received);
    }
    return body;
}

esp_err_t ApiContext::sendJsonSuccess(httpd_req_t* req, const char* extraFields) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    if (extraFields != nullptr && extraFields[0] != '\0') {
        std::string json = "{\"success\":true,";
        json += extraFields;
        json += "}";
        return httpd_resp_sendstr(req, json.c_str());
    }
    return httpd_resp_sendstr(req, "{\"success\":true}");
}

} // namespace web
