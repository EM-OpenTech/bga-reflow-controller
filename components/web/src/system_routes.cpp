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
 * @file system_routes.cpp
 * @brief Implementation of system diagnostics, settings, theme, and security REST routes.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/system_routes.hpp"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "storage/json_serializer.hpp"
#include "system_context.hpp"
#include "web/api_context.hpp"
#include "web/fsm_command_queue.hpp"
#include <array>
#include <vector>

namespace web {

namespace {

constexpr const char* TAG = "SystemRoutes";

inline void addBool(cJSON* obj, const char* name, bool value) {
    cJSON_AddBoolToObject(obj, name, value ? 1 : 0);
}

void buildSystemInfo(cJSON* root) {
    const esp_app_desc_t* appDesc = esp_app_get_description();
    cJSON_AddStringToObject(root, "firmwareVersion", appDesc ? appDesc->version : "0.9.0-rc1");
    cJSON_AddStringToObject(root, "idfVersion", appDesc ? appDesc->idf_ver : "6.0.2");
    cJSON_AddStringToObject(root, "buildDate", appDesc ? appDesc->date : "");
    cJSON_AddStringToObject(root, "buildTime", appDesc ? appDesc->time : "");
}

void buildStateInfo(cJSON* root, const ApiContext& ctx) {
    cJSON_AddStringToObject(root, "stateStr", ctx.fsm ? ctx.fsm->getStateString() : "UNKNOWN");
    cJSON_AddNumberToObject(root, "stateEnum", ctx.fsm ? static_cast<int>(ctx.fsm->getState()) : 0);
    cJSON_AddStringToObject(root, "activeProfile", ctx.fsm ? ctx.fsm->getActiveProfileFile().c_str() : "");
    addBool(root, "backupTaken", ctx.backupTaken);
    addBool(root, "schemaIncompatible", ctx.storage ? ctx.storage->hasSchemaIncompatibility() : false);
}

void buildMemoryInfo(cJSON* root, const TaskHandles& handles) {
    cJSON* mem = cJSON_CreateObject();
    cJSON_AddNumberToObject(mem, "freeHeap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(mem, "minFreeHeap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(mem, "largestFreeBlock", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON_AddItemToObject(root, "memory", mem);

    cJSON* taskHeadroom = cJSON_CreateObject();
    if (handles.safetyTask) {
        cJSON_AddNumberToObject(taskHeadroom, "safety", uxTaskGetStackHighWaterMark(handles.safetyTask));
    }
    if (handles.burstfireTask) {
        cJSON_AddNumberToObject(taskHeadroom, "burstfire", uxTaskGetStackHighWaterMark(handles.burstfireTask));
    }
    if (handles.controlTask) {
        cJSON_AddNumberToObject(taskHeadroom, "control", uxTaskGetStackHighWaterMark(handles.controlTask));
    }
    if (handles.inputTask) {
        cJSON_AddNumberToObject(taskHeadroom, "input", uxTaskGetStackHighWaterMark(handles.inputTask));
    }
    if (handles.webTask) {
        cJSON_AddNumberToObject(taskHeadroom, "web", uxTaskGetStackHighWaterMark(handles.webTask));
    }
    cJSON_AddItemToObject(root, "taskStackHeadroom", taskHeadroom);
}

void buildPostDiagnostics(cJSON* root, const PostReportInfo& post) {
    cJSON* postObj = cJSON_CreateObject();
    cJSON_AddNumberToObject(postObj, "resetReason", post.resetReason);
    cJSON_AddStringToObject(postObj, "resetReasonStr", post.resetReasonStr ? post.resetReasonStr : "UNKNOWN");
    addBool(postObj, "wasWatchdogReset", post.wasWatchdogReset);
    addBool(postObj, "wasBrownoutReset", post.wasBrownoutReset);
    cJSON_AddNumberToObject(postObj, "freeHeapBytes", post.freeHeapBytes);
    cJSON_AddNumberToObject(postObj, "freePsramBytes", post.freePsramBytes);
    addBool(postObj, "littleFsOk", post.littleFsOk);
    cJSON_AddNumberToObject(postObj, "littleFsFreeBytes", post.littleFsFreeBytes);
    addBool(postObj, "topSensorOk", post.topSensorOk);
    cJSON_AddNumberToObject(postObj, "topCjTemp", ApiContext::round1(post.topCjTemp));
    cJSON_AddNumberToObject(postObj, "topRawTemp", ApiContext::round1(post.topRawTemp));
    addBool(postObj, "bottomSensorOk", post.bottomSensorOk);
    cJSON_AddNumberToObject(postObj, "bottomCjTemp", ApiContext::round1(post.bottomCjTemp));
    cJSON_AddNumberToObject(postObj, "bottomRawTemp", ApiContext::round1(post.bottomRawTemp));
    addBool(postObj, "outputsSafe", post.outputsSafe);
    addBool(postObj, "allPassed", post.allPassed);
    cJSON_AddItemToObject(root, "post", postObj);
}

void buildStorageDiagnostics(cJSON* root, const storage::StorageManager* storage) {
    cJSON* storageObj = cJSON_CreateObject();
    size_t totalStorage = 0;
    size_t usedStorage = 0;
    bool storageOk = storage ? storage->getStorageInfo(totalStorage, usedStorage) : false;
    float usedPct = (storageOk && totalStorage > 0)
                        ? (static_cast<float>(usedStorage) / static_cast<float>(totalStorage)) * 100.0f
                        : 0.0f;

    addBool(storageObj, "mounted", storageOk);
    cJSON_AddNumberToObject(storageObj, "totalBytes", totalStorage);
    cJSON_AddNumberToObject(storageObj, "usedBytes", usedStorage);
    cJSON_AddNumberToObject(storageObj, "usedPercent", ApiContext::round1(usedPct));
    cJSON_AddNumberToObject(storageObj, "warnThreshold", ApiContext::round1(config::Limits::STORAGE_WARN_PERCENT));
    cJSON_AddNumberToObject(storageObj, "criticalThreshold", ApiContext::round1(config::Limits::STORAGE_MAX_PERCENT));
    addBool(storageObj, "isWarning", usedPct >= config::Limits::STORAGE_WARN_PERCENT);
    addBool(storageObj, "isCritical", usedPct >= config::Limits::STORAGE_MAX_PERCENT);
    cJSON_AddItemToObject(root, "storage", storageObj);
}

void buildLimitsAndResolutions(cJSON* root) {
    cJSON* limits = cJSON_CreateObject();
    cJSON_AddNumberToObject(limits, "MIN_TEMPERATURE", ApiContext::round1(config::Limits::MIN_TEMPERATURE));
    cJSON_AddNumberToObject(limits, "MAX_TEMPERATURE", ApiContext::round1(config::Limits::MAX_TEMPERATURE));
    cJSON_AddNumberToObject(limits, "MIN_SAFE_COOLING_TEMP", ApiContext::round1(config::Limits::MIN_SAFE_COOLING_TEMP));
    cJSON_AddNumberToObject(limits, "MAX_SAFE_COOLING_TEMP", ApiContext::round1(config::Limits::MAX_SAFE_COOLING_TEMP));
    cJSON_AddNumberToObject(limits, "MIN_RAMP_RATE", ApiContext::round2(config::Limits::MIN_RAMP_RATE));
    cJSON_AddNumberToObject(limits, "MAX_RAMP_RATE", ApiContext::round2(config::Limits::MAX_RAMP_RATE));
    cJSON_AddNumberToObject(limits, "MAX_STEP_TIME_S", config::Limits::MAX_STEP_TIME_S);
    cJSON_AddNumberToObject(limits, "MIN_STUCK_SSR_RISE", ApiContext::round2(config::Limits::MIN_STUCK_SSR_RISE));
    cJSON_AddNumberToObject(limits, "MAX_STUCK_SSR_RISE", ApiContext::round2(config::Limits::MAX_STUCK_SSR_RISE));
    cJSON_AddNumberToObject(limits, "MIN_STUCK_SSR_SEC", config::Limits::MIN_STUCK_SSR_SEC);
    cJSON_AddNumberToObject(limits, "MAX_STUCK_SSR_SEC", config::Limits::MAX_STUCK_SSR_SEC);
    cJSON_AddNumberToObject(limits, "MIN_NO_RISE_THRESH", ApiContext::round2(config::Limits::MIN_NO_RISE_THRESH));
    cJSON_AddNumberToObject(limits, "MAX_NO_RISE_THRESH", ApiContext::round2(config::Limits::MAX_NO_RISE_THRESH));
    cJSON_AddNumberToObject(limits, "MIN_NO_RISE_TIMEOUT_S", config::Limits::MIN_NO_RISE_TIMEOUT_S);
    cJSON_AddNumberToObject(limits, "MAX_NO_RISE_TIMEOUT_S", config::Limits::MAX_NO_RISE_TIMEOUT_S);
    cJSON_AddNumberToObject(limits, "MIN_HOLD_TOLERANCE", ApiContext::round2(config::Limits::MIN_HOLD_TOLERANCE));
    cJSON_AddNumberToObject(limits, "MAX_HOLD_TOLERANCE", ApiContext::round2(config::Limits::MAX_HOLD_TOLERANCE));
    cJSON_AddNumberToObject(limits, "MIN_SETTLE_S", config::Limits::MIN_SETTLE_S);
    cJSON_AddNumberToObject(limits, "MAX_SETTLE_S", config::Limits::MAX_SETTLE_S);
    cJSON_AddNumberToObject(limits, "MIN_FAN_DELAY_S", config::Limits::MIN_FAN_DELAY_S);
    cJSON_AddNumberToObject(limits, "MAX_FAN_DELAY_S", config::Limits::MAX_FAN_DELAY_S);
    cJSON_AddNumberToObject(limits, "MIN_FAN_DURATION_S", config::Limits::MIN_FAN_DURATION_S);
    cJSON_AddNumberToObject(limits, "MAX_FAN_DURATION_S", config::Limits::MAX_FAN_DURATION_S);
    cJSON_AddNumberToObject(limits, "MIN_EMA_ALPHA", ApiContext::round3(config::Limits::MIN_EMA_ALPHA));
    cJSON_AddNumberToObject(limits, "MAX_EMA_ALPHA", ApiContext::round3(config::Limits::MAX_EMA_ALPHA));
    cJSON_AddNumberToObject(limits, "MIN_FAULT_STREAK", config::Limits::MIN_FAULT_STREAK);
    cJSON_AddNumberToObject(limits, "MAX_FAULT_STREAK", config::Limits::MAX_FAULT_STREAK);
    cJSON_AddNumberToObject(limits, "MIN_CJ_OFFSET", ApiContext::round1(config::Limits::MIN_CJ_OFFSET));
    cJSON_AddNumberToObject(limits, "MAX_CJ_OFFSET", ApiContext::round1(config::Limits::MAX_CJ_OFFSET));
    cJSON_AddNumberToObject(limits, "MIN_BURST_WINDOW_MS", config::Limits::MIN_BURST_WINDOW_MS);
    cJSON_AddNumberToObject(limits, "MAX_BURST_WINDOW_MS", config::Limits::MAX_BURST_WINDOW_MS);
    cJSON_AddNumberToObject(limits, "MIN_PID_KP", ApiContext::round2(config::Limits::MIN_PID_KP));
    cJSON_AddNumberToObject(limits, "MAX_PID_KP", ApiContext::round2(config::Limits::MAX_PID_KP));
    cJSON_AddNumberToObject(limits, "MIN_PID_KI", ApiContext::round3(config::Limits::MIN_PID_KI));
    cJSON_AddNumberToObject(limits, "MAX_PID_KI", ApiContext::round3(config::Limits::MAX_PID_KI));
    cJSON_AddNumberToObject(limits, "MIN_PID_KD", ApiContext::round2(config::Limits::MIN_PID_KD));
    cJSON_AddNumberToObject(limits, "MAX_PID_KD", ApiContext::round2(config::Limits::MAX_PID_KD));
    cJSON_AddNumberToObject(limits, "MAX_PROFILE_STEPS", config::Limits::MAX_PROFILE_STEPS);
    cJSON_AddNumberToObject(limits, "MAX_PID_POINTS", config::Limits::MAX_PID_POINTS);
    cJSON_AddNumberToObject(limits, "MAX_HISTORY_POINTS", config::Limits::MAX_HISTORY_POINTS);
    cJSON_AddNumberToObject(limits, "MAX_JSON_SIZE_BYTES", config::Limits::MAX_JSON_PAYLOAD_BYTES);
    cJSON_AddNumberToObject(limits, "MAX_ZIP_SIZE_BYTES", config::Limits::MAX_ZIP_PAYLOAD_BYTES);
    cJSON_AddNumberToObject(limits, "STORAGE_WARN_PERCENT", ApiContext::round1(config::Limits::STORAGE_WARN_PERCENT));
    cJSON_AddNumberToObject(limits, "STORAGE_MAX_PERCENT", ApiContext::round1(config::Limits::STORAGE_MAX_PERCENT));
    cJSON_AddItemToObject(root, "limits", limits);

    cJSON* resolutions = cJSON_CreateObject();
    cJSON_AddNumberToObject(resolutions, "TEMPERATURE", ApiContext::round1(config::Resolution::TEMPERATURE));
    cJSON_AddNumberToObject(resolutions, "RAMP_RATE", ApiContext::round2(config::Resolution::RAMP_RATE));
    cJSON_AddNumberToObject(resolutions, "PID_GAIN_KP", ApiContext::round2(config::Resolution::PID_GAIN_KP));
    cJSON_AddNumberToObject(resolutions, "PID_GAIN_KI", ApiContext::round3(config::Resolution::PID_GAIN_KI));
    cJSON_AddNumberToObject(resolutions, "PID_GAIN_KD", ApiContext::round2(config::Resolution::PID_GAIN_KD));
    cJSON_AddNumberToObject(resolutions, "FILTER_ALPHA", ApiContext::round3(config::Resolution::FILTER_ALPHA));
    cJSON_AddNumberToObject(resolutions, "OFFSET_TEMP", ApiContext::round1(config::Resolution::OFFSET_TEMP));
    cJSON_AddNumberToObject(resolutions, "TOLERANCE_TEMP", ApiContext::round1(config::Resolution::TOLERANCE_TEMP));
    cJSON_AddNumberToObject(resolutions, "TIME_SEC", config::Resolution::TIME_SEC);
    cJSON_AddNumberToObject(resolutions, "TIME_MS", config::Resolution::TIME_MS);
    cJSON_AddItemToObject(root, "resolutions", resolutions);
}

void parseSettingsFromRoot(const cJSON* root, config::MachineSettings& settings) {
    auto parseBool = [&](const char* key, bool& target) {
        const cJSON* item = cJSON_GetObjectItem(root, key);
        if (item != nullptr) {
            target = (cJSON_IsTrue(item) != 0);
        }
    };
    auto parseString = [&](const char* key, std::string& target) {
        const cJSON* item = cJSON_GetObjectItem(root, key);
        if (item != nullptr && item->valuestring != nullptr) {
            target = item->valuestring;
        }
    };
    auto parseFloat = [&](const char* key, float& target, double (*rounder)(double)) {
        const cJSON* item = cJSON_GetObjectItem(root, key);
        if (item != nullptr && (cJSON_IsNumber(item) != 0) && !std::isnan(item->valuedouble) &&
            !std::isinf(item->valuedouble)) {
            target = static_cast<float>(rounder(item->valuedouble));
        }
    };
    auto parseUint32 = [&](const char* key, uint32_t& target) {
        const cJSON* item = cJSON_GetObjectItem(root, key);
        if (item != nullptr && (cJSON_IsNumber(item) != 0) && item->valuedouble >= 0.0) {
            target = static_cast<uint32_t>(item->valueint);
        }
    };
    auto parseUint8 = [&](const char* key, uint8_t& target) {
        const cJSON* item = cJSON_GetObjectItem(root, key);
        if (item != nullptr && (cJSON_IsNumber(item) != 0) && item->valuedouble >= 0.0) {
            target = static_cast<uint8_t>(item->valueint);
        }
    };

    parseBool("simulationMode", settings.simulationMode);
    parseString("language", settings.language);
    parseString("theme", settings.theme);
    parseBool("hardwareBuzzerEnabled", settings.hardwareBuzzerEnabled);
    parseString("defaultProfile", settings.defaultProfile);
    parseBool("showZones", settings.showZones);
    parseBool("showTalLine", settings.showTalLine);
    parseBool("showStepMarkers", settings.showStepMarkers);
    parseBool("showPidGains", settings.showPidGains);

    parseFloat("maxTempTop", settings.maxTempTop, ApiContext::round1);
    parseFloat("maxTempBottom", settings.maxTempBottom, ApiContext::round1);
    parseFloat("minTempTop", settings.minTempTop, ApiContext::round1);
    parseFloat("minTempBottom", settings.minTempBottom, ApiContext::round1);
    parseFloat("coolingSafeTemp", settings.coolingSafeTemp, ApiContext::round1);
    parseBool("enableSafetyWatchdog", settings.enableSafetyWatchdog);

    parseBool("enableStuckSsrCheck", settings.enableStuckSsrCheck);
    parseFloat("stuckSsrRiseThreshold", settings.stuckSsrRiseThreshold, ApiContext::round2);
    parseUint32("stuckSsrWindowSec", settings.stuckSsrWindowSec);

    parseBool("enableNoRiseCheck", settings.enableNoRiseCheck);
    parseFloat("noRiseThreshold", settings.noRiseThreshold, ApiContext::round2);
    parseUint32("noRiseTimeoutSec", settings.noRiseTimeoutSec);

    parseFloat("holdLowTolerance", settings.holdLowTolerance, ApiContext::round2);
    parseFloat("holdHighTolerance", settings.holdHighTolerance, ApiContext::round2);
    parseUint32("settleTimeS", settings.settleTimeS);

    parseUint32("fanCoolingDelayS", settings.fanCoolingDelayS);
    parseUint32("fanCoolingDurationS", settings.fanCoolingDurationS);

    parseBool("emaFilterEnabled", settings.emaFilterEnabled);
    parseFloat("emaAlpha", settings.emaAlpha, ApiContext::round3);
    parseUint8("faultStreakLimit", settings.faultStreakLimit);
    parseFloat("topCjOffset", settings.topCjOffset, ApiContext::round1);
    parseFloat("bottomCjOffset", settings.bottomCjOffset, ApiContext::round1);

    parseUint32("topBurstWindowMs", settings.topBurstWindowMs);
    parseUint32("bottomBurstWindowMs", settings.bottomBurstWindowMs);

    parseBool("pidLibraryEnabled", settings.pidLibraryEnabled);
    parseFloat("topKp", settings.topKp, ApiContext::round2);
    parseFloat("topKi", settings.topKi, ApiContext::round3);
    parseFloat("topKd", settings.topKd, ApiContext::round2);
    parseFloat("bottomKp", settings.bottomKp, ApiContext::round2);
    parseFloat("bottomKi", settings.bottomKi, ApiContext::round3);
    parseFloat("bottomKd", settings.bottomKd, ApiContext::round2);
}

bool isMismatchedSettingsPayload(const cJSON* root) {
    if (root == nullptr) {
        return false;
    }
    return (cJSON_GetObjectItem(root, "stepsTop") != nullptr || cJSON_GetObjectItem(root, "stepsBottom") != nullptr ||
            cJSON_GetObjectItem(root, "topSteps") != nullptr || cJSON_GetObjectItem(root, "bottomSteps") != nullptr ||
            cJSON_GetObjectItem(root, "topPoints") != nullptr || cJSON_GetObjectItem(root, "bottomPoints") != nullptr ||
            (cJSON_GetObjectItem(root, "top") != nullptr && cJSON_GetObjectItem(root, "bottom") != nullptr &&
             cJSON_GetObjectItem(root, "topKp") == nullptr));
}

bool isMismatchedPidLibraryPayload(const cJSON* root) {
    if (root == nullptr) {
        return false;
    }
    return (cJSON_GetObjectItem(root, "maxTempTop") != nullptr ||
            cJSON_GetObjectItem(root, "maxTempBottom") != nullptr || cJSON_GetObjectItem(root, "stepsTop") != nullptr ||
            cJSON_GetObjectItem(root, "stepsBottom") != nullptr || cJSON_GetObjectItem(root, "topSteps") != nullptr ||
            cJSON_GetObjectItem(root, "bottomSteps") != nullptr ||
            cJSON_GetObjectItem(root, "holdLowTolerance") != nullptr ||
            cJSON_GetObjectItem(root, "simulationMode") != nullptr ||
            cJSON_GetObjectItem(root, "hardwareBuzzerEnabled") != nullptr ||
            cJSON_GetObjectItem(root, "topBurstWindowMs") != nullptr);
}

bool isModificationPermitted(const fsm::ReflowFSM* fsm) {
    if (fsm == nullptr) {
        return true;
    }
    const auto st = fsm->getState();
    return (st == fsm::ReflowState::IDLE || st == fsm::ReflowState::DONE || st == fsm::ReflowState::BACKUP ||
            st == fsm::ReflowState::FAULT);
}

} // anonymous namespace

esp_err_t SystemRoutes::getStatusHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.fsm == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "FSM not ready");
        return ESP_OK;
    }

