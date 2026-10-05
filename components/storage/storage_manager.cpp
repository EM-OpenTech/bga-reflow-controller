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
 * @file storage_manager.cpp
 * @brief Implementation of LittleFS storage manager for settings, profiles and PID libraries.
 *
 * Handles atomic file writes via .tmp staging, JSON schema versioning and migrations,
 * default file initialization, and thread-safe LittleFS access.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "storage/storage_manager.hpp"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <cmath>

static const char* TAG = "STORAGE";

namespace storage {

// ============================================================================
// Lifecycle & LittleFS VFS Mount Management
// ============================================================================

StorageManager::StorageManager()
    : _basePath("/littlefs"), _partitionLabel("littlefs"), _mounted(false), _schemaIncompatible(false), _mutex(nullptr) {
    _mutex = xSemaphoreCreateMutex();
}

StorageManager::~StorageManager() {
    stop();
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

void StorageManager::lock() {
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }
}

void StorageManager::unlock() {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

esp_err_t StorageManager::begin(const char* basePath, const char* partitionLabel) {
    lock();
    if (_mounted) {
        unlock();
        return ESP_OK;
    }

    _basePath = basePath ? basePath : "/littlefs";
    _partitionLabel = partitionLabel ? partitionLabel : "littlefs";

    ESP_LOGI(TAG, "Mounting LittleFS partition '%s' at '%s'", _partitionLabel.c_str(), _basePath.c_str());

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = _basePath.c_str();
    conf.partition_label = _partitionLabel.c_str();
    conf.format_if_mount_failed = true; // Auto-format on blank flash / first boot
    conf.dont_mount = false;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "LittleFS mount failed! Partition may be corrupted.");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "LittleFS partition '%s' not found in partition table!", _partitionLabel.c_str());
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s).", esp_err_to_name(ret));
        }
        unlock();
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(_partitionLabel.c_str(), &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS Mounted successfully: Partition size: %zu KB, Used: %zu KB", total / 1024, used / 1024);
    }

    _mounted = true;
    unlock();

    createDirectories();
    createDefaultFilesIfMissing();

    return ESP_OK;
}

void StorageManager::stop() {
    lock();
    if (_mounted) {
        esp_vfs_littlefs_unregister(_partitionLabel.c_str());
        _mounted = false;
        ESP_LOGI(TAG, "LittleFS unmounted.");
    }
    unlock();
}

bool StorageManager::getStorageInfo(size_t& totalBytes, size_t& usedBytes) {
    lock();
    if (!_mounted) {
        unlock();
        return false;
    }
    esp_err_t ret = esp_littlefs_info(_partitionLabel.c_str(), &totalBytes, &usedBytes);
    unlock();
    return (ret == ESP_OK);
}

void StorageManager::createDirectories() {
    struct stat st;
    std::string webDir = _basePath + "/web";
    std::string configDir = _basePath + "/config";
    std::string profilesDir = _basePath + "/profiles";

    if (stat(webDir.c_str(), &st) != 0) mkdir(webDir.c_str(), 0755);
    if (stat(configDir.c_str(), &st) != 0) mkdir(configDir.c_str(), 0755);
    if (stat(profilesDir.c_str(), &st) != 0) mkdir(profilesDir.c_str(), 0755);
}

// ============================================================================
// File System & Atomic Staging Operations
// ============================================================================

bool StorageManager::fileExists(const std::string& filepath) {
    struct stat st;
    return (stat(filepath.c_str(), &st) == 0);
}

bool StorageManager::readTextFile(const std::string& filepath, std::string& outContent) {
    FILE* f = fopen(filepath.c_str(), "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", filepath.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        outContent = "";
        return true;
    }

    outContent.resize(size);
    size_t readBytes = fread(&outContent[0], 1, size, f);
    fclose(f);

    outContent.resize(readBytes);
    return true;
}

