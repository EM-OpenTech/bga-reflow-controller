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
 * @file rest_api.cpp
 * @brief Implementation of REST API Route Handlers.
 *
 * Implements endpoints for status polling, machine settings configuration,
 * profile management, manual overrides, history inspection, and OTA updates.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/rest_api.hpp"
#include "web/fsm_command_queue.hpp"
#include "system_context.hpp"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <cstring>
#include <string>
#include <sstream>
#include <cmath>

static inline double round1(double v) { return std::round(v * 10.0) / 10.0; }
static inline double round2(double v) { return std::round(v * 100.0) / 100.0; }
static inline double round3(double v) { return std::round(v * 1000.0) / 1000.0; }

static const char* TAG = "RestApi";

namespace web {

storage::StorageManager* RestApi::s_storage     = nullptr;
config::MachineSettings* RestApi::s_settings    = nullptr;
fsm::ReflowFSM*          RestApi::s_fsm         = nullptr;
WifiManager*             RestApi::s_wifi        = nullptr;
app::SystemContext*      RestApi::s_context     = nullptr;
bool                     RestApi::s_backupTaken = false;
TaskHandles              RestApi::s_taskHandles = {};

void RestApi::init(storage::StorageManager* storage,
                   config::MachineSettings* settings,
                   fsm::ReflowFSM*         fsm,
                   WifiManager*            wifi,
                   app::SystemContext*     context)
{
    s_storage  = storage;
    s_settings = settings;
    s_fsm      = fsm;
    s_wifi     = wifi;
    s_context  = context;
}

void RestApi::setTaskHandles(const TaskHandles& handles)
{
    s_taskHandles = handles;
}

std::string RestApi::readRequestBody(httpd_req_t *req)
{
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
        cur_len += received;
    }
    return body;
}