    cJSON* root = cJSON_CreateObject();
    buildSystemInfo(root);
    buildStateInfo(root, ctx);
    buildMemoryInfo(root, ctx.taskHandles);
    buildPostDiagnostics(root, ctx.postReport);
    buildStorageDiagnostics(root, ctx.storage);
    buildLimitsAndResolutions(root);

    char* rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (rendered == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rendered);
    cJSON_free(rendered);
    return ESP_OK;
}

esp_err_t SystemRoutes::getSettingsHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.storage == nullptr && ctx.settings == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    config::MachineSettings settings;
    bool loaded = false;
    if (ctx.settings != nullptr) {
        settings = *ctx.settings;
        loaded = true;
    } else if (ctx.storage != nullptr) {
        loaded = ctx.storage->loadSettings(settings);
    }

    std::string jsonStr;
    if (!loaded || !storage::JsonSerializer::serializeSettings(settings, jsonStr)) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, jsonStr.c_str());
    return ESP_OK;
}

esp_err_t SystemRoutes::postSettingsHandler(httpd_req_t* req) {
    auto& ctx = ApiContext::instance();
    if (ctx.storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    if (!isModificationPermitted(ctx.fsm)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Settings modification blocked during active process");
        return ESP_OK;
    }

    if (req->content_len == 0 || req->content_len > config::Limits::MAX_JSON_PAYLOAD_BYTES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload size");
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

    // Mismatch validation: reject if payload is actually a Reflow Profile or PID Library
    if (isMismatchedSettingsPayload(root)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Invalid payload: File contains profile or PID library data instead of machine settings");
        return ESP_OK;
    }

    config::MachineSettings settings;
    if (ctx.settings != nullptr) {
        settings = *ctx.settings;
    } else {
        ctx.storage->loadSettings(settings); // Load existing defaults
    }
    parseSettingsFromRoot(root, settings);
    cJSON_Delete(root);

    auto vres = settings.validate();
    if (!vres.valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            vres.errorMessage ? vres.errorMessage : "Invalid machine settings");
        return ESP_OK;
    }

    if (ctx.storage->saveSettings(settings)) {
        if (ctx.settings != nullptr) {
            *ctx.settings = settings;
        }
        ctx.pendingSettingsSave = false;

        app::FsmCommand cmd;
        cmd.type = app::FsmCommandType::RELOAD_SETTINGS;
        static_cast<void>(app::fsmCmdPost(cmd));

        ESP_LOGI(TAG, "Settings saved to Flash and RELOAD_SETTINGS dispatched to control_task.");
        return ApiContext::sendJsonSuccess(req);
    }

    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save settings");
    return ESP_OK;
}

esp_err_t SystemRoutes::getThemeHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.storage == nullptr && ctx.settings == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    config::MachineSettings settings;
    if (ctx.settings != nullptr) {
        settings = *ctx.settings;
    } else if (ctx.storage != nullptr) {
        ctx.storage->loadSettings(settings);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "theme", settings.theme.c_str());
    addBool(root, "showZones", settings.showZones);
    addBool(root, "showTalLine", settings.showTalLine);
    addBool(root, "showStepMarkers", settings.showStepMarkers);
    addBool(root, "showPidGains", settings.showPidGains);
    char* rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (rendered == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rendered);
    cJSON_free(rendered);
    return ESP_OK;
}

