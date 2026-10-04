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

#include "unity.h"
#include "web/ws_handler.hpp"
#include "web/static_file_server.hpp"
#include "cJSON.h"

// 1. Telemetry Data Defaults Test
static void test_web_telemetry_defaults()
{
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
static void test_web_mime_types()
{
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", web::StaticFileServer::getContentType("/index.html"));
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", web::StaticFileServer::getContentType("/index.html.gz"));
    TEST_ASSERT_EQUAL_STRING("text/css", web::StaticFileServer::getContentType("/style.css"));
    TEST_ASSERT_EQUAL_STRING("application/javascript", web::StaticFileServer::getContentType("/app.js"));
    TEST_ASSERT_EQUAL_STRING("application/json", web::StaticFileServer::getContentType("/config.json"));
    TEST_ASSERT_EQUAL_STRING("image/svg+xml", web::StaticFileServer::getContentType("/logo.svg"));
}

// 3. JSON Telemetry Serialization Test
static void test_web_json_serialization_deep()
{
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
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "stateEnum", t.stateEnum);
    cJSON_AddStringToObject(root, "state", t.stateStr.c_str());
    cJSON_AddBoolToObject(root, "preheatDone", t.preheatDone);
    cJSON_AddNumberToObject(root, "topTemp", t.topTemp);
    cJSON_AddNumberToObject(root, "bottomTemp", t.bottomTemp);
    cJSON_AddNumberToObject(root, "elapsed", t.elapsedSec);
    cJSON_AddBoolToObject(root, "lamp", t.lamp);

    char* rendered = cJSON_PrintUnformatted(root);
    TEST_ASSERT_NOT_NULL(rendered);

    cJSON *parsed = cJSON_Parse(rendered);
    TEST_ASSERT_NOT_NULL(parsed);

    cJSON *st = cJSON_GetObjectItem(parsed, "stateEnum");
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_INT(3, st->valueint);

    cJSON *lampItem = cJSON_GetObjectItem(parsed, "lamp");
    TEST_ASSERT_NOT_NULL(lampItem);
    TEST_ASSERT_TRUE(cJSON_IsTrue(lampItem));

    free(rendered);
    cJSON_Delete(root);
    cJSON_Delete(parsed);
}

// 4. Telemetry Precision & Rounding Test
static void test_web_telemetry_precision_serialization()
{
    web::TelemetryData t;
    t.stateEnum = 2;
    t.stateStr = "SOAK";
    t.preheatDone = false;
    // Feed floating point values with extra decimal noise
    t.topTemp = 150.567f;       // Should format to 150.6 (%.1f)
    t.bottomTemp = 140.043f;    // Should format to 140.0 (%.1f)
    t.topSet = 150.0f;
    t.bottomSet = 140.0f;
    t.topPower = 75.4f;         // Should format to 75 (%d)
    t.bottomPower = 60.8f;      // Should format to 61 (%d)
    t.topPidKp = 2.456f;        // Should format to 2.46 (%.2f)
    t.topPidKi = 0.0526f;       // Should format to 0.053 (%.3f)
    t.topPidKd = 1.234f;        // Should format to 1.23 (%.2f)
    t.bottomPidKp = 2.101f;     // Should format to 2.10 (%.2f)
    t.bottomPidKi = 0.0401f;    // Should format to 0.040 (%.3f)
    t.bottomPidKd = 0.999f;     // Should format to 1.00 (%.2f)

    std::string jsonStr = web::WebSocketHandler::serializeTelemetry(t);
    TEST_ASSERT_FALSE(jsonStr.empty());

    cJSON *root = cJSON_Parse(jsonStr.c_str());
    TEST_ASSERT_NOT_NULL(root);

    // Verify temperatures rounded to 1 decimal place
    cJSON *topTemp = cJSON_GetObjectItem(root, "topTemp");
    TEST_ASSERT_NOT_NULL(topTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.6f, static_cast<float>(topTemp->valuedouble));

    cJSON *botTemp = cJSON_GetObjectItem(root, "bottomTemp");
    TEST_ASSERT_NOT_NULL(botTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 140.0f, static_cast<float>(botTemp->valuedouble));

    // Verify PID Gains precision
    cJSON *topKp = cJSON_GetObjectItem(root, "topPidKp");
    TEST_ASSERT_NOT_NULL(topKp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.46f, static_cast<float>(topKp->valuedouble));

    cJSON *topKi = cJSON_GetObjectItem(root, "topPidKi");
    TEST_ASSERT_NOT_NULL(topKi);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.053f, static_cast<float>(topKi->valuedouble));

    cJSON *topKd = cJSON_GetObjectItem(root, "topPidKd");
    TEST_ASSERT_NOT_NULL(topKd);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.23f, static_cast<float>(topKd->valuedouble));

    cJSON *botKi = cJSON_GetObjectItem(root, "bottomPidKi");
    TEST_ASSERT_NOT_NULL(botKi);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.040f, static_cast<float>(botKi->valuedouble));

    cJSON_Delete(root);
}

#include "web/rest_api.hpp"

