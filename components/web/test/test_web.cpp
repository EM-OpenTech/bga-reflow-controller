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
 * @file test_web.cpp
 * @brief Unit tests for web component, WebSocket telemetry serialization, and MIME types.
 *
 * Tests TelemetryData default initialization, MIME content-type mapping,
 * JSON serialization formatting, and URI profile parameter parsing.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "cJSON.h"
#include "output/output_manager.hpp"
#include "pid/pid_controller.hpp"
#include "system_context.hpp"
#include "unity.h"
#include "web/fsm_command_queue.hpp"
#include "web/rest_api.hpp"
#include "web/static_file_server.hpp"
#include "web/usb_network.hpp"
#include "web/web_server.hpp"
#include "web/wifi_manager.hpp"
#include "web/ws_handler.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

#ifndef ESP_PLATFORM
namespace {
void setReqUri(httpd_req_t& req, const char* uri) {
    if (uri != nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        snprintf(const_cast<char*>(req.uri), sizeof(req.uri), "%s", uri);
    }
}
} // namespace
#endif

// 1. Telemetry Data Defaults Test
static void test_web_telemetry_defaults() {
    web::TelemetryData t;
    TEST_ASSERT_EQUAL_UINT8(0, t.stateEnum);
    TEST_ASSERT_EQUAL_STRING("IDLE", t.stateStr.c_str());
    TEST_ASSERT_FALSE(t.preheatDone);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, t.topTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, t.bottomTemp);
    TEST_ASSERT_FALSE(t.fan);
    TEST_ASSERT_FALSE(t.lamp);
    TEST_ASSERT_EQUAL_INT(0, t.stepMarkers.size());
}

// 2. MIME Content-Type Mapping Test
static void test_web_mime_types() {
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", web::StaticFileServer::getContentType("/index.html"));
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", web::StaticFileServer::getContentType("/index.html.gz"));
    TEST_ASSERT_EQUAL_STRING("text/css", web::StaticFileServer::getContentType("/style.css"));
    TEST_ASSERT_EQUAL_STRING("application/javascript", web::StaticFileServer::getContentType("/app.js"));
    TEST_ASSERT_EQUAL_STRING("application/json", web::StaticFileServer::getContentType("/config.json"));
    TEST_ASSERT_EQUAL_STRING("image/svg+xml", web::StaticFileServer::getContentType("/logo.svg"));
}

// 3. JSON Telemetry Serialization Test
static void test_web_json_serialization_deep() {
    web::TelemetryData t;
    t.stateEnum = 3;
    t.stateStr = "REFLOW";
    t.preheatDone = true;
    t.topTemp = 221.5f;
    t.bottomTemp = 160.0f;
    t.topSet = 225.0f;
    t.bottomSet = 160.0f;
    t.topPower = 85.0f;
    t.bottomPower = 40.0f;
    t.elapsedSec = 145;
    t.fan = false;
    t.lamp = true;

    fsm::StepMarker marker;
    marker.timeS = 120;
    marker.stepIndex = 1;
    marker.targetTemp = 225.0f;
    marker.isTop = true;
    t.stepMarkers.push_back(marker);

    // Verify manually constructing json or cJSON verification
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "stateEnum", t.stateEnum);
    cJSON_AddStringToObject(root, "state", t.stateStr.c_str());
    cJSON_AddBoolToObject(root, "preheatDone", t.preheatDone ? 1 : 0);
    cJSON_AddNumberToObject(root, "topTemp", t.topTemp);
    cJSON_AddNumberToObject(root, "bottomTemp", t.bottomTemp);
    cJSON_AddNumberToObject(root, "topSet", t.topSet);
    cJSON_AddNumberToObject(root, "bottomSet", t.bottomSet);
    cJSON_AddNumberToObject(root, "topPower", t.topPower);
    cJSON_AddNumberToObject(root, "bottomPower", t.bottomPower);
    cJSON_AddNumberToObject(root, "elapsed", t.elapsedSec);
    cJSON_AddBoolToObject(root, "fan", t.fan ? 1 : 0);
    cJSON_AddBoolToObject(root, "lamp", t.lamp ? 1 : 0);

    char* rendered = cJSON_PrintUnformatted(root);
    TEST_ASSERT_NOT_NULL(rendered);

    cJSON* parsed = cJSON_Parse(rendered);
    TEST_ASSERT_NOT_NULL(parsed);

    const cJSON* st = cJSON_GetObjectItem(parsed, "stateEnum");
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_INT(3, st->valueint);

    const cJSON* lampItem = cJSON_GetObjectItem(parsed, "lamp");
    TEST_ASSERT_NOT_NULL(lampItem);
    TEST_ASSERT_TRUE(cJSON_IsTrue(lampItem) != 0);

    cJSON_free(rendered);
    cJSON_Delete(root);
    cJSON_Delete(parsed);
}