bool StorageManager::writeTextFileAtomic(const std::string& filepath, const std::string& content) {
    std::string tmpPath = filepath + ".tmp";
    FILE* f = fopen(tmpPath.c_str(), "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open temp file for writing: %s", tmpPath.c_str());
        return false;
    }

    size_t written = fwrite(content.c_str(), 1, content.length(), f);
    fflush(f);
    fclose(f);

    if (written != content.length()) {
        ESP_LOGE(TAG, "Incomplete write to temp file: %s (%zu / %zu bytes)", tmpPath.c_str(), written, content.length());
        remove(tmpPath.c_str());
        return false;
    }

    remove(filepath.c_str());
    if (rename(tmpPath.c_str(), filepath.c_str()) != 0) {
        ESP_LOGE(TAG, "Failed to rename temp file %s to %s", tmpPath.c_str(), filepath.c_str());
        return false;
    }

    return true;
}

// ============================================================================
// MACHINE SETTINGS
// ============================================================================
bool StorageManager::loadSettings(config::MachineSettings& settings) {
    lock();
    std::string filePath = _basePath + "/config/settings.json";
    std::string jsonStr;

    if (!readTextFile(filePath, jsonStr) || jsonStr.empty()) {
        ESP_LOGW(TAG, "settings.json not found, creating default settings");
        unlock();
        saveSettings(settings);
        return true;
    }

    cJSON* root = cJSON_Parse(jsonStr.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse settings.json - file may be corrupted");
        unlock();
        return false;
    }

    auto getBool = [](cJSON* obj, const char* key, bool def) {
        cJSON* item = cJSON_GetObjectItem(obj, key);
        return cJSON_IsBool(item) ? cJSON_IsTrue(item) : def;
    };
    auto getFloat1 = [](cJSON* obj, const char* key, float def) {
        cJSON* item = cJSON_GetObjectItem(obj, key);
        if (!cJSON_IsNumber(item)) return def;
        return roundf(static_cast<float>(item->valuedouble) * 10.0f) / 10.0f;
    };
    // Round to 2 decimal places to eliminate IEEE 754 float32 artifacts
    auto getFloat2 = [](cJSON* obj, const char* key, float def) {
        cJSON* item = cJSON_GetObjectItem(obj, key);
        if (!cJSON_IsNumber(item)) return def;
        return roundf(static_cast<float>(item->valuedouble) * 100.0f) / 100.0f;
    };
    // Round to 3 decimal places (for Ki & Alpha which have fine precision)
    auto getFloat3 = [](cJSON* obj, const char* key, float def) {
        cJSON* item = cJSON_GetObjectItem(obj, key);
        if (!cJSON_IsNumber(item)) return def;
        return roundf(static_cast<float>(item->valuedouble) * 1000.0f) / 1000.0f;
    };
    auto getUint = [](cJSON* obj, const char* key, uint32_t def) {
        cJSON* item = cJSON_GetObjectItem(obj, key);
        return cJSON_IsNumber(item) ? static_cast<uint32_t>(item->valueint) : def;
    };
    auto getString = [](cJSON* obj, const char* key, const std::string& def) {
        cJSON* item = cJSON_GetObjectItem(obj, key);
        return cJSON_IsString(item) ? std::string(item->valuestring) : def;
    };

    // Schema Version & Migration
    uint16_t schemaVersion = static_cast<uint16_t>(getUint(root, "schemaVersion", 0));
    if (schemaVersion > config::Schema::MACHINE_SETTINGS) {
        ESP_LOGE(TAG, "Rejecting settings.json: Schema version (%u) is newer than supported firmware schema (%u)",
                 schemaVersion, config::Schema::MACHINE_SETTINGS);
        _schemaIncompatible = true;
        cJSON_Delete(root);
        unlock();
        return false;
    }
    if (schemaVersion < 1) {
        ESP_LOGI(TAG, "Migrated settings.json from legacy (v0) to schema v%u", config::Schema::MACHINE_SETTINGS);
    }

    // System & UI Preferences
    settings.simulationMode        = getBool(root, "simulationMode", settings.simulationMode);
    settings.language              = getString(root, "language", settings.language);
    settings.theme                 = getString(root, "theme", settings.theme);
    settings.hardwareBuzzerEnabled = getBool(root, "hardwareBuzzerEnabled", settings.hardwareBuzzerEnabled);
    settings.defaultProfile        = getString(root, "defaultProfile", settings.defaultProfile);
    settings.showZones             = getBool(root, "showZones", settings.showZones);
    settings.showTalLine           = getBool(root, "showTalLine", settings.showTalLine);
    settings.showStepMarkers       = getBool(root, "showStepMarkers", settings.showStepMarkers);
    settings.showPidGains          = getBool(root, "showPidGains", settings.showPidGains);

    // Thermal Safety Limits (1 decimal place)
    settings.maxTempTop            = getFloat1(root, "maxTempTop", settings.maxTempTop);
    settings.maxTempBottom         = getFloat1(root, "maxTempBottom", settings.maxTempBottom);
    settings.minTempTop            = getFloat1(root, "minTempTop", settings.minTempTop);
    settings.minTempBottom         = getFloat1(root, "minTempBottom", settings.minTempBottom);
    settings.coolingSafeTemp       = getFloat1(root, "coolingSafeTemp", settings.coolingSafeTemp);
    settings.enableSafetyWatchdog  = getBool(root, "enableSafetyWatchdog", settings.enableSafetyWatchdog);

    // Stuck SSR Watchdog
    settings.enableStuckSsrCheck   = getBool(root, "enableStuckSsrCheck", settings.enableStuckSsrCheck);
    settings.stuckSsrRiseThreshold = getFloat2(root, "stuckSsrRiseThreshold", settings.stuckSsrRiseThreshold);
    settings.stuckSsrWindowSec     = getUint(root, "stuckSsrWindowSec", settings.stuckSsrWindowSec);

    // Heater No-Rise Watchdog
    settings.enableNoRiseCheck     = getBool(root, "enableNoRiseCheck", settings.enableNoRiseCheck);
    settings.noRiseThreshold       = getFloat2(root, "noRiseThreshold", settings.noRiseThreshold);
    settings.noRiseTimeoutSec      = getUint(root, "noRiseTimeoutSec", settings.noRiseTimeoutSec);

    // FSM Hold Gate & Settle Tolerances
    settings.holdLowTolerance      = getFloat2(root, "holdLowTolerance", settings.holdLowTolerance);
    settings.holdHighTolerance     = getFloat2(root, "holdHighTolerance", settings.holdHighTolerance);
    settings.settleTimeS           = getUint(root, "settleTimeS", settings.settleTimeS);

    // Process Timings & Fan
    settings.fanCoolingDelayS      = getUint(root, "fanCoolingDelayS", settings.fanCoolingDelayS);
    settings.fanCoolingDurationS   = getUint(root, "fanCoolingDurationS", settings.fanCoolingDurationS);

    // Sensor Configuration (MAX31856)
    settings.emaFilterEnabled      = getBool(root, "emaFilterEnabled", settings.emaFilterEnabled);
    settings.emaAlpha              = getFloat3(root, "emaAlpha", settings.emaAlpha);
    settings.faultStreakLimit      = static_cast<uint8_t>(getUint(root, "faultStreakLimit", settings.faultStreakLimit));
    settings.topCjOffset           = getFloat1(root, "topCjOffset", settings.topCjOffset);
    settings.bottomCjOffset        = getFloat1(root, "bottomCjOffset", settings.bottomCjOffset);

    // SSR BurstFire Windows
    settings.topBurstWindowMs      = getUint(root, "topBurstWindowMs", settings.topBurstWindowMs);
    settings.bottomBurstWindowMs   = getUint(root, "bottomBurstWindowMs", settings.bottomBurstWindowMs);

    // PID Parameters
    settings.pidLibraryEnabled     = getBool(root, "pidLibraryEnabled", settings.pidLibraryEnabled);
    settings.topKp                 = getFloat2(root, "topKp", settings.topKp);
    settings.topKi                 = getFloat3(root, "topKi", settings.topKi);
    settings.topKd                 = getFloat2(root, "topKd", settings.topKd);
    settings.bottomKp              = getFloat2(root, "bottomKp", settings.bottomKp);
    settings.bottomKi              = getFloat3(root, "bottomKi", settings.bottomKi);
    settings.bottomKd              = getFloat2(root, "bottomKd", settings.bottomKd);

    cJSON_Delete(root);
    unlock();
    ESP_LOGI(TAG, "Loaded settings.json (schema v%u)", schemaVersion);
    return true;
}

