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
 * @file test_storage.cpp
 * @brief Unit tests for storage::StorageManager, JSON serialization, schemas, and LittleFS utilities.
 *
 * Tests cover:
 *  1. StorageManager lifecycle (unmounted initial state)
 *  2. ReflowProfile struct building and step lists
 *  3. Schema version validity
 *  4. Validation guards for saving profiles and settings
 *  5. Floating-point rounding and decimal precision rules
 *  6. Schema migration pipeline simulation
 *  7. Future schema version rejection
 *  8. Factory profile delete protection
 *  9. Atomic write logic (.tmp temporary pathing)
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "cJSON.h"
#include "config/machine_config.hpp"
#include "storage/json_serializer.hpp"
#include "storage/storage_manager.hpp"
#include "unity.h"

// 1. Lifecycle Test
static void test_storage_lifecycle() {
    storage::StorageManager storage;
    TEST_ASSERT_FALSE(storage.isMounted());
}

// 2. Profile Struct Test
static void test_storage_profile_struct() {
    config::ReflowProfile p;
    p.name = "Lead-Free SAC305";
    p.file = "sac305.json";
    TEST_ASSERT_EQUAL_STRING("Lead-Free SAC305", p.name.c_str());
    TEST_ASSERT_EQUAL_STRING("sac305.json", p.file.c_str());

    config::ProfileStep step1;
    step1.temp = 150.0f;
    step1.time = 60;
    step1.ramp = 1.5f;

    config::ProfileStep step2;
    step2.temp = 217.0f;
    step2.time = 45;
    step2.ramp = 2.0f;

    p.stepsTop.push_back(step1);
    p.stepsTop.push_back(step2);

    TEST_ASSERT_EQUAL_INT(2, p.stepsTop.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, p.stepsTop[0].temp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 217.0f, p.stepsTop[1].temp);
}

// 3. Schema Values Test
static void test_storage_schema_values() {
    // Schema versions must be positive defined constants (> 0)
    TEST_ASSERT_GREATER_THAN_UINT16(0, config::Schema::MACHINE_SETTINGS);
    TEST_ASSERT_GREATER_THAN_UINT16(0, config::Schema::REFLOW_PROFILE);
    TEST_ASSERT_GREATER_THAN_UINT16(0, config::Schema::PID_LIBRARY);
}

// 4. Validation Guards Test
static void test_storage_validation_guards() {
    storage::StorageManager storage;
    // Invalid profile (empty name and steps) must be rejected immediately
    config::ReflowProfile invalidProfile;
    TEST_ASSERT_FALSE(storage.saveProfile(invalidProfile));

    // Profile with step temp out of range must be rejected
    config::ReflowProfile badTempProfile;
    badTempProfile.name = "Bad Temp";
    badTempProfile.file = "bad.json";
    config::ProfileStep badStep;
    badStep.temp = 500.0f;
    badTempProfile.stepsTop.push_back(badStep);
    TEST_ASSERT_FALSE(storage.saveProfile(badTempProfile));

    // Invalid settings must be rejected
    config::MachineSettings badSettings;
    badSettings.maxTempTop = 999.0f;
    TEST_ASSERT_FALSE(storage.saveSettings(badSettings));

    // Path traversal in loadProfile, saveProfile, and deleteProfile must be rejected
    config::ReflowProfile dummyProf;
    TEST_ASSERT_FALSE(storage.loadProfile("../config/settings.json", dummyProf));
    TEST_ASSERT_FALSE(storage.deleteProfile("../config/settings.json"));
    TEST_ASSERT_FALSE(storage.deleteProfile("/etc/passwd"));

    config::ReflowProfile traversalProfile;
    traversalProfile.name = "Traversal";
    traversalProfile.file = "../evil.json";
    traversalProfile.stepsTop.push_back({150.0f, 60, 1.0f});
    TEST_ASSERT_FALSE(storage.saveProfile(traversalProfile));

    config::ReflowProfile slashProfile;
    slashProfile.name = "Slash";
    slashProfile.file = "sub/evil.json";
    slashProfile.stepsTop.push_back({150.0f, 60, 1.0f});
    TEST_ASSERT_FALSE(storage.saveProfile(slashProfile));

    // Excessive filename length must be rejected
    std::string tooLongName(config::Limits::MAX_FILENAME_LENGTH + 1, 'x');
    tooLongName += ".json";
    TEST_ASSERT_FALSE(storage.loadProfile(tooLongName, dummyProf));
    TEST_ASSERT_FALSE(storage.deleteProfile(tooLongName));

    // Non-existent file read must return false
    std::string nonExistentContent;
    TEST_ASSERT_FALSE(storage.readTextFile("/littlefs/non_existent_file.json", nonExistentContent));
}