esp_err_t SystemRoutes::postThemeHandler(httpd_req_t* req) {
    auto& ctx = ApiContext::instance();
    if (ctx.storage == nullptr && ctx.settings == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    std::string body = ApiContext::readRequestBody(req);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    config::MachineSettings settings;
    if (ctx.settings != nullptr) {
        settings = *ctx.settings;
    } else if (ctx.storage != nullptr) {
        ctx.storage->loadSettings(settings);
    }

    bool changed = false;
    const cJSON* themeItem = cJSON_GetObjectItem(root, "theme");
    if (themeItem != nullptr && (cJSON_IsString(themeItem) != 0)) {
        settings.theme = themeItem->valuestring;
        changed = true;
    }

    auto updateBool = [&](const char* key, bool& target) {
        const cJSON* item = cJSON_GetObjectItem(root, key);
        if (item != nullptr && (cJSON_IsBool(item) != 0)) {
            target = (cJSON_IsTrue(item) != 0);
            changed = true;
        }
    };

    updateBool("showZones", settings.showZones);
    updateBool("showTalLine", settings.showTalLine);
    updateBool("showStepMarkers", settings.showStepMarkers);
    updateBool("showPidGains", settings.showPidGains);

    if (changed) {
        if (ctx.settings != nullptr) {
            ctx.settings->theme = settings.theme;
            ctx.settings->showZones = settings.showZones;
            ctx.settings->showTalLine = settings.showTalLine;
            ctx.settings->showStepMarkers = settings.showStepMarkers;
            ctx.settings->showPidGains = settings.showPidGains;
        }
        if (isModificationPermitted(ctx.fsm)) {
            if (ctx.storage != nullptr) {
                ctx.storage->saveSettings(settings);
            }
            ctx.pendingSettingsSave = false;
        } else {
            ctx.pendingSettingsSave = true;
            ESP_LOGI(TAG, "Theme / display option updated in UI; Flash commit deferred until process completes.");
        }
    }
    cJSON_Delete(root);

    return ApiContext::sendJsonSuccess(req);
}

esp_err_t SystemRoutes::getPidLibraryHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.storage == nullptr && ctx.fsm == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    config::PidLibrary library;
    bool loaded = false;
    if (ctx.fsm != nullptr) {
        library = ctx.fsm->getPidLibrary();
        loaded = true;
    } else if (ctx.storage != nullptr) {
        loaded = ctx.storage->loadPidLibrary(library);
    }

    if (!loaded) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, R"({"top":[],"bottom":[]})");
        return ESP_OK;
    }

    std::string outJson;
    if (!storage::JsonSerializer::serializePidLibrary(library, outJson)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize PID library");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, outJson.c_str());
    return ESP_OK;
}