// ============================================================================
// GET /api/status
// ============================================================================
// Lightweight one-shot sync endpoint called ONCE by the frontend on WS connect/reconnect.
// Purpose:
//   - activeProfile: sync the profile dropdown (NOT in the WS stream by design)
//   - stateEnum/stateStr: pre-populate button states before the first WS frame (500ms)
//   - backupTaken: instance runtime tracking (resets to false on reboot)
// NOT included: elapsedSec — changes every second, immediately superseded by WS telemetry.
esp_err_t RestApi::getStatusHandler(httpd_req_t *req)
{
    if (s_fsm == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "FSM not ready");
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "activeProfile", s_fsm->getActiveProfileFile().c_str());
    cJSON_AddNumberToObject(root, "stateEnum",     static_cast<int>(s_fsm->getState()));
    cJSON_AddStringToObject(root, "stateStr",      s_fsm->getStateString());
    cJSON_AddBoolToObject(root,   "backupTaken",   s_backupTaken);

    const esp_app_desc_t* appDesc = esp_app_get_description();
    cJSON_AddStringToObject(root, "firmwareVersion", appDesc ? appDesc->version : "0.9.0-rc1");
    cJSON_AddStringToObject(root, "idfVersion",      appDesc ? appDesc->idf_ver : "6.0.2");
    cJSON_AddStringToObject(root, "buildDate",       appDesc ? appDesc->date : "");
    cJSON_AddStringToObject(root, "buildTime",       appDesc ? appDesc->time : "");
    cJSON_AddBoolToObject(root,   "schemaIncompatible", s_storage ? s_storage->hasSchemaIncompatibility() : false);

    // Expose central validation limits as Single Source of Truth for frontend UI
    cJSON* limits = cJSON_CreateObject();
    cJSON_AddNumberToObject(limits, "MIN_TEMPERATURE",       round1(config::Limits::MIN_TEMPERATURE));
    cJSON_AddNumberToObject(limits, "MAX_TEMPERATURE",       round1(config::Limits::MAX_TEMPERATURE));
    cJSON_AddNumberToObject(limits, "MIN_SAFE_COOLING_TEMP", round1(config::Limits::MIN_SAFE_COOLING_TEMP));
    cJSON_AddNumberToObject(limits, "MAX_SAFE_COOLING_TEMP", round1(config::Limits::MAX_SAFE_COOLING_TEMP));
    cJSON_AddNumberToObject(limits, "MIN_RAMP_RATE",         round2(config::Limits::MIN_RAMP_RATE));
    cJSON_AddNumberToObject(limits, "MAX_RAMP_RATE",         round2(config::Limits::MAX_RAMP_RATE));
    cJSON_AddNumberToObject(limits, "MAX_STEP_TIME_S",       config::Limits::MAX_STEP_TIME_S);
    cJSON_AddNumberToObject(limits, "MIN_STUCK_SSR_RISE",    round2(config::Limits::MIN_STUCK_SSR_RISE));
    cJSON_AddNumberToObject(limits, "MAX_STUCK_SSR_RISE",    round2(config::Limits::MAX_STUCK_SSR_RISE));
    cJSON_AddNumberToObject(limits, "MIN_STUCK_SSR_SEC",     config::Limits::MIN_STUCK_SSR_SEC);
    cJSON_AddNumberToObject(limits, "MAX_STUCK_SSR_SEC",     config::Limits::MAX_STUCK_SSR_SEC);
    cJSON_AddNumberToObject(limits, "MIN_NO_RISE_THRESH",    round2(config::Limits::MIN_NO_RISE_THRESH));
    cJSON_AddNumberToObject(limits, "MAX_NO_RISE_THRESH",    round2(config::Limits::MAX_NO_RISE_THRESH));
    cJSON_AddNumberToObject(limits, "MIN_NO_RISE_TIMEOUT_S", config::Limits::MIN_NO_RISE_TIMEOUT_S);
    cJSON_AddNumberToObject(limits, "MAX_NO_RISE_TIMEOUT_S", config::Limits::MAX_NO_RISE_TIMEOUT_S);
    cJSON_AddNumberToObject(limits, "MIN_HOLD_TOLERANCE",    round2(config::Limits::MIN_HOLD_TOLERANCE));
    cJSON_AddNumberToObject(limits, "MAX_HOLD_TOLERANCE",    round2(config::Limits::MAX_HOLD_TOLERANCE));
    cJSON_AddNumberToObject(limits, "MIN_SETTLE_S",          config::Limits::MIN_SETTLE_S);
    cJSON_AddNumberToObject(limits, "MAX_SETTLE_S",          config::Limits::MAX_SETTLE_S);
    cJSON_AddNumberToObject(limits, "MIN_FAN_DELAY_S",       config::Limits::MIN_FAN_DELAY_S);
    cJSON_AddNumberToObject(limits, "MAX_FAN_DELAY_S",       config::Limits::MAX_FAN_DELAY_S);
    cJSON_AddNumberToObject(limits, "MIN_FAN_DURATION_S",    config::Limits::MIN_FAN_DURATION_S);
    cJSON_AddNumberToObject(limits, "MAX_FAN_DURATION_S",    config::Limits::MAX_FAN_DURATION_S);
    cJSON_AddNumberToObject(limits, "MIN_EMA_ALPHA",         round3(config::Limits::MIN_EMA_ALPHA));
    cJSON_AddNumberToObject(limits, "MAX_EMA_ALPHA",         round3(config::Limits::MAX_EMA_ALPHA));
    cJSON_AddNumberToObject(limits, "MIN_FAULT_STREAK",      config::Limits::MIN_FAULT_STREAK);
    cJSON_AddNumberToObject(limits, "MAX_FAULT_STREAK",      config::Limits::MAX_FAULT_STREAK);
    cJSON_AddNumberToObject(limits, "MIN_CJ_OFFSET",         round1(config::Limits::MIN_CJ_OFFSET));
    cJSON_AddNumberToObject(limits, "MAX_CJ_OFFSET",         round1(config::Limits::MAX_CJ_OFFSET));
    cJSON_AddNumberToObject(limits, "MIN_BURST_WINDOW_MS",   config::Limits::MIN_BURST_WINDOW_MS);
    cJSON_AddNumberToObject(limits, "MAX_BURST_WINDOW_MS",   config::Limits::MAX_BURST_WINDOW_MS);
    cJSON_AddNumberToObject(limits, "MIN_PID_KP",            round2(config::Limits::MIN_PID_KP));
    cJSON_AddNumberToObject(limits, "MAX_PID_KP",            round2(config::Limits::MAX_PID_KP));
    cJSON_AddNumberToObject(limits, "MIN_PID_KI",            round3(config::Limits::MIN_PID_KI));
    cJSON_AddNumberToObject(limits, "MAX_PID_KI",            round3(config::Limits::MAX_PID_KI));
    cJSON_AddNumberToObject(limits, "MIN_PID_KD",            round2(config::Limits::MIN_PID_KD));
    cJSON_AddNumberToObject(limits, "MAX_PID_KD",            round2(config::Limits::MAX_PID_KD));
    cJSON_AddNumberToObject(limits, "MAX_PROFILE_STEPS",     config::Limits::MAX_PROFILE_STEPS);
    cJSON_AddNumberToObject(limits, "MAX_PID_POINTS",        config::Limits::MAX_PID_POINTS);
    cJSON_AddNumberToObject(limits, "MAX_HISTORY_POINTS",    config::Limits::MAX_HISTORY_POINTS);
    cJSON_AddNumberToObject(limits, "MAX_JSON_SIZE_BYTES",   config::Limits::MAX_JSON_PAYLOAD_BYTES);
    cJSON_AddNumberToObject(limits, "MAX_ZIP_SIZE_BYTES",    config::Limits::MAX_ZIP_PAYLOAD_BYTES);
    cJSON_AddItemToObject(root, "limits", limits);

    // Expose central step resolutions as Single Source of Truth for frontend UI
    cJSON* resolutions = cJSON_CreateObject();
    cJSON_AddNumberToObject(resolutions, "TEMPERATURE",    round1(config::Resolution::TEMPERATURE));
    cJSON_AddNumberToObject(resolutions, "RAMP_RATE",      round2(config::Resolution::RAMP_RATE));
    cJSON_AddNumberToObject(resolutions, "PID_GAIN_KP",    round2(config::Resolution::PID_GAIN_KP));
    cJSON_AddNumberToObject(resolutions, "PID_GAIN_KI",    round3(config::Resolution::PID_GAIN_KI));
    cJSON_AddNumberToObject(resolutions, "PID_GAIN_KD",    round2(config::Resolution::PID_GAIN_KD));
    cJSON_AddNumberToObject(resolutions, "FILTER_ALPHA",   round3(config::Resolution::FILTER_ALPHA));
    cJSON_AddNumberToObject(resolutions, "OFFSET_TEMP",    round1(config::Resolution::OFFSET_TEMP));
    cJSON_AddNumberToObject(resolutions, "TOLERANCE_TEMP", round1(config::Resolution::TOLERANCE_TEMP));
    cJSON_AddNumberToObject(resolutions, "TIME_SEC",       config::Resolution::TIME_SEC);
    cJSON_AddNumberToObject(resolutions, "TIME_MS",        config::Resolution::TIME_MS);
    cJSON_AddItemToObject(root, "resolutions", resolutions);

    // FreeRTOS System Memory Diagnostics
    cJSON* mem = cJSON_CreateObject();
    cJSON_AddNumberToObject(mem, "freeHeap",         esp_get_free_heap_size());
    cJSON_AddNumberToObject(mem, "minFreeHeap",      esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(mem, "largestFreeBlock",  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON_AddItemToObject(root, "memory", mem);

    // FreeRTOS Task Minimum Remaining Stack (High-Watermark in Bytes)
    cJSON* taskHeadroom = cJSON_CreateObject();
    if (s_taskHandles.safetyTask) {
        cJSON_AddNumberToObject(taskHeadroom, "safety", uxTaskGetStackHighWaterMark(s_taskHandles.safetyTask));
    }
    if (s_taskHandles.burstfireTask) {
        cJSON_AddNumberToObject(taskHeadroom, "burstfire", uxTaskGetStackHighWaterMark(s_taskHandles.burstfireTask));
    }
    if (s_taskHandles.controlTask) {
        cJSON_AddNumberToObject(taskHeadroom, "control", uxTaskGetStackHighWaterMark(s_taskHandles.controlTask));
    }
    if (s_taskHandles.inputTask) {
        cJSON_AddNumberToObject(taskHeadroom, "input", uxTaskGetStackHighWaterMark(s_taskHandles.inputTask));
    }
    if (s_taskHandles.webTask) {
        cJSON_AddNumberToObject(taskHeadroom, "web", uxTaskGetStackHighWaterMark(s_taskHandles.webTask));
    }
    cJSON_AddItemToObject(root, "taskStackHeadroom", taskHeadroom);

    char *rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rendered);
    cJSON_free(rendered);
    return ESP_OK;
}