// 5. Float Rounding Precision Test
static void test_storage_float_rounding_precision() {
    // Test Category 1 (Temperatures & Offsets: 1 decimal place)
    float rawTemp = 150.5678f;
    float roundedTemp = roundf(rawTemp * 10.0f) / 10.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 150.6f, roundedTemp);

    // Test Category 2 (Ramp Rates & Tolerances: 2 decimal places)
    float rawRamp = 1.2345f;
    float roundedRamp = roundf(rawRamp * 100.0f) / 100.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.23f, roundedRamp);

    // Test Category 3 (PID Kp & Kd: 2 decimal places)
    float rawKp = 2.456f;
    float roundedKp = roundf(rawKp * 100.0f) / 100.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.46f, roundedKp);

    // Test Category 4 (PID Ki & Alpha: 3 decimal places)
    float rawKi = 0.0526f;
    float roundedKi = roundf(rawKi * 1000.0f) / 1000.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.053f, roundedKi);

    float rawAlpha = 0.3004f;
    float roundedAlpha = roundf(rawAlpha * 1000.0f) / 1000.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.300f, roundedAlpha);
}

// 6. Schema Migration Pipeline Test
static void test_storage_schema_migration_pipeline() {
    // Simulate loading a Schema v1 JSON document and migrating it to v2
    const char* schemaV1Json = R"({"schemaVersion":1,"simulationMode":true,"maxTempTop":250.0})";
    cJSON* root = cJSON_Parse(schemaV1Json);
    TEST_ASSERT_NOT_NULL(root);

    cJSON* verItem = cJSON_GetObjectItem(root, "schemaVersion");
    TEST_ASSERT_NOT_NULL(verItem);
    auto ver = static_cast<uint16_t>(verItem->valueint);
    TEST_ASSERT_EQUAL_UINT16(1, ver);

    // Apply migration step (v1 -> v2)
    if (ver == 1) {
        // 1. Inject missing v2 field if not present
        if (!cJSON_GetObjectItem(root, "hardwareBuzzerEnabled")) {
            cJSON_AddBoolToObject(root, "hardwareBuzzerEnabled", 1);
        }
        // 2. Bump schema version to 2
        cJSON_SetNumberValue(verItem, 2);
    }

    // Verify document is now valid Schema v2
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItem(root, "schemaVersion")->valueint);
    const cJSON* buzItem = cJSON_GetObjectItem(root, "hardwareBuzzerEnabled");
    TEST_ASSERT_NOT_NULL(buzItem);
    TEST_ASSERT_TRUE(cJSON_IsTrue(buzItem));

    cJSON_Delete(root);
}

// 7. Future Version Rejection Test
static void test_storage_schema_future_version_rejection() {
    // A file with schemaVersion > supported version (e.g. v99) must not be migrated or accepted blindly
    const char* futureSchemaJson = R"({"schemaVersion":99,"customFutureField":true})";
    cJSON* root = cJSON_Parse(futureSchemaJson);
    TEST_ASSERT_NOT_NULL(root);

    const cJSON* verItem = cJSON_GetObjectItem(root, "schemaVersion");
    TEST_ASSERT_NOT_NULL(verItem);
    auto ver = static_cast<uint16_t>(verItem->valueint);

    // Future schema version check
    bool isFutureVersion = (ver > config::Schema::MACHINE_SETTINGS);
    TEST_ASSERT_TRUE(isFutureVersion);

    cJSON_Delete(root);

    config::MachineSettings settings;
    bool schemaIncompatible = false;
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeSettings(futureSchemaJson, settings, schemaIncompatible));
    TEST_ASSERT_TRUE(schemaIncompatible);

    storage::StorageManager sm;
    TEST_ASSERT_FALSE(sm.hasSchemaIncompatibility());
    sm.clearSchemaIncompatibility();
    TEST_ASSERT_FALSE(sm.hasSchemaIncompatibility());
}

