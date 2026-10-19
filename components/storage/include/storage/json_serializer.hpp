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
 * @file json_serializer.hpp
 * @brief Type-safe JSON serialization & schema migration engine for settings, profiles, and PID libraries.
 *
 * Encapsulates cJSON memory lifecycle, schema version validation, IEEE 754 precision rounding,
 * and robust default fallbacks.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "config/machine_config.hpp"
#include <string>

namespace storage {

/**
 * @class JsonSerializer
 * @brief Static utility class for domain model serialization and deserialization.
 */
class JsonSerializer {
public:
    // ========================================================================
    // Machine Settings Serializer
    // ========================================================================

    /**
     * @brief Serialize MachineSettings into a JSON formatted string.
     * @param settings Source settings structure.
     * @param[out] outJson Target string to hold JSON output.
     * @return true on serialization success, false otherwise.
     */
    static bool serializeSettings(const config::MachineSettings& settings, std::string& outJson);

    /**
     * @brief Deserialize MachineSettings from a JSON string with schema migration.
     * @param json Source JSON string.
     * @param[out] outSettings Target settings structure to populate.
     * @param[out] outSchemaIncompatible Set to true if JSON schema is newer than supported.
     * @return true on successful parsing and validation, false otherwise.
     */
    static bool deserializeSettings(const std::string& json, config::MachineSettings& outSettings,
                                    bool& outSchemaIncompatible);

    // ========================================================================
    // Reflow Profile Serializer
    // ========================================================================

    /**
     * @brief Serialize ReflowProfile into a JSON formatted string.
     * @param profile Source profile structure.
     * @param[out] outJson Target string to hold JSON output.
     * @return true on serialization success, false otherwise.
     */
    static bool serializeProfile(const config::ReflowProfile& profile, std::string& outJson);

    /**
     * @brief Deserialize ReflowProfile from a JSON string with schema migration.
     * @param json Source JSON string.
     * @param filename Active filename to associate with profile.
     * @param[out] outProfile Target profile structure to populate.
     * @param[out] outSchemaIncompatible Set to true if JSON schema is newer than supported.
     * @return true on successful parsing and validation, false otherwise.
     */
    static bool deserializeProfile(const std::string& json, const std::string& filename,
                                   config::ReflowProfile& outProfile, bool& outSchemaIncompatible);

    // ========================================================================
    // PID Library Serializer
    // ========================================================================

    /**
     * @brief Serialize PidLibrary into a JSON formatted string.
     * @param library Source PID library structure.
     * @param[out] outJson Target string to hold JSON output.
     * @return true on serialization success, false otherwise.
     */
    static bool serializePidLibrary(const config::PidLibrary& library, std::string& outJson);

    /**
     * @brief Deserialize PidLibrary from a JSON string with schema migration.
     * @param json Source JSON string.
     * @param[out] outLibrary Target PID library structure to populate.
     * @param[out] outSchemaIncompatible Set to true if JSON schema is newer than supported.
     * @return true on successful parsing and validation, false otherwise.
     */
    static bool deserializePidLibrary(const std::string& json, config::PidLibrary& outLibrary,
                                      bool& outSchemaIncompatible);
};

} // namespace storage
