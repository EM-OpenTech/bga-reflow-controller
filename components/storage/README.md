# Unified LittleFS Storage Manager Component (C++20)

Thread-safe, atomic LittleFS file system manager and JSON persistence engine for the **BGA Reflow Controller**, running natively on **ESP-IDF v6.0.2** and **FreeRTOS**.

This component manages persistent configuration storage, reflow profiles, PID tuning libraries, and Web UI static assets on SPI NOR Flash. It provides atomic file writes via staging, hardware power-loss resilience, domain model validation guards, path traversal prevention, and schema version migrations.

---

## 1. Overview & Architecture

Flash memory operations on embedded microcontrollers must guarantee data integrity even during sudden power losses, brownouts, or thermal cutoffs. The `storage` component abstracts the ESP-IDF Virtual File System (VFS) and the wear-leveling LittleFS driver into a thread-safe domain persistence layer:

```
    [ Web REST API / Web Server ]          [ AppController / Reflow FSM ]
                 |                                      |
                 +-------------------+------------------+
                                     |
                                     v
    +-------------------------------------------------------------------------+
    |                         storage::StorageManager                         |
    |  - FreeRTOS Mutex Protection (xSemaphoreCreateRecursiveMutex)           |
    |  - VFS Mount / Unmount Lifecycle (/littlefs)                            |
    |  - Atomic File Write Engine (.tmp staging -> rename)                    |
    |  - Path Traversal Sanitization ("..", "/", "\\")                        |
    |  - Factory Profile Delete Protection ("factory-profile.json")           |
    |  - Schema Versioning, Future Version Rejection, & Migration Pipeline    |
    |  - IEEE 754 Float Artifact Elimination (round1, round2, round3)         |
    |  - cJSON Parsing & Serialization Engine                                 |
    +-------------------------------------------------------------------------+
                                     |
                                     v
    +-------------------------------------------------------------------------+
    |                 ESP-IDF VFS LittleFS (joltwallet/littlefs)              |
    |  - Partition Label: "littlefs" (16 MB SPI Flash Partition Table)        |
    |  - Dynamic Wear Leveling, Power-Loss Resilient B-Tree Indices           |
    +-------------------------------------------------------------------------+
                                     |
                                     v
                      [ Physical SPI Flash Memory ]
```

---

## 2. File System Layout & Default Hierarchy

On initialization (`begin()`), the storage manager verifies the directory structure, cleans up any stale `.tmp` files, and automatically formats blank partitions on first boot (`format_if_mount_failed = true`).

```
/littlefs/
├── config/
│   ├── settings.json            # MachineSettings (thermal limits, watchdogs, UI settings)
│   └── pid_library.json         # PidLibrary (temperature-dependent Kp, Ki, Kd points)
└── profiles/
    ├── factory-profile.json     # Protected immutable reference SAC305 profile
    └── *.json                   # User-defined dual-channel reflow profiles
```

> [!NOTE]
> **Web UI Assets Storage:**  
> The Web UI frontend assets (`index.html.gz`, `app.js.gz`, `style.css.gz`, `de.js.gz`, `chart.min.js.gz`) are **embedded directly into the firmware binary** via `components/web/CMakeLists.txt` (`EMBED_FILES`) and served directly from program flash (RODATA) by the `StaticFileServer`. They do not reside on the LittleFS partition. This keeps the LittleFS filesystem exclusively dedicated to dynamic, mutable user configuration and telemetry profiles.

### Automatic Provisioning
If the filesystem is freshly formatted or files are absent:
* **Directories:** Creates `/littlefs/config` and `/littlefs/profiles` automatically.
* **`settings.json`:** Generates baseline machine settings with safe thermal boundaries and watchdog timers.
* **`factory-profile.json`:** Generates a standard lead-free SAC305 reference curve (Top and Bottom steps) so the controller is immediately operational.
* **`pid_library.json`:** Populates default baseline tuning points (150 °C and 220 °C for Top; 150 °C and 180 °C for Bottom).
* **Startup Cleanup:** Automatically scans `/config` and `/profiles` for any stale `.tmp` files leftover from brownouts or interrupted writes and unlinks them.