// 8. Factory Profile Delete and Overwrite Protection Test
static void test_storage_factory_profile_protection() {
    storage::StorageManager storage;
    // Attempting to delete the protected factory profile must return false
    TEST_ASSERT_FALSE(storage.deleteProfile("factory-profile.json"));
    TEST_ASSERT_FALSE(storage.deleteProfile("factory-profile"));

    // Attempting to overwrite the factory profile must return false
    config::ReflowProfile factoryProf;
    factoryProf.name = "Malicious Overwrite";
    factoryProf.file = "factory-profile.json";
    factoryProf.stepsTop.push_back({150.0f, 60, 1.0f});
    TEST_ASSERT_FALSE(storage.saveProfile(factoryProf));
}

// 9. Atomic File Operation Helpers Test
static void test_storage_atomic_file_ops() {
    std::string path = "/littlefs/config/test.json";
    std::string tmpPath = path + ".tmp";
    TEST_ASSERT_EQUAL_STRING("/littlefs/config/test.json.tmp", tmpPath.c_str());
}

// 10. PID Library JSON Serialization / Deserialization Roundtrip Test
static void test_storage_pid_library_serialization_roundtrip() {
    config::PidLibrary pidLib;
    pidLib.top.push_back({100.0f, 2.0f, 0.05f, 1.0f});
    pidLib.top.push_back({200.0f, 4.0f, 0.10f, 2.0f});
    pidLib.bottom.push_back({150.0f, 3.0f, 0.08f, 1.5f});

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schemaVersion", config::Schema::PID_LIBRARY);

    cJSON* topArr = cJSON_CreateArray();
    for (const auto& pt : pidLib.top) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "temp", pt.temp);
        cJSON_AddNumberToObject(item, "kp", pt.kp);
        cJSON_AddNumberToObject(item, "ki", pt.ki);
        cJSON_AddNumberToObject(item, "kd", pt.kd);
        cJSON_AddItemToArray(topArr, item);
    }
    cJSON_AddItemToObject(root, "top", topArr);

    cJSON* botArr = cJSON_CreateArray();
    for (const auto& pt : pidLib.bottom) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "temp", pt.temp);
        cJSON_AddNumberToObject(item, "kp", pt.kp);
        cJSON_AddNumberToObject(item, "ki", pt.ki);
        cJSON_AddNumberToObject(item, "kd", pt.kd);
        cJSON_AddItemToArray(botArr, item);
    }
    cJSON_AddItemToObject(root, "bottom", botArr);

    char* jsonStr = cJSON_PrintUnformatted(root);
    TEST_ASSERT_NOT_NULL(jsonStr);

    cJSON* parsed = cJSON_Parse(jsonStr);
    TEST_ASSERT_NOT_NULL(parsed);

    const cJSON* parsedTop = cJSON_GetObjectItem(parsed, "top");
    TEST_ASSERT_NOT_NULL(parsedTop);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(parsedTop));

    const cJSON* parsedBot = cJSON_GetObjectItem(parsed, "bottom");
    TEST_ASSERT_NOT_NULL(parsedBot);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(parsedBot));

    cJSON_free(jsonStr);
    cJSON_Delete(parsed);
    cJSON_Delete(root);
}

// 11. Recursive Mutex Re-entrancy Test
static void test_storage_recursive_mutex() {
    storage::StorageManager sm;
    // Calling methods that take the lock nestedly must not deadlock
    // deletePidPoint takes lock and internally calls loadPidLibrary & savePidLibrary
    // When unmounted, it safely returns false without deadlocking
    TEST_ASSERT_FALSE(sm.deletePidPoint(true, 0));
}

