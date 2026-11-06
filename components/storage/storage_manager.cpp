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
 * Handles atomic file writes via .tmp staging, default file initialization,
 * and thread-safe LittleFS VFS filesystem access using RAII lock guards and linear guard clauses.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "storage/storage_manager.hpp"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "storage/json_serializer.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <sys/stat.h>

namespace storage {

namespace {

constexpr const char* TAG = "STORAGE";

struct FileCloser {
    void operator()(FILE* fp) const {
        if (fp != nullptr) {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            (void)fclose(fp);
        }
    }
};
using UniqueFile = std::unique_ptr<FILE, FileCloser>;

struct DirCloser {
    void operator()(DIR* dp) const {
        if (dp != nullptr) {
            (void)closedir(dp);
        }
    }
};
using UniqueDir = std::unique_ptr<DIR, DirCloser>;

/**
 * @brief Validates a filename against path traversal attempts and maximum length.
 */
bool isValidFilename(const std::string& filename) {
    if (filename.empty() || filename.length() > config::Limits::MAX_FILENAME_LENGTH) {
        return false;
    }
    if (filename.find("..") != std::string::npos || filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) {
        return false;
    }
    return true;
}

/**
 * @brief Checks if a profile name corresponds to the immutable reference factory profile.
 */
bool isProtectedFactoryProfile(const std::string& filename) {
    return (filename == "factory-profile.json" || filename == "factory-profile");
}

} // anonymous namespace

// ============================================================================
// Lifecycle & LittleFS VFS Mount Management
// ============================================================================

StorageManager::StorageManager()
    : _basePath("/littlefs"), _partitionLabel("littlefs"), _mutex(xSemaphoreCreateRecursiveMutex()), _profileCache{} {}

StorageManager::~StorageManager() {
    stop();
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

void StorageManager::lock() const {
    if (_mutex != nullptr) {
        xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    }
}

void StorageManager::unlock() const {
    if (_mutex != nullptr) {
        xSemaphoreGiveRecursive(_mutex);
    }
}

esp_err_t StorageManager::begin(const char* basePath, const char* partitionLabel) {
    LockGuard guard(*this);
    if (_mounted) {
        return ESP_OK;
    }

    _basePath = (basePath != nullptr) ? basePath : "/littlefs";
    while (_basePath.length() > 1 && _basePath.back() == '/') {
        _basePath.pop_back();
    }
    _partitionLabel = (partitionLabel != nullptr) ? partitionLabel : "littlefs";

    ESP_LOGI(TAG, "Mounting LittleFS partition '%s' at '%s'", _partitionLabel.c_str(), _basePath.c_str());

    esp_vfs_littlefs_conf_t conf{};
    conf.base_path = _basePath.c_str();
    conf.partition_label = _partitionLabel.c_str();
    conf.format_if_mount_failed = 1; // Auto-format on blank flash / first boot
    conf.grow_on_mount = 1;          // Automatically expand filesystem to match partition size
    conf.dont_mount = 0;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "LittleFS mount failed! Partition may be corrupted.");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "LittleFS partition '%s' not found in partition table!", _partitionLabel.c_str());
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s).", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0;
    size_t used = 0;
    ret = esp_littlefs_info(_partitionLabel.c_str(), &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS Mounted successfully: Partition size: %zu KB, Used: %zu KB", total / 1024, used / 1024);
    }

    _mounted = true;
    createDirectories();
    createDefaultFilesIfMissing();
    loadAllProfilesIntoCache();

    return ESP_OK;
}

void StorageManager::stop() {
    LockGuard guard(*this);
    if (_mounted) {
        esp_vfs_littlefs_unregister(_partitionLabel.c_str());
        _mounted = false;
        _profileCache.clear();
        _profileCacheLoaded = false;
        ESP_LOGI(TAG, "LittleFS unmounted.");
    }
}

bool StorageManager::getStorageInfo(size_t& totalBytes, size_t& usedBytes) const {
    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }
    esp_err_t ret = esp_littlefs_info(_partitionLabel.c_str(), &totalBytes, &usedBytes);
    return (ret == ESP_OK);
}