bool StorageManager::saveSettings(const config::MachineSettings& settings) {
    auto vres = settings.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "saveSettings rejected: %s", vres.errorMessage ? vres.errorMessage : "invalid settings");
        return false;
    }

    lock();
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        unlock();
        return false;
    }

    auto round1 = [](float v) -> double { return roundf(v * 10.0f) / 10.0; };
    auto round2 = [](float v) -> double { return roundf(v * 100.0f) / 100.0; };
    auto round3 = [](float v) -> double { return roundf(v * 1000.0f) / 1000.0; };

    // Schema Version
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

    // Thermal Safety Limits — round to 1 decimal place before serializing
    cJSON_AddNumberToObject(root, "maxTempTop",            round1(settings.maxTempTop));
    cJSON_AddNumberToObject(root, "maxTempBottom",         round1(settings.maxTempBottom));
    cJSON_AddNumberToObject(root, "minTempTop",            round1(settings.minTempTop));
    cJSON_AddNumberToObject(root, "minTempBottom",         round1(settings.minTempBottom));
    cJSON_AddNumberToObject(root, "coolingSafeTemp",       round1(settings.coolingSafeTemp));
    cJSON_AddBoolToObject(root, "enableSafetyWatchdog",     settings.enableSafetyWatchdog);

    // Stuck SSR Watchdog
    cJSON_AddBoolToObject(root, "enableStuckSsrCheck", settings.enableStuckSsrCheck);
    cJSON_AddNumberToObject(root, "stuckSsrRiseThreshold", round2(settings.stuckSsrRiseThreshold));
    cJSON_AddNumberToObject(root, "stuckSsrWindowSec",     settings.stuckSsrWindowSec);

    // Heater No-Rise Watchdog
    cJSON_AddBoolToObject(root, "enableNoRiseCheck", settings.enableNoRiseCheck);
    cJSON_AddNumberToObject(root, "noRiseThreshold",       round2(settings.noRiseThreshold));
    cJSON_AddNumberToObject(root, "noRiseTimeoutSec",      settings.noRiseTimeoutSec);

    // FSM Hold Gate & Settle Tolerances
    cJSON_AddNumberToObject(root, "holdLowTolerance",      round2(settings.holdLowTolerance));
    cJSON_AddNumberToObject(root, "holdHighTolerance",     round2(settings.holdHighTolerance));
    cJSON_AddNumberToObject(root, "settleTimeS",           settings.settleTimeS);

    // Process Timings & Fan
    cJSON_AddNumberToObject(root, "fanCoolingDelayS",      settings.fanCoolingDelayS);
    cJSON_AddNumberToObject(root, "fanCoolingDurationS",   settings.fanCoolingDurationS);

    // Sensor Configuration (MAX31856)
    cJSON_AddBoolToObject(root, "emaFilterEnabled", settings.emaFilterEnabled);
    cJSON_AddNumberToObject(root, "emaAlpha",              round3(settings.emaAlpha));
    cJSON_AddNumberToObject(root, "faultStreakLimit",      settings.faultStreakLimit);
    cJSON_AddNumberToObject(root, "topCjOffset",           round1(settings.topCjOffset));
    cJSON_AddNumberToObject(root, "bottomCjOffset",        round1(settings.bottomCjOffset));

    // SSR BurstFire Windows
    cJSON_AddNumberToObject(root, "topBurstWindowMs",      settings.topBurstWindowMs);
    cJSON_AddNumberToObject(root, "bottomBurstWindowMs",   settings.bottomBurstWindowMs);

    // PID Parameters
    cJSON_AddBoolToObject(root, "pidLibraryEnabled", settings.pidLibraryEnabled);
    cJSON_AddNumberToObject(root, "topKp",                 round2(settings.topKp));
    cJSON_AddNumberToObject(root, "topKi",                 round3(settings.topKi));
    cJSON_AddNumberToObject(root, "topKd",                 round2(settings.topKd));
    cJSON_AddNumberToObject(root, "bottomKp",              round2(settings.bottomKp));
    cJSON_AddNumberToObject(root, "bottomKi",              round3(settings.bottomKi));
    cJSON_AddNumberToObject(root, "bottomKd",              round2(settings.bottomKd));

    char* rendered = cJSON_Print(root);
    cJSON_Delete(root);

    if (!rendered) {
        ESP_LOGE(TAG, "cJSON_Print failed (out of memory) while serializing settings.json");
        unlock();
        return false;
    }

    std::string filePath = _basePath + "/config/settings.json";
    bool success = writeTextFileAtomic(filePath, std::string(rendered));
    cJSON_free(rendered);
    unlock();

    if (success) ESP_LOGI(TAG, "Saved settings.json atomically");
    return success;
}

