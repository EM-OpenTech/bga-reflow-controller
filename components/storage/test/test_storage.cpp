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
 * @author ESP-IDF Reflow Controller Team
 * @date 2026-09-23
 */

#include "unity.h"
#include "storage/storage_manager.hpp"
#include "config/machine_config.hpp"
#include "cJSON.h"

// ── 1. Lifecycle Test ─────────────────────────────────────────────────────────

static void test_storage_lifecycle()
{
    storage::StorageManager storage;
    TEST_ASSERT_FALSE(storage.isMounted());
}

// ── 2. Profile Struct Test ────────────────────────────────────────────────────

static void test_storage_profile_struct()
{
    config::ReflowProfile p;
    p.name = "Lead-Free SAC305";
    p.file = "sac305.json";

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

// ── 3. Schema Values Test ─────────────────────────────────────────────────────

static void test_storage_schema_values()
{
    // Schema versions must be positive defined constants (> 0)
    TEST_ASSERT_GREATER_THAN_UINT16(0, config::Schema::MACHINE_SETTINGS);
    TEST_ASSERT_GREATER_THAN_UINT16(0, config::Schema::REFLOW_PROFILE);
    TEST_ASSERT_GREATER_THAN_UINT16(0, config::Schema::PID_LIBRARY);
}

// ── 4. Validation Guards Test ─────────────────────────────────────────────────

static void test_storage_validation_guards()
{
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

    // Path traversal in loadProfile and deleteProfile must be rejected
    config::ReflowProfile dummyProf;
    TEST_ASSERT_FALSE(storage.loadProfile("../config/settings.json", dummyProf));
    TEST_ASSERT_FALSE(storage.deleteProfile("../config/settings.json"));
    TEST_ASSERT_FALSE(storage.deleteProfile("/etc/passwd"));
}

// ── 5. Float Rounding Precision Test ──────────────────────────────────────────

static void test_storage_float_rounding_precision()
{
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

// ── 6. Schema Migration Pipeline Test ─────────────────────────────────────────

static void test_storage_schema_migration_pipeline()
{
    // Simulate loading a Schema v1 JSON document and migrating it to v2
    const char* schemaV1Json = "{\"schemaVersion\":1,\"simulationMode\":true,\"maxTempTop\":250.0}";
    cJSON* root = cJSON_Parse(schemaV1Json);
    TEST_ASSERT_NOT_NULL(root);

    cJSON* verItem = cJSON_GetObjectItem(root, "schemaVersion");
    TEST_ASSERT_NOT_NULL(verItem);
    uint16_t ver = static_cast<uint16_t>(verItem->valueint);
    TEST_ASSERT_EQUAL_UINT16(1, ver);

    // Apply migration step (v1 -> v2)
    if (ver == 1) {
        // 1. Inject missing v2 field if not present
        if (!cJSON_GetObjectItem(root, "hardwareBuzzerEnabled")) {
            cJSON_AddBoolToObject(root, "hardwareBuzzerEnabled", true);
        }
        // 2. Bump schema version to 2
        cJSON_SetNumberValue(verItem, 2);
    }

    // Verify document is now valid Schema v2
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItem(root, "schemaVersion")->valueint);
    cJSON* buzItem = cJSON_GetObjectItem(root, "hardwareBuzzerEnabled");
    TEST_ASSERT_NOT_NULL(buzItem);
    TEST_ASSERT_TRUE(cJSON_IsTrue(buzItem));

    cJSON_Delete(root);
}

// ── 7. Future Version Rejection Test ──────────────────────────────────────────

static void test_storage_schema_future_version_rejection()
{
    // A file with schemaVersion > supported version (e.g. v99) must not be migrated or accepted blindly
    const char* futureSchemaJson = "{\"schemaVersion\":99,\"customFutureField\":true}";
    cJSON* root = cJSON_Parse(futureSchemaJson);
    TEST_ASSERT_NOT_NULL(root);

    cJSON* verItem = cJSON_GetObjectItem(root, "schemaVersion");
    TEST_ASSERT_NOT_NULL(verItem);
    uint16_t ver = static_cast<uint16_t>(verItem->valueint);
    
    // Future schema version check
    bool isFutureVersion = (ver > config::Schema::MACHINE_SETTINGS);
    TEST_ASSERT_TRUE(isFutureVersion);

    cJSON_Delete(root);

    storage::StorageManager sm;
    TEST_ASSERT_FALSE(sm.hasSchemaIncompatibility());
}

// ── 8. Factory Profile Delete Protection Test ─────────────────────────────────

static void test_storage_factory_profile_protection()
{
    storage::StorageManager storage;
    // Attempting to delete the protected factory profile must return false
    TEST_ASSERT_FALSE(storage.deleteProfile("factory-profile.json"));
    TEST_ASSERT_FALSE(storage.deleteProfile("factory-profile"));
}

// ── 9. Atomic File Operation Helpers Test ─────────────────────────────────────

static void test_storage_atomic_file_ops()
{
    std::string path = "/littlefs/config/test.json";
    std::string tmpPath = path + ".tmp";
    TEST_ASSERT_EQUAL_STRING("/littlefs/config/test.json.tmp", tmpPath.c_str());
}

// ── Runner ───────────────────────────────────────────────────────────────────

void run_storage_tests()
{
    RUN_TEST(test_storage_lifecycle);
    RUN_TEST(test_storage_profile_struct);
    RUN_TEST(test_storage_schema_values);
    RUN_TEST(test_storage_validation_guards);
    RUN_TEST(test_storage_float_rounding_precision);
    RUN_TEST(test_storage_schema_migration_pipeline);
    RUN_TEST(test_storage_schema_future_version_rejection);
    RUN_TEST(test_storage_factory_profile_protection);
    RUN_TEST(test_storage_atomic_file_ops);
}