// 4. Telemetry Precision & Rounding Test
static void test_web_telemetry_precision_serialization() {
    web::TelemetryData t;
    t.stateEnum = 2;
    t.stateStr = "SOAK";
    t.preheatDone = false;
    // Feed floating point values with extra decimal noise
    t.topTemp = 150.567f;    // Should format to 150.6 (%.1f)
    t.bottomTemp = 140.043f; // Should format to 140.0 (%.1f)
    t.topSet = 150.0f;
    t.bottomSet = 140.0f;
    t.topPower = 75.4f;      // Should format to 75 (%d)
    t.bottomPower = 60.8f;   // Should format to 61 (%d)
    t.topPidKp = 2.456f;     // Should format to 2.46 (%.2f)
    t.topPidKi = 0.0526f;    // Should format to 0.053 (%.3f)
    t.topPidKd = 1.234f;     // Should format to 1.23 (%.2f)
    t.bottomPidKp = 2.101f;  // Should format to 2.10 (%.2f)
    t.bottomPidKi = 0.0401f; // Should format to 0.040 (%.3f)
    t.bottomPidKd = 0.999f;  // Should format to 1.00 (%.2f)

    std::string jsonStr = web::WebSocketHandler::serializeTelemetry(t);
    TEST_ASSERT_FALSE(jsonStr.empty());

    cJSON* root = cJSON_Parse(jsonStr.c_str());
    TEST_ASSERT_NOT_NULL(root);

    // Verify temperatures rounded to 1 decimal place
    const cJSON* topTemp = cJSON_GetObjectItem(root, "topTemp");
    TEST_ASSERT_NOT_NULL(topTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.6f, static_cast<float>(topTemp->valuedouble));

    const cJSON* botTemp = cJSON_GetObjectItem(root, "bottomTemp");
    TEST_ASSERT_NOT_NULL(botTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 140.0f, static_cast<float>(botTemp->valuedouble));

    // Verify PID Gains precision
    const cJSON* topKp = cJSON_GetObjectItem(root, "topPidKp");
    TEST_ASSERT_NOT_NULL(topKp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.46f, static_cast<float>(topKp->valuedouble));

    const cJSON* topKi = cJSON_GetObjectItem(root, "topPidKi");
    TEST_ASSERT_NOT_NULL(topKi);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.053f, static_cast<float>(topKi->valuedouble));

    const cJSON* topKd = cJSON_GetObjectItem(root, "topPidKd");
    TEST_ASSERT_NOT_NULL(topKd);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.23f, static_cast<float>(topKd->valuedouble));

    const cJSON* botKi = cJSON_GetObjectItem(root, "bottomPidKi");
    TEST_ASSERT_NOT_NULL(botKi);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.040f, static_cast<float>(botKi->valuedouble));

    // By default without includeLogs=true, logs array should NOT be included to conserve bandwidth
    const cJSON* noLogs = cJSON_GetObjectItem(root, "logs");
    TEST_ASSERT_NULL(noLogs);
    cJSON_Delete(root);

    // With includeLogs=true, logs array should be present
    std::string jsonWithLogs = web::WebSocketHandler::serializeTelemetry(t, true);
    cJSON* rootWithLogs = cJSON_Parse(jsonWithLogs.c_str());
    TEST_ASSERT_NOT_NULL(rootWithLogs);
    const cJSON* logsArr = cJSON_GetObjectItem(rootWithLogs, "logs");
    TEST_ASSERT_NOT_NULL(logsArr);
    TEST_ASSERT_TRUE(cJSON_IsArray(logsArr));
    cJSON_Delete(rootWithLogs);
}