// ============================================================================
// REFLOW PROFILES
// ============================================================================
bool StorageManager::loadProfile(const std::string& filename, config::ReflowProfile& profile) {
    if (filename.empty() || filename.find("..") != std::string::npos ||
        filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
        ESP_LOGE(TAG, "loadProfile rejected illegal filename: %s", filename.c_str());
        return false;
    }

    lock();
    std::string filePath = _basePath + "/profiles/" + filename;
    std::string jsonStr;

    if (!readTextFile(filePath, jsonStr)) {
        ESP_LOGE(TAG, "Failed to read profile file: %s", filename.c_str());
        unlock();
        return false;
    }

    cJSON* root = cJSON_Parse(jsonStr.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON in profile: %s", filename.c_str());
        unlock();
        return false;
    }

    profile.file = filename;
    cJSON* schemaObj = cJSON_GetObjectItem(root, "schemaVersion");
    uint16_t schemaVer = (schemaObj && cJSON_IsNumber(schemaObj)) ? static_cast<uint16_t>(schemaObj->valueint) : 0;
    if (schemaVer > config::Schema::REFLOW_PROFILE) {
        ESP_LOGE(TAG, "Rejecting profile '%s': Schema version (%u) is newer than supported firmware schema (%u)",
                 filename.c_str(), schemaVer, config::Schema::REFLOW_PROFILE);
        _schemaIncompatible = true;
        cJSON_Delete(root);
        unlock();
        return false;
    }
    if (schemaVer < 1) {
        ESP_LOGI(TAG, "Migrated profile '%s' from legacy (v0) to schema v%u", filename.c_str(), config::Schema::REFLOW_PROFILE);
    }

    cJSON* nameObj = cJSON_GetObjectItem(root, "name");
    profile.name = cJSON_IsString(nameObj) ? nameObj->valuestring : filename;
    if (profile.name.empty() || profile.name.length() > 30) {
        ESP_LOGE(TAG, "Failed to load profile '%s': Name is empty or exceeds 30 characters", filename.c_str());
        cJSON_Delete(root);
        unlock();
        return false;
    }

    profile.stepsTop.clear();
    profile.stepsBottom.clear();

    auto parseSteps = [](cJSON* array, std::vector<config::ProfileStep>& outSteps) -> bool {
        if (!array) return true;
        if (!cJSON_IsArray(array)) return false;
        int count = cJSON_GetArraySize(array);
        if (count > static_cast<int>(config::MAX_PROFILE_STEPS)) {
            return false;
        }
        for (int i = 0; i < count; i++) {
            cJSON* item = cJSON_GetArrayItem(array, i);
            if (!item) continue;
            config::ProfileStep step;
            cJSON* temp = cJSON_GetObjectItem(item, "temp");
            cJSON* time = cJSON_GetObjectItem(item, "time");
            cJSON* ramp = cJSON_GetObjectItem(item, "ramp");
            // Round to eliminate IEEE 754 float32 precision artifacts
            if (temp && cJSON_IsNumber(temp)) step.temp = roundf(static_cast<float>(temp->valuedouble) * 10.0f) / 10.0f;
            if (time && cJSON_IsNumber(time)) step.time = static_cast<uint32_t>(time->valueint);
            if (ramp && cJSON_IsNumber(ramp)) step.ramp = roundf(static_cast<float>(ramp->valuedouble) * 100.0f) / 100.0f;
            outSteps.push_back(step);
        }
        return true;
    };

    if (!parseSteps(cJSON_GetObjectItem(root, "stepsTop"), profile.stepsTop) ||
        !parseSteps(cJSON_GetObjectItem(root, "stepsBottom"), profile.stepsBottom)) {
        ESP_LOGE(TAG, "Failed to load profile '%s': Step count exceeds maximum allowed limit of %zu",
                 filename.c_str(), config::MAX_PROFILE_STEPS);
        profile.stepsTop.clear();
        profile.stepsBottom.clear();
        cJSON_Delete(root);
        unlock();
        return false;
    }

    auto vres = profile.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "Failed to load profile '%s': %s", filename.c_str(), vres.errorMessage ? vres.errorMessage : "invalid profile");
        profile.stepsTop.clear();
        profile.stepsBottom.clear();
        cJSON_Delete(root);
        unlock();
        return false;
    }

    cJSON_Delete(root);
    unlock();
    ESP_LOGI(TAG, "Loaded profile '%s' (schema v%u, %zu top steps, %zu bottom steps)", filename.c_str(), schemaVer, profile.stepsTop.size(), profile.stepsBottom.size());
    return true;
}

