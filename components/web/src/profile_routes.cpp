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
 * @file profile_routes.cpp
 * @brief Implementation of profile management REST route handlers.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/profile_routes.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include "storage/json_serializer.hpp"
#include "web/api_context.hpp"
#include <array>
#include <cstring>
#include <vector>

namespace web {

namespace {

void addProfileStepsToJSON(cJSON* parent, const char* key, const std::vector<config::ProfileStep>& steps) {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& s : steps) {
        cJSON* step = cJSON_CreateObject();
        cJSON_AddNumberToObject(step, "temp", ApiContext::round1(s.temp));
        cJSON_AddNumberToObject(step, "time", s.time);
        cJSON_AddNumberToObject(step, "ramp", ApiContext::round2(s.ramp));
        cJSON_AddItemToArray(arr, step);
    }
    cJSON_AddItemToObject(parent, key, arr);
}

void parseProfileStepsFromJSON(cJSON* arr, std::vector<config::ProfileStep>& out) {
    if (cJSON_IsArray(arr) == 0) {
        return;
    }
    int sz = cJSON_GetArraySize(arr);
    for (int i = 0; i < sz; ++i) {
        cJSON* s = cJSON_GetArrayItem(arr, i);
        if (!s) {
            continue;
        }
        config::ProfileStep step;
        cJSON* t = cJSON_GetObjectItem(s, "temp");
        if (!t) {
            t = cJSON_GetObjectItem(s, "targetTemp");
        }
        cJSON* tm = cJSON_GetObjectItem(s, "time");
        if (!tm) {
            tm = cJSON_GetObjectItem(s, "durationS");
        }
        cJSON* r = cJSON_GetObjectItem(s, "ramp");
        if (!r) {
            r = cJSON_GetObjectItem(s, "rampRate");
        }

        if ((t != nullptr) && (cJSON_IsNumber(t) != 0)) {
            step.temp = static_cast<float>(ApiContext::round1(t->valuedouble));
        }
        if ((tm != nullptr) && (cJSON_IsNumber(tm) != 0)) {
            step.time = static_cast<uint32_t>(tm->valueint);
        }
        if ((r != nullptr) && (cJSON_IsNumber(r) != 0)) {
            step.ramp = static_cast<float>(ApiContext::round2(r->valuedouble));
        }
        out.push_back(step);
    }
}

std::string urlDecode(const std::string& input) {
    std::string decoded;
    decoded.reserve(input.length());
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '%' && i + 2 < input.length()) {
            std::array<char, 3> hex = {input[i + 1], input[i + 2], 0};
            char* end = nullptr;
            long val = strtol(hex.data(), &end, 16);
            if (end == hex.data() + 2) {
                decoded += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        decoded += input[i];
    }
    return decoded;
}

bool isMismatchedProfilePayload(const cJSON* root) {
    if (root == nullptr) {
        return false;
    }
    return (cJSON_GetObjectItem(root, "maxTempTop") != nullptr ||
            cJSON_GetObjectItem(root, "maxTempBottom") != nullptr ||
            cJSON_GetObjectItem(root, "holdLowTolerance") != nullptr ||
            cJSON_GetObjectItem(root, "topBurstWindowMs") != nullptr ||
            cJSON_GetObjectItem(root, "simulationMode") != nullptr ||
            cJSON_GetObjectItem(root, "hardwareBuzzerEnabled") != nullptr ||
            cJSON_GetObjectItem(root, "topPoints") != nullptr || cJSON_GetObjectItem(root, "bottomPoints") != nullptr ||
            cJSON_GetObjectItem(root, "topKp") != nullptr || cJSON_GetObjectItem(root, "topKi") != nullptr);
}

} // anonymous namespace

std::string ProfileRoutes::extractProfileFilenameFromUri(const std::string& uriStr, const std::string& queryStr) {
    std::string filename;
    if (!uriStr.empty()) {
        size_t qPos = uriStr.find('?');
        std::string pathOnly = (qPos != std::string::npos) ? uriStr.substr(0, qPos) : uriStr;

        const char* prefix = "/api/profiles/";
        size_t prefixLen = strlen(prefix);
        const char* legacyPrefix = "/api/profile/";
        size_t legacyLen = strlen(legacyPrefix);

        if (pathOnly.length() > prefixLen && pathOnly.starts_with(prefix)) {
            filename = pathOnly.substr(prefixLen);
        } else if (pathOnly.length() > legacyLen && pathOnly.starts_with(legacyPrefix)) {
            filename = pathOnly.substr(legacyLen);
        }
    }

    if (filename.empty() && !queryStr.empty()) {
        std::array<char, 64> buf{};
        if (httpd_query_key_value(queryStr.c_str(), "file", buf.data(), buf.size()) == ESP_OK ||
            httpd_query_key_value(queryStr.c_str(), "name", buf.data(), buf.size()) == ESP_OK) {
            filename = buf.data();
        }
    }

    if (filename.empty()) {
        return "";
    }

    filename = urlDecode(filename);
    if (!filename.ends_with(".json")) {
        filename += ".json";
    }

    return filename;
}

std::string ProfileRoutes::extractProfileFilename(httpd_req_t* req) {
    if (req == nullptr) {
        return "";
    }
    std::string uri = req->uri ? req->uri : "";
    std::string query;
    std::array<char, 128> param{};
    if (httpd_req_get_url_query_str(req, param.data(), param.size()) == ESP_OK) {
        query = param.data();
    }
    return extractProfileFilenameFromUri(uri, query);
}