// 5. URI Profile Path Parameter Extraction Test
static void test_web_profile_path_param_extraction()
{
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
    TEST_ASSERT_EQUAL_STRING("My Custom Profile.json", 
        web::RestApi::extractProfileFilenameFromUri("/api/profiles/My%20Custom%20Profile.json").c_str());

    // 5. Stripping query params attached to path
    TEST_ASSERT_EQUAL_STRING("custom.json", 
        web::RestApi::extractProfileFilenameFromUri("/api/profiles/custom.json?t=123456").c_str());

    // 6. Legacy path /api/profile/...
    TEST_ASSERT_EQUAL_STRING("leaded.json", 
        web::RestApi::extractProfileFilenameFromUri("/api/profile/leaded.json").c_str());

    // 7. Query param fallback (?file=XYZ)
    TEST_ASSERT_EQUAL_STRING("fallback.json", 
        web::RestApi::extractProfileFilenameFromUri("/api/profiles", "file=fallback.json").c_str());
}

// 6. Mismatched Payload Content Detection Test
static void test_web_mismatched_payload_detection()
{
    // 1. Settings JSON containing Profile fields (stepsTop / stepsBottom)
    const char* profileJson = "{\"name\":\"Test\",\"stepsBottom\":[{\"temp\":150,\"time\":30,\"ramp\":1.0}]}";
    cJSON* pRoot = cJSON_Parse(profileJson);
    TEST_ASSERT_NOT_NULL(pRoot);
    bool isProfileInSettings = (cJSON_GetObjectItem(pRoot, "stepsTop") || cJSON_GetObjectItem(pRoot, "stepsBottom") ||
                                cJSON_GetObjectItem(pRoot, "topSteps") || cJSON_GetObjectItem(pRoot, "bottomSteps"));
    TEST_ASSERT_TRUE(isProfileInSettings);
    cJSON_Delete(pRoot);

    // 2. Profile JSON containing Settings fields (simulationMode / maxTempTop)
    const char* settingsJson = "{\"simulationMode\":true,\"maxTempTop\":250.0,\"topKp\":2.0}";
    cJSON* sRoot = cJSON_Parse(settingsJson);
    TEST_ASSERT_NOT_NULL(sRoot);
    bool isSettingsInProfile = (cJSON_GetObjectItem(sRoot, "maxTempTop") || cJSON_GetObjectItem(sRoot, "simulationMode") ||
                                cJSON_GetObjectItem(sRoot, "topKp"));
    TEST_ASSERT_TRUE(isSettingsInProfile);
    cJSON_Delete(sRoot);

    // 3. PID Library JSON containing Profile fields
    const char* pidJson = "{\"top\":[{\"temp\":100,\"kp\":2.0,\"ki\":0.02,\"kd\":0.5}]}";
    cJSON* pidRoot = cJSON_Parse(pidJson);
    TEST_ASSERT_NOT_NULL(pidRoot);
    bool isProfileInPid = (cJSON_GetObjectItem(pidRoot, "stepsTop") || cJSON_GetObjectItem(pidRoot, "stepsBottom") ||
                           cJSON_GetObjectItem(pidRoot, "maxTempTop"));
    TEST_ASSERT_FALSE(isProfileInPid); // Valid PID library does not contain profile or settings fields
    cJSON_Delete(pidRoot);
}

// 7. Domain Model Range Validations Test
static void test_web_domain_model_range_validations()
{
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

    // 3. PidLibrary Validation
    config::PidLibrary pidLib;
    TEST_ASSERT_FALSE(pidLib.validate().valid); // Empty library is invalid

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

#include "web/wifi_manager.hpp"

// 8. Wi-Fi Default Constants Test
static void test_web_wifi_constants()
{
    TEST_ASSERT_EQUAL_STRING("BGA Reflow Controller", web::DEFAULT_AP_SSID);
    TEST_ASSERT_EQUAL_STRING("reflow123", web::DEFAULT_AP_PASSWORD);
    TEST_ASSERT_EQUAL_STRING("reflow", web::DEFAULT_MDNS_HOST);
    TEST_ASSERT_EQUAL_STRING("wifi_sec", web::NVS_WIFI_NAMESPACE);
    TEST_ASSERT_EQUAL_UINT32(4, web::MAX_WS_CLIENTS);
}

// 9. Circular Log Buffer Capture Test
static void test_web_log_buffer_capture()
{
    web::appendLogLine("[TEST] Log message 1");
    web::appendLogLine("[TEST] Log message 2");

    auto logs = web::getLatestLogs(5);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, logs.size());

    bool found1 = false;
    bool found2 = false;
    for (const auto& line : logs) {
        if (line == "[TEST] Log message 1") found1 = true;
        if (line == "[TEST] Log message 2") found2 = true;
    }
    TEST_ASSERT_TRUE(found1);
    TEST_ASSERT_TRUE(found2);
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_web_tests()
{
    RUN_TEST(test_web_telemetry_defaults);
    RUN_TEST(test_web_mime_types);
    RUN_TEST(test_web_json_serialization_deep);
    RUN_TEST(test_web_telemetry_precision_serialization);
    RUN_TEST(test_web_profile_path_param_extraction);
    RUN_TEST(test_web_mismatched_payload_detection);
    RUN_TEST(test_web_domain_model_range_validations);
    RUN_TEST(test_web_wifi_constants);
    RUN_TEST(test_web_log_buffer_capture);
}