// ============================================================================
// GET /api/settings
// ============================================================================
esp_err_t RestApi::getSettingsHandler(httpd_req_t *req)
{
    if (s_storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    config::MachineSettings settings;
    if (s_storage->loadSettings(settings)) {
        // Build JSON representation
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "schemaVersion", config::Schema::MACHINE_SETTINGS);
        // System & UI Preferences
        cJSON_AddBoolToObject(root, "simulationMode", settings.simulationMode);
        cJSON_AddStringToObject(root, "language", settings.language.c_str());
        cJSON_AddStringToObject(root, "theme", settings.theme.c_str());
        cJSON_AddBoolToObject(root, "hardwareBuzzerEnabled", settings.hardwareBuzzerEnabled);
        cJSON_AddStringToObject(root, "defaultProfile", settings.defaultProfile.c_str());
        cJSON_AddBoolToObject(root, "showZones", settings.showZones);
        cJSON_AddBoolToObject(root, "showTalLine", settings.showTalLine);
        cJSON_AddBoolToObject(root, "showStepMarkers", settings.showStepMarkers);
        cJSON_AddBoolToObject(root, "showPidGains", settings.showPidGains);

        // Thermal Safety Limits (1 decimal place)
        cJSON_AddNumberToObject(root, "maxTempTop", round1(settings.maxTempTop));
        cJSON_AddNumberToObject(root, "maxTempBottom", round1(settings.maxTempBottom));
        cJSON_AddNumberToObject(root, "minTempTop", round1(settings.minTempTop));
        cJSON_AddNumberToObject(root, "minTempBottom", round1(settings.minTempBottom));
        cJSON_AddNumberToObject(root, "coolingSafeTemp", round1(settings.coolingSafeTemp));
        cJSON_AddBoolToObject(root, "enableSafetyWatchdog", settings.enableSafetyWatchdog);

        // Stuck SSR Watchdog
        cJSON_AddBoolToObject(root, "enableStuckSsrCheck", settings.enableStuckSsrCheck);
        cJSON_AddNumberToObject(root, "stuckSsrRiseThreshold", round2(settings.stuckSsrRiseThreshold));
        cJSON_AddNumberToObject(root, "stuckSsrWindowSec", settings.stuckSsrWindowSec);

        // Heater No-Rise Watchdog
        cJSON_AddBoolToObject(root, "enableNoRiseCheck", settings.enableNoRiseCheck);
        cJSON_AddNumberToObject(root, "noRiseThreshold", round2(settings.noRiseThreshold));
        cJSON_AddNumberToObject(root, "noRiseTimeoutSec", settings.noRiseTimeoutSec);

        // FSM Hold Gate & Settle Tolerances
        cJSON_AddNumberToObject(root, "holdLowTolerance", round2(settings.holdLowTolerance));
        cJSON_AddNumberToObject(root, "holdHighTolerance", round2(settings.holdHighTolerance));
        cJSON_AddNumberToObject(root, "settleTimeS", settings.settleTimeS);

        // Process Timings & Fan
        cJSON_AddNumberToObject(root, "fanCoolingDelayS", settings.fanCoolingDelayS);
        cJSON_AddNumberToObject(root, "fanCoolingDurationS", settings.fanCoolingDurationS);

        // Sensor Configuration (MAX31856)
        cJSON_AddBoolToObject(root, "emaFilterEnabled", settings.emaFilterEnabled);
        cJSON_AddNumberToObject(root, "emaAlpha", round3(settings.emaAlpha));
        cJSON_AddNumberToObject(root, "faultStreakLimit", settings.faultStreakLimit);
        cJSON_AddNumberToObject(root, "topCjOffset", round1(settings.topCjOffset));
        cJSON_AddNumberToObject(root, "bottomCjOffset", round1(settings.bottomCjOffset));

        // SSR Burst-Fire Windows
        cJSON_AddNumberToObject(root, "topBurstWindowMs", settings.topBurstWindowMs);
        cJSON_AddNumberToObject(root, "bottomBurstWindowMs", settings.bottomBurstWindowMs);

        // PID Standard Gains
        cJSON_AddBoolToObject(root, "pidLibraryEnabled", settings.pidLibraryEnabled);
        cJSON_AddNumberToObject(root, "topKp", round2(settings.topKp));
        cJSON_AddNumberToObject(root, "topKi", round3(settings.topKi));
        cJSON_AddNumberToObject(root, "topKd", round2(settings.topKd));
        cJSON_AddNumberToObject(root, "bottomKp", round2(settings.bottomKp));
        cJSON_AddNumberToObject(root, "bottomKi", round3(settings.bottomKi));
        cJSON_AddNumberToObject(root, "bottomKd", round2(settings.bottomKd));

        char* rendered = cJSON_PrintUnformatted(root);
        httpd_resp_sendstr(req, rendered);
        cJSON_free(rendered);
        cJSON_Delete(root);
    } else {
        httpd_resp_sendstr(req, "{}");
    }
    return ESP_OK;
}