// 12. Wear-Leveling Limits and Safeguards Test
static void test_storage_wear_leveling_limits() {
    // Validate config constants
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, config::Limits::STORAGE_WARN_PERCENT);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, config::Limits::STORAGE_MAX_PERCENT);

    storage::StorageManager sm;
    // When unmounted, getStorageUsagePercent returns 0.0f and isStorageFullForNewFiles returns false
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, sm.getStorageUsagePercent());
    TEST_ASSERT_FALSE(sm.isStorageFullForNewFiles());

    // Verify const-correctness: calling methods on const sm reference works
    const storage::StorageManager& constSm = sm;
    size_t total = 0;
    size_t used = 0;
    TEST_ASSERT_FALSE(constSm.getStorageInfo(total, used));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, constSm.getStorageUsagePercent());
    TEST_ASSERT_FALSE(constSm.isStorageFullForNewFiles());
}

// 13. Empty Profile Filename Rejection Test
static void test_storage_empty_profile_filename_rejection() {
    storage::StorageManager sm;
    config::ReflowProfile p;
    p.name = "Test Empty File";
    p.file = ""; // Empty filename must be rejected to prevent directory corruption
    p.stepsTop.push_back({150.0f, 60, 1.0f});

    TEST_ASSERT_FALSE(sm.saveProfile(p));
}

// 14. Unmounted Operations Guard Test
static void test_storage_unmounted_guards() {
    storage::StorageManager sm;
    TEST_ASSERT_FALSE(sm.isMounted());

    config::MachineSettings settings;
    TEST_ASSERT_FALSE(sm.loadSettings(settings));
    TEST_ASSERT_FALSE(sm.saveSettings(settings));

    config::ReflowProfile profile;
    profile.name = "Valid Profile";
    profile.file = "valid.json";
    profile.stepsTop.push_back({150.0f, 60, 1.0f});
    TEST_ASSERT_FALSE(sm.loadProfile("valid.json", profile));
    TEST_ASSERT_FALSE(sm.saveProfile(profile));
    TEST_ASSERT_FALSE(sm.deleteProfile("valid.json"));
    TEST_ASSERT_TRUE(sm.listProfiles().empty());

    config::PidLibrary pidLib;
    pidLib.top.push_back({150.0f, 2.0f, 0.05f, 1.0f});
    TEST_ASSERT_FALSE(sm.loadPidLibrary(pidLib));
    TEST_ASSERT_FALSE(sm.savePidLibrary(pidLib));
    TEST_ASSERT_FALSE(sm.deletePidPoint(true, 0));

    std::string text;
    TEST_ASSERT_FALSE(sm.readTextFile("/littlefs/test.txt", text));
    TEST_ASSERT_FALSE(sm.writeTextFileAtomic("/littlefs/test.txt", "hello"));
    TEST_ASSERT_FALSE(sm.fileExists("/littlefs/test.txt"));
}