esp_err_t SystemRoutes::postPidLibraryHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    if (!isModificationPermitted(ctx.fsm)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                            "Forbidden: PID library modification blocked during active process");
        return ESP_OK;
    }

    if (req->content_len == 0 || req->content_len > config::Limits::MAX_JSON_PAYLOAD_BYTES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload size");
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

    // Mismatch validation: reject if payload is machine settings or Reflow Profile
    if (isMismatchedPidLibraryPayload(root)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Invalid payload: File contains machine settings or profile data instead of a PID library");
        return ESP_OK;
    }

    config::PidLibrary library;
    auto parsePoints = [](cJSON* arr, std::vector<config::PidPoint>& out) {
        if (arr == nullptr || (cJSON_IsArray(arr) == 0)) {
            return;
        }

        auto parseField = [](cJSON* obj, const char* key, double (*rounder)(double), float& outVal) {
            cJSON* val = cJSON_GetObjectItem(obj, key);
            if (val != nullptr && (cJSON_IsNumber(val) != 0) && !std::isnan(val->valuedouble) &&
                !std::isinf(val->valuedouble)) {
                outVal = static_cast<float>(rounder(val->valuedouble));
            }
        };

        const int maxCount = static_cast<int>(config::Limits::MAX_PID_POINTS + 1);
        const int count = std::min(cJSON_GetArraySize(arr), maxCount);
        for (int i = 0; i < count; i++) {
            cJSON* item = cJSON_GetArrayItem(arr, i);
            if (item == nullptr) {
                continue;
            }
            config::PidPoint pt;
            parseField(item, "temp", ApiContext::round1, pt.temp);
            parseField(item, "kp", ApiContext::round2, pt.kp);
            parseField(item, "ki", ApiContext::round3, pt.ki);
            parseField(item, "kd", ApiContext::round2, pt.kd);
            out.push_back(pt);
        }
    };

    cJSON* topArr = cJSON_GetObjectItem(root, "top");
    if (topArr == nullptr) {
        topArr = cJSON_GetObjectItem(root, "topPoints");
    }
    cJSON* botArr = cJSON_GetObjectItem(root, "bottom");
    if (botArr == nullptr) {
        botArr = cJSON_GetObjectItem(root, "bottomPoints");
    }

    parsePoints(topArr, library.top);
    parsePoints(botArr, library.bottom);
    cJSON_Delete(root);

    auto vres = library.validate();
    if (!vres.valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            vres.errorMessage != nullptr ? vres.errorMessage : "Invalid PID library");
        return ESP_OK;
    }

    bool ok = ctx.storage->savePidLibrary(library);
    if (ok && (ctx.fsm != nullptr)) {
        auto state = ctx.fsm->getState();
        if (state == fsm::ReflowState::IDLE || state == fsm::ReflowState::DONE || state == fsm::ReflowState::FAULT) {
            ctx.fsm->setPidLibrary(library);
            ESP_LOGI(TAG, "PID Library applied immediately to RAM (FSM is %s).", ctx.fsm->getStateString());
        } else {
            ESP_LOGI(TAG, "PID Library saved to Flash; RAM update deferred until active process returns to IDLE.");
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? R"({"success":true})" : R"({"success":false})");
    return ESP_OK;
}