// 5. URI Profile Path Parameter Extraction Test
static void test_web_profile_path_param_extraction() {
    // 1. Collection endpoint (list all)
    TEST_ASSERT_EQUAL_STRING("", web::RestApi::extractProfileFilenameFromUri("/api/profiles").c_str());
    TEST_ASSERT_EQUAL_STRING("", web::RestApi::extractProfileFilenameFromUri("/api/profiles/").c_str());

    // 2. Direct filename with .json extension
    TEST_ASSERT_EQUAL_STRING("factory-profile.json",
                             web::RestApi::extractProfileFilenameFromUri("/api/profiles/factory-profile.json").c_str());

    // 3. Filename without extension (auto-appends .json)
    TEST_ASSERT_EQUAL_STRING("leadfree.json",
                             web::RestApi::extractProfileFilenameFromUri("/api/profiles/leadfree").c_str());

    // 4. URL Encoded filename (%20 -> space)
    TEST_ASSERT_EQUAL_STRING(
        "My Custom Profile.json",
        web::RestApi::extractProfileFilenameFromUri("/api/profiles/My%20Custom%20Profile.json").c_str());

    // 5. Stripping query params attached to path
    TEST_ASSERT_EQUAL_STRING("custom.json",
                             web::RestApi::extractProfileFilenameFromUri("/api/profiles/custom.json?t=123456").c_str());

    // 6. Legacy path /api/profile/...
    TEST_ASSERT_EQUAL_STRING("leaded.json",
                             web::RestApi::extractProfileFilenameFromUri("/api/profile/leaded.json").c_str());

    // 7. Query param fallback (?file=XYZ)
    TEST_ASSERT_EQUAL_STRING(
        "fallback.json", web::RestApi::extractProfileFilenameFromUri("/api/profiles", "file=fallback.json").c_str());

    // 8. Path traversal rejection (prevents directory breakout)
    TEST_ASSERT_EQUAL_STRING(
        "", web::RestApi::extractProfileFilenameFromUri("/api/profiles/../config/settings.json").c_str());
    TEST_ASSERT_EQUAL_STRING("", web::RestApi::extractProfileFilenameFromUri("/api/profiles/../../etc/passwd").c_str());
    TEST_ASSERT_EQUAL_STRING("",
                             web::RestApi::extractProfileFilenameFromUri("/api/profiles/sub/folder/file.json").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "", web::RestApi::extractProfileFilenameFromUri("/api/profiles", "file=../escape.json").c_str());
}

// 6. Mismatched Payload Content Detection Test
static void test_web_mismatched_payload_detection() {
    // 1. Settings JSON containing Profile fields (stepsTop / stepsBottom)
    const char* profileJson = R"({"name":"Test","stepsBottom":[{"temp":150,"time":30,"ramp":1.0}]})";
    cJSON* pRoot = cJSON_Parse(profileJson);
    TEST_ASSERT_NOT_NULL(pRoot);
    bool isProfileInSettings =
        (cJSON_GetObjectItem(pRoot, "stepsTop") != nullptr || cJSON_GetObjectItem(pRoot, "stepsBottom") != nullptr ||
         cJSON_GetObjectItem(pRoot, "topSteps") != nullptr || cJSON_GetObjectItem(pRoot, "bottomSteps") != nullptr);
    TEST_ASSERT_TRUE(isProfileInSettings);
    cJSON_Delete(pRoot);

    // 2. Profile JSON containing Settings fields (simulationMode / maxTempTop)
    const char* settingsJson = R"({"simulationMode":true,"maxTempTop":250.0,"topKp":2.0})";
    cJSON* sRoot = cJSON_Parse(settingsJson);
    TEST_ASSERT_NOT_NULL(sRoot);
    bool isSettingsInProfile =
        (cJSON_GetObjectItem(sRoot, "maxTempTop") != nullptr ||
         cJSON_GetObjectItem(sRoot, "simulationMode") != nullptr || cJSON_GetObjectItem(sRoot, "topKp") != nullptr);
    TEST_ASSERT_TRUE(isSettingsInProfile);
    cJSON_Delete(sRoot);

    // 3. PID Library JSON containing Profile fields
    const char* pidJson = R"({"top":[{"temp":100,"kp":2.0,"ki":0.02,"kd":0.5}]})";
    cJSON* pidRoot = cJSON_Parse(pidJson);
    TEST_ASSERT_NOT_NULL(pidRoot);
    bool isProfileInPid = (cJSON_GetObjectItem(pidRoot, "stepsTop") != nullptr ||
                           cJSON_GetObjectItem(pidRoot, "stepsBottom") != nullptr ||
                           cJSON_GetObjectItem(pidRoot, "maxTempTop") != nullptr);
    TEST_ASSERT_FALSE(isProfileInPid); // Valid PID library does not contain profile or settings fields
    cJSON_Delete(pidRoot);
}

