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
 * @file storage_manager.hpp
 * @brief Thread-safe Unified LittleFS Storage Manager.
 *
 * Handles mounting/unmounting LittleFS filesystem, atomic file operations,
 * and JSON serialization for settings, profiles, and PID libraries via cJSON.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "config/machine_config.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string>
#include <vector>

namespace storage {

/**
 * @class StorageManager
 * @brief Thread-safe Unified LittleFS Storage Manager.
 */
class StorageManager {
public:
    /**
     * @brief Default constructor. Initializes mutex lock.
     */
    StorageManager();

    /**
     * @brief Destructor. Ensures unmounting and deletes mutex lock.
     */
    ~StorageManager();

    // Prevent copying
    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    /**
     * @brief Initialize and mount LittleFS filesystem.
     * @param basePath VFS mount path (default "/littlefs")
     * @param partitionLabel Partition label in default_16MB_partitions.csv (default "littlefs")
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t begin(const char* basePath = "/littlefs", const char* partitionLabel = "littlefs");

    /**
     * @brief Unmount LittleFS filesystem.
     */
    void stop();

    /**
     * @brief Check if LittleFS filesystem is mounted.
     * @return true if currently mounted, false otherwise.
     */
    bool isMounted() const { return _mounted; }

    /**
     * @brief Get total and used storage bytes on LittleFS.
     * @param totalBytes Output total partition capacity in bytes.
     * @param usedBytes Output currently used bytes.
     * @return true on query success, false on error.
     */
    bool getStorageInfo(size_t& totalBytes, size_t& usedBytes);

    // ========================================================================
    // MACHINE SETTINGS (settings.json)
    // ========================================================================

    /**
     * @brief Load machine settings from /littlefs/config/settings.json.
     * @param settings Output settings struct populated from JSON.
     * @return true on success, false on failure or parsing error.
     */
    bool loadSettings(config::MachineSettings& settings);

    /**
     * @brief Save machine settings to /littlefs/config/settings.json atomically.
     * @param settings Machine settings struct to serialize.
     * @return true on success, false on validation failure or disk error.
     */
    bool saveSettings(const config::MachineSettings& settings);

    // ========================================================================
    // REFLOW PROFILES (*.json in /littlefs/profiles/)
    // ========================================================================

    /**
     * @brief Load a specific reflow profile from /littlefs/profiles/<filename>.
     * @param filename Name of file (e.g. "sac305.json").
     * @param profile Output reflow profile structure.
     * @return true on success, false on failure.
     */
    bool loadProfile(const std::string& filename, config::ReflowProfile& profile);

    /**
     * @brief Save a reflow profile atomically to /littlefs/profiles/<profile.file>.
     * @param profile Profile structure to serialize.
     * @return true on success, false on validation failure or write error.
     */
    bool saveProfile(const config::ReflowProfile& profile);

    /**
     * @brief Delete a specific profile file. Factory profile is protected from deletion.
     * @param filename Name of file to delete.
     * @return true on success, false if file does not exist or is protected.
     */
    bool deleteProfile(const std::string& filename);

    /**
     * @brief List all profile JSON files found in /littlefs/profiles/.
     * @return List of filenames.
     */
    std::vector<std::string> listProfiles();

    // ========================================================================
    // PID LIBRARY (pid_library.json)
    // ========================================================================

    /**
     * @brief Load PID gain scheduling library from /littlefs/config/pid_library.json.
     * @param library Output PID library structure.
     * @return true on success, false on error.
     */
    bool loadPidLibrary(config::PidLibrary& library);

    /**
     * @brief Save PID gain scheduling library atomically.
     * @param library PID library structure to serialize.
     * @return true on success, false on validation failure or disk error.
     */
    bool savePidLibrary(const config::PidLibrary& library);

    /**
     * @brief Remove a specific PID point by channel and index.
     * @param isTopChannel true for Top heater, false for Bottom heater.
     * @param index Point index within channel vector.
     * @return true on success, false if index is out of range.
     */
    bool deletePidPoint(bool isTopChannel, size_t index);

    // ========================================================================
    // FILE SYSTEM & SAFETY UTILITIES
    // ========================================================================

    /**
     * @brief Check if a file exists on the filesystem.
     * @param filepath Full VFS path to file.
     * @return true if file exists, false otherwise.
     */
    bool fileExists(const std::string& filepath);

    /**
     * @brief Read full content of a text file into a string.
     * @param filepath Full VFS path to file.
     * @param outContent Output buffer receiving file content.
     * @return true on success, false on open/read error.
     */
    bool readTextFile(const std::string& filepath, std::string& outContent);

    /**
     * @brief Atomically write string content to file using temporary .tmp file.
     * @param filepath Target file path.
     * @param content String data to write.
     * @return true on success, false on write/rename failure.
     */
    bool writeTextFileAtomic(const std::string& filepath, const std::string& content);

    /**
     * @brief Check if any loaded file had an incompatible schema version (from newer firmware).
     * @return true if schema incompatibility occurred.
     */
    bool hasSchemaIncompatibility() const { return _schemaIncompatible; }

    /**
     * @brief Clear schema incompatibility warning flag.
     */
    void clearSchemaIncompatibility() { _schemaIncompatible = false; }

private:
    std::string       _basePath;            ///< VFS mount path prefix (e.g. "/littlefs")
    std::string       _partitionLabel;      ///< Flash partition label (e.g. "littlefs")
    bool              _mounted;             ///< Mount state flag
    bool              _schemaIncompatible;  ///< Flag set when newer schema version is detected
    SemaphoreHandle_t _mutex;               ///< FreeRTOS mutex for thread-safe access

    void lock();
    void unlock();

    void createDirectories();
    void createDefaultFilesIfMissing();
};

} // namespace storage