// ============================================================================
// POST /api/settings
// ============================================================================
esp_err_t RestApi::postSettingsHandler(httpd_req_t *req)
{
    if (s_storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    // 0. State Guard: Reject modifications during active reflow / autotune
    if (s_fsm != nullptr) {
        auto st = s_fsm->getState();
        if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE && st != fsm::ReflowState::BACKUP) {
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Settings modification blocked during active process");
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

    std::string body = readRequestBody(req);
    if (body.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read request body");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    // 1. Mismatch validation: reject if payload is actually a Reflow Profile or PID Library
    if (cJSON_GetObjectItem(root, "stepsTop") || cJSON_GetObjectItem(root, "stepsBottom") ||
        cJSON_GetObjectItem(root, "topSteps") || cJSON_GetObjectItem(root, "bottomSteps") ||
        cJSON_GetObjectItem(root, "topPoints") || cJSON_GetObjectItem(root, "bottomPoints") ||
        (cJSON_GetObjectItem(root, "top") && cJSON_GetObjectItem(root, "bottom") && !cJSON_GetObjectItem(root, "topKp"))) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload: File contains profile or PID library data instead of machine settings");
        return ESP_OK;
    }

    config::MachineSettings settings;
    s_storage->loadSettings(settings); // Load existing defaults

    // Parse updated fields
    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "simulationMode"))) settings.simulationMode = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "language")) && item->valuestring) settings.language = item->valuestring;
    if ((item = cJSON_GetObjectItem(root, "theme")) && item->valuestring) settings.theme = item->valuestring;
    if ((item = cJSON_GetObjectItem(root, "hardwareBuzzerEnabled"))) settings.hardwareBuzzerEnabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "defaultProfile")) && item->valuestring) settings.defaultProfile = item->valuestring;
    if ((item = cJSON_GetObjectItem(root, "showZones"))) settings.showZones = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "showTalLine"))) settings.showTalLine = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "showStepMarkers"))) settings.showStepMarkers = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "showPidGains"))) settings.showPidGains = cJSON_IsTrue(item);

    if ((item = cJSON_GetObjectItem(root, "maxTempTop"))) settings.maxTempTop = (float)round1(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "maxTempBottom"))) settings.maxTempBottom = (float)round1(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "minTempTop"))) settings.minTempTop = (float)round1(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "minTempBottom"))) settings.minTempBottom = (float)round1(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "coolingSafeTemp"))) settings.coolingSafeTemp = (float)round1(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "enableSafetyWatchdog"))) settings.enableSafetyWatchdog = cJSON_IsTrue(item);

    if ((item = cJSON_GetObjectItem(root, "enableStuckSsrCheck"))) settings.enableStuckSsrCheck = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "stuckSsrRiseThreshold"))) settings.stuckSsrRiseThreshold = (float)round2(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "stuckSsrWindowSec"))) settings.stuckSsrWindowSec = (uint32_t)item->valueint;

    if ((item = cJSON_GetObjectItem(root, "enableNoRiseCheck"))) settings.enableNoRiseCheck = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "noRiseThreshold"))) settings.noRiseThreshold = (float)round2(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "noRiseTimeoutSec"))) settings.noRiseTimeoutSec = (uint32_t)item->valueint;

    if ((item = cJSON_GetObjectItem(root, "holdLowTolerance"))) settings.holdLowTolerance = (float)round2(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "holdHighTolerance"))) settings.holdHighTolerance = (float)round2(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "settleTimeS"))) settings.settleTimeS = (uint32_t)item->valueint;

    if ((item = cJSON_GetObjectItem(root, "fanCoolingDelayS"))) settings.fanCoolingDelayS = (uint32_t)item->valueint;
    if ((item = cJSON_GetObjectItem(root, "fanCoolingDurationS"))) settings.fanCoolingDurationS = (uint32_t)item->valueint;

    if ((item = cJSON_GetObjectItem(root, "emaFilterEnabled"))) settings.emaFilterEnabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "emaAlpha"))) settings.emaAlpha = (float)round3(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "faultStreakLimit"))) settings.faultStreakLimit = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItem(root, "topCjOffset"))) settings.topCjOffset = (float)round1(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "bottomCjOffset"))) settings.bottomCjOffset = (float)round1(item->valuedouble);

    if ((item = cJSON_GetObjectItem(root, "topBurstWindowMs"))) settings.topBurstWindowMs = (uint32_t)item->valueint;
    if ((item = cJSON_GetObjectItem(root, "bottomBurstWindowMs"))) settings.bottomBurstWindowMs = (uint32_t)item->valueint;

    if ((item = cJSON_GetObjectItem(root, "pidLibraryEnabled"))) settings.pidLibraryEnabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "topKp"))) settings.topKp = (float)round2(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "topKi"))) settings.topKi = (float)round3(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "topKd"))) settings.topKd = (float)round2(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "bottomKp"))) settings.bottomKp = (float)round2(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "bottomKi"))) settings.bottomKi = (float)round3(item->valuedouble);
    if ((item = cJSON_GetObjectItem(root, "bottomKd"))) settings.bottomKd = (float)round2(item->valuedouble);

    cJSON_Delete(root);

    // Sanity range validation using domain model
    auto vres = settings.validate();
    if (!vres.valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, vres.errorMessage ? vres.errorMessage : "Invalid machine settings");
        return ESP_OK;
    }

    if (s_storage->saveSettings(settings)) {
        // Post thread-safe command to control_task (Core 1) to synchronize RAM & actuators
        app::FsmCommand cmd;
        cmd.type = app::FsmCommandType::RELOAD_SETTINGS;
        app::fsmCmdPost(cmd);

        ESP_LOGI(TAG, "Settings saved to Flash and RELOAD_SETTINGS dispatched to control_task.");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save settings");
    }
    return ESP_OK;
}

std::string RestApi::extractProfileFilenameFromUri(const std::string& uriStr, const std::string& queryStr)
{
    std::string filename;
    if (!uriStr.empty()) {
        // Strip query params (?...)
        size_t qPos = uriStr.find('?');
        std::string pathOnly = (qPos != std::string::npos) ? uriStr.substr(0, qPos) : uriStr;

        const char* prefix = "/api/profiles/";
        size_t prefixLen = strlen(prefix);
        if (pathOnly.length() > prefixLen && pathOnly.rfind(prefix, 0) == 0) {
            filename = pathOnly.substr(prefixLen);
        } else {
            const char* legacyPrefix = "/api/profile/";
            size_t legacyLen = strlen(legacyPrefix);
            if (pathOnly.length() > legacyLen && pathOnly.rfind(legacyPrefix, 0) == 0) {
                filename = pathOnly.substr(legacyLen);
            }
        }
    }

    // Fallback: check query string
    if (filename.empty() && !queryStr.empty()) {
        char buf[64] = {0};
        if (httpd_query_key_value(queryStr.c_str(), "file", buf, sizeof(buf)) == ESP_OK ||
            httpd_query_key_value(queryStr.c_str(), "name", buf, sizeof(buf)) == ESP_OK) {
            filename = buf;
        }
    }

    // URL decode if needed (e.g. %20 -> space)
    if (!filename.empty()) {
        std::string decoded;
        decoded.reserve(filename.length());
        for (size_t i = 0; i < filename.length(); ++i) {
            if (filename[i] == '%' && i + 2 < filename.length()) {
                char hex[3] = { filename[i+1], filename[i+2], 0 };
                char* end = nullptr;
                long val = strtol(hex, &end, 16);
                if (end == hex + 2) {
                    decoded += static_cast<char>(val);
                    i += 2;
                    continue;
                }
            }
            decoded += filename[i];
        }
        filename = decoded;
    }

    // Auto-append .json if not present
    if (!filename.empty() && (filename.length() <= 5 || filename.substr(filename.length() - 5) != ".json")) {
        filename += ".json";
    }

    return filename;
}

static std::string extractProfileFilename(httpd_req_t *req)
{
    if (!req) return "";
    std::string uri = (req != nullptr) ? req->uri : "";
    std::string query;
    char param[128] = {0};
    if (httpd_req_get_url_query_str(req, param, sizeof(param)) == ESP_OK) {
        query = param;
    }
    return RestApi::extractProfileFilenameFromUri(uri, query);
}

static void addProfileStepsToJSON(cJSON *parent, const char *key, const std::vector<config::ProfileStep> &steps)
{
    cJSON *arr = cJSON_CreateArray();
    for (const auto& s : steps) {
        cJSON *step = cJSON_CreateObject();
        cJSON_AddNumberToObject(step, "temp", round1(s.temp));
        cJSON_AddNumberToObject(step, "time", s.time);
        cJSON_AddNumberToObject(step, "ramp", round2(s.ramp));
        cJSON_AddItemToArray(arr, step);
    }
    cJSON_AddItemToObject(parent, key, arr);
}

