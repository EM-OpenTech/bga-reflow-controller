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
 * @file json_serializer.cpp
 * @brief Implementation of JSON serialization & schema migration engine.
 *
 * Encapsulates cJSON memory lifecycle using RAII smart pointers, linear guard clauses,
 * schema version validation, IEEE-754 precision rounding, and robust default fallbacks.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "storage/json_serializer.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include <cmath>
#include <memory>
#include <utility>

namespace storage {

namespace {

constexpr const char* TAG = "JSON_SERIALIZER";

// ============================================================================
// Internal Helper Utilities (cJSON Safe Extraction & Decimal Rounding)
// ============================================================================

struct CJsonDeleter {
    void operator()(cJSON* ptr) const {
        if (ptr) {
            cJSON_Delete(ptr);
        }
    }
};

using UniqueCJson = std::unique_ptr<cJSON, CJsonDeleter>;

inline double round1(float v) {
    return static_cast<double>(roundf(v * 10.0f) / 10.0f);
}
inline double round2(float v) {
    return static_cast<double>(roundf(v * 100.0f) / 100.0f);
}
inline double round3(float v) {
    return static_cast<double>(roundf(v * 1000.0f) / 1000.0f);
}

inline void addBool(cJSON* obj, const char* name, bool value) {
    cJSON_AddBoolToObject(obj, name, value ? 1 : 0);
}

bool getBool(cJSON* obj, const char* key, bool def) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsBool(item) != 0) ? (cJSON_IsTrue(item) != 0) : def;
}

float getFloatRounded(cJSON* obj, const char* key, float def, float scale) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    if ((cJSON_IsNumber(item) == 0) || std::isnan(item->valuedouble) || std::isinf(item->valuedouble)) {
        return def;
    }
    return roundf(static_cast<float>(item->valuedouble) * scale) / scale;
}

inline float getFloat1(cJSON* obj, const char* key, float def) {
    return getFloatRounded(obj, key, def, 10.0f);
}
inline float getFloat2(cJSON* obj, const char* key, float def) {
    return getFloatRounded(obj, key, def, 100.0f);
}
inline float getFloat3(cJSON* obj, const char* key, float def) {
    return getFloatRounded(obj, key, def, 1000.0f);
}

uint32_t getUint(cJSON* obj, const char* key, uint32_t def) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsNumber(item) != 0) ? static_cast<uint32_t>(item->valueint) : def;
}

std::string getString(cJSON* obj, const char* key, const std::string& def) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsString(item) != 0) ? std::string(item->valuestring) : def;
}

bool parseProfileSteps(cJSON* array, std::vector<config::ProfileStep>& outSteps) {
    if (!array) {
        return true;
    }
    if (cJSON_IsArray(array) == 0) {
        return false;
    }
    int count = cJSON_GetArraySize(array);
    if (count < 0 || std::cmp_greater(count, config::Limits::MAX_PROFILE_STEPS)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(array, i);
        if (!item) {
            continue;
        }
        config::ProfileStep step;
        step.temp = getFloat1(item, "temp", step.temp);
        step.time = getUint(item, "time", step.time);
        step.ramp = getFloat2(item, "ramp", step.ramp);
        outSteps.push_back(step);
    }
    return true;
}

void addProfileStepsArray(cJSON* rootObj, const char* key, const std::vector<config::ProfileStep>& steps) {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& step : steps) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "temp", round1(step.temp));
        cJSON_AddNumberToObject(item, "time", step.time);
        cJSON_AddNumberToObject(item, "ramp", round2(step.ramp));
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddItemToObject(rootObj, key, arr);
}

bool parsePidPoints(cJSON* arr, std::vector<config::PidPoint>& outPoints) {
    if (!arr) {
        return true;
    }
    if (cJSON_IsArray(arr) == 0) {
        return false;
    }
    int count = cJSON_GetArraySize(arr);
    if (count < 0 || std::cmp_greater(count, config::Limits::MAX_PID_POINTS)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(arr, i);
        if (!item) {
            continue;
        }
        config::PidPoint pt;
        pt.temp = getFloat1(item, "temp", pt.temp);
        pt.kp = getFloat2(item, "kp", pt.kp);
        pt.ki = getFloat3(item, "ki", pt.ki);
        pt.kd = getFloat2(item, "kd", pt.kd);
        outPoints.push_back(pt);
    }
    return true;
}