float StorageManager::getStorageUsagePercent() const {
    size_t total = 0;
    size_t used = 0;
    if (!getStorageInfo(total, used) || total == 0) {
        return 0.0f;
    }
    return (static_cast<float>(used) / static_cast<float>(total)) * 100.0f;
}

bool StorageManager::isStorageFullForNewFiles() const {
    size_t total = 0;
    size_t used = 0;
    if (!getStorageInfo(total, used) || total == 0) {
        return false;
    }
    float usedPercent = (static_cast<float>(used) / static_cast<float>(total)) * 100.0f;
    return (usedPercent >= config::Limits::STORAGE_MAX_PERCENT);
}

void StorageManager::createDirectories() {
    struct stat st = {};
    if (stat(_basePath.c_str(), &st) != 0) {
        (void)mkdir(_basePath.c_str(), 0755);
    }
    std::string configDir = _basePath + "/config";
    std::string profilesDir = _basePath + "/profiles";

    if (stat(configDir.c_str(), &st) != 0) {
        (void)mkdir(configDir.c_str(), 0755);
    }
    if (stat(profilesDir.c_str(), &st) != 0) {
        (void)mkdir(profilesDir.c_str(), 0755);
    }

    // Clean up any stale temporary files left behind from interrupted writes
    auto cleanupTmpFiles = [](const std::string& dirPath) {
        UniqueDir dir(opendir(dirPath.c_str()));
        if (dir == nullptr) {
            return;
        }
        const struct dirent* entry = nullptr;
        while ((entry = readdir(dir.get())) != nullptr) {
            std::string name = entry->d_name;
            if (name.length() > 4 && name.substr(name.length() - 4) == ".tmp") {
                std::string fullPath = dirPath;
                fullPath += "/";
                fullPath += name;
                (void)remove(fullPath.c_str());
                ESP_LOGW(TAG, "Removed stale temporary file: %s", fullPath.c_str());
            }
        }
    };

    cleanupTmpFiles(configDir);
    cleanupTmpFiles(profilesDir);
}

// ============================================================================
// File System & Atomic Staging Operations
// ============================================================================

bool StorageManager::fileExists(const std::string& filepath) const {
    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }
    struct stat st = {};
    return (stat(filepath.c_str(), &st) == 0);
}

bool StorageManager::readTextFile(const std::string& filepath, std::string& outContent) {
    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }

    UniqueFile f(fopen(filepath.c_str(), "r"));
    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", filepath.c_str());
        return false;
    }

    (void)fseek(f.get(), 0, SEEK_END);
    long size = ftell(f.get());
    (void)fseek(f.get(), 0, SEEK_SET);

    if (size < 0) {
        ESP_LOGE(TAG, "Failed to determine file size: %s", filepath.c_str());
        return false;
    }

    if (size == 0) {
        outContent.clear();
        return true;
    }

    auto fileSize = static_cast<size_t>(size);
    if (fileSize > config::Limits::MAX_JSON_PAYLOAD_BYTES) {
        ESP_LOGE(TAG, "File size (%ld bytes) exceeds maximum payload limit (%zu bytes): %s", size,
                 config::Limits::MAX_JSON_PAYLOAD_BYTES, filepath.c_str());
        return false;
    }

    outContent.resize(fileSize);
    size_t readBytes = fread(outContent.data(), 1, fileSize, f.get());

    outContent.resize(readBytes);
    return true;
}