---

## 3. Robustness & Data Integrity Mechanisms

### 3.1 True Atomic File Staging (`writeTextFileAtomic`)
Direct in-place writes to flash risk leaving corrupt, half-written files if power fails mid-transfer. `StorageManager` uses an atomic staging mechanism:
1. Data is written to a temporary staging file: `<filepath>.tmp`.
2. `fflush()` and `fclose()` guarantee all cached bytes flush to physical SPI flash.
3. The temporary file is renamed to the target filename via POSIX `rename()`.
4. Under LittleFS, `rename()` is an atomic directory metadata pointer swap that replaces `<filepath>` in a single power-safe operation. If a power outage occurs before the swap, the original `<filepath>` remains completely intact.
5. If `rename()` fails, the staging `<filepath>.tmp` file is safely unlinked.

### 3.2 Path Traversal & Filename Length Prevention
Profile filenames received from external REST API endpoints or WebSocket frames are strictly sanitized. Filenames containing directory traversal sequences (`..`), forward slashes (`/`), backslashes (`\`), or exceeding `config::Limits::MAX_FILENAME_LENGTH` (40 chars) are immediately rejected:
```cpp
if (filename.empty() || filename.length() > config::Limits::MAX_FILENAME_LENGTH ||
    filename.find("..") != std::string::npos ||
    filename.find('/') != std::string::npos || 
    filename.find('\\') != std::string::npos) {
    return false; // Access Denied
}
```

### 3.3 Protected Factory Profile
The built-in reference profile `factory-profile.json` is hard-coded as protected. Any invocation of `deleteProfile("factory-profile.json")` or attempt to overwrite an existing factory profile via `saveProfile()` returns `false` and preserves the factory baseline.

### 3.4 Floating-Point Precision Sanitization
Serializing IEEE 754 32-bit floats directly to text can produce precision noise (e.g. `150.00000012` or `0.3000000119`). `StorageManager` sanitizes floating-point values to their architectural resolutions before JSON serialization:
* **`round1` (0.1 °C):** Temperatures (`maxTempTop`, `coolingSafeTemp`, setpoints, offsets).
* **`round2` (0.01):** Ramp rates (°C/s), $K_p$, $K_d$, and hold tolerances.
* **`round3` (0.001):** Integral gains ($K_i$) and EMA filter smoothing factors ($\alpha$).

### 3.5 Domain Validation Guard & Heap OOM Protection
* **Pre-Write Validation:** Before writing any file to flash, `saveSettings()`, `saveProfile()`, and `savePidLibrary()` execute the domain model's self-validating `.validate()` method. If any parameter violates `config::Limits`, the write is aborted before touching flash.
* **Read OOM Guard:** `readTextFile()` enforces an upper bound against `config::Limits::MAX_JSON_PAYLOAD_BYTES` (64 KB). Any corrupted file with an anomalous size descriptor is rejected before dynamic heap allocation.

### 3.6 Schema Versioning & Forward Incompatibility Guard
All JSON configurations store a `schemaVersion` attribute:
* **Forward Compatibility:** If a configuration file has a `schemaVersion` greater than the running firmware supports (e.g., a backup created by a newer firmware version), it is rejected to prevent undefined behavior. The `_schemaIncompatible` flag is set and can be queried via `hasSchemaIncompatibility()`.
* **Backward Compatibility:** Legacy configurations missing `schemaVersion` (v0) are transparently migrated to schema version 1 upon load.

### 3.7 Re-Entrant Mutex Concurrency
`StorageManager` employs a FreeRTOS recursive mutex (`xSemaphoreCreateRecursiveMutex`). This permits nested calls from the same task (e.g. `deletePidPoint` acquiring the lock and safely calling `loadPidLibrary` and `savePidLibrary`) without deadlocks, while guaranteeing strict serial execution between Core 0 (Web REST API) and Core 1 (Control / Autotune task).

### 3.8 Flash Wear-Leveling Safeguards & Allocation Limits
LittleFS utilizes dynamic block-level wear leveling with copy-on-write logging. However, when flash partitions reach high capacity ($\ge 90\%$), copy-on-write compaction suffers from extreme write amplification, and the lack of spare erase blocks degrades NOR flash endurance.
To ensure lifetime longevity:
* **80% Advisory Warning (`config::Limits::STORAGE_WARN_PERCENT`):** The REST API reports `isWarning = true`, enabling proactive UI diagnostics before write rejections happen.
* **90% Hard Guard (`config::Limits::STORAGE_MAX_PERCENT`):** `isStorageFullForNewFiles()` blocks creation of new profiles or files, returning `"Storage limit of 90% reached. Save rejected to preserve flash wear leveling."`. Existing file updates (e.g. modifying `settings.json` or saving an existing profile) remain permitted so operations never lock up.

---

## 4. Public API Reference

Defined in `storage::StorageManager` (`components/storage/include/storage/storage_manager.hpp`):

| Method | Return Type | Description |
| :--- | :---: | :--- |
| `begin(basePath, partitionLabel)` | `esp_err_t` | Mounts LittleFS partition and provisions missing directories. |
| `stop()` | `void` | Flushes caches and unmounts LittleFS VFS. |
| `isMounted()` | `bool` | Returns `true` if LittleFS is mounted and accessible. |
| `getStorageInfo(total, used)` | `bool` | Queries partition total capacity and currently utilized bytes. |
| `getStorageUsagePercent()` | `float` | Returns currently used storage in percent (0.0% – 100.0%). |
| `isStorageFullForNewFiles()` | `bool` | Checks if storage $\ge$ 90% (`STORAGE_MAX_PERCENT`) to preserve wear leveling. |
| `loadSettings(settings)` | `bool` | Deserializes `config/settings.json` into `MachineSettings`. |
| `saveSettings(settings)` | `bool` | Validates and atomically saves `config/settings.json`. |
| `loadProfile(filename, profile)` | `bool` | Loads and parses a reflow profile from `profiles/<filename>`. |
| `saveProfile(profile)` | `bool` | Validates, checks 90% limit for new files, and atomically saves profile. |
| `deleteProfile(filename)` | `bool` | Deletes a profile file (protected files cannot be deleted). |
| `listProfiles()` | `std::vector<std::string>` | Lists all `*.json` profile filenames in `/profiles/`. |
| `loadPidLibrary(library)` | `bool` | Loads gain scheduling tables from `config/pid_library.json`. |
| `savePidLibrary(library)` | `bool` | Validates and atomically saves `config/pid_library.json`. |
| `deletePidPoint(isTop, index)` | `bool` | Removes a specific tuning point from the PID library. |
| `fileExists(filepath)` | `bool` | Checks if a file exists via `stat()`. |
| `readTextFile(filepath, outStr)` | `bool` | Reads entire file content into an `std::string`. |
| `writeTextFileAtomic(path, str)` | `bool` | Writes string data atomically via staging `.tmp` file. |
| `hasSchemaIncompatibility()` | `bool` | Returns `true` if a newer schema version was rejected. |

---

## 5. C++20 Usage Example

```cpp
#include "storage/storage_manager.hpp"
#include "esp_log.h"

static const char* TAG = "STORAGE_EXAMPLE";

void storage_demo()
{
    storage::StorageManager storage;

    // 1. Mount LittleFS partition
    esp_err_t err = storage.begin("/littlefs", "littlefs");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return;
    }

    // 2. Query storage usage & flash wear-leveling status
    size_t totalBytes = 0, usedBytes = 0;
    if (storage.getStorageInfo(totalBytes, usedBytes)) {
        float usedPct = storage.getStorageUsagePercent();
        ESP_LOGI(TAG, "Storage: %zu KB used / %zu KB total (%.1f%%)", 
                 usedBytes / 1024, totalBytes / 1024, usedPct);
        if (storage.isStorageFullForNewFiles()) {
            ESP_LOGW(TAG, "Flash storage >= 90%%! New profile creation is locked to preserve wear leveling.");
        }
    }

    // 3. Load or initialize machine settings
    config::MachineSettings settings;
    if (storage.loadSettings(settings)) {
        ESP_LOGI(TAG, "Loaded settings: language=%s, maxTempTop=%.1f°C",
                 settings.language.c_str(), settings.maxTempTop);
    }

    // 4. Save custom reflow profile atomically
    config::ReflowProfile myProfile;
    myProfile.name = "Low-Temp Bismuth Sn42Bi58";
    myProfile.file = "sn42bi58.json";
    myProfile.stepsBottom.push_back({100.0f, 60, 1.0f});
    myProfile.stepsTop.push_back({138.0f, 30, 1.2f});

    if (storage.saveProfile(myProfile)) {
        ESP_LOGI(TAG, "Profile saved successfully: %s", myProfile.file.c_str());
    }

    // 5. List available profiles
    std::vector<std::string> profileList = storage.listProfiles();
    ESP_LOGI(TAG, "Found %zu profiles on disk:", profileList.size());
    for (const auto& file : profileList) {
        ESP_LOGI(TAG, " - %s", file.c_str());
    }
}
```

---

## 6. Unit Tests & Automated Verification

The component includes 12 automated Unity unit tests located in [`test/test_storage.cpp`](test/test_storage.cpp) verifying filesystem operations, JSON parsing, security guards, re-entrant locking, and wear leveling:

| Test Case | Description |
| :--- | :--- |
| `test_storage_lifecycle` | Asserts unmounted initial state and clean lifecycle destructors. |
| `test_storage_profile_struct` | Verifies dual-channel step vector populations and floating-point fidelity. |
| `test_storage_schema_values` | Confirms schema version constants are non-zero positive integers. |
| `test_storage_validation_guards` | Verifies rejection of invalid profiles, out-of-range temperatures, filename length violations, non-existent files, and directory traversal attempts (`../`). |
| `test_storage_float_rounding_precision` | Validates rounding precision rules for temperatures (0.1), ramp rates (0.01), and PID $K_i$ (0.001). |
| `test_storage_schema_migration_pipeline` | Simulates legacy schema v1 migration to schema v2 with default property injection. |
| `test_storage_schema_future_version_rejection` | Asserts files with higher schema versions (e.g. v99) are safely rejected. |
| `test_storage_factory_profile_protection` | Confirms `deleteProfile("factory-profile.json")` and `deleteProfile("factory-profile")` are blocked and return `false`. |
| `test_storage_atomic_file_ops` | Verifies temporary path formatting and `.tmp` naming integrity. |
| `test_storage_pid_library_serialization_roundtrip` | Full JSON serialization and deserialization roundtrip test of multi-point PID tables. |
| `test_storage_recursive_mutex` | Asserts recursive mutex re-entrancy does not deadlock when nested locking occurs. |
| `test_storage_wear_leveling_limits` | Validates 80% advisory warning and 90% hard limit constants, unmounted fallback behavior, and const query correctness. |

---

## 7. CMake Integration

Defined in `components/storage/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "storage_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES "config" "freertos" "joltwallet__littlefs"
    PRIV_REQUIRES "cjson" "nvs_flash" "log"
)
```

Managed dependencies via `components/storage/idf_component.yml`:
* `joltwallet/littlefs`: LittleFS port for ESP-IDF
* `espressif/cjson`: Lightweight C JSON parser and generator

---

## 8. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../../LICENSE) file for the full license text.