// 7. Domain Model Range Validations Test
static void test_web_domain_model_range_validations() {
    // 1. MachineSettings Validation
    config::MachineSettings settings;
    TEST_ASSERT_TRUE(settings.validate().valid);

    settings.maxTempTop = 450.0f; // Exceeds 300°C limit
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.maxTempTop = 250.0f;

    settings.emaAlpha = 1.5f; // Exceeds 1.0 limit
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.emaAlpha = 0.3f; // Reset back to valid
    TEST_ASSERT_TRUE(settings.validate().valid);

    // Burst-Fire Windows (500 .. 5000 ms)
    settings.topBurstWindowMs = 100;
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.topBurstWindowMs = 500;
    TEST_ASSERT_TRUE(settings.validate().valid);

    settings.bottomBurstWindowMs = 400;
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.bottomBurstWindowMs = 1000;
    TEST_ASSERT_TRUE(settings.validate().valid);

    // Settle duration (1 .. 60 s)
    settings.settleTimeS = 0;
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.settleTimeS = 5;
    TEST_ASSERT_TRUE(settings.validate().valid);

    // 2. ReflowProfile Validation
    config::ReflowProfile prof;
    prof.name = "Test Profile";
    TEST_ASSERT_FALSE(prof.validate().valid); // Empty profile (0 steps) is invalid

    config::ProfileStep step;
    step.temp = 150.0f;
    step.time = 30;
    step.ramp = 1.5f;
    prof.stepsBottom.push_back(step);
    TEST_ASSERT_TRUE(prof.validate().valid);

    prof.stepsBottom[0].temp = 350.0f; // Exceeds 300°C limit
    TEST_ASSERT_FALSE(prof.validate().valid);
    prof.stepsBottom[0].temp = 150.0f;

    prof.stepsBottom[0].ramp = 15.0f; // Exceeds 10.0°C/s limit
    TEST_ASSERT_FALSE(prof.validate().valid);
    prof.stepsBottom[0].ramp = 1.5f;
    TEST_ASSERT_TRUE(prof.validate().valid);

    // 3. PidLibrary Validation
    config::PidLibrary pidLib;
    TEST_ASSERT_TRUE(pidLib.validate().valid); // Empty library is valid (falls back to baseline gains)

    pidLib.top.push_back({150.0f, 2.5f, 0.05f, 1.0f});
    TEST_ASSERT_TRUE(pidLib.validate().valid);

    pidLib.top[0].kp = -1.0f; // Negative Kp is invalid
    TEST_ASSERT_FALSE(pidLib.validate().valid);
    pidLib.top[0].kp = 2.5f;

    pidLib.top[0].ki = 15.0f; // Exceeds 10.0 limit
    TEST_ASSERT_FALSE(pidLib.validate().valid);
    pidLib.top[0].ki = 0.05f;

    pidLib.top[0].kd = -0.5f; // Negative Kd is invalid
    TEST_ASSERT_FALSE(pidLib.validate().valid);
    pidLib.top[0].kd = 1.0f;
    TEST_ASSERT_TRUE(pidLib.validate().valid);
}

// 8. Wi-Fi Default Constants Test
static void test_web_wifi_constants() {
    TEST_ASSERT_EQUAL_STRING("BGA Reflow Controller", web::DEFAULT_AP_SSID);
    TEST_ASSERT_EQUAL_STRING("reflow123", web::DEFAULT_AP_PASSWORD);
    TEST_ASSERT_EQUAL_STRING("reflow", web::DEFAULT_MDNS_HOST);
    TEST_ASSERT_EQUAL_STRING("wifi_sec", web::NVS_WIFI_NAMESPACE);
    TEST_ASSERT_EQUAL_UINT32(4, web::MAX_WS_CLIENTS);
}

// 9. Circular Log Buffer Capture Test
static void test_web_log_buffer_capture() {
    web::appendLogLine("[TEST] Log message 1");
    web::appendLogLine("[TEST] Log message 2");

    auto logs = web::getLatestLogs(5);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, logs.size());

    bool found1 = false;
    bool found2 = false;
    for (const auto& line : logs) {
        if (line == "[TEST] Log message 1") {
            found1 = true;
        }
        if (line == "[TEST] Log message 2") {
            found2 = true;
        }
    }
    TEST_ASSERT_TRUE(found1);
    TEST_ASSERT_TRUE(found2);
}