namespace {

esp_err_t serveSingleProfile(httpd_req_t* req, const std::string& singleFile, const ApiContext& ctx) {
    config::ReflowProfile prof;
    if (!ctx.storage->loadProfile(singleFile, prof)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Profile not found");
        return ESP_OK;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schemaVersion", config::Schema::REFLOW_PROFILE);
    cJSON_AddStringToObject(root, "name", prof.name.c_str());
    cJSON_AddStringToObject(root, "file", prof.file.c_str());
    addProfileStepsToJSON(root, "stepsBottom", prof.stepsBottom);
    addProfileStepsToJSON(root, "stepsTop", prof.stepsTop);

    char* rendered = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rendered);
    cJSON_free(rendered);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t serveProfileList(httpd_req_t* req, const ApiContext& ctx) {
    auto profiles = ctx.storage->listProfiles();
    cJSON* root = cJSON_CreateArray();
    for (const auto& filename : profiles) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "file", filename.c_str());
        std::string displayName = filename;
        if (displayName.length() > 5 && displayName.substr(displayName.length() - 5) == ".json") {
            displayName = displayName.substr(0, displayName.length() - 5);
        }
        cJSON_AddStringToObject(item, "name", displayName.c_str());
        cJSON_AddItemToArray(root, item);
    }

    char* rendered = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rendered);
    cJSON_free(rendered);
    cJSON_Delete(root);
    return ESP_OK;
}

} // anonymous namespace

esp_err_t ProfileRoutes::getProfilesHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    std::string singleFile = extractProfileFilename(req);
    if (!singleFile.empty() && singleFile != ".json") {
        return serveSingleProfile(req, singleFile, ctx);
    }
    return serveProfileList(req, ctx);
}

esp_err_t ProfileRoutes::getProfileHandler(httpd_req_t* req) {
    return getProfilesHandler(req);
}

esp_err_t ProfileRoutes::postProfileHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    if (ctx.fsm != nullptr) {
        const auto st = ctx.fsm->getState();
        const bool canModify =
            (st == fsm::ReflowState::IDLE || st == fsm::ReflowState::DONE || st == fsm::ReflowState::BACKUP);
        if (!canModify) {
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                "Forbidden: Profile modification blocked during active process");
            return ESP_OK;
        }
    }

    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty payload");
        return ESP_OK;
    }
    if (req->content_len > config::Limits::MAX_JSON_PAYLOAD_BYTES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large (max 64 KB)");
        return ESP_OK;
    }

    std::string body = ApiContext::readRequestBody(req);
    if (body.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read request body");
        return ESP_OK;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    // Mismatch validation: reject if payload is machine settings or PID library
    if (isMismatchedProfilePayload(root)) {
        cJSON_Delete(root);
        httpd_resp_send_err(
            req, HTTPD_400_BAD_REQUEST,
            "Invalid payload: File contains machine settings or PID library instead of a reflow profile");
        return ESP_OK;
    }

    config::ReflowProfile prof;
    cJSON* nameItem = cJSON_GetObjectItem(root, "name");
    cJSON* fileItem = cJSON_GetObjectItem(root, "file");
    if (nameItem != nullptr && nameItem->valuestring != nullptr) {
        prof.name = nameItem->valuestring;
    }
    if (fileItem != nullptr && fileItem->valuestring != nullptr) {
        prof.file = fileItem->valuestring;
    } else {
        prof.file = prof.name + ".json";
    }

    cJSON* botArr = cJSON_GetObjectItem(root, "stepsBottom");
    if (!botArr) {
        botArr = cJSON_GetObjectItem(root, "bottomSteps");
    }
    cJSON* topArr = cJSON_GetObjectItem(root, "stepsTop");
    if (!topArr) {
        topArr = cJSON_GetObjectItem(root, "topSteps");
    }

    parseProfileStepsFromJSON(botArr, prof.stepsBottom);
    parseProfileStepsFromJSON(topArr, prof.stepsTop);
    cJSON_Delete(root);

    auto vres = prof.validate();
    if (!vres.valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            vres.errorMessage ? vres.errorMessage : "Invalid reflow profile");
        return ESP_OK;
    }

    if (prof.file == "factory-profile.json" || prof.file == "factory-profile") {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Cannot overwrite protected factory profile");
        return ESP_OK;
    }

    config::ReflowProfile existingProf;
    bool isNewProfile = !ctx.storage->loadProfile(prof.file, existingProf);
    if (isNewProfile && ctx.storage->isStorageFullForNewFiles()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Storage limit of 90% reached. Save rejected to preserve flash wear leveling.");
        return ESP_OK;
    }

    if (ctx.storage->saveProfile(prof)) {
        return ApiContext::sendJsonSuccess(req);
    }

    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save profile");
    return ESP_OK;
}

esp_err_t ProfileRoutes::deleteProfileHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    if (ctx.fsm != nullptr) {
        const auto st = ctx.fsm->getState();
        const bool canDelete =
            (st == fsm::ReflowState::IDLE || st == fsm::ReflowState::DONE || st == fsm::ReflowState::BACKUP);
        if (!canDelete) {
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Profile deletion blocked during active process");
            return ESP_OK;
        }
    }

    std::string filename = extractProfileFilename(req);
    if (filename.empty() || filename == ".json") {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile filename");
        return ESP_OK;
    }

    if (filename == "factory-profile.json") {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Cannot delete protected factory profile");
        return ESP_OK;
    }

    if (ctx.storage->deleteProfile(filename)) {
        return ApiContext::sendJsonSuccess(req);
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Profile not found or delete failed");
    return ESP_OK;
}

} // namespace web