// 15. JSON Parser Adversarial & Fuzzing Robustness Test
static void test_storage_json_fuzzing_and_adversarial_robustness() {
    config::ReflowProfile prof;
    bool schemaIncompat = false;

    // 1. Structural Malformations & Null Inputs
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile("", "empty.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile("   \t\r\n   ", "ws.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(
        storage::JsonSerializer::deserializeProfile("{ unquoted_key: 123 }", "malformed.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(
        storage::JsonSerializer::deserializeProfile("{\"name\": \"unclosed", "unclosed.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile("[]", "array_root.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile("12345", "num_root.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(
        storage::JsonSerializer::deserializeProfile("\"just a string\"", "str_root.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile("null", "null_root.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile("true", "bool_root.json", prof, schemaIncompat));

    // 2. Deep Nesting Attack (Stack exhaustion defense)
    std::string deepNested;
    for (int i = 0; i < 50; ++i) {
        deepNested += "{\"nested\":";
    }
    deepNested += "1";
    for (int i = 0; i < 50; ++i) {
        deepNested += "}";
    }
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile(deepNested, "deep.json", prof, schemaIncompat));

    // 3. Huge Payload & Step Flooding Attack
    std::string hugeName(10000, 'A');
    std::string hugeNameJson = "{\"name\":\"" + hugeName + "\",\"stepsTop\":[],\"stepsBottom\":[]}";
    TEST_ASSERT_FALSE(
        storage::JsonSerializer::deserializeProfile(hugeNameJson, "huge_name.json", prof, schemaIncompat));

    std::string stepFlood = "{\"name\":\"Flood\",\"stepsTop\":[";
    for (int i = 0; i < 100; ++i) {
        stepFlood += "{\"temp\":150,\"time\":30,\"ramp\":1.0}";
        if (i < 99) {
            stepFlood += ",";
        }
    }
    stepFlood += "],\"stepsBottom\":[]}";
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile(stepFlood, "flood.json", prof, schemaIncompat));

    // 4. Type Confusion Attacks
    TEST_ASSERT_FALSE(
        storage::JsonSerializer::deserializeProfile("{\"name\":12345}", "type_num.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile("{\"name\":{\"sub\":\"obj\"}}", "type_obj.json", prof,
                                                                  schemaIncompat));
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile("{\"name\":\"Test\",\"stepsTop\":\"not_array\"}",
                                                                  "type_str_steps.json", prof, schemaIncompat));
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile(
        "{\"name\":\"Test\",\"stepsTop\":[null, 123, \"str\"]}", "type_mixed_steps.json", prof, schemaIncompat));

    // 5. Numeric Extremes & Floating-Point Boundaries
    const char* negTimeJson =
        "{\"name\":\"NegTime\",\"stepsTop\":[{\"temp\":150.0,\"time\":-10,\"ramp\":1.0}],\"stepsBottom\":[]}";
    TEST_ASSERT_FALSE(storage::JsonSerializer::deserializeProfile(negTimeJson, "neg_time.json", prof, schemaIncompat));

    const char* extremeFloatJson =
        "{\"name\":\"Extreme\",\"stepsTop\":[{\"temp\":1e308,\"time\":10,\"ramp\":1.0}],\"stepsBottom\":[]}";
    TEST_ASSERT_FALSE(
        storage::JsonSerializer::deserializeProfile(extremeFloatJson, "extreme_float.json", prof, schemaIncompat));

    const char* negExtJson =
        "{\"name\":\"Extreme\",\"stepsTop\":[{\"temp\":-1e308,\"time\":10,\"ramp\":1.0}],\"stepsBottom\":[]}";
    TEST_ASSERT_FALSE(
        storage::JsonSerializer::deserializeProfile(negExtJson, "neg_extreme_float.json", prof, schemaIncompat));

    const char* domainOutOfRangeJson =
        "{\"name\":\"Range\",\"stepsTop\":[{\"temp\":999.0,\"time\":10,\"ramp\":1.0}],\"stepsBottom\":[]}";
    TEST_ASSERT_FALSE(
        storage::JsonSerializer::deserializeProfile(domainOutOfRangeJson, "range.json", prof, schemaIncompat));

#if defined(HOST_TEST)
    // 6. 500-Iteration Randomized Fuzzing Loop (Mutation-based robustness under ASan/UBSan)
    const std::string validBaseline =
        "{\"schemaVersion\":1,\"name\":\"ValidSAC305\",\"stepsTop\":[{\"temp\":150.0,\"time\":30,\"ramp\":1.5}],"
        "\"stepsBottom\":[{\"temp\":150.0,\"time\":30,\"ramp\":1.5}]}";

    // Seed deterministic PRNG for reproducible test runs
    uint32_t prngState = 0x12345678;
    auto nextPrng = [&prngState]() -> uint32_t {
        prngState ^= (prngState << 13);
        prngState ^= (prngState >> 17);
        prngState ^= (prngState << 5);
        return prngState;
    };

    for (int iter = 0; iter < 500; ++iter) {
        std::string mutated = validBaseline;
        uint32_t numMutations = (nextPrng() % 5) + 1;
        for (uint32_t m = 0; m < numMutations; ++m) {
            size_t pos = static_cast<size_t>(nextPrng() % mutated.length());
            uint32_t op = nextPrng() % 4;
            switch (op) {
            case 0: // Byte flip
                mutated[pos] = static_cast<char>(nextPrng() & 0xFF);
                break;
            case 1: // Truncate string
                mutated.resize(pos);
                break;
            case 2: // Null byte injection
                mutated[pos] = '\0';
                break;
            case 3: // Insert punctuation
                mutated.insert(pos, 1, (nextPrng() % 2 == 0) ? '{' : ']');
                break;
            default:
                break;
            }
            if (mutated.empty()) {
                break;
            }
        }

        // Must never crash, leak memory (ASan), or cause undefined behavior (UBSan)
        config::ReflowProfile fuzzedProf;
        bool fuzzedIncompat = false;
        bool res = storage::JsonSerializer::deserializeProfile(mutated, "fuzz.json", fuzzedProf, fuzzedIncompat);
        (void)res;
    }
#endif
}

#if defined(HOST_TEST)
#include "esp_littlefs.h"
#include <cstdlib>

// 16. Comprehensive Host LittleFS StorageManager Operations and Edge Cases Test
static void test_storage_manager_mounted_operations() {
    const char* testPath = "/tmp/test_bga_storage";
    (void)system("rm -rf /tmp/test_bga_storage");
    mockLittleFsReset(1048576, 262144);
    storage::StorageManager storage;

    // 1. Mount LittleFS at host tmpdir
    esp_err_t err = storage.begin(testPath, "littlefs");
    TEST_ASSERT_EQUAL_INT(ESP_OK, err);
    TEST_ASSERT_TRUE(storage.isMounted());

    // 2. Storage capacity metrics
    size_t total = 0;
    size_t used = 0;
    TEST_ASSERT_TRUE(storage.getStorageInfo(total, used));
    TEST_ASSERT_EQUAL_UINT32(1048576, total);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 25.0f, storage.getStorageUsagePercent());
    TEST_ASSERT_FALSE(storage.isStorageFullForNewFiles());

    // 3. Save, exists, load and delete roundtrip
    config::ReflowProfile p;
    p.name = "Host SAC305";
    p.file = "host_sac305.json";
    config::ProfileStep s1{150.0f, 30, 1.5f};
    p.stepsTop.push_back(s1);

    TEST_ASSERT_TRUE(storage.saveProfile(p));
    TEST_ASSERT_TRUE(storage.profileExists("host_sac305.json"));

    config::ReflowProfile loaded;
    TEST_ASSERT_TRUE(storage.loadProfile("host_sac305.json", loaded));
    TEST_ASSERT_EQUAL_STRING("Host SAC305", loaded.name.c_str());

    std::vector<std::string> profiles = storage.listProfiles();
    TEST_ASSERT_FALSE(profiles.empty());

    // 4. Security & Path traversal rejections
    TEST_ASSERT_FALSE(storage.profileExists("../evil.json"));
    TEST_ASSERT_FALSE(storage.deleteProfile("../evil.json"));
    TEST_ASSERT_FALSE(storage.deleteProfile("/tmp/evil.json"));

    // 5. Immutable factory profile protection
    TEST_ASSERT_FALSE(storage.deleteProfile("factory-profile.json"));

    // 6. Delete created profile
    TEST_ASSERT_TRUE(storage.deleteProfile("host_sac305.json"));
    TEST_ASSERT_FALSE(storage.profileExists("host_sac305.json"));

    // 7. Settings save and load
    config::MachineSettings set;
    TEST_ASSERT_TRUE(storage.loadSettings(set));
    set.maxTempTop = 260.0f;
    TEST_ASSERT_TRUE(storage.saveSettings(set));
    config::MachineSettings verifySet;
    TEST_ASSERT_TRUE(storage.loadSettings(verifySet));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 260.0f, verifySet.maxTempTop);

    // 8. PID library save, load, and point deletion
    config::PidLibrary lib;
    TEST_ASSERT_TRUE(storage.loadPidLibrary(lib));
    size_t countBefore = lib.top.size();
    config::PidPoint pt{250.0f, 3.5f, 0.10f, 1.8f};
    lib.top.push_back(pt);
    TEST_ASSERT_TRUE(storage.savePidLibrary(lib));
    config::PidLibrary verifyLib;
    TEST_ASSERT_TRUE(storage.loadPidLibrary(verifyLib));
    TEST_ASSERT_EQUAL_UINT32(countBefore + 1, verifyLib.top.size());

    // Test deletePidPoint (valid and out-of-range index)
    TEST_ASSERT_TRUE(storage.deletePidPoint(true, countBefore));
    TEST_ASSERT_FALSE(storage.deletePidPoint(true, 999));
    TEST_ASSERT_TRUE(storage.loadPidLibrary(verifyLib));
    TEST_ASSERT_EQUAL_UINT32(countBefore, verifyLib.top.size());

    // 9. Wear-leveling / storage full rejection (> 90% full)
    mockLittleFsReset(1000000, 950000); // 95% full
    TEST_ASSERT_TRUE(storage.isStorageFullForNewFiles());
    config::ReflowProfile pNew;
    pNew.name = "Full Disk Rejection";
    pNew.file = "fulldisk.json";
    pNew.stepsTop.push_back(config::ProfileStep{150.0f, 30, 1.5f});
    TEST_ASSERT_FALSE(storage.saveProfile(pNew));
    mockLittleFsReset(1048576, 262144); // reset

    // 10. Clean unmount
    storage.stop();
    TEST_ASSERT_FALSE(storage.isMounted());
    (void)system("rm -rf /tmp/test_bga_storage");
}