bool StorageManager::writeTextFileAtomic(const std::string& filepath, const std::string& content) {
    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }

    std::string tmpPath = filepath + ".tmp";
    UniqueFile f(fopen(tmpPath.c_str(), "w"));
    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open temp file for writing: %s", tmpPath.c_str());
        return false;
    }

    size_t written = fwrite(content.c_str(), 1, content.length(), f.get());
    (void)fflush(f.get());
    f.reset();

    if (written != content.length()) {
        ESP_LOGE(TAG, "Incomplete write to temp file: %s (%zu / %zu bytes)", tmpPath.c_str(), written,
                 content.length());
        (void)remove(tmpPath.c_str());
        return false;
    }

    // Atomic replacement: POSIX rename() under LittleFS replaces filepath atomically
    if (rename(tmpPath.c_str(), filepath.c_str()) != 0) {
        ESP_LOGE(TAG, "Failed to rename temp file %s to %s", tmpPath.c_str(), filepath.c_str());
        (void)remove(tmpPath.c_str());
        return false;
    }

    return true;
}

// ============================================================================
// Machine Settings Storage Methods
// ============================================================================

bool StorageManager::loadSettings(config::MachineSettings& settings) {
    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }

    std::string filePath = _basePath + "/config/settings.json";
    std::string jsonStr;

    if (!fileExists(filePath) || !readTextFile(filePath, jsonStr) || jsonStr.empty()) {
        ESP_LOGW(TAG, "settings.json not found, creating default settings");
        settings = config::MachineSettings{};
        saveSettings(settings);
        return true;
    }

    bool ok = JsonSerializer::deserializeSettings(jsonStr, settings, _schemaIncompatible);
    if (ok) {
        ESP_LOGI(TAG, "Loaded settings.json successfully");
    }
    return ok;
}

bool StorageManager::saveSettings(const config::MachineSettings& settings) {
    std::string jsonStr;
    if (!JsonSerializer::serializeSettings(settings, jsonStr)) {
        return false;
    }

    LockGuard guard(*this);
    if (!_mounted) {
        ESP_LOGE(TAG, "saveSettings failed: LittleFS not mounted");
        return false;
    }

    std::string filePath = _basePath + "/config/settings.json";
    bool success = writeTextFileAtomic(filePath, jsonStr);
    if (success) {
        ESP_LOGI(TAG, "Saved settings.json atomically");
    }
    return success;
}

// ============================================================================
// Reflow Profiles Storage Methods
// ============================================================================

void StorageManager::loadAllProfilesIntoCache() {
    _profileCache.clear();
    std::string dirPath = _basePath + "/profiles";
    UniqueDir dir(opendir(dirPath.c_str()));
    if (dir == nullptr) {
        _profileCacheLoaded = true;
        return;
    }

    const struct dirent* entry = nullptr;
    while ((entry = readdir(dir.get())) != nullptr) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            std::string name = entry->d_name;
            if (name.length() > 5 && name.substr(name.length() - 5) == ".json") {
                std::string filePath = dirPath + "/" + name;
                std::string jsonStr;
                if (readTextFile(filePath, jsonStr)) {
                    config::ReflowProfile prof;
                    if (JsonSerializer::deserializeProfile(jsonStr, name, prof, _schemaIncompatible)) {
                        _profileCache.push_back(prof);
                    }
                }
            }
        }
    }
    _profileCacheLoaded = true;
    ESP_LOGI(TAG, "Profile cache initialized with %zu profiles from Flash.", _profileCache.size());
}

bool StorageManager::loadProfile(const std::string& filename, config::ReflowProfile& profile) {
    if (!isValidFilename(filename)) {
        ESP_LOGE(TAG, "loadProfile rejected illegal filename: %s", filename.c_str());
        return false;
    }

    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }

    if (_profileCacheLoaded) {
        auto it = std::find_if(_profileCache.begin(), _profileCache.end(),
                               [&filename](const config::ReflowProfile& p) { return p.file == filename; });
        if (it != _profileCache.end()) {
            profile = *it;
            return true;
        }
    }

    std::string filePath = _basePath + "/profiles/" + filename;
    std::string jsonStr;

    if (!readTextFile(filePath, jsonStr)) {
        ESP_LOGE(TAG, "Failed to read profile file: %s", filename.c_str());
        return false;
    }

    bool ok = JsonSerializer::deserializeProfile(jsonStr, filename, profile, _schemaIncompatible);
    if (ok) {
        if (_profileCacheLoaded) {
            _profileCache.push_back(profile);
        }
        ESP_LOGI(TAG, "Loaded profile '%s' successfully (%zu top steps, %zu bottom steps)", filename.c_str(),
                 profile.stepsTop.size(), profile.stepsBottom.size());
    }
    return ok;
}