bool StorageManager::saveProfile(const config::ReflowProfile& profile) {
    auto vres = profile.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "saveProfile rejected: %s", vres.errorMessage ? vres.errorMessage : "invalid profile");
        return false;
    }

    lock();
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        unlock();
        return false;
    }

    cJSON_AddNumberToObject(root, "schemaVersion", config::Schema::REFLOW_PROFILE);
    cJSON_AddStringToObject(root, "name", profile.name.c_str());
    cJSON_AddStringToObject(root, "file", profile.file.c_str());

    auto addStepsArray = [](cJSON* rootObj, const char* key, const std::vector<config::ProfileStep>& steps) {
        cJSON* arr = cJSON_CreateArray();
        for (const auto& step : steps) {
            cJSON* item = cJSON_CreateObject();
            // Round before serializing to avoid float32 precision garbage
            double temp_rounded = roundf(step.temp * 10.0f) / 10.0;
            double ramp_rounded = roundf(step.ramp * 100.0f) / 100.0;
            cJSON_AddNumberToObject(item, "temp", temp_rounded);
            cJSON_AddNumberToObject(item, "time", step.time);
            cJSON_AddNumberToObject(item, "ramp", ramp_rounded);
            cJSON_AddItemToArray(arr, item);
        }
        cJSON_AddItemToObject(rootObj, key, arr);
    };

    addStepsArray(root, "stepsTop", profile.stepsTop);
    addStepsArray(root, "stepsBottom", profile.stepsBottom);

    char* rendered = cJSON_Print(root);
    cJSON_Delete(root);

    if (!rendered) {
        ESP_LOGE(TAG, "cJSON_Print failed (out of memory) while serializing profile '%s'", profile.file.c_str());
        unlock();
        return false;
    }

    std::string filePath = _basePath + "/profiles/" + profile.file;
    bool success = writeTextFileAtomic(filePath, std::string(rendered));
    cJSON_free(rendered);
    unlock();

    if (success) ESP_LOGI(TAG, "Saved profile '%s' atomically", profile.file.c_str());
    return success;
}