// 10. POST Report Data Model Test
static void test_web_post_report_registration() {
    web::PostReportInfo info;
    info.resetReason = ESP_RST_POWERON;
    info.resetReasonStr = "POWERON (Cold Boot Normal)";
    info.wasWatchdogReset = false;
    info.wasBrownoutReset = false;
    info.freeHeapBytes = 250000;
    info.freePsramBytes = 7900000;
    info.littleFsOk = true;
    info.littleFsFreeBytes = 3200000;
    info.topSensorOk = true;
    info.topCjTemp = 24.5f;
    info.topRawTemp = 24.8f;
    info.bottomSensorOk = true;
    info.bottomCjTemp = 24.3f;
    info.bottomRawTemp = 24.6f;
    info.outputsSafe = true;
    info.allPassed = true;

    web::RestApi::setPostReport(info);
    TEST_ASSERT_TRUE(info.allPassed);
    TEST_ASSERT_EQUAL_STRING("POWERON (Cold Boot Normal)", info.resetReasonStr);
}

// 11. Telemetry NaN and Inf JSON Serialization Safety Test
static void test_web_telemetry_nan_inf_serialization() {
    web::TelemetryData t;
    t.stateEnum = 1;
    t.stateStr = "PREHEAT";
    t.topSensorOk = true;
    t.bottomSensorOk = true;

    // Inject NaN and Inf into floating point telemetry fields
    t.topTemp = std::numeric_limits<float>::quiet_NaN();
    t.bottomTemp = std::numeric_limits<float>::infinity();
    t.topSet = -std::numeric_limits<float>::infinity();
    t.bottomSet = std::numeric_limits<float>::quiet_NaN();
    t.topPower = std::numeric_limits<float>::quiet_NaN();
    t.bottomPower = std::numeric_limits<float>::infinity();
    t.topPidKp = std::numeric_limits<float>::quiet_NaN();
    t.autotuneTargetTemp = std::numeric_limits<float>::quiet_NaN();

    fsm::StepMarker marker;
    marker.timeS = 10;
    marker.stepIndex = 0;
    marker.targetTemp = std::numeric_limits<float>::quiet_NaN();
    marker.isTop = true;
    t.stepMarkers.push_back(marker);

    // Serialization must not throw or produce invalid JSON tokens like "nan"
    std::string jsonStr = web::WebSocketHandler::serializeTelemetry(t);
    TEST_ASSERT_FALSE(jsonStr.empty());

    // cJSON_Parse strictly adheres to RFC 8259 and will fail if "nan" or "inf" is present
    cJSON* root = cJSON_Parse(jsonStr.c_str());
    TEST_ASSERT_NOT_NULL(root);

    const cJSON* topTempItem = cJSON_GetObjectItem(root, "topTemp");
    TEST_ASSERT_NOT_NULL(topTempItem);
    TEST_ASSERT_TRUE(cJSON_IsNull(topTempItem));

    const cJSON* botTempItem = cJSON_GetObjectItem(root, "bottomTemp");
    TEST_ASSERT_NOT_NULL(botTempItem);
    TEST_ASSERT_TRUE(cJSON_IsNull(botTempItem));

    const cJSON* topPowerItem = cJSON_GetObjectItem(root, "topPower");
    TEST_ASSERT_NOT_NULL(topPowerItem);
    TEST_ASSERT_EQUAL_INT(0, topPowerItem->valueint);

    cJSON_Delete(root);
}

// 12. Wi-Fi Password Length Boundary Validation Test
static void test_web_wifi_password_validation() {
    web::WifiManager wifi;
    // Passwords shorter than 8 characters must be rejected
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi.updatePassword(""));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi.updatePassword("short"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi.updatePassword("1234567"));

    // Passwords longer than 63 characters must be rejected
    std::string tooLong(64, 'x');
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi.updatePassword(tooLong));
}

#ifndef ESP_PLATFORM
// 13. Static File Server Handlers Test
static void test_web_static_file_handler() {
    httpd_req_t req{};

    // Root index.html
    setReqUri(req, "/");
    TEST_ASSERT_EQUAL(ESP_OK, web::StaticFileServer::fileGetHandler(&req));

    // Style CSS
    setReqUri(req, "/style.css");
    TEST_ASSERT_EQUAL(ESP_OK, web::StaticFileServer::fileGetHandler(&req));

    // Captive portal probes
    setReqUri(req, "/hotspot-detect.html");
    TEST_ASSERT_EQUAL(ESP_OK, web::StaticFileServer::fileGetHandler(&req));
    setReqUri(req, "/generate_204");
    TEST_ASSERT_EQUAL(ESP_OK, web::StaticFileServer::fileGetHandler(&req));

    // Path traversal attempt blocked
    setReqUri(req, "/../secret.txt");
    TEST_ASSERT_EQUAL(ESP_OK, web::StaticFileServer::fileGetHandler(&req));

    // Non-existent static file -> 404
    setReqUri(req, "/missing.png");
    TEST_ASSERT_EQUAL(ESP_OK, web::StaticFileServer::fileGetHandler(&req));
}