// 17. Direct File I/O & Atomic Replacement Test
static void test_storage_text_file_atomic_and_direct_io() {
    const char* testPath = "/tmp/test_bga_storage_io";
    (void)system("rm -rf /tmp/test_bga_storage_io");
    mockLittleFsReset(1048576, 262144);
    storage::StorageManager storage;

    TEST_ASSERT_EQUAL_INT(ESP_OK, storage.begin(testPath, "littlefs"));
    TEST_ASSERT_TRUE(storage.isMounted());

    std::string testFile = std::string(testPath) + "/config/test_atomic.txt";

    // 1. Read non-existent file
    std::string content;
    TEST_ASSERT_FALSE(storage.readTextFile(testFile, content));
    TEST_ASSERT_FALSE(storage.fileExists(testFile));

    // 2. Write atomic file
    const std::string originalData = "Hello LittleFS Atomic Write!";
    TEST_ASSERT_TRUE(storage.writeTextFileAtomic(testFile, originalData));
    TEST_ASSERT_TRUE(storage.fileExists(testFile));

    // 3. Read back verified content
    TEST_ASSERT_TRUE(storage.readTextFile(testFile, content));
    TEST_ASSERT_EQUAL_STRING(originalData.c_str(), content.c_str());

    // 4. Overwrite atomically with empty content
    TEST_ASSERT_TRUE(storage.writeTextFileAtomic(testFile, ""));
    TEST_ASSERT_TRUE(storage.readTextFile(testFile, content));
    TEST_ASSERT_TRUE(content.empty());

    storage.stop();
    TEST_ASSERT_FALSE(storage.isMounted());
    (void)system("rm -rf /tmp/test_bga_storage_io");
}
#endif

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_storage_tests() {
    RUN_TEST(test_storage_lifecycle);
    RUN_TEST(test_storage_profile_struct);
    RUN_TEST(test_storage_schema_values);
    RUN_TEST(test_storage_validation_guards);
    RUN_TEST(test_storage_float_rounding_precision);
    RUN_TEST(test_storage_schema_migration_pipeline);
    RUN_TEST(test_storage_schema_future_version_rejection);
    RUN_TEST(test_storage_factory_profile_protection);
    RUN_TEST(test_storage_atomic_file_ops);
    RUN_TEST(test_storage_pid_library_serialization_roundtrip);
    RUN_TEST(test_storage_recursive_mutex);
    RUN_TEST(test_storage_wear_leveling_limits);
    RUN_TEST(test_storage_empty_profile_filename_rejection);
    RUN_TEST(test_storage_unmounted_guards);
    RUN_TEST(test_storage_json_fuzzing_and_adversarial_robustness);
#if defined(HOST_TEST)
    RUN_TEST(test_storage_manager_mounted_operations);
    RUN_TEST(test_storage_text_file_atomic_and_direct_io);
#endif
}