void addPidPointsArray(cJSON* rootObj, const char* key, const std::vector<config::PidPoint>& points) {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& pt : points) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "temp", round1(pt.temp));
        cJSON_AddNumberToObject(item, "kp", round2(pt.kp));
        cJSON_AddNumberToObject(item, "ki", round3(pt.ki));
        cJSON_AddNumberToObject(item, "kd", round2(pt.kd));
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddItemToObject(rootObj, key, arr);
}

} // anonymous namespace

// ============================================================================
// Machine Settings Serializer
// ============================================================================

bool JsonSerializer::serializeSettings(const config::MachineSettings& settings, std::string& outJson) {
    auto vres = settings.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "serializeSettings rejected: %s", vres.errorMessage ? vres.errorMessage : "invalid settings");
        return false;
    }

    UniqueCJson root(cJSON_CreateObject());
    if (!root) {
        return false;
    }

    // Schema Version
    cJSON_AddNumberToObject(root.get(), "schemaVersion", config::Schema::MACHINE_SETTINGS);

    // System & UI Preferences
    addBool(root.get(), "simulationMode", settings.simulationMode);
    cJSON_AddStringToObject(root.get(), "language", settings.language.c_str());
    cJSON_AddStringToObject(root.get(), "theme", settings.theme.c_str());
    addBool(root.get(), "hardwareBuzzerEnabled", settings.hardwareBuzzerEnabled);
    cJSON_AddStringToObject(root.get(), "defaultProfile", settings.defaultProfile.c_str());
    addBool(root.get(), "showZones", settings.showZones);
    addBool(root.get(), "showTalLine", settings.showTalLine);
    addBool(root.get(), "showStepMarkers", settings.showStepMarkers);
    addBool(root.get(), "showPidGains", settings.showPidGains);

    // Thermal Safety Limits
    cJSON_AddNumberToObject(root.get(), "maxTempTop", round1(settings.maxTempTop));
    cJSON_AddNumberToObject(root.get(), "maxTempBottom", round1(settings.maxTempBottom));
    cJSON_AddNumberToObject(root.get(), "minTempTop", round1(settings.minTempTop));
    cJSON_AddNumberToObject(root.get(), "minTempBottom", round1(settings.minTempBottom));
    cJSON_AddNumberToObject(root.get(), "coolingSafeTemp", round1(settings.coolingSafeTemp));
    addBool(root.get(), "enableSafetyWatchdog", settings.enableSafetyWatchdog);

    // Stuck SSR Watchdog
    addBool(root.get(), "enableStuckSsrCheck", settings.enableStuckSsrCheck);
    cJSON_AddNumberToObject(root.get(), "stuckSsrRiseThreshold", round2(settings.stuckSsrRiseThreshold));
    cJSON_AddNumberToObject(root.get(), "stuckSsrWindowSec", settings.stuckSsrWindowSec);

    // Heater No-Rise Watchdog
    addBool(root.get(), "enableNoRiseCheck", settings.enableNoRiseCheck);
    cJSON_AddNumberToObject(root.get(), "noRiseThreshold", round2(settings.noRiseThreshold));
    cJSON_AddNumberToObject(root.get(), "noRiseTimeoutSec", settings.noRiseTimeoutSec);

    // FSM Hold Gate & Settle Tolerances
    cJSON_AddNumberToObject(root.get(), "holdLowTolerance", round2(settings.holdLowTolerance));
    cJSON_AddNumberToObject(root.get(), "holdHighTolerance", round2(settings.holdHighTolerance));
    cJSON_AddNumberToObject(root.get(), "settleTimeS", settings.settleTimeS);

    // Process Timings & Fan
    cJSON_AddNumberToObject(root.get(), "fanCoolingDelayS", settings.fanCoolingDelayS);
    cJSON_AddNumberToObject(root.get(), "fanCoolingDurationS", settings.fanCoolingDurationS);

    // Sensor Configuration (MAX31856)
    addBool(root.get(), "emaFilterEnabled", settings.emaFilterEnabled);
    cJSON_AddNumberToObject(root.get(), "emaAlpha", round3(settings.emaAlpha));
    cJSON_AddNumberToObject(root.get(), "faultStreakLimit", settings.faultStreakLimit);
    cJSON_AddNumberToObject(root.get(), "topCjOffset", round1(settings.topCjOffset));
    cJSON_AddNumberToObject(root.get(), "bottomCjOffset", round1(settings.bottomCjOffset));

    // SSR BurstFire Windows
    cJSON_AddNumberToObject(root.get(), "topBurstWindowMs", settings.topBurstWindowMs);
    cJSON_AddNumberToObject(root.get(), "bottomBurstWindowMs", settings.bottomBurstWindowMs);

    // PID Parameters
    addBool(root.get(), "pidLibraryEnabled", settings.pidLibraryEnabled);
    cJSON_AddNumberToObject(root.get(), "topKp", round2(settings.topKp));
    cJSON_AddNumberToObject(root.get(), "topKi", round3(settings.topKi));
    cJSON_AddNumberToObject(root.get(), "topKd", round2(settings.topKd));
    cJSON_AddNumberToObject(root.get(), "bottomKp", round2(settings.bottomKp));
    cJSON_AddNumberToObject(root.get(), "bottomKi", round3(settings.bottomKi));
    cJSON_AddNumberToObject(root.get(), "bottomKd", round2(settings.bottomKd));

    char* rendered = cJSON_Print(root.get());
    if (!rendered) {
        return false;
    }
    outJson = rendered;
    cJSON_free(rendered);
    return true;
}