bool StorageManager::deleteProfile(const std::string& filename) {
    if (filename.empty() || filename.find("..") != std::string::npos ||
        filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
        ESP_LOGE(TAG, "deleteProfile rejected illegal filename: %s", filename.c_str());
        return false;
    }

    lock();
    if (filename == "factory-profile.json") {
        ESP_LOGW(TAG, "Cannot delete protected factory profile");
        unlock();
        return false;
    }

    std::string filePath = _basePath + "/profiles/" + filename;
    int ret = remove(filePath.c_str());
    unlock();
    return (ret == 0);
}

std::vector<std::string> StorageManager::listProfiles() {
    lock();
    std::vector<std::string> files;
    std::string dirPath = _basePath + "/profiles";
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        unlock();
        return files;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            std::string name = entry->d_name;
            if (name.length() > 5 && name.substr(name.length() - 5) == ".json") {
                files.push_back(name);
            }
        }
    }
    closedir(dir);
    unlock();
    return files;
}

// ============================================================================
// PID LIBRARY
// ============================================================================
bool StorageManager::loadPidLibrary(config::PidLibrary& library) {
    lock();
    std::string filePath = _basePath + "/config/pid_library.json";
    std::string jsonStr;

    if (!readTextFile(filePath, jsonStr) || jsonStr.empty()) {
        ESP_LOGW(TAG, "pid_library.json not found or empty – PID library unavailable");
        unlock();
        return false;
    }

    cJSON* root = cJSON_Parse(jsonStr.c_str());
    if (!root) {
        unlock();
        return false;
    }

    cJSON* schemaObj = cJSON_GetObjectItem(root, "schemaVersion");
    uint16_t schemaVer = (schemaObj && cJSON_IsNumber(schemaObj)) ? static_cast<uint16_t>(schemaObj->valueint) : 0;
    if (schemaVer > config::Schema::PID_LIBRARY) {
        ESP_LOGE(TAG, "Rejecting pid_library.json: Schema version (%u) is newer than supported firmware schema (%u)",
                 schemaVer, config::Schema::PID_LIBRARY);
        _schemaIncompatible = true;
        cJSON_Delete(root);
        unlock();
        return false;
    }
    if (schemaVer < 1) {
        ESP_LOGI(TAG, "Migrated pid_library.json from legacy (v0) to schema v%u", config::Schema::PID_LIBRARY);
    }

    auto parsePidPoints = [](cJSON* arr, std::vector<config::PidPoint>& outPoints) -> bool {
        if (!arr) return true;
        if (!cJSON_IsArray(arr)) return false;
        int count = cJSON_GetArraySize(arr);
        if (count > static_cast<int>(config::MAX_PID_POINTS)) {
            return false;
        }
        for (int i = 0; i < count; i++) {
            cJSON* item = cJSON_GetArrayItem(arr, i);
            if (!item) continue;
            config::PidPoint pt;
            cJSON* temp = cJSON_GetObjectItem(item, "temp");
            cJSON* kp   = cJSON_GetObjectItem(item, "kp");
            cJSON* ki   = cJSON_GetObjectItem(item, "ki");
            cJSON* kd   = cJSON_GetObjectItem(item, "kd");
            // Round to eliminate IEEE 754 float32 precision artifacts
            if (temp && cJSON_IsNumber(temp)) pt.temp = roundf(static_cast<float>(temp->valuedouble) * 10.0f) / 10.0f;
            if (kp   && cJSON_IsNumber(kp))   pt.kp   = roundf(static_cast<float>(kp->valuedouble)   * 100.0f) / 100.0f;
            if (ki   && cJSON_IsNumber(ki))   pt.ki   = roundf(static_cast<float>(ki->valuedouble)   * 1000.0f) / 1000.0f;
            if (kd   && cJSON_IsNumber(kd))   pt.kd   = roundf(static_cast<float>(kd->valuedouble)   * 100.0f) / 100.0f;
            outPoints.push_back(pt);
        }
        return true;
    };

    library.top.clear();
    library.bottom.clear();
    if (!parsePidPoints(cJSON_GetObjectItem(root, "top"), library.top) ||
        !parsePidPoints(cJSON_GetObjectItem(root, "bottom"), library.bottom)) {
        ESP_LOGE(TAG, "Failed to load PID Library: Point count exceeds maximum allowed limit of %zu", config::MAX_PID_POINTS);
        library.top.clear();
        library.bottom.clear();
        cJSON_Delete(root);
        unlock();
        return false;
    }

    auto vres = library.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "Failed to load PID Library: %s", vres.errorMessage ? vres.errorMessage : "invalid PID library");
        library.top.clear();
        library.bottom.clear();
        cJSON_Delete(root);
        unlock();
        return false;
    }

    cJSON_Delete(root);
    unlock();
    ESP_LOGI(TAG, "Loaded pid_library.json (schema v%u, %zu top points, %zu bottom points)", schemaVer, library.top.size(), library.bottom.size());
    return true;
}