static void parseProfileStepsFromJSON(cJSON *arr, std::vector<config::ProfileStep> &out)
{
    if (!cJSON_IsArray(arr)) return;
    int sz = cJSON_GetArraySize(arr);
    for (int i = 0; i < sz; ++i) {
        cJSON *s = cJSON_GetArrayItem(arr, i);
        if (!s) continue;
        config::ProfileStep step;
        cJSON *t  = cJSON_GetObjectItem(s, "temp");
        if (!t) t = cJSON_GetObjectItem(s, "targetTemp");
        cJSON *tm = cJSON_GetObjectItem(s, "time");
        if (!tm) tm = cJSON_GetObjectItem(s, "durationS");
        cJSON *r  = cJSON_GetObjectItem(s, "ramp");
        if (!r) r = cJSON_GetObjectItem(s, "rampRate");

        if (t  && cJSON_IsNumber(t))  step.temp = (float)round1(t->valuedouble);
        if (tm && cJSON_IsNumber(tm)) step.time = (uint32_t)tm->valueint;
        if (r  && cJSON_IsNumber(r))  step.ramp = (float)round2(r->valuedouble);
        out.push_back(step);
    }
}

// ============================================================================
// GET /api/profiles & GET /api/profiles/{filename}
// ============================================================================
esp_err_t RestApi::getProfilesHandler(httpd_req_t *req)
{
    if (s_storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    std::string singleFile = extractProfileFilename(req);
    if (!singleFile.empty() && singleFile != ".json") {
        config::ReflowProfile prof;
        if (s_storage->loadProfile(singleFile, prof)) {
            // Return Profile JSON
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "schemaVersion", config::Schema::REFLOW_PROFILE);
            cJSON_AddStringToObject(root, "name", prof.name.c_str());
            cJSON_AddStringToObject(root, "file", prof.file.c_str());
            addProfileStepsToJSON(root, "stepsBottom", prof.stepsBottom);
            addProfileStepsToJSON(root, "stepsTop",    prof.stepsTop);

            char* rendered = cJSON_PrintUnformatted(root);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, rendered);
            cJSON_free(rendered);
            cJSON_Delete(root);
            return ESP_OK;
        } else {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Profile not found");
            return ESP_OK;
        }
    }

    // List all profiles
    auto profiles = s_storage->listProfiles();
    cJSON *root = cJSON_CreateArray();
    for (const auto& filename : profiles) {
        cJSON *item = cJSON_CreateObject();
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

// ============================================================================
// GET /api/profile (Legacy query-param fallback)
// ============================================================================
esp_err_t RestApi::getProfileHandler(httpd_req_t *req)
{
    return getProfilesHandler(req);
}

// ============================================================================
// POST /api/profiles & POST /api/profile
// ============================================================================
esp_err_t RestApi::postProfileHandler(httpd_req_t *req)
{
    if (s_storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    // 0. State Guard: Reject profile modifications during active process
    if (s_fsm != nullptr) {
        auto st = s_fsm->getState();
        if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE && st != fsm::ReflowState::BACKUP) {
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Profile modification blocked during active process");
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

    std::string body = readRequestBody(req);
    if (body.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read request body");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    // 1. Mismatch validation: reject if payload is machine settings or PID library
    if (cJSON_GetObjectItem(root, "maxTempTop") || cJSON_GetObjectItem(root, "maxTempBottom") ||
        cJSON_GetObjectItem(root, "holdLowTolerance") || cJSON_GetObjectItem(root, "topBurstWindowMs") ||
        cJSON_GetObjectItem(root, "simulationMode") || cJSON_GetObjectItem(root, "hardwareBuzzerEnabled") ||
        cJSON_GetObjectItem(root, "topPoints") || cJSON_GetObjectItem(root, "bottomPoints") ||
        cJSON_GetObjectItem(root, "topKp") || cJSON_GetObjectItem(root, "topKi")) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload: File contains machine settings or PID library instead of a reflow profile");
        return ESP_OK;
    }

    config::ReflowProfile prof;
    cJSON *nameItem = cJSON_GetObjectItem(root, "name");
    cJSON *fileItem = cJSON_GetObjectItem(root, "file");
    if (nameItem && nameItem->valuestring) prof.name = nameItem->valuestring;
    if (fileItem && fileItem->valuestring) {
        prof.file = fileItem->valuestring;
    } else {
        prof.file = prof.name + ".json";
    }

    cJSON *botArr = cJSON_GetObjectItem(root, "stepsBottom");
    if (!botArr) botArr = cJSON_GetObjectItem(root, "bottomSteps");
    cJSON *topArr = cJSON_GetObjectItem(root, "stepsTop");
    if (!topArr) topArr = cJSON_GetObjectItem(root, "topSteps");

    parseProfileStepsFromJSON(botArr, prof.stepsBottom);
    parseProfileStepsFromJSON(topArr, prof.stepsTop);
    cJSON_Delete(root);

    // Validate using domain model
    auto vres = prof.validate();
    if (!vres.valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, vres.errorMessage ? vres.errorMessage : "Invalid reflow profile");
        return ESP_OK;
    }

    if (s_storage->saveProfile(prof)) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save profile");
    }
    return ESP_OK;
}

// ============================================================================
// DELETE /api/profiles/{filename} & DELETE /api/profile?file=XYZ
// ============================================================================
esp_err_t RestApi::deleteProfileHandler(httpd_req_t *req)
{
    if (s_storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    // 0. State Guard: Reject profile deletion during active process
    if (s_fsm != nullptr) {
        auto st = s_fsm->getState();
        if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE && st != fsm::ReflowState::BACKUP) {
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

    if (s_storage->deleteProfile(filename)) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true}");
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Profile not found or delete failed");
    return ESP_OK;
}

// ============================================================================
// POST /api/control
// ============================================================================
esp_err_t RestApi::postControlHandler(httpd_req_t *req)
{
    if (s_fsm == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "FSM not ready");
        return ESP_OK;
    }

    std::string body = readRequestBody(req);
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *actionItem = cJSON_GetObjectItem(root, "action");
    cJSON *profileItem = cJSON_GetObjectItem(root, "profile");

    if (!actionItem || !actionItem->valuestring) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing action");
        return ESP_OK;
    }

    std::string action = actionItem->valuestring;
    ESP_LOGI(TAG, "REST API: Control action '%s' received", action.c_str());

    using app::FsmCommand;
    using app::FsmCommandType;
    using app::fsmCmdPost;

// ============================================================================
// Helper: post command or return 503
// ============================================================================
    // Fix 3: all commands report failure if the queue is full.
    auto postOrFail = [&](FsmCommand cmd) -> bool {
        if (!fsmCmdPost(cmd)) {
            cJSON_Delete(root);
            ESP_LOGE(TAG, "CMD queue full – '%s' dropped!", action.c_str());
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Command queue full");
            return false;
        }
        return true;
    };

    if (action == "preheat") {
        if (s_fsm != nullptr) {
            auto st = s_fsm->getState();
            if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE && st != fsm::ReflowState::COOLING) {
                cJSON_Delete(root);
                ESP_LOGW(TAG, "PREHEAT rejected: not permitted in state %s", s_fsm->getStateString());
                httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Preheat only allowed in IDLE, DONE or COOLING");
                return ESP_OK;
            }
        }

        std::string profName = (profileItem && profileItem->valuestring)
                               ? profileItem->valuestring : "";
        if (profName.empty() && s_context != nullptr) {
            profName = s_context->getSnapshot().activeProfileFile;
        }
        if (profName.empty()) {
            cJSON_Delete(root);
            ESP_LOGW(TAG, "PREHEAT rejected: No profile selected");
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No profile selected");
            return ESP_OK;
        }

        // Load profile on Core 0, transfer via pointer queue to Core 1
        auto* prof = new config::ReflowProfile();
        if (s_storage == nullptr || !s_storage->loadProfile(profName, *prof)) {
            delete prof;
            cJSON_Delete(root);
            ESP_LOGW(TAG, "PREHEAT: profile '%s' not found", profName.c_str());
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Profile not found");
            return ESP_OK;
        }

        FsmCommand cmd{};
        cmd.type    = FsmCommandType::PREHEAT;
        cmd.profile = prof;
        if (!fsmCmdPost(cmd)) {
            delete prof;  // Queue full – free immediately to avoid leak
            cJSON_Delete(root);
            ESP_LOGE(TAG, "PREHEAT command queue full – profile freed");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Command queue full");
            return ESP_OK;
        }
        ESP_LOGI(TAG, "CMD queued: PREHEAT profile '%s'", profName.c_str());

    } else if (action == "reflow") {
        if (s_fsm != nullptr) {
            auto st = s_fsm->getState();
            if (st != fsm::ReflowState::PREHEAT) {
                cJSON_Delete(root);
                ESP_LOGW(TAG, "REFLOW rejected: not permitted in state %s", s_fsm->getStateString());
                httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Reflow only permitted during PREHEAT");
                return ESP_OK;
            }
        }
        FsmCommand cmd{}; cmd.type = FsmCommandType::REFLOW;
        if (!postOrFail(cmd)) return ESP_OK;
        ESP_LOGI(TAG, "CMD queued: REFLOW");

    } else if (action == "stop" || action == "stopAutotune") {
        FsmCommand cmd{}; cmd.type = FsmCommandType::STOP;
        if (!postOrFail(cmd)) return ESP_OK;
        ESP_LOGI(TAG, "CMD queued: STOP");

    } else if (action == "autotune") {
        std::string channel = "top";
        cJSON* chJson = cJSON_GetObjectItem(root, "channel");
        if (chJson && chJson->valuestring) channel = chJson->valuestring;
        if (channel != "top" && channel != "bottom") {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid autotune channel (must be 'top' or 'bottom')");
            return ESP_OK;
        }
        bool isTop = (channel == "top");

        float targetTemp = 150.0f;
        cJSON* tempJson = cJSON_GetObjectItem(root, "targetTemp");
        if (tempJson && cJSON_IsNumber(tempJson)) targetTemp = (float)tempJson->valuedouble;
        if (targetTemp < config::Limits::MIN_TEMPERATURE || targetTemp > config::Limits::MAX_TEMPERATURE) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Autotune target temperature out of range (30..300°C)");
            return ESP_OK;
        }

        FsmCommand cmd{};
        cmd.type = FsmCommandType::AUTOTUNE_START;
        cmd.tuneIsTop = isTop;
        cmd.tuneTargetTemp = targetTemp;
        if (!postOrFail(cmd)) return ESP_OK;
        ESP_LOGI(TAG, "CMD queued: AUTOTUNE_START (channel=%s, targetTemp=%.1f°C)",
                 isTop ? "TOP" : "BOTTOM", targetTemp);

    } else if (action == "skip") {
        FsmCommand cmd{}; cmd.type = FsmCommandType::SKIP_STEP;
        if (!postOrFail(cmd)) return ESP_OK;
        ESP_LOGI(TAG, "CMD queued: SKIP_STEP");

    } else if (action == "resetFault") {
        FsmCommand cmd{}; cmd.type = FsmCommandType::RESET_FAULT;
        if (!postOrFail(cmd)) return ESP_OK;
        ESP_LOGI(TAG, "CMD queued: RESET_FAULT");

    } else if (action == "enterBackup") {
        if (s_fsm != nullptr) {
            auto st = s_fsm->getState();
            if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE && st != fsm::ReflowState::BACKUP) {
                cJSON_Delete(root);
                httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Cannot enter backup state while process is active");
                return ESP_OK;
            }
        }
        FsmCommand cmd{}; cmd.type = FsmCommandType::ENTER_BACKUP;
        if (!postOrFail(cmd)) return ESP_OK;
        ESP_LOGI(TAG, "CMD queued: ENTER_BACKUP");

    } else if (action == "exitBackup") {
        FsmCommand cmd{}; cmd.type = FsmCommandType::EXIT_BACKUP;
        if (!postOrFail(cmd)) return ESP_OK;
        ESP_LOGI(TAG, "CMD queued: EXIT_BACKUP");

    } else {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
        return ESP_OK;
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// ============================================================================
// POST /api/overrides
// ============================================================================
esp_err_t RestApi::postOverridesHandler(httpd_req_t *req)
{
    if (s_fsm == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "FSM not ready");
        return ESP_OK;
    }

    std::string body = readRequestBody(req);
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *fan = cJSON_GetObjectItem(root, "fan");
    cJSON *lamp = cJSON_GetObjectItem(root, "lamp");
    if (fan && cJSON_IsBool(fan)) {
        bool fanOn = cJSON_IsTrue(fan);
        s_fsm->setFanOverride(fanOn);
        ESP_LOGI(TAG, "REST Override: Fan set to %s", fanOn ? "ON" : "OFF");
    }
    if (lamp && cJSON_IsBool(lamp)) {
        bool lampOn = cJSON_IsTrue(lamp);
        s_fsm->setLampOverride(lampOn);
        ESP_LOGI(TAG, "REST Override: Lamp set to %s", lampOn ? "ON" : "OFF");
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// ============================================================================
// GET /api/history
// ============================================================================
esp_err_t RestApi::getHistoryHandler(httpd_req_t *req)
{
    if (s_context == nullptr) {
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

    if (s_context->lock(50)) {
        const auto& data = s_context->getData();
        pointsCopy   = data.history;
        markersCopy  = data.stepMarkers;
        profileName  = data.activeProfileFile;
        stateStr     = data.stateStr;
        stateEnum    = static_cast<int>(data.state);
        elapsedSec   = data.elapsedSec;
        talSec       = data.talSec;
        s_context->unlock();
    }

    httpd_resp_set_type(req, "application/json");

    // Build header JSON
    char chunk[256];
    int len = snprintf(chunk, sizeof(chunk),
        "{\"profileFile\":\"%s\",\"state\":\"%s\",\"stateEnum\":%d,\"elapsedSec\":%lu,\"talSec\":%lu,\"markers\":[",
        profileName.c_str(), stateStr.c_str(), stateEnum, (unsigned long)elapsedSec, (unsigned long)talSec);
    httpd_resp_send_chunk(req, chunk, len);

    // Stream step markers
    for (size_t i = 0; i < markersCopy.size(); ++i) {
        const auto& m = markersCopy[i];
        len = snprintf(chunk, sizeof(chunk),
            "%s{\"step\":%u,\"temp\":%.1f,\"time\":%lu,\"isTop\":%s}",
            (i > 0 ? "," : ""),
            (unsigned)m.stepIndex, m.targetTemp, (unsigned long)m.timeS, (m.isTop ? "true" : "false"));
        httpd_resp_send_chunk(req, chunk, len);
    }

    len = snprintf(chunk, sizeof(chunk), "],\"points\":[");
    httpd_resp_send_chunk(req, chunk, len);

    // Stream history points in format: [timeS, topTemp, botTemp, topSet, botSet]
    for (size_t i = 0; i < pointsCopy.size(); ++i) {
        const auto& p = pointsCopy[i];
        len = snprintf(chunk, sizeof(chunk),
            "%s[%u,%.1f,%.1f,%.1f,%.1f]",
            (i > 0 ? "," : ""),
            (unsigned)p.timeS, p.topTemp, p.bottomTemp, p.topSet, p.bottomSet);
        httpd_resp_send_chunk(req, chunk, len);
    }

    len = snprintf(chunk, sizeof(chunk), "]}");
    httpd_resp_send_chunk(req, chunk, len);
    httpd_resp_send_chunk(req, nullptr, 0); // End chunked response
    return ESP_OK;
}

// ============================================================================
// GET /api/security/status
// ============================================================================
esp_err_t RestApi::getSecurityStatusHandler(httpd_req_t *req)
{
    if (s_wifi == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi not ready");
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", s_wifi->getSsid().c_str());
    cJSON_AddBoolToObject(root, "passwordChanged", s_wifi->isPasswordChanged());

    char* rendered = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rendered);
    cJSON_free(rendered);
    cJSON_Delete(root);
    return ESP_OK;
}

// ============================================================================
// POST /api/security/wifi
// ============================================================================
esp_err_t RestApi::postSecurityWifiHandler(httpd_req_t *req)
{
    if (s_wifi == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi not ready");
        return ESP_OK;
    }

    std::string body = readRequestBody(req);
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *keepDefault = cJSON_GetObjectItem(root, "keepDefault");
    cJSON *newPass = cJSON_GetObjectItem(root, "password");

    if (keepDefault && cJSON_IsTrue(keepDefault)) {
        s_wifi->confirmDefaultPassword();
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Default-Passwort beibehalten.\"}");
        return ESP_OK;
    }

    if (newPass && newPass->valuestring) {
        std::string passStr = newPass->valuestring;
        esp_err_t err = s_wifi->updatePassword(passStr);
        cJSON_Delete(root);
        if (err == ESP_OK) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Passwort geändert! Bitte verbinde dein Gerät neu mit dem WLAN 'BGA Reflow Controller' und dem neuen Passwort.\"}");
            return ESP_OK;
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Passwort ungültig (mindestens 8 Zeichen)");
            return ESP_OK;
        }
    }

    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Fehlender Parameter");
    return ESP_OK;
}