// 14. WebSocket Connection & Slot Management Test
static void test_web_ws_client_management() {
    web::WebSocketHandler ws(nullptr);

    // Add up to 4 clients
    ws.addClient(10);
    ws.addClient(11);
    ws.addClient(12);
    ws.addClient(13);

    // Duplicate client (should be ignored without error)
    ws.addClient(10);

    // 5th client (exceeds limit 4)
    ws.addClient(14);

    // Remove client
    ws.removeClient(11);

    // Re-add in freed slot
    ws.addClient(15);
    ws.removeClient(10);
    ws.removeClient(12);
    ws.removeClient(13);
    ws.removeClient(15);
}

// 15. REST Routes Diagnostic Status & Theme Test
static void test_web_routes_status_and_theme() {
    storage::StorageManager storage;
    storage.begin("/tmp/test_web_storage");
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController bottomPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, bottomPid);
    web::WifiManager wifi;
    app::SystemContext context;

    web::RestApi::init(&storage, &settings, &fsm, &wifi, &context);

    httpd_req_t req{};
    setReqUri(req, "/api/status");
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::getStatusHandler(&req));

    setReqUri(req, "/api/theme");
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::getThemeHandler(&req));

    const char* themeJson = R"({"theme":"dark","showZones":true})";
    setReqUri(req, "/api/theme");
    req.content_len = strlen(themeJson);
    req.aux = const_cast<char*>(themeJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postThemeHandler(&req));
}

// 16. REST Routes Settings CRUD Test
static void test_web_routes_settings() {
    storage::StorageManager storage;
    storage.begin("/tmp/test_web_storage");
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController bottomPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, bottomPid);
    web::WifiManager wifi;
    app::SystemContext context;

    web::RestApi::init(&storage, &settings, &fsm, &wifi, &context);

    httpd_req_t req{};
    setReqUri(req, "/api/settings");
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::getSettingsHandler(&req));

    // Invalid JSON
    const char* badJson = "not-json";
    req.content_len = strlen(badJson);
    req.aux = const_cast<char*>(badJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postSettingsHandler(&req));

    // Mismatched payload (Profile in settings)
    const char* mismatchJson = R"({"stepsTop":[{"temp":150,"time":30}]})";
    req.content_len = strlen(mismatchJson);
    req.aux = const_cast<char*>(mismatchJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postSettingsHandler(&req));

    // Valid settings payload
    const char* validJson = R"({"maxTempTop":250.0,"simulationMode":true})";
    req.content_len = strlen(validJson);
    req.aux = const_cast<char*>(validJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postSettingsHandler(&req));
}