bool StorageManager::profileExists(const std::string& filename) const {
    if (!isValidFilename(filename)) {
        return false;
    }
    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }
    if (_profileCacheLoaded) {
        return std::any_of(_profileCache.begin(), _profileCache.end(),
                           [&filename](const config::ReflowProfile& p) { return p.file == filename; });
    }
    const std::string filePath = _basePath + "/profiles/" + filename;
    return fileExists(filePath);
}

bool StorageManager::saveProfile(const config::ReflowProfile& profile) {
    if (!isValidFilename(profile.file)) {
        ESP_LOGE(TAG, "saveProfile rejected illegal filename: %s", profile.file.c_str());
        return false;
    }

    auto vres = profile.validate();
    if (!vres.valid) {
        ESP_LOGE(TAG, "saveProfile rejected: %s", vres.errorMessage ? vres.errorMessage : "invalid profile");
        return false;
    }

    LockGuard guard(*this);
    if (!_mounted) {
        ESP_LOGE(TAG, "saveProfile failed: LittleFS not mounted");
        return false;
    }
    std::string filePath = _basePath + "/profiles/" + profile.file;

    struct stat st = {};
    bool fileAlreadyExists = (stat(filePath.c_str(), &st) == 0);

    // Prevent overwriting existing protected factory profile
    if (fileAlreadyExists && isProtectedFactoryProfile(profile.file)) {
        ESP_LOGW(TAG, "Cannot overwrite protected factory profile: %s", profile.file.c_str());
        return false;
    }

    // Check wear-leveling threshold: reject creating a new profile if storage >= STORAGE_MAX_PERCENT (90%)
    bool isNewProfile = !fileAlreadyExists;
    if (isNewProfile && isStorageFullForNewFiles()) {
        ESP_LOGE(TAG, "Storage limit of %.0f%% reached. Save rejected to preserve flash wear leveling: %s",
                 static_cast<double>(config::Limits::STORAGE_MAX_PERCENT), profile.file.c_str());
        return false;
    }

    std::string jsonStr;
    if (!JsonSerializer::serializeProfile(profile, jsonStr)) {
        return false;
    }

    bool success = writeTextFileAtomic(filePath, jsonStr);
    if (success) {
        if (_profileCacheLoaded) {
            auto it = std::find_if(_profileCache.begin(), _profileCache.end(),
                                   [&](const config::ReflowProfile& p) { return p.file == profile.file; });
            if (it != _profileCache.end()) {
                *it = profile;
            } else {
                _profileCache.push_back(profile);
            }
        }
        ESP_LOGI(TAG, "Saved profile '%s' atomically", profile.file.c_str());
    }
    return success;
}

bool StorageManager::deleteProfile(const std::string& filename) {
    if (!isValidFilename(filename)) {
        ESP_LOGE(TAG, "deleteProfile rejected illegal filename: %s", filename.c_str());
        return false;
    }

    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }

    if (isProtectedFactoryProfile(filename)) {
        ESP_LOGW(TAG, "Cannot delete protected factory profile");
        return false;
    }

    std::string filePath = _basePath + "/profiles/" + filename;
    int ret = remove(filePath.c_str());
    if (ret == 0) {
        if (_profileCacheLoaded) {
            _profileCache.erase(std::remove_if(_profileCache.begin(), _profileCache.end(),
                                               [&](const config::ReflowProfile& p) { return p.file == filename; }),
                                _profileCache.end());
        }
        return true;
    }
    return false;
}