// ============================================================================
// GET /api/theme
// ============================================================================
// Thin wrapper: theme & chart display preferences live inside settings.json.
esp_err_t RestApi::getThemeHandler(httpd_req_t *req)
{
    if (s_storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    config::MachineSettings settings;
    s_storage->loadSettings(settings);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "theme", settings.theme.c_str());
    cJSON_AddBoolToObject(root, "showZones", settings.showZones);
    cJSON_AddBoolToObject(root, "showTalLine", settings.showTalLine);
    cJSON_AddBoolToObject(root, "showStepMarkers", settings.showStepMarkers);
    cJSON_AddBoolToObject(root, "showPidGains", settings.showPidGains);
    char *rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rendered);
    cJSON_free(rendered);
    return ESP_OK;
}

// ============================================================================
// POST /api/theme
// ============================================================================
esp_err_t RestApi::postThemeHandler(httpd_req_t *req)
{
    if (s_storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    std::string body = readRequestBody(req);
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    config::MachineSettings settings;
    s_storage->loadSettings(settings);

    bool changed = false;
    cJSON *themeItem = cJSON_GetObjectItem(root, "theme");
    if (themeItem && cJSON_IsString(themeItem)) {
        settings.theme = themeItem->valuestring;
        changed = true;
    }
    cJSON *zonesItem = cJSON_GetObjectItem(root, "showZones");
    if (zonesItem && cJSON_IsBool(zonesItem)) {
        settings.showZones = cJSON_IsTrue(zonesItem);
        changed = true;
    }
    cJSON *talItem = cJSON_GetObjectItem(root, "showTalLine");
    if (talItem && cJSON_IsBool(talItem)) {
        settings.showTalLine = cJSON_IsTrue(talItem);
        changed = true;
    }
    cJSON *markersItem = cJSON_GetObjectItem(root, "showStepMarkers");
    if (markersItem && cJSON_IsBool(markersItem)) {
        settings.showStepMarkers = cJSON_IsTrue(markersItem);
        changed = true;
    }
    cJSON *pidGainsItem = cJSON_GetObjectItem(root, "showPidGains");
    if (pidGainsItem && cJSON_IsBool(pidGainsItem)) {
        settings.showPidGains = cJSON_IsTrue(pidGainsItem);
        changed = true;
    }

    if (changed) {
        s_storage->saveSettings(settings);
    }

    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// ============================================================================
// GET /api/pidlibrary
// ============================================================================
// Reads /littlefs/config/pid_library.json and returns it as JSON.
esp_err_t RestApi::getPidLibraryHandler(httpd_req_t *req)
{
    if (s_storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    config::PidLibrary library;
    if (!s_storage->loadPidLibrary(library)) {
        // Return an empty but valid default if file does not exist yet
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"top\":[],\"bottom\":[]}");
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schemaVersion", config::Schema::PID_LIBRARY);

    auto addPoints = [](cJSON *parent, const char *key,
                        const std::vector<config::PidPoint> &pts) {
        cJSON *arr = cJSON_CreateArray();
        for (const auto &pt : pts) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "temp", round1(pt.temp));
            cJSON_AddNumberToObject(item, "kp",   round2(pt.kp));
            cJSON_AddNumberToObject(item, "ki",   round3(pt.ki));
            cJSON_AddNumberToObject(item, "kd",   round2(pt.kd));
            cJSON_AddItemToArray(arr, item);
        }
        cJSON_AddItemToObject(parent, key, arr);
    };

    addPoints(root, "top",    library.top);
    addPoints(root, "bottom", library.bottom);

    char *rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rendered);
    cJSON_free(rendered);
    return ESP_OK;
}