bool StorageManager::savePidLibrary(const config::PidLibrary& library) {
    auto vres = library.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "savePidLibrary rejected: %s", vres.errorMessage ? vres.errorMessage : "invalid PID library");
        return false;
    }

    lock();
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        unlock();
        return false;
    }

    cJSON_AddNumberToObject(root, "schemaVersion", config::Schema::PID_LIBRARY);

    auto addPidPoints = [](cJSON* rootObj, const char* key, const std::vector<config::PidPoint>& points) {
        cJSON* arr = cJSON_CreateArray();
        for (const auto& pt : points) {
            cJSON* item = cJSON_CreateObject();
            // Round before serializing to avoid float32 precision garbage in JSON
            double temp_r = roundf(pt.temp * 10.0f) / 10.0;
            double kp_r   = roundf(pt.kp   * 100.0f) / 100.0;
            double ki_r   = roundf(pt.ki   * 1000.0f) / 1000.0;
            double kd_r   = roundf(pt.kd   * 100.0f) / 100.0;
            cJSON_AddNumberToObject(item, "temp", temp_r);
            cJSON_AddNumberToObject(item, "kp",   kp_r);
            cJSON_AddNumberToObject(item, "ki",   ki_r);
            cJSON_AddNumberToObject(item, "kd",   kd_r);
            cJSON_AddItemToArray(arr, item);
        }
        cJSON_AddItemToObject(rootObj, key, arr);
    };

    addPidPoints(root, "top", library.top);
    addPidPoints(root, "bottom", library.bottom);

    char* rendered = cJSON_Print(root);
    cJSON_Delete(root);

    if (!rendered) {
        ESP_LOGE(TAG, "cJSON_Print failed (out of memory) while serializing pid_library.json");
        unlock();
        return false;
    }

    std::string filePath = _basePath + "/config/pid_library.json";
    bool success = writeTextFileAtomic(filePath, std::string(rendered));
    cJSON_free(rendered);
    unlock();
    return success;
}