bool JsonSerializer::deserializeSettings(const std::string& json, config::MachineSettings& outSettings,
                                         bool& outSchemaIncompatible) {
    UniqueCJson root(cJSON_Parse(json.c_str()));
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse settings JSON - file corrupted");
        return false;
    }

    auto schemaVersion = static_cast<uint16_t>(getUint(root.get(), "schemaVersion", 0));
    if (schemaVersion > config::Schema::MACHINE_SETTINGS) {
        ESP_LOGE(TAG, "Rejecting settings: Schema version (%u) is newer than supported (%u)", schemaVersion,
                 config::Schema::MACHINE_SETTINGS);
        outSchemaIncompatible = true;
        return false;
    }

    outSettings.simulationMode = getBool(root.get(), "simulationMode", outSettings.simulationMode);
    outSettings.language = getString(root.get(), "language", outSettings.language);
    outSettings.theme = getString(root.get(), "theme", outSettings.theme);
    outSettings.hardwareBuzzerEnabled = getBool(root.get(), "hardwareBuzzerEnabled", outSettings.hardwareBuzzerEnabled);
    outSettings.defaultProfile = getString(root.get(), "defaultProfile", outSettings.defaultProfile);
    outSettings.showZones = getBool(root.get(), "showZones", outSettings.showZones);
    outSettings.showTalLine = getBool(root.get(), "showTalLine", outSettings.showTalLine);
    outSettings.showStepMarkers = getBool(root.get(), "showStepMarkers", outSettings.showStepMarkers);
    outSettings.showPidGains = getBool(root.get(), "showPidGains", outSettings.showPidGains);

    outSettings.maxTempTop = getFloat1(root.get(), "maxTempTop", outSettings.maxTempTop);
    outSettings.maxTempBottom = getFloat1(root.get(), "maxTempBottom", outSettings.maxTempBottom);
    outSettings.minTempTop = getFloat1(root.get(), "minTempTop", outSettings.minTempTop);
    outSettings.minTempBottom = getFloat1(root.get(), "minTempBottom", outSettings.minTempBottom);
    outSettings.coolingSafeTemp = getFloat1(root.get(), "coolingSafeTemp", outSettings.coolingSafeTemp);
    outSettings.enableSafetyWatchdog = getBool(root.get(), "enableSafetyWatchdog", outSettings.enableSafetyWatchdog);

    outSettings.enableStuckSsrCheck = getBool(root.get(), "enableStuckSsrCheck", outSettings.enableStuckSsrCheck);
    outSettings.stuckSsrRiseThreshold =
        getFloat2(root.get(), "stuckSsrRiseThreshold", outSettings.stuckSsrRiseThreshold);
    outSettings.stuckSsrWindowSec = getUint(root.get(), "stuckSsrWindowSec", outSettings.stuckSsrWindowSec);

    outSettings.enableNoRiseCheck = getBool(root.get(), "enableNoRiseCheck", outSettings.enableNoRiseCheck);
    outSettings.noRiseThreshold = getFloat2(root.get(), "noRiseThreshold", outSettings.noRiseThreshold);
    outSettings.noRiseTimeoutSec = getUint(root.get(), "noRiseTimeoutSec", outSettings.noRiseTimeoutSec);

    outSettings.holdLowTolerance = getFloat2(root.get(), "holdLowTolerance", outSettings.holdLowTolerance);
    outSettings.holdHighTolerance = getFloat2(root.get(), "holdHighTolerance", outSettings.holdHighTolerance);
    outSettings.settleTimeS = getUint(root.get(), "settleTimeS", outSettings.settleTimeS);

    outSettings.fanCoolingDelayS = getUint(root.get(), "fanCoolingDelayS", outSettings.fanCoolingDelayS);
    outSettings.fanCoolingDurationS = getUint(root.get(), "fanCoolingDurationS", outSettings.fanCoolingDurationS);

    outSettings.emaFilterEnabled = getBool(root.get(), "emaFilterEnabled", outSettings.emaFilterEnabled);
    outSettings.emaAlpha = getFloat3(root.get(), "emaAlpha", outSettings.emaAlpha);
    outSettings.faultStreakLimit =
        static_cast<uint8_t>(getUint(root.get(), "faultStreakLimit", outSettings.faultStreakLimit));
    outSettings.topCjOffset = getFloat1(root.get(), "topCjOffset", outSettings.topCjOffset);
    outSettings.bottomCjOffset = getFloat1(root.get(), "bottomCjOffset", outSettings.bottomCjOffset);

    outSettings.topBurstWindowMs = getUint(root.get(), "topBurstWindowMs", outSettings.topBurstWindowMs);
    outSettings.bottomBurstWindowMs = getUint(root.get(), "bottomBurstWindowMs", outSettings.bottomBurstWindowMs);

    outSettings.pidLibraryEnabled = getBool(root.get(), "pidLibraryEnabled", outSettings.pidLibraryEnabled);
    outSettings.topKp = getFloat2(root.get(), "topKp", outSettings.topKp);
    outSettings.topKi = getFloat3(root.get(), "topKi", outSettings.topKi);
    outSettings.topKd = getFloat2(root.get(), "topKd", outSettings.topKd);
    outSettings.bottomKp = getFloat2(root.get(), "bottomKp", outSettings.bottomKp);
    outSettings.bottomKi = getFloat3(root.get(), "bottomKi", outSettings.bottomKi);
    outSettings.bottomKd = getFloat2(root.get(), "bottomKd", outSettings.bottomKd);

    auto vres = outSettings.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "Loaded settings failed validation: %s", vres.errorMessage ? vres.errorMessage : "invalid");
        outSettings = config::MachineSettings{};
        return false;
    }
    return true;
}