// ============================================================================
// POST /api/pidlibrary
// ============================================================================
// Accepts {"top":[...],"bottom":[...]} and persists to LittleFS.
esp_err_t RestApi::postPidLibraryHandler(httpd_req_t *req)
{
    if (s_storage == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage not ready");
        return ESP_OK;
    }

    // 0. State Guard: Reject PID library modifications during active reflow / autotune
    if (s_fsm != nullptr) {
        auto st = s_fsm->getState();
        if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE && st != fsm::ReflowState::BACKUP) {
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: PID library modification blocked during active process");
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

    std::string body = readRequestBody(req);
    if (body.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read request body");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    // 1. Mismatch validation: reject if payload is machine settings or Reflow Profile
    if (cJSON_GetObjectItem(root, "maxTempTop") || cJSON_GetObjectItem(root, "maxTempBottom") ||
        cJSON_GetObjectItem(root, "stepsTop") || cJSON_GetObjectItem(root, "stepsBottom") ||
        cJSON_GetObjectItem(root, "topSteps") || cJSON_GetObjectItem(root, "bottomSteps") ||
        cJSON_GetObjectItem(root, "holdLowTolerance") || cJSON_GetObjectItem(root, "simulationMode") ||
        cJSON_GetObjectItem(root, "hardwareBuzzerEnabled") || cJSON_GetObjectItem(root, "topBurstWindowMs")) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload: File contains machine settings or profile data instead of a PID library");
        return ESP_OK;
    }

    config::PidLibrary library;

    auto parsePoints = [](cJSON *arr, std::vector<config::PidPoint> &out) {
        if (!cJSON_IsArray(arr)) return;
        int count = cJSON_GetArraySize(arr);
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(arr, i);
            if (!item) continue;
            config::PidPoint pt;
            cJSON *temp = cJSON_GetObjectItem(item, "temp");
            cJSON *kp   = cJSON_GetObjectItem(item, "kp");
            cJSON *ki   = cJSON_GetObjectItem(item, "ki");
            cJSON *kd   = cJSON_GetObjectItem(item, "kd");
            if (temp && cJSON_IsNumber(temp)) pt.temp = (float)round1(temp->valuedouble);
            if (kp   && cJSON_IsNumber(kp))   pt.kp   = (float)round2(kp->valuedouble);
            if (ki   && cJSON_IsNumber(ki))   pt.ki   = (float)round3(ki->valuedouble);
            if (kd   && cJSON_IsNumber(kd))   pt.kd   = (float)round2(kd->valuedouble);
            out.push_back(pt);
        }
    };

    cJSON *topArr = cJSON_GetObjectItem(root, "top");
    if (!topArr) topArr = cJSON_GetObjectItem(root, "topPoints");
    cJSON *botArr = cJSON_GetObjectItem(root, "bottom");
    if (!botArr) botArr = cJSON_GetObjectItem(root, "bottomPoints");

    parsePoints(topArr, library.top);
    parsePoints(botArr, library.bottom);
    cJSON_Delete(root);

    // Validate using domain model
    auto vres = library.validate();
    if (!vres.valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, vres.errorMessage ? vres.errorMessage : "Invalid PID library");
        return ESP_OK;
    }

    bool ok = s_storage->savePidLibrary(library);
    if (ok && s_fsm) {
        auto state = s_fsm->getState();
        if (state == fsm::ReflowState::IDLE || state == fsm::ReflowState::DONE) {
            s_fsm->setPidLibrary(library);
            ESP_LOGI(TAG, "PID Library applied immediately to RAM (FSM is IDLE).");
        } else {
            ESP_LOGI(TAG, "PID Library saved to Flash; RAM update deferred until active process returns to IDLE.");
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"success\":true}" : "{\"success\":false}");
    return ESP_OK;
}