esp_err_t SystemRoutes::getHistoryHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.context == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Context not ready");
        return ESP_OK;
    }

    std::vector<app::SystemContextData::TelemetryHistoryPoint> pointsCopy;
    std::vector<fsm::StepMarker> markersCopy;
    std::string profileName;
    std::string stateStr;
    uint32_t elapsedSec = 0;
    uint32_t talSec = 0;
    int stateEnum = 0;

    if (ctx.context->lock(50)) {
        const auto& data = ctx.context->getData();
        pointsCopy = data.history;
        markersCopy = data.stepMarkers;
        profileName = data.activeProfileFile;
        stateStr = data.stateStr;
        stateEnum = static_cast<int>(data.state);
        elapsedSec = data.elapsedSec;
        talSec = data.talSec;
        ctx.context->unlock();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");

    std::array<char, 256> chunk{};
    int len = snprintf(chunk.data(), chunk.size(),
                       R"({"profileFile":"%s","state":"%s","stateEnum":%d,"elapsedSec":%lu,"talSec":%lu,"markers":[)",
                       profileName.c_str(), stateStr.c_str(), stateEnum, static_cast<unsigned long>(elapsedSec),
                       static_cast<unsigned long>(talSec));
    if (httpd_resp_send_chunk(req, chunk.data(), len) != ESP_OK) {
        return ESP_FAIL;
    }

    for (size_t i = 0; i < markersCopy.size(); ++i) {
        const auto& m = markersCopy[i];
        len = snprintf(chunk.data(), chunk.size(), R"(%s{"step":%u,"temp":%.1f,"time":%lu,"isTop":%s})",
                       (i > 0 ? "," : ""), static_cast<unsigned>(m.stepIndex), static_cast<double>(m.targetTemp),
                       static_cast<unsigned long>(m.timeS), (m.isTop ? "true" : "false"));
        if (httpd_resp_send_chunk(req, chunk.data(), len) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    len = snprintf(chunk.data(), chunk.size(), R"(],"points":[)");
    if (httpd_resp_send_chunk(req, chunk.data(), len) != ESP_OK) {
        return ESP_FAIL;
    }

    for (size_t i = 0; i < pointsCopy.size(); ++i) {
        const auto& p = pointsCopy[i];
        len =
            snprintf(chunk.data(), chunk.size(), "%s[%u,%.1f,%.1f,%.1f,%.1f]", (i > 0 ? "," : ""),
                     static_cast<unsigned>(p.timeS), static_cast<double>(p.topTemp), static_cast<double>(p.bottomTemp),
                     static_cast<double>(p.topSet), static_cast<double>(p.bottomSet));
        if (httpd_resp_send_chunk(req, chunk.data(), len) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    len = snprintf(chunk.data(), chunk.size(), "]}");
    if (httpd_resp_send_chunk(req, chunk.data(), len) != ESP_OK) {
        return ESP_FAIL;
    }
    httpd_resp_send_chunk(req, nullptr, 0); // End chunked response
    return ESP_OK;
}

esp_err_t SystemRoutes::getSecurityStatusHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.wifi == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi not ready");
        return ESP_OK;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", ctx.wifi->getSsid().c_str());
    addBool(root, "passwordChanged", ctx.wifi->isPasswordChanged());

    char* rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (rendered == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rendered);
    cJSON_free(rendered);
    return ESP_OK;
}

esp_err_t SystemRoutes::postSecurityWifiHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.wifi == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi not ready");
        return ESP_OK;
    }

    if (ctx.fsm != nullptr) {
        const auto st = ctx.fsm->getState();
        if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE) {
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                "Forbidden: Wi-Fi password modification blocked during active process or maintenance");
            return ESP_OK;
        }
    }

    std::string body = ApiContext::readRequestBody(req);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    const cJSON* keepDefault = cJSON_GetObjectItem(root, "keepDefault");
    const cJSON* newPass = cJSON_GetObjectItem(root, "password");

    if (keepDefault != nullptr && (cJSON_IsTrue(keepDefault) != 0)) {
        ctx.wifi->confirmDefaultPassword();
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, R"({"success":true,"message":"Default-Passwort beibehalten."})");
        return ESP_OK;
    }

    if (newPass != nullptr && (newPass->valuestring != nullptr)) {
        std::string passStr = newPass->valuestring;
        esp_err_t err = ctx.wifi->updatePassword(passStr, false);
        cJSON_Delete(root);
        if (err != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Passwort ungültig (mindestens 8 Zeichen)");
            return ESP_OK;
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, R"({"success":true,"message":"Passwort geändert! Bitte verbinde dein Gerät neu mit )"
                                R"(dem WLAN 'BGA Reflow Controller' und dem neuen Passwort."})");

#ifdef ESP_PLATFORM
        // Allow HTTP 200 frame to leave the network stack before restarting SoftAP
        vTaskDelay(pdMS_TO_TICKS(250));
#endif
        ctx.wifi->applyLiveConfig();
        return ESP_OK;
    }

    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Fehlender Parameter");
    return ESP_OK;
}

esp_err_t SystemRoutes::postBackupCompleteHandler(httpd_req_t* req) {
    auto& ctx = ApiContext::instance();
    ctx.backupTaken = true;
    ESP_LOGI(TAG, "REST API: System backup recorded as completed for this instance.");
    return ApiContext::sendJsonSuccess(req, "\"backupTaken\":true");
}

} // namespace web