// ============================================================================
// Reflow Profile Serializer
// ============================================================================

bool JsonSerializer::serializeProfile(const config::ReflowProfile& profile, std::string& outJson) {
    auto vres = profile.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "serializeProfile rejected: %s", vres.errorMessage ? vres.errorMessage : "invalid");
        return false;
    }

    UniqueCJson root(cJSON_CreateObject());
    if (!root) {
        return false;
    }

    cJSON_AddNumberToObject(root.get(), "schemaVersion", config::Schema::REFLOW_PROFILE);
    cJSON_AddStringToObject(root.get(), "name", profile.name.c_str());
    cJSON_AddStringToObject(root.get(), "file", profile.file.c_str());

    addProfileStepsArray(root.get(), "stepsTop", profile.stepsTop);
    addProfileStepsArray(root.get(), "stepsBottom", profile.stepsBottom);

    char* rendered = cJSON_Print(root.get());
    if (!rendered) {
        return false;
    }
    outJson = rendered;
    cJSON_free(rendered);
    return true;
}

bool JsonSerializer::deserializeProfile(const std::string& json, const std::string& filename,
                                        config::ReflowProfile& outProfile, bool& outSchemaIncompatible) {
    UniqueCJson root(cJSON_Parse(json.c_str()));
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse profile JSON: %s", filename.c_str());
        return false;
    }

    outProfile.file = filename;
    auto schemaVer = static_cast<uint16_t>(getUint(root.get(), "schemaVersion", 0));
    if (schemaVer > config::Schema::REFLOW_PROFILE) {
        ESP_LOGE(TAG, "Rejecting profile '%s': Schema version (%u) is newer than supported (%u)", filename.c_str(),
                 schemaVer, config::Schema::REFLOW_PROFILE);
        outSchemaIncompatible = true;
        return false;
    }

    outProfile.name = getString(root.get(), "name", filename);
    if (outProfile.name.empty() || outProfile.name.length() > config::Limits::MAX_NAME_LENGTH) {
        ESP_LOGE(TAG, "Profile '%s' invalid: Name is empty or exceeds limit", filename.c_str());
        return false;
    }

    outProfile.stepsTop.clear();
    outProfile.stepsBottom.clear();

    if (!parseProfileSteps(cJSON_GetObjectItem(root.get(), "stepsTop"), outProfile.stepsTop) ||
        !parseProfileSteps(cJSON_GetObjectItem(root.get(), "stepsBottom"), outProfile.stepsBottom)) {
        ESP_LOGE(TAG, "Profile '%s' invalid: Step count exceeds limit", filename.c_str());
        outProfile.stepsTop.clear();
        outProfile.stepsBottom.clear();
        return false;
    }

    auto vres = outProfile.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "Profile '%s' validation failed: %s", filename.c_str(),
                 vres.errorMessage ? vres.errorMessage : "invalid");
        outProfile.stepsTop.clear();
        outProfile.stepsBottom.clear();
        return false;
    }

    return true;
}