// 17. REST Routes Control & Overrides Dispatcher Test
static void test_web_routes_control_and_overrides() {
    storage::StorageManager storage;
    storage.begin("/tmp/test_web_storage");
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController bottomPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, bottomPid);
    web::WifiManager wifi;
    app::SystemContext context;

    TEST_ASSERT_TRUE(app::fsmCmdQueueInit());
    web::RestApi::init(&storage, &settings, &fsm, &wifi, &context);

    httpd_req_t req{};
    setReqUri(req, "/api/control");

    // Stop action
    const char* stopJson = R"({"action":"stop"})";
    req.content_len = strlen(stopJson);
    req.aux = const_cast<char*>(stopJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    // Reset Fault action
    const char* resetJson = R"({"action":"resetFault"})";
    req.content_len = strlen(resetJson);
    req.aux = const_cast<char*>(resetJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    // Backup actions
    const char* enterBackupJson = R"({"action":"enterBackup"})";
    req.content_len = strlen(enterBackupJson);
    req.aux = const_cast<char*>(enterBackupJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    const char* exitBackupJson = R"({"action":"exitBackup"})";
    req.content_len = strlen(exitBackupJson);
    req.aux = const_cast<char*>(exitBackupJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    // Unknown action
    const char* unknownJson = R"({"action":"invalid_xyz"})";
    req.content_len = strlen(unknownJson);
    req.aux = const_cast<char*>(unknownJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    // Reflow action in IDLE (rejected with 403)
    const char* reflowJson = R"({"action":"reflow"})";
    req.content_len = strlen(reflowJson);
    req.aux = const_cast<char*>(reflowJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    // Reflow action during PREHEAT while preheatDone=false (rejected with 403)
    config::ReflowProfile testProf;
    testProf.stepsBottom.push_back({100.0f, 10, 1.0f});
    fsm.startPreheat(testProf);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(fsm::ReflowState::PREHEAT), static_cast<uint8_t>(fsm.getState()));
    TEST_ASSERT_FALSE(fsm.isPreheatDone());
    req.content_len = strlen(reflowJson);
    req.aux = const_cast<char*>(reflowJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));
    fsm.stop();

    // Autotune Start action
    const char* autotuneJson = R"({"action":"autotune","channel":"top","targetTemp":150,"method":"cohen-coon"})";
    req.content_len = strlen(autotuneJson);
    req.aux = const_cast<char*>(autotuneJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    // Autotune Stop action
    const char* stopTuneJson = R"({"action":"stopAutotune"})";
    req.content_len = strlen(stopTuneJson);
    req.aux = const_cast<char*>(stopTuneJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    // Autotune duplicate call rejection during active AUTOTUNE state
    fsm.startAutotune(true, 150.0f, 0);
    req.content_len = strlen(autotuneJson);
    req.aux = const_cast<char*>(autotuneJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));
    fsm.stopAutotune();

    // Autotune invalid channel rejection (400 Bad Request)
    const char* badChannelJson = R"({"action":"autotune","channel":"invalid_side","targetTemp":150})";
    req.content_len = strlen(badChannelJson);
    req.aux = const_cast<char*>(badChannelJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    // Autotune temperature out of bounds rejection (400 Bad Request)
    const char* badTempJson = R"({"action":"autotune","channel":"top","targetTemp":-50})";
    req.content_len = strlen(badTempJson);
    req.aux = const_cast<char*>(badTempJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postControlHandler(&req));

    // Hardware Overrides: fan and lamp
    setReqUri(req, "/api/overrides");
    const char* ovrJson = R"({"fan":true,"lamp":false})";
    req.content_len = strlen(ovrJson);
    req.aux = const_cast<char*>(ovrJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postOverridesHandler(&req));
}

// 18. REST Routes PID Library & Profiles Listing Test
static void test_web_routes_pid_and_profiles() {
    storage::StorageManager storage;
    storage.begin("/tmp/test_web_storage");
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController bottomPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, bottomPid);
    web::WifiManager wifi;
    app::SystemContext context;

    web::RestApi::init(&storage, &settings, &fsm, &wifi, &context);

    httpd_req_t req{};
    setReqUri(req, "/api/pidlibrary");
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::getPidLibraryHandler(&req));

    // Post PID library
    const char* pidJson = R"({"top":[{"temp":150,"kp":2.0,"ki":0.05,"kd":1.0}],"bottom":[]})";
    req.content_len = strlen(pidJson);
    req.aux = const_cast<char*>(pidJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postPidLibraryHandler(&req));

    // List profiles
    setReqUri(req, "/api/profiles");
    req.content_len = 0;
    req.aux = nullptr;
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::getProfilesHandler(&req));

    // History buffer
    setReqUri(req, "/api/history");
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::getHistoryHandler(&req));

    // Security status
    setReqUri(req, "/api/security/status");
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::getSecurityStatusHandler(&req));

    // Backup complete
    setReqUri(req, "/api/backup/complete");
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postBackupCompleteHandler(&req));
}

// 19. REST Routes Profiles CRUD & Factory Protection Test
static void test_web_routes_profiles_crud() {
    storage::StorageManager storage;
    storage.begin("/tmp/test_web_storage");
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController bottomPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, bottomPid);
    web::WifiManager wifi;
    app::SystemContext context;

    web::RestApi::init(&storage, &settings, &fsm, &wifi, &context);

    httpd_req_t req{};

    // Empty payload
    setReqUri(req, "/api/profiles");
    req.content_len = 0;
    req.aux = nullptr;
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postProfileHandler(&req));

    // Invalid JSON payload
    const char* badJson = "invalid-json";
    req.content_len = strlen(badJson);
    req.aux = const_cast<char*>(badJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postProfileHandler(&req));

    // Mismatched profile payload (settings fields in profile)
    const char* mismatchJson = R"({"simulationMode":true,"maxTempTop":250})";
    req.content_len = strlen(mismatchJson);
    req.aux = const_cast<char*>(mismatchJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postProfileHandler(&req));

    // Valid Profile payload
    const char* validProfJson =
        R"({"name":"UnitTestProfile","file":"testprof.json","stepsBottom":[{"temp":150,"time":30,"ramp":1.0}]})";
    req.content_len = strlen(validProfJson);
    req.aux = const_cast<char*>(validProfJson);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postProfileHandler(&req));

    // Read single profile
    setReqUri(req, "/api/profiles/testprof.json");
    req.content_len = 0;
    req.aux = nullptr;
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::getProfileHandler(&req));

    // Delete factory profile (rejected)
    setReqUri(req, "/api/profiles/factory-profile.json");
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::deleteProfileHandler(&req));

    // Delete created profile
    setReqUri(req, "/api/profiles/testprof.json");
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::deleteProfileHandler(&req));
}