// ============================================================================
// POST /api/backup/complete
// ============================================================================
// Marks that a system backup was successfully created during this controller instance runtime.
esp_err_t RestApi::postBackupCompleteHandler(httpd_req_t *req)
{
    s_backupTaken = true;
    ESP_LOGI(TAG, "REST API: System backup recorded as completed for this instance.");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"backupTaken\":true}");
    return ESP_OK;
}

// ============================================================================
// POST /api/ota
// ============================================================================
// Stream-writes uploaded firmware binary chunk by chunk into the next OTA partition.
esp_err_t RestApi::postOtaUpdateHandler(httpd_req_t *req)
{
    if (s_fsm != nullptr) {
        auto st = s_fsm->getState();
        if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE && st != fsm::ReflowState::BACKUP) {
            ESP_LOGW(TAG, "OTA update rejected: reflow/autotune process is active (state=%s)", s_fsm->getStateString());
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "OTA update forbidden while heating process is active");
            return ESP_OK;
        }
    }

    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty firmware image payload");
        return ESP_OK;
    }

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition available for update");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting OTA update to partition '%s' at offset 0x%" PRIx32 " (size: %zu bytes)...",
             update_partition->label, update_partition->address, req->content_len);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        return ESP_OK;
    }

    char buf[2048];
    size_t total_received = 0;
    bool is_first_chunk = true;

    while (total_received < req->content_len) {
        int received = httpd_req_recv(req, buf, sizeof(buf));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "OTA receive socket error / timeout (received %zu / %zu bytes)", total_received, req->content_len);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA receive stream interrupted");
            return ESP_OK;
        }

        if (is_first_chunk) {
            if (received < sizeof(esp_image_header_t) || static_cast<uint8_t>(buf[0]) != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGE(TAG, "OTA image header invalid: magic byte 0x%02X != 0x%02X", static_cast<uint8_t>(buf[0]), ESP_IMAGE_HEADER_MAGIC);
                esp_ota_abort(ota_handle);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ESP32 binary header (magic byte mismatch)");
                return ESP_OK;
            }
            is_first_chunk = false;
        }

        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_write failed");
            return ESP_OK;
        }
        total_received += received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end validation failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation / digest failed");
        return ESP_OK;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to activate new boot partition");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA update successfully written to '%s'! Scheduling reboot in 1.5s...", update_partition->label);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"OTA update successful! Rebooting...\",\"rebootInMs\":2500}");

    // Spawn one-shot background reboot task so HTTP response can finish sending
    xTaskCreate([](void*) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        ESP_LOGI(TAG, "Rebooting into newly flashed firmware...");
        esp_restart();
    }, "ota_reboot", 3072, nullptr, 5, nullptr);

    return ESP_OK;
}

} // namespace web