// ============================================================================
// PID Library Serializer
// ============================================================================

bool JsonSerializer::serializePidLibrary(const config::PidLibrary& library, std::string& outJson) {
    auto vres = library.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "serializePidLibrary rejected: %s", vres.errorMessage ? vres.errorMessage : "invalid");
        return false;
    }

    UniqueCJson root(cJSON_CreateObject());
    if (!root) {
        return false;
    }

    cJSON_AddNumberToObject(root.get(), "schemaVersion", config::Schema::PID_LIBRARY);
    addPidPointsArray(root.get(), "top", library.top);
    addPidPointsArray(root.get(), "bottom", library.bottom);

    char* rendered = cJSON_Print(root.get());
    if (!rendered) {
        return false;
    }
    outJson = rendered;
    cJSON_free(rendered);
    return true;
}

bool JsonSerializer::deserializePidLibrary(const std::string& json, config::PidLibrary& outLibrary,
                                           bool& outSchemaIncompatible) {
    UniqueCJson root(cJSON_Parse(json.c_str()));
    if (!root) {
        return false;
    }

    auto schemaVer = static_cast<uint16_t>(getUint(root.get(), "schemaVersion", 0));
    if (schemaVer > config::Schema::PID_LIBRARY) {
        ESP_LOGE(TAG, "Rejecting pid_library.json: Schema version (%u) is newer than supported (%u)", schemaVer,
                 config::Schema::PID_LIBRARY);
        outSchemaIncompatible = true;
        return false;
    }

    outLibrary.top.clear();
    outLibrary.bottom.clear();
    if (!parsePidPoints(cJSON_GetObjectItem(root.get(), "top"), outLibrary.top) ||
        !parsePidPoints(cJSON_GetObjectItem(root.get(), "bottom"), outLibrary.bottom)) {
        ESP_LOGE(TAG, "PID Library invalid: Point count exceeds limit");
        outLibrary.top.clear();
        outLibrary.bottom.clear();
        return false;
    }

    auto vres = outLibrary.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "PID Library validation failed: %s", vres.errorMessage ? vres.errorMessage : "invalid");
        outLibrary.top.clear();
        outLibrary.bottom.clear();
        return false;
    }

    return true;
}

} // namespace storage