bool StorageManager::deletePidPoint(bool isTopChannel, size_t index) {
    config::PidLibrary lib;
    if (!loadPidLibrary(lib)) return false;

    auto& vec = isTopChannel ? lib.top : lib.bottom;
    if (index >= vec.size()) return false;

    vec.erase(vec.begin() + index);
    return savePidLibrary(lib);
}

void StorageManager::createDefaultFilesIfMissing() {
    std::string factoryProfilePath = _basePath + "/profiles/factory-profile.json";
    if (!fileExists(factoryProfilePath)) {
        config::ReflowProfile defaultProfile;
        defaultProfile.name = "Factory Profile";
        defaultProfile.file = "factory-profile.json";
        defaultProfile.stepsTop = {
            { 150.0f, 60, 1.2f },
            { 180.0f, 90, 1.0f },
            { 245.0f, 30, 1.5f },
            {  50.0f,  0, 2.0f }
        };
        defaultProfile.stepsBottom = {
            { 150.0f, 60, 1.2f },
            { 165.0f, 90, 0.8f },
            { 165.0f, 30, 0.8f },
            {  50.0f,  0, 2.0f }
        };
        saveProfile(defaultProfile);
    }

    std::string pidLibPath = _basePath + "/config/pid_library.json";
    if (!fileExists(pidLibPath)) {
        config::PidLibrary defaultPidLib;
        defaultPidLib.top = {
            { 150.0f, 2.5f, 0.05f, 1.0f },
            { 220.0f, 3.0f, 0.08f, 1.5f }
        };
        defaultPidLib.bottom = {
            { 150.0f, 2.0f, 0.04f, 1.0f },
            { 180.0f, 2.5f, 0.06f, 1.2f }
        };
        savePidLibrary(defaultPidLib);
    }
}

} // namespace storage