std::vector<std::string> StorageManager::listProfiles() const {
    LockGuard guard(*this);
    if (!_mounted) {
        return {};
    }

    if (_profileCacheLoaded) {
        std::vector<std::string> files;
        files.reserve(_profileCache.size());
        std::transform(_profileCache.begin(), _profileCache.end(), std::back_inserter(files),
                       [](const config::ReflowProfile& p) { return p.file; });
        return files;
    }

    std::vector<std::string> files;
    std::string dirPath = _basePath + "/profiles";
    UniqueDir dir(opendir(dirPath.c_str()));
    if (dir == nullptr) {
        return files;
    }

    const struct dirent* entry = nullptr;
    while ((entry = readdir(dir.get())) != nullptr) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            std::string name = entry->d_name;
            if (name.length() > 5 && name.substr(name.length() - 5) == ".json") {
                files.push_back(name);
            }
        }
    }
    return files;
}

// ============================================================================
// PID Library Storage Methods
// ============================================================================

bool StorageManager::loadPidLibrary(config::PidLibrary& library) {
    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }

    std::string filePath = _basePath + "/config/pid_library.json";
    std::string jsonStr;

    if (!fileExists(filePath) || !readTextFile(filePath, jsonStr) || jsonStr.empty()) {
        ESP_LOGW(TAG, "pid_library.json not found or empty – PID library unavailable");
        return false;
    }

    bool ok = JsonSerializer::deserializePidLibrary(jsonStr, library, _schemaIncompatible);
    if (ok) {
        ESP_LOGI(TAG, "Loaded pid_library.json successfully (%zu top points, %zu bottom points)", library.top.size(),
                 library.bottom.size());
    }
    return ok;
}

bool StorageManager::savePidLibrary(const config::PidLibrary& library) {
    std::string jsonStr;
    if (!JsonSerializer::serializePidLibrary(library, jsonStr)) {
        return false;
    }

    LockGuard guard(*this);
    if (!_mounted) {
        ESP_LOGE(TAG, "savePidLibrary failed: LittleFS not mounted");
        return false;
    }

    std::string filePath = _basePath + "/config/pid_library.json";
    bool success = writeTextFileAtomic(filePath, jsonStr);
    return success;
}

bool StorageManager::deletePidPoint(bool isTopChannel, size_t index) {
    LockGuard guard(*this);
    if (!_mounted) {
        return false;
    }

    config::PidLibrary lib;
    if (!loadPidLibrary(lib)) {
        return false;
    }

    auto& vec = isTopChannel ? lib.top : lib.bottom;
    if (index >= vec.size()) {
        return false;
    }

    vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(index));
    return savePidLibrary(lib);
}

void StorageManager::createDefaultFilesIfMissing() {
    std::string settingsPath = _basePath + "/config/settings.json";
    if (!fileExists(settingsPath)) {
        config::MachineSettings defaultSettings;
        saveSettings(defaultSettings);
    }

    std::string factoryProfilePath = _basePath + "/profiles/factory-profile.json";
    if (!fileExists(factoryProfilePath)) {
        config::ReflowProfile defaultProfile;
        defaultProfile.name = "Factory Profile";
        defaultProfile.file = "factory-profile.json";
        defaultProfile.stepsTop = {
            {.temp = 150.0f, .time = 60, .ramp = 1.2f},
            {.temp = 180.0f, .time = 90, .ramp = 1.0f},
            {.temp = 245.0f, .time = 30, .ramp = 1.5f},
            {.temp = 50.0f, .time = 0, .ramp = 2.0f},
        };
        defaultProfile.stepsBottom = {
            {.temp = 150.0f, .time = 60, .ramp = 1.2f},
            {.temp = 165.0f, .time = 90, .ramp = 0.8f},
            {.temp = 165.0f, .time = 30, .ramp = 0.8f},
            {.temp = 50.0f, .time = 0, .ramp = 2.0f},
        };
        saveProfile(defaultProfile);
    }

    std::string pidLibPath = _basePath + "/config/pid_library.json";
    if (!fileExists(pidLibPath)) {
        config::PidLibrary defaultPidLib;
        // Default PID library is initialized empty (top and bottom empty) to mandate
        // heater-specific tuning/calibration by the operator.
        savePidLibrary(defaultPidLib);
    }
}

} // namespace storage