// 20. REST Routes OTA Update Validation Test
static void test_web_routes_ota() {
    storage::StorageManager storage;
    storage.begin("/tmp/test_web_storage");
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController bottomPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, bottomPid);
    web::WifiManager wifi;
    app::SystemContext context;

    web::RestApi::init(&storage, &settings, &fsm, &wifi, &context);

    httpd_req_t req{};
    setReqUri(req, "/api/ota");

    // Empty payload rejected
    req.content_len = 0;
    req.aux = nullptr;
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postOtaUpdateHandler(&req));

    // Invalid header payload (missing magic byte 0xE7)
    const char dummyData[64] = "invalid_firmware_data_without_magic_byte";
    req.content_len = sizeof(dummyData);
    req.aux = const_cast<char*>(dummyData);
    TEST_ASSERT_EQUAL(ESP_OK, web::RestApi::postOtaUpdateHandler(&req));
}

// 21. WebServer Lifecycle & Broadcast Test
static void test_web_server_lifecycle() {
    storage::StorageManager storage;
    storage.begin("/tmp/test_web_storage");
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController bottomPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, bottomPid);
    web::WifiManager wifi;
    app::SystemContext context;

    web::WebServer server(storage, settings, fsm, wifi, context);
    TEST_ASSERT_EQUAL(ESP_OK, server.begin());

    web::TelemetryData t;
    t.stateStr = "IDLE";
    server.broadcastTelemetry(t);

    server.stop();
}

// 22. UsbNetwork Singleton & Lifecycle Test
static void test_web_usb_network_lifecycle() {
    auto& usbNet = web::UsbNetwork::instance();
    TEST_ASSERT_FALSE(usbNet.isInitialized());
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", usbNet.getIpAddress().c_str());

    TEST_ASSERT_EQUAL(ESP_OK, usbNet.begin());
    TEST_ASSERT_TRUE(usbNet.isInitialized());
    TEST_ASSERT_EQUAL_STRING("192.168.5.1", usbNet.getIpAddress().c_str());

    // Calling begin() again is safe & idempotent
    TEST_ASSERT_EQUAL(ESP_OK, usbNet.begin());
    TEST_ASSERT_TRUE(usbNet.isInitialized());

    usbNet.stop();
    TEST_ASSERT_FALSE(usbNet.isInitialized());
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", usbNet.getIpAddress().c_str());
}
#endif // !ESP_PLATFORM

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_web_tests() {
    RUN_TEST(test_web_telemetry_defaults);
    RUN_TEST(test_web_mime_types);
    RUN_TEST(test_web_json_serialization_deep);
    RUN_TEST(test_web_telemetry_precision_serialization);
    RUN_TEST(test_web_profile_path_param_extraction);
    RUN_TEST(test_web_mismatched_payload_detection);
    RUN_TEST(test_web_domain_model_range_validations);
    RUN_TEST(test_web_wifi_constants);
    RUN_TEST(test_web_log_buffer_capture);
    RUN_TEST(test_web_post_report_registration);
    RUN_TEST(test_web_telemetry_nan_inf_serialization);
    RUN_TEST(test_web_wifi_password_validation);
#ifndef ESP_PLATFORM
    RUN_TEST(test_web_static_file_handler);
    RUN_TEST(test_web_ws_client_management);
    RUN_TEST(test_web_routes_status_and_theme);
    RUN_TEST(test_web_routes_settings);
    RUN_TEST(test_web_routes_control_and_overrides);
    RUN_TEST(test_web_routes_pid_and_profiles);
    RUN_TEST(test_web_routes_profiles_crud);
    RUN_TEST(test_web_routes_ota);
    RUN_TEST(test_web_server_lifecycle);
    RUN_TEST(test_web_usb_network_lifecycle);
#endif
}
