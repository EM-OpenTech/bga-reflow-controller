/**
 * BGA Reflow Controller – Frontend Application
 * Language: English (EN)
 * Standards: Modern ES6+, Strict Mode, Namespace Isolation, Zero Global Leaks.
 */

'use strict';

const ReflowApp = (function () {

    // ── 1. STATE STORE ──────────────────────────────────────────────────────
    const store = {
        // Actor States (Direct from Backend)
        lampState: false,
        fanState: false,

        // System State (0=IDLE, 1=PREHEAT, 2=SOAK, 3=REFLOW, 4=COOLING, 5=DONE, 6=FAULT, 7=AUTOTUNE, 8=BACKUP)
        stateEnum: 0,
        stateStr: 'IDLE',
        prevStateEnum: null,

        // Active Profile
        activeProfileName: 'No Profile Loaded',
        activeProfileFile: '',
        profiles: null,

        // Live Measurements
        topTemp: 0.0,
        bottomTemp: 0.0,
        topSet: 0.0,
        bottomSet: 0.0,
        topPower: 0,
        bottomPower: 0,
        elapsedSec: 0,

        // Computed HUD Metrics
        topPeakTemp: 0.0,
        bottomPeakTemp: 0.0,
        topRampRate: 0.0,
        bottomRampRate: 0.0,
        lastTopTemp: 0.0,
        lastBottomTemp: 0.0,
        lastRampCalcTime: Date.now(),
        hasFirstRampReading: false,
        talSec: 0,

        // UI State
        currentTab: 'chart',
        currentTheme: 'light',
        currentLanguage: localStorage.getItem('reflow_lang') || 'en',
        statusLogOpen: false,
        wsConnected: false,
        backupTaken: false,
        showZones: true,
        showTalLine: true,
        showStepMarkers: true,
        showPidGains: false,
        lastTelemetryTime: 0,

        // Stock PID Settings & Library Mode
        pidLibraryEnabled: false,
        topKp: 2.0,
        topKi: 0.05,
        topKd: 1.0,
        bottomKp: 2.0,
        bottomKi: 0.04,
        bottomKd: 1.0,

        // Downgrade / Incompatibility flag
        schemaIncompatibleNotified: false
    };

    // Centralized Global Validation Limits & Sanity Bounds (Re-usable for future UI inputs)
    const VALIDATION_LIMITS = {
        MIN_TEMPERATURE: 30.0,
        MAX_TEMPERATURE: 300.0,
        MIN_RAMP_RATE: 0.1,
        MAX_RAMP_RATE: 10.0,
        MAX_STEP_TIME_S: 600,
        MIN_SAFE_COOLING_TEMP: 20.0,
        MAX_SAFE_COOLING_TEMP: 100.0,
        MIN_HOLD_TOLERANCE: 0.5,
        MAX_HOLD_TOLERANCE: 30.0,
        MIN_SETTLE_S: 1,
        MAX_SETTLE_S: 60,
        MIN_BURST_WINDOW_MS: 500,
        MAX_BURST_WINDOW_MS: 5000,
        MIN_EMA_ALPHA: 0.01,
        MAX_EMA_ALPHA: 1.0,
        MIN_CJ_OFFSET: -8.0,
        MAX_CJ_OFFSET: 7.9,
        MAX_PROFILE_STEPS: 10,
        MAX_PID_POINTS: 15,
        MAX_JSON_SIZE_BYTES: 65536,    // 64 KB
        MAX_ZIP_SIZE_BYTES: 524288     // 512 KB
    };

    // Centralized Domain Model Schema Versions (Firmware Compatibility Guards)
    const SCHEMA_VERSIONS = {
        MACHINE_SETTINGS: 1,
        REFLOW_PROFILE: 1,
        PID_LIBRARY: 1
    };

    // Centralized Step Resolutions (Single Source of Truth for Input Steps)
    const VALIDATION_RESOLUTIONS = {
        TEMPERATURE: 0.1,
        RAMP_RATE: 0.01,
        PID_GAIN_KP: 0.01,
        PID_GAIN_KI: 0.001,
        PID_GAIN_KD: 0.01,
        FILTER_ALPHA: 0.001,
        OFFSET_TEMP: 0.1,
        TOLERANCE_TEMP: 0.1,
        TIME_SEC: 1,
        TIME_MS: 100
    };

    // Centralized Mathematical Formatting Helpers (DIN 1333 / Half-Away-From-Zero Rounding)
    const Format = {
        round: (val, decimals = 1, fallback = '--') => {
            if (val === null || val === undefined || isNaN(val) || val === '') return fallback;
            const num = Number(val);
            if (!isFinite(num)) return fallback;
            const factor = Math.pow(10, decimals);
            return (Math.round((num + Number.EPSILON) * factor) / factor).toFixed(decimals);
        },
        temp:  (val) => Format.round(val, 1, '0.0'),
        ramp:  (val) => Format.round(val, 2, '0.00'),
        kp:    (val) => Format.round(val, 2, '0.00'),
        kd:    (val) => Format.round(val, 2, '0.00'),
        ki:    (val) => Format.round(val, 3, '0.000'),
        alpha: (val) => Format.round(val, 3, '0.000'),
        int:   (val) => (val === null || val === undefined || isNaN(val) || val === '') ? '0' : Math.round(Number(val)).toString(),
        pct:   (val) => (val === null || val === undefined || isNaN(val) || val === '') ? '0' : Math.round(Number(val)).toString(),
        parseFloat: (val, fallback = 0) => {
            if (val === null || val === undefined || val === '') return fallback;
            const clean = String(val).replace(',', '.').trim();
            const num = parseFloat(clean);
            return isNaN(num) ? fallback : num;
        },
        parseInt: (val, fallback = 0) => {
            if (val === null || val === undefined || val === '') return fallback;
            const clean = String(val).replace(',', '.').trim();
            const num = parseInt(clean, 10);
            return isNaN(num) ? fallback : num;
        }
    };

    // English UI Strings (Single Source of Truth & Fallback Dictionary)
    const STRINGS = {
        STATE_IDLE: 'IDLE',
        STATE_PREHEAT: 'PREHEAT',
        STATE_SOAK: 'SOAK',
        STATE_REFLOW: 'REFLOW',
        STATE_COOLING: 'COOLING',
        STATE_DONE: 'DONE',
        STATE_FAULT: 'FAULT',
        STATE_AUTOTUNE: 'AUTOTUNE',
        STATE_BACKUP: 'BACKUP',
        STATE_UNKNOWN: 'UNKNOWN',

        DISCONNECTED: 'DISCONNECTED'
    };

    const DEFAULT_STRINGS = {
        "status.connecting": "CONNECTING...",
        "status.connected": "CONNECTED",
        "status.disconnected": "DISCONNECTED",
        "status.badge_title": "Click for System-Log",
        "status.log_disconnected": "✕ Controller disconnected. Waiting for connection...",
        "status.log_unknown_state": "⚠ Unknown controller state. Waiting for valid telemetry...",
        "status.log_all_ok": "✓ All systems operational (No faults recorded)",

        "hud.runtime_prefix": "Runtime:",
        "hud.select_profile_placeholder": "— Select Profile —",

        "state.0": "IDLE",
        "state.1": "PREHEAT",
        "state.2": "SOAK",
        "state.3": "REFLOW",
        "state.4": "COOLING",
        "state.5": "DONE",
        "state.6": "FAULT",
        "state.7": "AUTOTUNE",
        "state.8": "BACKUP",
        "state.unknown": "UNKNOWN",

        "ctrl.lamp_title": "Worklight",
        "ctrl.lamp_on": "Worklight (ON)",
        "ctrl.lamp_off": "Worklight (OFF)",
        "ctrl.fan_title": "Cooling Fan",
        "ctrl.fan_on": "Cooling Fan (ON)",
        "ctrl.fan_off": "Cooling Fan (OFF)",
        "ctrl.export_csv_title": "Export CSV",
        "ctrl.export_png_title": "Export PNG",
        "ctrl.tooltip_disconnected": "⚠️ Telemetry disconnected - starting a process is locked until connection is restored.",
        "ctrl.start_idle_hint": "Step 2: Starts Reflow after preheating is complete",
        "ctrl.start_preheat_hint": "Advance from Preheat to Reflow",

        "chart.axis_runtime": "Runtime (min)",
        "chart.axis_temperature": "Temperature (°C)",
        "chart.ds_top_pv": "Top PV",
        "chart.ds_top_sp": "Top SP",
        "chart.ds_bot_pv": "Bottom PV",
        "chart.ds_bot_sp": "Bottom SP",
        "chart.tooltip_time": "Time: {{time}} min ({{sec}}s)",
        "chart.step_badge": "Step {{num}}",
        "chart.zone_preheat": "PREHEAT (100-150°C)",
        "chart.zone_soak": "SOAK (150-190°C)",
        "chart.zone_prereflow": "PRE-REFLOW (190-217°C)",
        "chart.zone_reflow": "REFLOW (217-245°C)",

        "profile.no_steps": 'No steps defined. Click "+ Add Step" below.',
        "profile.no_profile_selected": 'No profile selected. Choose a profile from the dropdown above or click "+ New Profile".',
        "profile.select_placeholder": "— Select Profile to Edit —",
        "profile.no_profiles": "No Profiles Available",
        "profile.delete_step_title": "Delete Step",
        "profile.btn_new": "+ New Profile",

        "settings.opt_lang_en": "English (Default)",
        "settings.opt_lang_de": "Deutsch",
        "settings.opt_no_default_profile": "— Not Selected (Default) —",

        "tunepid.no_points": 'No PID points defined. Click "+ Add PID Point" below.',
        "tunepid.delete_point_title": "Delete Point",

        "modal.ws_disconnected_title": "Safety Interlock Active",
        "modal.ws_disconnected_msg": "Live telemetry stream is disconnected. Starting Reflow, Preheat, or Autotune is locked for safety until reconnection.",
        "modal.preheat_required_title": "Preheat Required",
        "modal.preheat_required_body": "A BGA reflow cycle begins with preheating the PCB (bottom heater).\n\nWould you like to start Preheat now with profile \"{{profile}}\"?",
        "modal.btn_start_preheat": "Start Preheat",
        "modal.confirm_stop_reflow_title": "Stop Reflow?",
        "modal.confirm_stop_reflow_body": "Are you sure you want to stop the reflow process and enter cooldown?",
        "modal.confirm_stop_autotune_title": "Stop Autotune?",
        "modal.confirm_stop_autotune_body": "Are you sure you want to stop the running PID autotune?",
        "modal.no_profile_preheat": "Please select a profile before starting Preheat.",
        "modal.no_profile_title": "No Profile Selected",
        "modal.no_data_export": "No telemetry data recorded yet to export.",
        "modal.no_data_title": "No Data",
        "modal.invalid_profile_data": "Invalid profile data.",
        "modal.invalid_profile_title": "Invalid Profile",
        "modal.incomplete_profile_bottom": "Bottom heater profile must contain at least 1 step.",
        "modal.incomplete_profile_top": "Top heater profile must contain at least 1 step.",
        "modal.incomplete_profile_any": "Profile must contain at least 1 step (Top or Bottom heater).",
        "modal.incomplete_profile_title": "Incomplete Profile",
        "modal.profile_steps_limit_reached": "Maximum of {{max}} steps per heater reached.",
        "modal.profile_steps_limit_title": "Step Limit Reached",
        "modal.profile_steps_exceeded": "Profile exceeds maximum limit of {{max}} steps per heater.",
        "modal.profile_saved": 'Profile "{{name}}" saved successfully!',
        "modal.profile_saved_title": "Profile Saved",
        "modal.save_failed_title": "Save Failed",
        "modal.profile_save_failed": "Failed to save profile to controller.",
        "modal.factory_profile_protected": "The factory default profile cannot be deleted.",
        "modal.protected_profile_title": "Protected Profile",
        "modal.delete_profile_confirm": 'Are you sure you want to delete profile "{{file}}"?',
        "modal.delete_profile_title": "Delete Profile",
        "modal.profile_deleted": 'Profile "{{file}}" deleted successfully.',
        "modal.profile_deleted_title": "Profile Deleted",
        "modal.profile_delete_failed": "Failed to delete profile.",
        "modal.delete_failed_title": "Delete Failed",
        "modal.error_title": "Error",
        "modal.pid_lib_saved": "PID Library saved successfully to controller!",
        "modal.pid_lib_saved_title": "PID Library Saved",
        "modal.pid_lib_save_failed": "Failed to save PID Library to controller.",
        "modal.pid_points_limit_reached": "Maximum of {{max}} PID points per heater reached.",
        "modal.pid_points_limit_title": "Point Limit Reached",
        "modal.pid_points_exceeded": "PID Library exceeds maximum limit of {{max}} points per heater.",
        "modal.empty_pid_points": "PID Library must contain at least 1 point (Top or Bottom heater).",
        "modal.invalid_pid_title": "Invalid PID Library",
        "modal.autotune_temp_required": "Please enter a target temperature between 40°C and 280°C.",
        "modal.autotune_temp_required_title": "Target Temperature Required",
        "modal.autotune_temp_invalid": "Target temperature must be between 40°C and 280°C.",
        "modal.autotune_temp_invalid_title": "Invalid Temperature Range",
        "modal.autotune_confirm": "Start PID Autotune for {{heater}} heater at {{temp}}°C?",
        "modal.autotune_confirm_title": "Start Autotune",
        "modal.autotune_cmd_sent": "Autotune command sent to controller.",
        "modal.autotune_cmd_title": "Autotune",
        "modal.autotune_start_error": "Error starting Autotune.",
        "modal.autotune_start_error_title": "Autotune Error",
        "modal.autotune_complete_body": "New PID point ({{temp}}°C: Kp={{kp}}, Ki={{ki}}, Kd={{kd}}) for {{heater}} heater was automatically saved to the controller's PID Library.\n\nYou can fine-tune these values in the table anytime; if edited manually, click 'Save PID Library' to store your changes.",
        "modal.autotune_complete_title": "Autotune Complete & Saved!",
        "modal.restore_pid_confirm": "Restore PID Library with {{top}} top and {{bottom}} bottom points?",
        "modal.restore_pid_title": "Restore PID Library",
        "modal.restore_settings_confirm": "Restore Machine Settings from selected file?",
        "modal.restore_settings_title": "Restore Settings",
        "modal.import_profile_confirm": 'Import Profile "{{name}}" ({{file}})?',
        "modal.import_profile_title": "Import Profile",
        "modal.restore_zip_confirm": 'Restore {{count}} files from "{{file}}" to the controller?',
        "modal.restore_zip_title": "Full ZIP Restore",
        "modal.btn_restore": "Restore",
        "modal.btn_restore_all": "Restore All",
        "modal.btn_import": "Import",
        "modal.btn_start": "Start",
        "modal.prompt_new_profile": "Enter a name for the new profile:",
        "modal.prompt_new_profile_title": "New Profile",
        "modal.prompt_new_profile_ph": "Custom Profile (max. 30 chars)",
        "modal.profile_name_too_long": "Profile name is too long ({{len}} chars). Maximum allowed is 30 characters.",
        "modal.profile_name_invalid_title": "Invalid Profile Name",
        "modal.profile_name_empty": "Profile name cannot be empty.",
        "modal.btn_ok": "OK",
        "modal.btn_cancel": "Cancel",
        "modal.btn_confirm": "Confirm",
        "modal.btn_delete": "Delete",
        "modal.btn_stop": "Stop",
        "modal.upload_failed_title": "⚠️ Upload Failed",
        "modal.invalid_json_title": "⚠️ Invalid JSON File",
        "modal.invalid_zip_title": "⚠️ Invalid ZIP Archive",
        "modal.restore_failed_title": "Restore Failed",
        "modal.restore_success_title": "Restore Successful",
        "modal.restore_partial_title": "⚠️ Restore Complete (With Warnings)",
        "modal.import_failed_title": "Import Failed",
        "modal.incompatible_schema_title": "⚠️ Incompatible File Version",
        "modal.restore_pid_failed_msg": "Failed to restore PID Library to controller.",
        "modal.restore_settings_failed_msg": "Failed to restore machine settings to controller.",
        "modal.import_profile_failed_msg": "Failed to import profile to controller.",
        "modal.sec_title": "⚠️ Wi-Fi Security Notice",
        "modal.sec_msg": "The controller is running with the default Access Point password ('reflow123'). Please set a secure password or confirm keeping the default.",
        "modal.sec_btn_keep": "Keep Default",
        "modal.sec_btn_change": "Set Password",
        "modal.backup_locked_title": "⚠️ Maintenance & Backup Locked: Process Active",
        "modal.backup_locked_msg": "Reflow process or autotune is currently active: Backup and maintenance functions are only available in IDLE state.",
        "modal.save_locked_active_title": "⚠️ Save Failed: Process Active",
        "modal.save_settings_locked_active": "Failed to save settings: Reflow process or autotune is currently active. Configuration changes are only permitted in IDLE state.",
        "modal.save_profile_locked_active": "Failed to save profile: Reflow process or autotune is currently active. Profile modifications are only permitted in IDLE state.",
        "modal.save_pid_locked_active": "Failed to save PID Library: Reflow process or autotune is currently active. PID modifications are only permitted in IDLE state.",
        "modal.delete_profile_locked_active": "Failed to delete profile: Reflow process or autotune is currently active. Profile deletions are only permitted in IDLE state.",
        "modal.delete_active_profile_title": "⚠️ Delete Running Profile?",
        "modal.delete_active_profile_confirm": "Profile '{{file}}' is currently active in the running reflow process. The current process will finish safely from RAM, but the profile will be deleted from storage and unavailable for future runs. Are you sure you want to delete it?",
        "modal.profile_save_backup_locked": "Profile modification is locked during backup or restore operations.",
        "modal.profile_delete_backup_locked": "Profile deletion is locked during backup or restore operations.",
        "modal.autotune_locked_title": "⚠️ Autotune Locked: Process Active",
        "modal.autotune_locked_msg": "Reflow process or another operation is currently active: Autotune can only be started in IDLE state.",

        "toast.settings_saved": "Machine Settings saved successfully to controller!",
        "toast.settings_error": "Failed to save settings to controller.",
        "toast.profile_saved": "Profile '{{name}}' saved successfully.",
        "toast.profile_deleted": "Profile '{{name}}' deleted.",
        "toast.profile_loaded": "Profile '{{name}}' loaded.",
        "toast.autotune_started": "Autotune started for {{heater}} at {{temp}}°C.",
        "toast.autotune_stopped": "Autotune stopped.",
        "toast.pid_lib_saved": "PID Library saved successfully.",
        "toast.pid_lib_error": "Failed to save PID Library.",
        "toast.wifi_pass_updated": "Wi-Fi password updated successfully! Controller AP will restart with new credentials.",
        "toast.wifi_pass_short": "Password must be at least 8 characters long.",
        "toast.wifi_pass_kept": "Default Wi-Fi password confirmed.",
        "toast.backup_started": "Creating backup...",
        "toast.backup_success": "✓ 1:1 LittleFS ZIP backup downloaded successfully ({{count}} files)!",
        "toast.backup_failed": "Failed to generate ZIP backup.",
        "toast.restore_success": "Restore successful! Reloading system...",
        "toast.restore_error": "Restore failed.",
        "toast.pid_restored": "✓ PID Library restored successfully!",
        "toast.restore_pid_failed": "Failed to restore PID Library.",
        "toast.settings_restored": "✓ Machine Settings restored successfully!",
        "toast.restore_settings_failed": "Failed to restore settings.",
        "toast.profile_imported": "✓ Profile \"{{name}}\" imported successfully!",
        "toast.import_profile_failed": "Failed to import profile.",
        "toast.restore_zip_complete": "✓ Full ZIP Restore Complete: {{count}} files restored successfully!{{skipped}}",
        "toast.restore_zip_failed": "Error restoring from ZIP archive.",

        "backup.title": "Backup & Restore",
        "backup.subtitle": "Generate a complete 1:1 LittleFS flash archive or restore individual system configurations.",
        "backup.system_info_title": "System & Firmware Information",
        "backup.lbl_fw_version": "Firmware Version",
        "backup.lbl_idf_version": "ESP-IDF Framework",
        "backup.lbl_build_time": "Build Date & Time",
        "backup.status_success": "Backup successful!",
        "backup.orb_title": "Click to Download 1:1 Full LittleFS ZIP Backup",
        "backup.orb_text": "BACKUP",
        "backup.orb_hint": "Click the blue orb to dive in and download all LittleFS JSON files as a ZIP archive",
        "backup.session_saved": "Active Instance: System backup verified & ready for maintenance",
        "backup.btn_create_another": "⟲ Download Fresh Backup",
        "backup.btn_back_to_portal": "← Back to Restore Options",
        "backup.card_zip_title": "Full System ZIP Archive",
        "backup.card_zip_desc": "Restores all machine settings, PID libraries, and reflow profiles in a single atomic operation.",
        "backup.btn_upload_zip": "Upload .zip Backup",

        "backup.modular_title": "Modular Component Management",

        "backup.card_settings_title": "Machine Settings",
        "backup.card_settings_desc": "Restores system parameters, safety limits, SSR timings, and hardware defaults.",
        "backup.btn_upload_settings": "Restore settings.json",

        "backup.card_profile_title": "Reflow Profile",
        "backup.card_profile_desc": "Imports or updates a single dual-heater temperature curve profile into flash memory.",
        "backup.btn_upload_profile": "Import Profile .json",

        "backup.card_pid_title": "PID Gain Library",
        "backup.card_pid_desc": "Restores temperature-dependent Kp, Ki, Kd gain scheduling points.",
        "backup.btn_upload_pid": "Restore pid_library.json",

        // OTA Firmware Update
        "ota.title": "Firmware & System Update (OTA)",
        "ota.subtitle": "Upload the single bga_reflow_controller.bin file to update firmware and web interface in 1 click.",
        "ota.drop_main": "Choose Firmware Binary (.bin)",
        "ota.drop_sub": "or drag & drop file here",
        "ota.btn_flash": "Flash Firmware",
        "ota.btn_cancel": "Cancel",
        "ota.status_uploading": "Uploading firmware...",
        "ota.status_flashing": "Flashing OTA partition...",
        "ota.status_rebooting": "Update successful! Rebooting controller...",
        "ota.warning_power": "⚠️ Do not turn off power or disconnect Wi-Fi while flashing!",
        "ota.confirm_title": "⚡ Confirm Firmware Update",
        "ota.confirm_msg": "Are you sure you want to flash \"{{file}}\" ({{size}})? The controller will automatically restart after flashing.",
        "ota.success_title": "🎉 Firmware Update Complete",
        "ota.success_msg": "The new firmware was flashed successfully and the controller is back online!",
        "ota.reconnect_msg": "Controller is rebooting... Reconnecting in {{sec}}s...",
        "ota.error_invalid_file": "Please select a valid .bin firmware binary file.",
        "ota.error_locked": "Firmware update locked: A reflow process or autotune is currently active. Please return to IDLE state first.",

        "modal.schema_downgrade_title": "⚠️ Incompatible Configuration Data Detected (Downgrade)",
        "modal.schema_downgrade_msg": "Configuration files from a newer firmware version were found on the controller. For safety reasons, these were not loaded and the controller is currently running with safe factory defaults.\n\nOptions:\n• Update back to the newer firmware to keep using your existing configuration.\n• Or restore your existing files (settings.json, profiles, etc.) from your backup to ensure a clean migration.\n• Or save your settings in the respective tab to overwrite them with the current defaults.",

        // Validation Errors
        "val.no_files_backup": "No files found to backup from controller.",
        "val.empty_zip": "ZIP archive is empty or invalid.",
        "val.invalid_json_syntax": "Invalid JSON syntax in file.",
        "val.file_too_large": "File exceeds maximum size limit ({{size}} KB).",
        "val.invalid_json": "Invalid JSON syntax: Unable to parse file.",
        "val.mismatched_settings": "Invalid file: File contains profile or PID data instead of machine settings.",
        "val.mismatched_profile": "Invalid file: File contains machine settings or PID library instead of a reflow profile.",
        "val.mismatched_pid": "Invalid file: File contains machine settings or profile data instead of a PID library.",
        "val.invalid_temp_bounds": "Invalid temperature values: Must be between {{min}}°C and {{max}}°C.",
        "val.empty_profile_steps": "Profile curves must contain at least one step for Top and Bottom heaters.",
        "val.empty_pid_points": "PID library must contain at least one valid PID point.",
        "val.incompatible_schema": "Incompatible file: Schema version (v{{ver}}) is newer than supported controller firmware (v{{cur}}). Please update firmware first.",
        "val.zip_no_valid_files": "No valid controller configuration files found inside ZIP archive."
    };

    // ── 2. TRANSLATION ENGINE (Custom lightweight t() with interpolation & fallback) ──
    /**
     * Translates key with optional default text and placeholder interpolation (e.g. {{temp}}, {{heater}}).
     * English is the native fallback if key is missing or language is 'en'.
     */
    function t(key, defaultOrVars, maybeVars) {
        let defaultText = DEFAULT_STRINGS[key] !== undefined ? DEFAULT_STRINGS[key] : key;
        let vars = {};

        if (typeof defaultOrVars === 'string') {
            defaultText = defaultOrVars;
            if (typeof maybeVars === 'object' && maybeVars !== null) {
                vars = maybeVars;
            }
        } else if (typeof defaultOrVars === 'object' && defaultOrVars !== null) {
            vars = defaultOrVars;
        }

        const text = (window.LANG && window.LANG[key] !== undefined) ? window.LANG[key] : defaultText;
        if (typeof text !== 'string') return String(defaultText);
        return text.replace(/\{\{(\w+)\}\}/g, (_, k) => (vars[k] !== undefined ? vars[k] : _));
    }

    /**
     * Applies translations to all tagged DOM elements.
     * Caches native English default text on first pass for zero-data loss switching.
     */
    function applyTranslations() {
        const lang = store.currentLanguage || 'en';

        // 1. Text elements
        document.querySelectorAll('[data-lang]').forEach(el => {
            const key = el.getAttribute('data-lang');
            if (!el.dataset.langDefault) {
                el.dataset.langDefault = el.textContent.trim();
            }
            el.textContent = (lang !== 'en' && window.LANG?.[key]) ? window.LANG[key] : el.dataset.langDefault;
        });

        // 2. Input placeholders
        document.querySelectorAll('[data-lang-placeholder]').forEach(el => {
            const key = el.getAttribute('data-lang-placeholder');
            if (!el.dataset.langPlaceholderDefault) {
                el.dataset.langPlaceholderDefault = el.placeholder;
            }
            el.placeholder = (lang !== 'en' && window.LANG?.[key]) ? window.LANG[key] : el.dataset.langPlaceholderDefault;
        });

        // 3. Titles & Tooltips
        document.querySelectorAll('[data-lang-title]').forEach(el => {
            const key = el.getAttribute('data-lang-title');
            if (!el.dataset.langTitleDefault) {
                el.dataset.langTitleDefault = el.title;
            }
            el.title = (lang !== 'en' && window.LANG?.[key]) ? window.LANG[key] : el.dataset.langTitleDefault;
        });

        // 4. Update dynamic status badge text and title
        const badge = $('state-badge');
        if (badge) {
            badge.title = t('status.badge_title', 'Click for System-Log');
            if (!store.wsConnected) {
                badge.innerText = t('status.disconnected', 'DISCONNECTED');
            } else if (store.stateStr) {
                updateStatusBadge(store.stateEnum, store.stateStr);
            }
        }

        // 5. Update dynamic actor icons titles (Worklight / Cooling Fan)
        updateActorIcons();

        // 6. Update Elapsed Time string
        const elapsedEl = $('elapsed-time');
        if (elapsedEl) {
            elapsedEl.textContent = `${t('hud.runtime_prefix', 'Runtime:')} ${formatTime(store.elapsedSec)} min`;
        }

        // 7. Refresh PID tables & Profile tables if data exists
        if (pidLibData && (pidLibData.top || pidLibData.bottom)) {
            renderPidTable('table-pid-top', pidLibData.top, true);
            renderPidTable('table-pid-bottom', pidLibData.bottom, false);
        }
        if (currentProfileData) {
            renderProfileTable('table-top', currentProfileData.stepsTop, true);
            renderProfileTable('table-bottom', currentProfileData.stepsBottom, false);
        } else {
            renderProfileTable('table-top', [], true);
            renderProfileTable('table-bottom', [], false);
        }

        // 8. Refresh dropdown placeholders
        const chartSel = $('chart-profile-select');

        // 9. Refresh PID Library disabled banner
        updatePidLibraryDisabledBanner();
        if (chartSel && chartSel.options && chartSel.options.length > 0 && chartSel.options[0].value === '') {
            chartSel.options[0].textContent = t('hud.select_profile_placeholder', '— Select Profile —');
        }

        const profSel = $('profile-select');
        if (profSel && profSel.options && profSel.options.length > 0 && profSel.options[0].value === '') {
            profSel.options[0].textContent = t('profile.select_placeholder', '— Select Profile to Edit —');
        }

        const defaultProfSel = $('set-default-profile');
        if (defaultProfSel && defaultProfSel.options && defaultProfSel.options.length > 0 && defaultProfSel.options[0].value === '') {
            defaultProfSel.options[0].textContent = t('settings.opt_no_default_profile', '— Not Selected (Default) —');
        }

        // 9. Refresh Chart labels, axis titles and re-render
        if (chartInstance) {
            if (chartInstance.options?.scales?.x?.title) {
                chartInstance.options.scales.x.title.text = t('chart.axis_runtime', 'Runtime (min)');
            }
            if (chartInstance.options?.scales?.y?.title) {
                chartInstance.options.scales.y.title.text = t('chart.axis_temperature', 'Temperature (°C)');
            }
            if (chartInstance.data?.datasets) {
                if (chartInstance.data.datasets[0]) chartInstance.data.datasets[0].label = t('chart.ds_top_pv', 'Top PV');
                if (chartInstance.data.datasets[1]) chartInstance.data.datasets[1].label = t('chart.ds_top_sp', 'Top SP');
                if (chartInstance.data.datasets[2]) chartInstance.data.datasets[2].label = t('chart.ds_bot_pv', 'Bottom PV');
                if (chartInstance.data.datasets[3]) chartInstance.data.datasets[3].label = t('chart.ds_bot_sp', 'Bottom SP');
            }
            chartInstance.update();
        }
    }

    /**
     * Switches active language, dynamically loads /lang/de.js from LittleFS if needed.
     */
    function setLanguage(lang) {
        store.currentLanguage = lang;
        localStorage.setItem('reflow_lang', lang);

        const select = $('set-lang');
        if (select) select.value = lang;

        if (lang === 'en') {
            window.LANG = null;
            applyTranslations();
            return;
        }

        const existing = document.getElementById('lang-bundle');
        if (existing) existing.remove();

        const script = document.createElement('script');
        script.id = 'lang-bundle';
        script.src = `/lang/${lang}.js`;
        script.onload = () => {
            console.log(`[Language] Pack '${lang}.js' loaded successfully.`);
            applyTranslations();
        };
        script.onerror = () => {
            console.warn(`[Language] Could not load '/lang/${lang}.js', falling back to English.`);
            window.LANG = null;
            applyTranslations();
        };
        document.head.appendChild(script);
    }

    function initLanguage() {
        const saved = localStorage.getItem('reflow_lang') || 'en';
        setLanguage(saved);

        const select = $('set-lang');
        if (select) {
            select.value = saved;
            // Note: Language is applied and persisted when 'Save Settings' is clicked
        }
    }

    // ── 3. DOM UTILITIES ────────────────────────────────────────────────────
    const $ = (id) => document.getElementById(id);

    function setText(id, text) {
        const el = $(id);
        if (el) el.innerText = text;
    }

    function formatTime(seconds) {
        const s = Math.max(0, Math.floor(seconds));
        const m = Math.floor(s / 60);
        const remS = s % 60;
        return `${m.toString().padStart(2, '0')}:${remS.toString().padStart(2, '0')}`;
    }

    // ── 3. MODAL SYSTEM (replaces native alert/confirm) ─────────────────────
    const Modal = (() => {
        const ICONS = {
            info: { cls: 'icon-info', char: 'ℹ' },
            success: { cls: 'icon-success', char: '✓' },
            warning: { cls: 'icon-warning', char: '⚠' },
            danger: { cls: 'icon-danger', char: '✕' }
        };
        const TITLES = {
            info: 'Info', success: 'Success', warning: 'Warning', danger: 'Error'
        };

        let _resolve = null;

        function _show({ type = 'info', title, body, buttons, withInput = false, inputPlaceholder = '', inputType = 'text', inputValue = '', maxLength = null }) {
            const overlay = document.getElementById('modal-overlay');
            const iconEl = document.getElementById('modal-icon');
            const titleEl = document.getElementById('modal-title');
            const bodyEl = document.getElementById('modal-body');
            const inputEl = document.getElementById('modal-input');
            const actionsEl = document.getElementById('modal-actions');
            if (!overlay) return Promise.resolve(null);

            const ic = ICONS[type] || ICONS.info;
            iconEl.className = `modal-icon ${ic.cls}`;
            iconEl.textContent = ic.char;
            titleEl.textContent = title || TITLES[type] || 'Info';
            bodyEl.textContent = body;

            if (withInput) {
                inputEl.value = inputValue || '';
                inputEl.placeholder = inputPlaceholder || '';
                inputEl.type = inputType || 'text';
                if (maxLength) {
                    inputEl.maxLength = maxLength;
                } else {
                    inputEl.removeAttribute('maxlength');
                }
                inputEl.classList.remove('u-hidden');
                setTimeout(() => inputEl.focus(), 50);
            } else {
                inputEl.classList.add('u-hidden');
            }

            actionsEl.innerHTML = '';
            buttons.forEach(btn => {
                const el = document.createElement('button');
                el.type = 'button';
                el.className = `btn ${btn.cls || 'btn-outline'}`;
                el.textContent = btn.label;
                el.onclick = () => {
                    _close();
                    if (_resolve) {
                        _resolve(btn.value !== undefined ? btn.value : (withInput ? inputEl.value : true));
                        _resolve = null;
                    }
                };
                actionsEl.appendChild(el);
            });

            overlay.classList.add('active');

            return new Promise(resolve => { _resolve = resolve; });
        }

        function _close() {
            const overlay = document.getElementById('modal-overlay');
            if (overlay) overlay.classList.remove('active');
        }

        // Strict modal mode: Backdrop clicks are ignored.
        // Modals can only be closed by explicitly clicking one of the modal action buttons (OK, Cancel, Confirm, etc.).

        return {
            alert(body, { type = 'info', title } = {}) {
                return _show({
                    type, title, body,
                    buttons: [{ label: t('modal.btn_ok', 'OK'), cls: 'btn-primary', value: true }]
                });
            },
            confirm(body, { type = 'warning', title, confirmLabel, cancelLabel } = {}) {
                return _show({
                    type, title, body,
                    buttons: [
                        { label: cancelLabel || t('modal.btn_cancel', 'Cancel'), cls: 'btn-outline', value: false },
                        { label: confirmLabel || t('modal.btn_confirm', 'Confirm'), cls: type === 'danger' ? 'btn-danger' : 'btn-primary', value: true }
                    ]
                });
            },
            prompt(body, { type = 'info', title, inputPlaceholder = '', inputType = 'text', inputValue = '', maxLength = null, createLabel, cancelLabel } = {}) {
                return _show({
                    type, title, body, withInput: true, inputPlaceholder, inputType, inputValue, maxLength,
                    buttons: [
                        { label: cancelLabel || t('modal.btn_cancel', 'Cancel'), cls: 'btn-outline', value: null },
                        { label: createLabel || t('profile.btn_new', 'Create'), cls: 'btn-primary' }
                    ]
                });
            },
            _prompt(body, opts) {
                return this.prompt(body, opts);
            },
            custom(opts) {
                return _show(opts);
            }
        };
    })();

    // ── 3. REST API CLIENT ──────────────────────────────────────────────────
    async function apiRequest(endpoint, method = 'GET', data = null) {
        const options = {
            method: method,
            headers: {}
        };
        if (data && (method === 'POST' || method === 'PUT' || method === 'DELETE')) {
            options.headers['Content-Type'] = 'application/json';
            options.body = JSON.stringify(data);
        }

        try {
            const res = await fetch(endpoint, options);
            if (!res.ok) {
                console.warn(`[API] ${method} ${endpoint} HTTP ${res.status}`);
                let errDetail = `HTTP ${res.status}`;
                try {
                    const errObj = await res.json();
                    if (errObj && errObj.error) errDetail = errObj.error;
                } catch (_) {
                    try {
                        const errText = await res.text();
                        if (errText) errDetail = errText;
                    } catch (_) {}
                }
                return { success: false, error: errDetail, status: res.status };
            }
            const contentType = res.headers.get('content-type');
            if (contentType && contentType.includes('application/json')) {
                return await res.json();
            }
            return await res.text();
        } catch (err) {
            console.error(`[API Error] ${method} ${endpoint}:`, err);
            return { success: false, error: err.message || 'Network error', status: 0 };
        }
    }

    // ── 4. STATUS LOG DROPDOWN ──────────────────────────────────────────────
    function toggleStatusLog() {
        const dropdown = $('status-log-dropdown');
        if (!dropdown) return;
        store.statusLogOpen = !store.statusLogOpen;
        dropdown.classList.toggle('active', store.statusLogOpen);
    }

    function updateLogs(logs) {
        const list = $('status-log-list');
        if (!list) return;

        // 1. Connection check
        if (!store.wsConnected) {
            list.innerHTML = `<div class="status-log-item status-log-item--error">${t('status.log_disconnected', '✕ Controller disconnected. Waiting for connection...')}</div>`;
            return;
        }

        // 2. Unknown State check
        if (store.stateEnum === -1) {
            list.innerHTML = `<div class="status-log-item status-log-item--warn">${t('status.log_unknown_state', '⚠ Unknown controller state. Waiting for valid telemetry...')}</div>`;
            return;
        }

        if (!logs || !Array.isArray(logs) || logs.length === 0) {
            list.innerHTML = `<div class="status-log-item status-log-item--ok">${t('status.log_all_ok', '✓ All systems operational (No faults recorded)')}</div>`;
            return;
        }

        // 3. Filter noise (webserver internal request logs, HTTP handshakes, static file delivery, network socket lifecycle)
        const filtered = logs.filter(line => {
            if (!line || typeof line !== 'string') return false;
            const lower = line.toLowerCase();
            if (lower.includes('handshake done') ||
                lower.includes('httpd') ||
                lower.includes('static file') ||
                lower.includes('serving file') ||
                lower.includes('400 bad request') ||
                lower.includes('wifi:<ba-add>') ||
                lower.includes('wifi:') ||
                lower.includes('session') ||
                lower.includes('websocket client') ||
                lower.includes('new websocket') ||
                lower.includes('max websocket clients')) {
                return false;
            }
            return true;
        });

        // 4. If no meaningful logs remain
        if (filtered.length === 0) {
            list.innerHTML = `<div class="status-log-item status-log-item--ok">${t('status.log_all_ok', '✓ All systems operational (No faults recorded)')}</div>`;
            return;
        }

        // Check if there are any warnings or errors
        const hasErrorsOrWarnings = filtered.some(line =>
            line.startsWith('E ') || line.startsWith('W ') ||
            line.includes('ERROR') || line.includes('CRITICAL') || line.includes('FAULT') || line.includes('WARN')
        );

        list.innerHTML = '';

        // If no faults recorded in history and not in FAULT state, show the clean operational banner
        if (!hasErrorsOrWarnings && store.stateEnum !== 6) {
            const okItem = document.createElement('div');
            okItem.className = 'status-log-item status-log-item--ok';
            okItem.innerText = t('status.log_all_ok', '✓ All systems operational (No faults recorded)');
            list.appendChild(okItem);
        }

        // Render events in reverse chronological order (newest first, max 10 to keep dropdown clean)
        const maxDisplay = Math.min(filtered.length, 10);
        for (let i = filtered.length - 1; i >= filtered.length - maxDisplay; --i) {
            const line = filtered[i];
            const item = document.createElement('div');
            item.className = 'status-log-item';

            if (line.startsWith('E ') || line.includes('CRITICAL') || line.includes('ERROR') || line.includes('FAULT')) {
                item.className += ' status-log-item--error';
            } else if (line.startsWith('W ') || line.includes('WARN') || line.includes('timeout')) {
                item.className += ' status-log-item--warn';
            } else {
                item.className += ' status-log-item--info';
            }
            item.innerText = line;
            list.appendChild(item);
        }
    }

    // ── 5. THEME & DISPLAY PREFERENCES ─────────────────────────────────────
    async function initTheme() {
        const savedTheme = localStorage.getItem('reflow_theme') || 'light';
        const savedZones = localStorage.getItem('reflow_show_zones');
        const savedTal = localStorage.getItem('reflow_show_tal');
        const savedMarkers = localStorage.getItem('reflow_show_markers');
        const savedPidGains = localStorage.getItem('reflow_show_pid_gains');

        if (savedZones !== null) store.showZones = (savedZones === 'true');
        if (savedTal !== null) store.showTalLine = (savedTal === 'true');
        if (savedMarkers !== null) store.showStepMarkers = (savedMarkers === 'true');
        if (savedPidGains !== null) store.showPidGains = (savedPidGains === 'true');

        setTheme(savedTheme);
        updateCheckboxStates();

        // Sync with ESP32 NVS settings in background
        try {
            const data = await apiRequest('/api/theme', 'GET');
            if (data) {
                if (data.theme && data.theme !== store.currentTheme) {
                    setTheme(data.theme);
                }
                if (data.showZones !== undefined) {
                    store.showZones = !!data.showZones;
                }
                if (data.showTalLine !== undefined) {
                    store.showTalLine = !!data.showTalLine;
                }
                if (data.showStepMarkers !== undefined) {
                    store.showStepMarkers = !!data.showStepMarkers;
                }
                if (data.showPidGains !== undefined) {
                    store.showPidGains = !!data.showPidGains;
                }
                updateCheckboxStates();
                if (chartInstance) chartInstance.update('none');
            }
        } catch (e) {
            console.warn('[Theme/Prefs] Failed to sync preferences with controller:', e);
        }
    }

    function updateCheckboxStates() {
        const chkZones = $('chk-zones');
        if (chkZones) chkZones.checked = store.showZones;
        const chkTal = $('chk-tal');
        if (chkTal) chkTal.checked = store.showTalLine;
        const chkMarkers = $('chk-markers');
        if (chkMarkers) chkMarkers.checked = store.showStepMarkers;
        const chkPid = $('chk-pid-gains');
        if (chkPid) chkPid.checked = store.showPidGains;

        const topTag = $('top-pid-tag');
        const botTag = $('bottom-pid-tag');
        if (topTag) topTag.classList.toggle('u-hidden', !store.showPidGains);
        if (botTag) botTag.classList.toggle('u-hidden', !store.showPidGains);
    }

    function setTheme(theme) {
        store.currentTheme = theme;
        document.documentElement.setAttribute('data-theme', theme);
        localStorage.setItem('reflow_theme', theme);

        const btn = $('theme-toggle');
        if (btn) {
            btn.setAttribute('title', theme === 'dark' ? 'Switch to Light Mode' : 'Switch to Dark Mode');
        }

        if (chartInstance) {
            updateChartTheme(theme);
        }
    }

    function toggleTheme() {
        const next = store.currentTheme === 'dark' ? 'light' : 'dark';
        setTheme(next);
        apiRequest('/api/theme', 'POST', { theme: next });
    }

    // ── 6. NAVIGATION & TAB SWITCHING ───────────────────────────────────────
    function initTabs() {
        const tabs = [
            { btnId: 'nav-tab-chart', panelId: 'panel-chart', name: 'chart' },
            { btnId: 'nav-tab-profile', panelId: 'panel-profile', name: 'profile' },
            { btnId: 'nav-tab-settings', panelId: 'panel-settings', name: 'settings' },
            { btnId: 'nav-tab-tunepid', panelId: 'panel-tunepid', name: 'tunepid' },
            { btnId: 'nav-tab-backup', panelId: 'panel-backup', name: 'backup' }
        ];

        tabs.forEach(tab => {
            const btn = $(tab.btnId);
            if (btn) {
                btn.addEventListener('click', (e) => {
                    e.preventDefault();
                    switchTab(tab.name);
                });
            }
        });

        // Restore last active tab across browser reloads
        const savedTab = sessionStorage.getItem('reflow_active_tab');
        if (savedTab && tabs.some(t => t.name === savedTab)) {
            switchTab(savedTab);
        }
    }

    async function switchTab(tabName) {
        // State safety check for backup tab
        if (tabName === 'backup') {
            const isAllowedState = (store.stateEnum === 0 || store.stateEnum === 5 || store.stateEnum === 8);
            if (!isAllowedState) {
                await Modal.alert(
                    t('modal.backup_locked_msg', 'Reflow process or autotune is currently active: Backup and maintenance functions are only available in IDLE state.'),
                    {
                        type: 'warning',
                        title: t('modal.backup_locked_title', '⚠️ Maintenance & Backup Locked: Process Active')
                    }
                );
                return;
            }
        }

        const previousTab = store.currentTab;
        store.currentTab = tabName;
        sessionStorage.setItem('reflow_active_tab', tabName);

        const tabs = ['chart', 'profile', 'settings', 'tunepid', 'backup'];
        tabs.forEach(name => {
            const btn = $(`nav-tab-${name}`);
            const panel = $(`panel-${name}`);
            const isActive = (name === tabName);

            if (btn) {
                btn.classList.toggle('active', isActive);
                btn.setAttribute('aria-selected', isActive ? 'true' : 'false');
            }
            if (panel) {
                panel.classList.toggle('active', isActive);
            }
        });

        // FSM BACKUP State transition safety
        if (tabName === 'backup' && previousTab !== 'backup') {
            try {
                await apiRequest('/api/control', 'POST', { action: 'enterBackup' });
            } catch (err) {
                console.warn('[ReflowApp] Failed to enter backup state:', err);
            }
            await syncStatusWithController();
        } else if (previousTab === 'backup' && tabName !== 'backup') {
            try {
                await apiRequest('/api/control', 'POST', { action: 'exitBackup' });
            } catch (err) {
                console.warn('[ReflowApp] Failed to exit backup state:', err);
            }
        }

        // Trigger chart resize / re-render safely after DOM reflow when switching back to chart tab
        if (tabName === 'chart' && chartInstance) {
            requestAnimationFrame(() => {
                if (chartInstance && store.currentTab === 'chart') {
                    chartInstance.resize();
                    chartInstance.update('none');
                }
            });
        } else if (tabName === 'settings') {
            loadSettingsFromController();
        } else if (tabName === 'profile') {
            loadProfileList(null, true);
        } else if (tabName === 'tunepid') {
            loadPidLibraryFromController();
            updatePidPreview();
            updatePidLibraryDisabledBanner();
        }
    }

    // ── 7. STATUS BADGE (Driven 100% by Backend FSM) ────────────────────────
    function updateStatusBadge(stateEnum, stateStr, preheatDone) {
        const badge = $('state-badge');
        if (!badge) return;

        if (stateEnum === undefined || stateEnum === null || isNaN(Number(stateEnum)) || Number(stateEnum) < 0 || Number(stateEnum) > 8) {
            store.stateEnum = -1;
            store.stateStr = STRINGS.STATE_UNKNOWN;
            badge.innerText = t('state.unknown', STRINGS.STATE_UNKNOWN);
            badge.className = 'status-badge status-unknown';
            return;
        }

        store.stateEnum = Number(stateEnum);
        store.stateStr = stateStr || STRINGS.STATE_IDLE;

        const localizedState = t(`state.${store.stateEnum}`, store.stateStr);
        badge.innerText = localizedState.toUpperCase();
        badge.className = 'status-badge';

        // 0=IDLE -> status-idle
        // 1=PREHEAT -> status-preheat
        // 2=SOAK, 3=REFLOW, 7=AUTOTUNE, 8=BACKUP -> status-active
        // 4=COOLING, 5=DONE -> status-cooldown
        // 6=FAULT -> status-fault
        switch (store.stateEnum) {
            case 0: badge.classList.add('status-idle'); break;
            case 1: badge.classList.add('status-preheat'); break;
            case 2:
            case 3:
            case 7:
            case 8: badge.classList.add('status-active'); break;
            case 4:
            case 5: badge.classList.add('status-cooldown'); break;
            case 6: badge.classList.add('status-fault'); break;
            default: badge.classList.add('status-unknown'); break;
        }
        // Preheat Done blink overlay
        if (preheatDone && store.stateEnum === 1) {
            badge.classList.remove('status-preheat');
            badge.classList.add('status-preheat-done');
        }
    }

    // ── 8. ACTOR OVERRIDES (LAMP & FAN) ─────────────────────────────────────
    function updateActorIcons() {
        const lampBtn = $('icon-lamp');
        if (lampBtn) {
            lampBtn.classList.toggle('icon-lamp-on', store.lampState);
            lampBtn.classList.toggle('icon-off', !store.lampState);
            lampBtn.classList.toggle('active', store.lampState);
            lampBtn.setAttribute('title', store.lampState ? t('ctrl.lamp_on', 'Worklight (ON)') : t('ctrl.lamp_off', 'Worklight (OFF)'));
        }

        const fanBtn = $('icon-fan');
        if (fanBtn) {
            fanBtn.classList.toggle('icon-fan-on', store.fanState);
            fanBtn.classList.toggle('icon-off', !store.fanState);
            fanBtn.classList.toggle('active', store.fanState);
            fanBtn.setAttribute('title', store.fanState ? t('ctrl.fan_on', 'Cooling Fan (ON)') : t('ctrl.fan_off', 'Cooling Fan (OFF)'));

            const rotor = fanBtn.querySelector('.fan-rotor');
            if (rotor) {
                rotor.classList.toggle('fan-spin', store.fanState);
            }
        }
    }

    function toggleLamp() {
        const next = !store.lampState;
        store.lampState = next;
        updateActorIcons();
        apiRequest('/api/overrides', 'POST', { lamp: next });
    }

    function toggleFan() {
        const next = !store.fanState;
        store.fanState = next;
        updateActorIcons();
        apiRequest('/api/overrides', 'POST', { fan: next });
    }

    // ── 9. PROCESS CONTROLS & SAFETY INTERLOCK ─────────────────────────────
    function updateControlButtonsState() {
        const isLive = Boolean(store.wsConnected && store.lastTelemetryTime > 0 && (Date.now() - store.lastTelemetryTime < 3500));
        const btnStart = $('btn-start');
        const btnPreheat = $('btn-preheat');
        const btnStartTune = $('btn-start-autotune');

        if (!isLive) {
            const tooltipDis = t('ctrl.tooltip_disconnected', '⚠️ Telemetry disconnected - starting a process is locked until connection is restored.');
            if (btnStart) btnStart.setAttribute('title', tooltipDis);
            if (btnPreheat) btnPreheat.setAttribute('title', tooltipDis);
            if (btnStartTune) btnStartTune.setAttribute('title', tooltipDis);
        } else {
            if (btnStart) {
                if (store.stateEnum === 0 || store.stateEnum === 5 || store.stateEnum === 6) {
                    btnStart.setAttribute('title', t('ctrl.start_idle_hint', 'Step 2: Starts Reflow after preheating is complete'));
                } else if (store.stateEnum === 1) {
                    btnStart.setAttribute('title', t('ctrl.start_preheat_hint', 'Advance from Preheat to Reflow'));
                } else {
                    btnStart.removeAttribute('title');
                }
            }
            if (btnPreheat) btnPreheat.removeAttribute('title');
            if (btnStartTune) btnStartTune.removeAttribute('title');
        }
    }

    async function startPreheat() {
        if (!store.wsConnected || (Date.now() - store.lastTelemetryTime > 3500)) {
            await Modal.alert(t('modal.ws_disconnected_msg', 'Live telemetry stream is disconnected. Starting Reflow, Preheat, or Autotune is locked for safety until reconnection.'), {
                type: 'warning',
                title: t('modal.ws_disconnected_title', 'Safety Interlock Active')
            });
            return;
        }
        const profName = $('chart-profile-select')?.value || store.activeProfileFile;
        if (!profName) {
            await Modal.alert(t('modal.no_profile_preheat', 'Please select a profile before starting Preheat.'), {
                type: 'warning',
                title: t('modal.no_profile_title', 'No Profile Selected')
            });
            return;
        }
        const res = await apiRequest('/api/control', 'POST', { action: 'preheat', profile: profName });
        if (res && res.success) {
            console.log(`[FSM] Preheat started with ${profName}`);
        }
    }

    async function startReflow() {
        if (!store.wsConnected || (Date.now() - store.lastTelemetryTime > 3500)) {
            await Modal.alert(t('modal.ws_disconnected_msg', 'Live telemetry stream is disconnected. Starting Reflow, Preheat, or Autotune is locked for safety until reconnection.'), {
                type: 'warning',
                title: t('modal.ws_disconnected_title', 'Safety Interlock Active')
            });
            return;
        }

        // In IDLE state (0, 5, 6): Guide the user to start Preheat first with a 1-click action
        if (store.stateEnum === 0 || store.stateEnum === 5 || store.stateEnum === 6) {
            const profName = $('chart-profile-select')?.value || store.activeProfileFile;
            if (!profName) {
                await Modal.alert(t('modal.no_profile_preheat', 'Please select a profile before starting Preheat.'), {
                    type: 'warning',
                    title: t('modal.no_profile_title', 'No Profile Selected')
                });
                return;
            }
            const cleanProf = profName.replace(/\.json$/i, '');
            const ok = await Modal.confirm(
                t('modal.preheat_required_body', 'A BGA reflow cycle begins with preheating the PCB (bottom heater).\n\nWould you like to start Preheat now with profile "{{profile}}"?', { profile: cleanProf }),
                {
                    type: 'info',
                    title: t('modal.preheat_required_title', 'Preheat Required'),
                    confirmLabel: t('modal.btn_start_preheat', 'Start Preheat'),
                    cancelLabel: t('modal.btn_cancel', 'Cancel')
                }
            );
            if (ok) {
                await startPreheat();
            }
            return;
        }

        const res = await apiRequest('/api/control', 'POST', { action: 'reflow' });
        if (res && res.success) {
            console.log('[FSM] Reflow started.');
        }
    }

    async function stopReflow() {
        const res = await apiRequest('/api/control', 'POST', { action: 'stop' });
        if (res && res.success) {
            console.log('[FSM] Process stopped.');
        }
    }

    async function skipStep() {
        const res = await apiRequest('/api/control', 'POST', { action: 'skip' });
        if (res && res.success) {
            console.log('[FSM] Step skipped.');
        }
    }

    // ── 10. CHART.JS LIVE DIAGRAMM & PLUGINS ────────────────────────────────
    let chartInstance = null;
    let cachedStepMarkers = [];

    // Custom Chart.js Plugin: Process Zones, TAL Line & Step-Markers
    const reflowChartPlugin = {
        id: 'reflowChartPlugin',
        beforeDraw: (chart) => {
            const { ctx, chartArea, scales } = chart;
            if (!chartArea || !scales.x || !scales.y) return;

            const isDark = store.currentTheme === 'dark';

            // 1. Draw Process Target Temperature Zones (Horizontal Bands)
            if (store.showZones) {
                const zones = [
                    {
                        label: t('chart.zone_preheat', 'PREHEAT (100-150°C)'),
                        min: 100, max: 150,
                        color: isDark ? 'rgba(34, 197, 94, 0.05)' : 'rgba(34, 197, 94, 0.06)',
                        borderColor: isDark ? 'rgba(34, 197, 94, 0.15)' : 'rgba(34, 197, 94, 0.2)'
                    },
                    {
                        label: t('chart.zone_soak', 'SOAK (150-190°C)'),
                        min: 150, max: 190,
                        color: isDark ? 'rgba(234, 179, 8, 0.05)' : 'rgba(234, 179, 8, 0.06)',
                        borderColor: isDark ? 'rgba(234, 179, 8, 0.15)' : 'rgba(234, 179, 8, 0.2)'
                    },
                    {
                        label: t('chart.zone_prereflow', 'PRE-REFLOW (190-217°C)'),
                        min: 190, max: 217,
                        color: isDark ? 'rgba(249, 115, 22, 0.05)' : 'rgba(249, 115, 22, 0.06)',
                        borderColor: isDark ? 'rgba(249, 115, 22, 0.15)' : 'rgba(249, 115, 22, 0.2)'
                    },
                    {
                        label: t('chart.zone_reflow', 'REFLOW (217-245°C)'),
                        min: 217, max: 255,
                        color: isDark ? 'rgba(239, 68, 68, 0.06)' : 'rgba(239, 68, 68, 0.07)',
                        borderColor: isDark ? 'rgba(239, 68, 68, 0.2)' : 'rgba(239, 68, 68, 0.25)'
                    }
                ];

                ctx.save();
                zones.forEach(zone => {
                    const yTop = scales.y.getPixelForValue(zone.max);
                    const yBottom = scales.y.getPixelForValue(zone.min);
                    const height = yBottom - yTop;

                    // Fill zone
                    ctx.fillStyle = zone.color;
                    ctx.fillRect(chartArea.left, yTop, chartArea.width, height);

                    // Top dashed border
                    ctx.strokeStyle = zone.borderColor;
                    ctx.lineWidth = 1;
                    ctx.setLineDash([4, 4]);
                    ctx.beginPath();
                    ctx.moveTo(chartArea.left, yTop);
                    ctx.lineTo(chartArea.right, yTop);
                    ctx.stroke();

                    // Zone text label positioned cleanly to the left of the top-right export buttons
                    ctx.setLineDash([]);
                    ctx.fillStyle = isDark ? 'rgba(255, 255, 255, 0.50)' : 'rgba(15, 23, 42, 0.50)';
                    ctx.font = '9px monospace';
                    ctx.textAlign = 'right';
                    ctx.textBaseline = 'top';
                    ctx.fillText(zone.label, chartArea.right - 8, yTop + 4);
                });
                ctx.restore();
            }

            // 2. Separate TAL Line (217°C Liquidus Threshold) - Light red, dashed
            if (store.showTalLine) {
                const talY = scales.y.getPixelForValue(217.0);
                if (talY >= chartArea.top && talY <= chartArea.bottom) {
                    ctx.save();
                    // Light red dashed line
                    ctx.strokeStyle = isDark ? 'rgba(248, 113, 113, 0.65)' : 'rgba(239, 68, 68, 0.7)';
                    ctx.lineWidth = 1.4;
                    ctx.setLineDash([6, 4]);
                    ctx.beginPath();
                    ctx.moveTo(chartArea.left, talY);
                    ctx.lineTo(chartArea.right, talY);
                    ctx.stroke();

                    // Label: TAL (>217°C) positioned right above the line
                    ctx.setLineDash([]);
                    ctx.fillStyle = isDark ? '#f87171' : '#dc2626';
                    ctx.font = 'bold 9px monospace';
                    ctx.textAlign = 'left';
                    ctx.textBaseline = 'bottom';
                    ctx.fillText('TAL (>217°C)', chartArea.left + 8, talY - 2);
                    ctx.restore();
                }
            }
        },
        afterDatasetsDraw: (chart) => {
            if (!store.showStepMarkers) return;

            const { ctx, chartArea, scales } = chart;
            if (!ctx || !chartArea || !scales?.x || !scales?.y || !cachedStepMarkers.length) return;

            ctx.save();
            const light = document.documentElement.getAttribute('data-theme') === 'light';

            cachedStepMarkers.forEach(m => {
                const time = m.time !== undefined ? m.time : m.timeS;
                const temp = m.temp !== undefined ? m.temp : m.targetTemp;
                const step = m.step !== undefined ? m.step : (m.stepIndex ?? 0);
                const isTop = !!m.isTop;

                if (time === undefined || temp === undefined) return;

                const x = scales.x.getPixelForValue(Number(time));
                if (!isFinite(x) || x < chartArea.left || x > chartArea.right) return;

                const targetY = scales.y.getPixelForValue(Number(temp));

                // 1. Vertical Dashed Line (Monochrome / Neutral)
                ctx.setLineDash([5, 4]);
                ctx.strokeStyle = light ? 'rgba(15, 23, 42, 0.65)' : 'rgba(203, 213, 225, 0.65)';
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.moveTo(x, chartArea.top);
                ctx.lineTo(x, chartArea.bottom);
                ctx.stroke();

                // 2. Point Marker on Curve at Target Temperature
                if (targetY >= chartArea.top && targetY <= chartArea.bottom) {
                    ctx.setLineDash([]);
                    ctx.fillStyle = light ? '#0f172a' : '#f8fafc';
                    ctx.beginPath();
                    ctx.arc(x, targetY, 3.5, 0, 2 * Math.PI);
                    ctx.fill();

                    ctx.strokeStyle = light ? '#ffffff' : '#0f172a';
                    ctx.lineWidth = 1.5;
                    ctx.stroke();
                }

                // 3. Monochrome Pill Badge at Top of Chart
                // Line 1: "Step 1"
                // Line 2: "Bot@150°C" / "Top@150°C"
                const line1 = t('chart.step_badge', 'Step {{num}}', { num: step + 1 });
                const line2 = `${isTop ? 'Top' : 'Bot'}@${Math.round(temp)}°C`;

                const lineH = 12;
                ctx.font = 'bold 10px Outfit, sans-serif';
                const w1 = ctx.measureText(line1).width;
                ctx.font = '11px Outfit, sans-serif';
                const w2 = ctx.measureText(line2).width;
                const padding = 6;
                const pillWidth = Math.max(w1, w2) + padding * 2;
                const pillHeight = lineH * 2 + 6;

                let pillX = x + 4;
                if (pillX + pillWidth > chartArea.right) pillX = x - pillWidth - 4;
                const pillY = chartArea.top + (isTop ? 6 : 38);

                // Pill Background & Border (Black/White / Dark Slate)
                ctx.fillStyle = light ? 'rgba(241, 245, 249, 0.95)' : 'rgba(15, 23, 42, 0.95)';
                ctx.strokeStyle = light ? 'rgba(203, 213, 225, 0.8)' : 'rgba(51, 65, 85, 0.8)';
                ctx.lineWidth = 1;

                if (ctx.roundRect) {
                    ctx.beginPath();
                    ctx.roundRect(pillX, pillY, pillWidth, pillHeight, 4);
                    ctx.fill();
                    ctx.stroke();
                } else {
                    ctx.fillRect(pillX, pillY, pillWidth, pillHeight);
                }

                // Text
                ctx.fillStyle = light ? '#0f172a' : '#f8fafc';
                ctx.textAlign = 'left';
                ctx.textBaseline = 'top';

                ctx.font = 'bold 10px Outfit, sans-serif';
                ctx.fillText(line1, pillX + padding, pillY + 4);

                ctx.font = '11px Outfit, sans-serif';
                ctx.fillText(line2, pillX + padding, pillY + 4 + lineH);
            });

            ctx.restore();
        }
    };

    let chartMouseX = null;
    let chartMouseY = null;

    // ── Custom HTML/CSS Tooltip Handler (4-Quadrant 2D Avoidance & Theme-Reactive) ──
    function externalTooltipHandler(context) {
        const { chart, tooltip } = context;
        const parent = chart.canvas.parentNode;
        if (!parent) return;

        let tooltipEl = parent.querySelector('.chartjs-custom-tooltip');
        if (!tooltipEl) {
            tooltipEl = document.createElement('div');
            tooltipEl.className = 'chartjs-custom-tooltip';
            parent.appendChild(tooltipEl);
        }

        if (tooltip.opacity === 0) {
            tooltipEl.style.opacity = '0';
            return;
        }

        if (tooltip.body) {
            const titleLines = tooltip.title || [];
            const bodyLines = tooltip.body.map(b => b.lines);

            let innerHtml = '';
            titleLines.forEach(title => {
                innerHtml += `<div class="chart-tooltip-title">${title}</div>`;
            });

            innerHtml += '<div class="chart-tooltip-body">';
            bodyLines.forEach((body, i) => {
                const colors = (tooltip.labelColors && tooltip.labelColors[i]) || {};
                const bg = colors.borderColor || colors.backgroundColor || 'var(--accent)';
                const colorBox = `<span class="chart-tooltip-color-box" style="background:${bg}"></span>`;
                innerHtml += `<div class="chart-tooltip-row">${colorBox}<span>${body}</span></div>`;
            });
            innerHtml += '</div>';

            tooltipEl.innerHTML = innerHtml;
        }

        const containerWidth = parent.clientWidth;
        const containerHeight = parent.clientHeight;
        const tooltipWidth = tooltipEl.offsetWidth || 150;
        const tooltipHeight = tooltipEl.offsetHeight || 80;

        // Reference Position: Cursor mouse coordinates if available, otherwise data point caret
        const refX = (chartMouseX !== null && !isNaN(chartMouseX)) ? chartMouseX : tooltip.caretX;
        const refY = (chartMouseY !== null && !isNaN(chartMouseY)) ? chartMouseY : tooltip.caretY;

        // ── 4-Quadrant 2D Avoidance Logic ──
        // 1. Horizontal: Left half -> RIGHT of cursor, Right half -> LEFT of cursor
        const isLeftHalf = refX < (containerWidth * 0.5);
        let posX = isLeftHalf ? (refX + 16) : (refX - tooltipWidth - 16);

        // 2. Vertical: Top half -> BELOW cursor, Bottom half -> ABOVE cursor
        const isTopHalf = refY < (containerHeight * 0.5);
        let posY = isTopHalf ? (refY + 16) : (refY - tooltipHeight - 16);

        // Edge Clamping: Ensure tooltip never exceeds chart container borders
        posX = Math.max(8, Math.min(containerWidth - tooltipWidth - 8, posX));
        posY = Math.max(8, Math.min(containerHeight - tooltipHeight - 8, posY));

        tooltipEl.style.opacity = '1';
        tooltipEl.style.left = `${posX}px`;
        tooltipEl.style.top = `${posY}px`;
    }

    function initChart() {
        const canvas = $('liveChart');
        if (!canvas) return;

        const isDark = store.currentTheme === 'dark';
        const gridColor = isDark ? 'rgba(255, 255, 255, 0.07)' : 'rgba(0, 0, 0, 0.06)';
        const tickColor = isDark ? 'rgba(255, 255, 255, 0.65)' : 'rgba(0, 0, 0, 0.65)';

        const config = {
            type: 'line',
            data: {
                datasets: [
                    {
                        // 0: TOP HEATER (IST) – Blue
                        label: t('chart.ds_top_pv', 'Top PV'),
                        data: [],
                        borderColor: '#0284c7',
                        backgroundColor: 'rgba(2, 132, 199, 0.08)',
                        borderWidth: 2.2,
                        pointRadius: 0,
                        pointHoverRadius: 4,
                        tension: 0.15,
                        fill: false
                    },
                    {
                        // 1: TOP SETPOINT – Blue Dashed
                        label: t('chart.ds_top_sp', 'Top SP'),
                        data: [],
                        borderColor: 'rgba(2, 132, 199, 0.55)',
                        borderWidth: 1.5,
                        borderDash: [5, 4],
                        pointRadius: 0,
                        pointHoverRadius: 0,
                        tension: 0,
                        fill: false
                    },
                    {
                        // 2: BOTTOM HEATER (IST) – Green
                        label: t('chart.ds_bot_pv', 'Bottom PV'),
                        data: [],
                        borderColor: '#22c55e',
                        backgroundColor: 'rgba(34, 197, 94, 0.08)',
                        borderWidth: 2.2,
                        pointRadius: 0,
                        pointHoverRadius: 4,
                        tension: 0.15,
                        fill: false
                    },
                    {
                        // 3: BOTTOM SETPOINT – Green Dashed
                        label: t('chart.ds_bot_sp', 'Bottom SP'),
                        data: [],
                        borderColor: 'rgba(34, 197, 94, 0.55)',
                        borderWidth: 1.5,
                        borderDash: [5, 4],
                        pointRadius: 0,
                        pointHoverRadius: 0,
                        tension: 0,
                        fill: false
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: false,
                interaction: {
                    mode: 'index',
                    intersect: false
                },
                layout: {
                    padding: { top: 14, right: 14, bottom: 4, left: 4 }
                },
                plugins: {
                    legend: {
                        display: true,
                        position: 'top',
                        align: 'center',
                        labels: {
                            color: tickColor,
                            font: { family: 'monospace', size: 10, weight: 'bold' },
                            boxWidth: 14,
                            boxHeight: 2,
                            padding: 12,
                            usePointStyle: false
                        }
                    },
                    tooltip: {
                        enabled: false,
                        external: externalTooltipHandler,
                        callbacks: {
                            title: (items) => {
                                if (!items.length) return '';
                                const sec = items[0].parsed.x;
                                return t('chart.tooltip_time', 'Time: {{time}} min ({{sec}}s)', { time: formatTime(sec), sec });
                            },
                            label: (item) => {
                                return `<strong>${item.dataset.label}:</strong> ${Format.temp(item.parsed.y)} °C`;
                            }
                        }
                    }
                },
                scales: {
                    x: {
                        type: 'linear',
                        title: {
                            display: true,
                            text: t('chart.axis_runtime', 'Runtime (min)'),
                            color: tickColor,
                            font: { family: 'monospace', size: 10, weight: 'bold' }
                        },
                        min: 0,
                        suggestedMax: 600, // Starts at 10 minutes (300s)
                        grid: { color: gridColor },
                        ticks: {
                            color: tickColor,
                            font: { family: 'monospace', size: 9 },
                            stepSize: 60, // 1-minute grid ticks
                            callback: (val) => {
                                const m = Math.floor(val / 60);
                                const s = val % 60;
                                return s === 0 ? `${m}m` : `${m}:${s.toString().padStart(2, '0')}`;
                            }
                        }
                    },
                    y: {
                        type: 'linear',
                        title: {
                            display: true,
                            text: t('chart.axis_temperature', 'Temperature (°C)'),
                            color: tickColor,
                            font: { family: 'monospace', size: 10, weight: 'bold' }
                        },
                        min: 0,
                        suggestedMax: 260,
                        grid: { color: gridColor },
                        ticks: {
                            color: tickColor,
                            font: { family: 'monospace', size: 9 },
                            stepSize: 20, // 0, 20, 40, 60, 80, 100...
                            callback: (val) => `${val}°C`
                        }
                    }
                }
            },
            plugins: [reflowChartPlugin]
        };

        // Track exact cursor coordinates on canvas for intelligent 4-quadrant tooltip positioning
        canvas.addEventListener('mousemove', (e) => {
            const rect = canvas.getBoundingClientRect();
            chartMouseX = e.clientX - rect.left;
            chartMouseY = e.clientY - rect.top;
        });

        canvas.addEventListener('mouseleave', () => {
            chartMouseX = null;
            chartMouseY = null;
        });

        chartInstance = new Chart(canvas, config);
    }

    function updateChartTheme(theme) {
        if (!chartInstance) return;
        const isDark = theme === 'dark';
        const gridColor = isDark ? 'rgba(255, 255, 255, 0.07)' : 'rgba(0, 0, 0, 0.06)';
        const tickColor = isDark ? 'rgba(255, 255, 255, 0.65)' : 'rgba(0, 0, 0, 0.65)';

        chartInstance.options.scales.x.grid.color = gridColor;
        chartInstance.options.scales.x.ticks.color = tickColor;
        chartInstance.options.scales.x.title.color = tickColor;

        chartInstance.options.scales.y.grid.color = gridColor;
        chartInstance.options.scales.y.ticks.color = tickColor;
        chartInstance.options.scales.y.title.color = tickColor;

        chartInstance.options.plugins.legend.labels.color = tickColor;

        chartInstance.update('none');
    }

    function resetChart() {
        if (!chartInstance) return;
        chartInstance.data.datasets.forEach(ds => ds.data = []);
        cachedStepMarkers = [];
        chartInstance.options.scales.x.suggestedMax = 600; // 10 minutes default
        chartInstance.update('none');
    }

    function appendChartData(elapsedSec, topTemp, topSet, bottomTemp, bottomSet, markers) {
        if (!chartInstance) return;

        // Dynamically expand X-axis in 2-minute blocks (120s) when process duration approaches right edge
        const currentMax = chartInstance.options.scales.x.suggestedMax || 600;
        if (elapsedSec >= currentMax - 10) {
            chartInstance.options.scales.x.suggestedMax = currentMax + 120; // +2 minutes
        }

        const ds0 = chartInstance.data.datasets[0].data;
        const last = ds0.length > 0 ? ds0[ds0.length - 1] : null;

        if (last && last.x === elapsedSec) {
            // Update current second measurement in-place
            last.y = topTemp;
            chartInstance.data.datasets[1].data[ds0.length - 1].y = topSet;
            chartInstance.data.datasets[2].data[ds0.length - 1].y = bottomTemp;
            chartInstance.data.datasets[3].data[ds0.length - 1].y = bottomSet;
        } else {
            // Push new second data point
            ds0.push({ x: elapsedSec, y: topTemp });
            chartInstance.data.datasets[1].data.push({ x: elapsedSec, y: topSet });
            chartInstance.data.datasets[2].data.push({ x: elapsedSec, y: bottomTemp });
            chartInstance.data.datasets[3].data.push({ x: elapsedSec, y: bottomSet });
        }

        if (markers && Array.isArray(markers)) {
            cachedStepMarkers = markers;
        }

        // Render fast without layout animations
        chartInstance.update('none');
    }

    async function restoreHistory() {
        try {
            const data = await apiRequest('/api/history', 'GET');
            if (data) {
                if (data.stateEnum !== undefined) {
                    store.stateEnum = Number(data.stateEnum);
                    store.prevStateEnum = Number(data.stateEnum);
                }
                if (data.talSec !== undefined) {
                    store.talSec = Number(data.talSec);
                    setText('stat-tal', `${store.talSec}s`);
                }
                if (data.elapsedSec !== undefined) {
                    store.elapsedSec = Number(data.elapsedSec);
                    setText('elapsed-time', `${t('hud.runtime_prefix', 'Runtime:')} ${formatTime(store.elapsedSec)} min`);
                }

                if (data.points && Array.isArray(data.points) && data.points.length > 0 && chartInstance) {
                    console.log(`[History] Restoring ${data.points.length} telemetry points from controller...`);

                    let maxTop = 0;
                    let maxBot = 0;
                    for (const p of data.points) {
                        if (p[1] > maxTop) maxTop = p[1];
                        if (p[2] > maxBot) maxBot = p[2];
                    }
                    store.topPeakTemp = maxTop;
                    store.bottomPeakTemp = maxBot;
                    setText('top-peak', store.topPeakTemp.toFixed(1));
                    setText('bottom-peak', store.bottomPeakTemp.toFixed(1));

                    chartInstance.data.datasets[0].data = data.points.map(p => ({ x: p[0], y: p[1] }));
                    chartInstance.data.datasets[1].data = data.points.map(p => ({ x: p[0], y: p[3] }));
                    chartInstance.data.datasets[2].data = data.points.map(p => ({ x: p[0], y: p[2] }));
                    chartInstance.data.datasets[3].data = data.points.map(p => ({ x: p[0], y: p[4] }));

                    if (data.markers && Array.isArray(data.markers)) {
                        cachedStepMarkers = data.markers;
                    }

                    if (data.profileFile) {
                        store.activeProfileFile = data.profileFile;
                        store.activeProfileName = data.profileFile.replace(/\.json$/i, '');
                        const cSel = $('chart-profile-select');
                        if (cSel && cSel.querySelector(`option[value="${data.profileFile}"]`)) {
                            cSel.value = data.profileFile;
                        }
                    }

                    const maxTime = data.points[data.points.length - 1][0] || 0;
                    const currentMax = chartInstance.options.scales.x.suggestedMax || 600;
                    if (maxTime >= currentMax - 10) {
                        chartInstance.options.scales.x.suggestedMax = Math.ceil((maxTime + 60) / 120) * 120;
                    }

                    chartInstance.update('none');
                }
            }
        } catch (e) {
            console.warn('[History] Failed to restore history from controller:', e);
        }
    }

    // ── 11. EXPORT MODULE (CSV & PNG) ───────────────────────────────────────
    async function exportCSV() {
        if (!chartInstance) return;

        const ds0 = chartInstance.data.datasets[0].data; // Top Temp
        const ds1 = chartInstance.data.datasets[1].data; // Top Set
        const ds2 = chartInstance.data.datasets[2].data; // Bottom Temp
        const ds3 = chartInstance.data.datasets[3].data; // Bottom Set

        if (!ds0.length) {
            await Modal.alert(t('modal.no_data_export', 'No telemetry data recorded yet to export.'), {
                type: 'info',
                title: t('modal.no_data_title', 'No Data')
            });
            return;
        }

        let csv = 'Time_s;Top_Temp_C;Top_Setpoint_C;Bottom_Temp_C;Bottom_Setpoint_C\r\n';
        for (let i = 0; i < ds0.length; i++) {
            const time = ds0[i].x;
            const topT = ds0[i] ? Format.temp(ds0[i].y) : '0.0';
            const topS = ds1[i] ? Format.temp(ds1[i].y) : '0.0';
            const botT = ds2[i] ? Format.temp(ds2[i].y) : '0.0';
            const botS = ds3[i] ? Format.temp(ds3[i].y) : '0.0';
            csv += `${time};${topT};${topS};${botT};${botS}\r\n`;
        }

        const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        const now = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
        a.href = url;
        a.download = `reflow_telemetry_${now}.csv`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }

    function exportPNG() {
        if (!chartInstance) return;
        const canvas = $('liveChart');
        if (!canvas) return;

        // Render on offscreen canvas to guarantee solid background
        const isDark = store.currentTheme === 'dark';
        const offCanvas = document.createElement('canvas');
        offCanvas.width = canvas.width;
        offCanvas.height = canvas.height;
        const offCtx = offCanvas.getContext('2d');

        offCtx.fillStyle = isDark ? '#0f172a' : '#ffffff';
        offCtx.fillRect(0, 0, offCanvas.width, offCanvas.height);
        offCtx.drawImage(canvas, 0, 0);

        const url = offCanvas.toDataURL('image/png');
        const a = document.createElement('a');
        const now = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
        a.href = url;
        a.download = `reflow_chart_${now}.png`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
    }

    // ── 12. TELEMETRY HUD UPDATER (CALLED VIA WS) ───────────────────────────
    function updateTelemetryHUD(data) {
        if (!data) return;

        // State Machine Transition Detection: Reset peaks and chart ONLY when a new run starts live (PREHEAT or AUTOTUNE)
        const nextState = Number(data.stateEnum ?? 0);
        if (store.prevStateEnum !== null && store.prevStateEnum === 0 && (nextState === 1 || nextState === 7)) {
            store.topPeakTemp = data.topTemp || 0.0;
            store.bottomPeakTemp = data.bottomTemp || 0.0;
            store.hasFirstRampReading = false;
            resetChart();
        }
        store.prevStateEnum = nextState;

        // 1. Status Badge
        updateStatusBadge(data.stateEnum, data.state, data.preheatDone);

        // 2. Active Profile Name – sync dropdown only when process is running (not IDLE struct-default)
        if (data.profileFile && nextState >= 1 && data.profileFile !== store.activeProfileFile) {
            store.activeProfileFile = data.profileFile;
            store.activeProfileName = data.profileFile.replace(/\.json$/i, '');
            const cSel = $('chart-profile-select');
            if (cSel && cSel.querySelector(`option[value="${data.profileFile}"]`)) {
                cSel.value = data.profileFile;
            }
        }
        // Lock dropdown while a process is running (stateEnum 1-4, 7), unlock in IDLE (0)
        const cSel2 = $('chart-profile-select');
        if (cSel2) cSel2.disabled = ((nextState >= 1 && nextState <= 4) || nextState === 7);

        // 3. Live Top Heater HUD
        store.topTemp = data.topTemp ?? store.topTemp;
        store.topSet = data.topSet ?? store.topSet;
        store.topPower = data.topPower ?? store.topPower;
        setText('top-temp', Format.temp(store.topTemp));
        setText('top-set', Format.temp(store.topSet));
        setText('top-power', Format.int(store.topPower));

        if ((store.stateEnum >= 1 && store.stateEnum <= 4) || store.stateEnum === 7) {
            if (store.topTemp > store.topPeakTemp) store.topPeakTemp = store.topTemp;
        }
        setText('top-peak', Format.temp(store.topPeakTemp));

        const isProcessActive = (store.stateEnum === 1 || // PREHEAT
                                 store.stateEnum === 2 || // SOAK
                                 store.stateEnum === 3 || // REFLOW
                                 store.stateEnum === 4 || // COOLING
                                 store.stateEnum === 7);  // AUTOTUNE

        // Top Settle / Hold Badge
        const topWait = $('top-wait');
        if (topWait) {
            if (isProcessActive) {
                if (data.topSettling) {
                    topWait.className = 'wait-alert wait-alert--settle';
                    topWait.innerText = `⏳ Settle (${data.topSettleRemainSec ?? 0}s)`;
                    topWait.classList.remove('u-hidden');
                } else if (data.topHolding) {
                    topWait.className = 'wait-alert wait-alert--hold';
                    topWait.innerText = `⏱ Hold (${data.topHoldRemainSec ?? 0}s)`;
                    topWait.classList.remove('u-hidden');
                } else {
                    topWait.classList.add('u-hidden');
                }
            } else {
                topWait.classList.add('u-hidden');
            }
        }

        // 4. Live Bottom Heater HUD
        store.bottomTemp = data.bottomTemp ?? store.bottomTemp;
        store.bottomSet = data.bottomSet ?? store.bottomSet;
        store.bottomPower = data.bottomPower ?? store.bottomPower;
        setText('bottom-temp', Format.temp(store.bottomTemp));
        setText('bottom-set', Format.temp(store.bottomSet));
        setText('bottom-power', Format.int(store.bottomPower));

        if ((store.stateEnum >= 1 && store.stateEnum <= 4) || store.stateEnum === 7) {
            if (store.bottomTemp > store.bottomPeakTemp) store.bottomPeakTemp = store.bottomTemp;
        }
        setText('bottom-peak', Format.temp(store.bottomPeakTemp));

        // Bottom Settle / Hold Badge
        const botWait = $('bottom-wait');
        if (botWait) {
            if (isProcessActive) {
                if (data.bottomSettling) {
                    botWait.className = 'wait-alert wait-alert--settle';
                    botWait.innerText = `⏳ Settle (${data.bottomSettleRemainSec ?? 0}s)`;
                    botWait.classList.remove('u-hidden');
                } else if (data.bottomHolding) {
                    botWait.className = 'wait-alert wait-alert--hold';
                    botWait.innerText = `⏱ Hold (${data.bottomHoldRemainSec ?? 0}s)`;
                    botWait.classList.remove('u-hidden');
                } else {
                    botWait.classList.add('u-hidden');
                }
            } else {
                botWait.classList.add('u-hidden');
            }
        }

        // 4b. Live Active PID Gains Display
        if (store.showPidGains) {
            if (store.stateEnum === 7 || data.tuneActive) {
                // During Open-Loop Autotuning, PID controller is bypassed (open loop step test)
                setText('top-pid-kp', '--');
                setText('top-pid-ki', '--');
                setText('top-pid-kd', '--');
                setText('bottom-pid-kp', '--');
                setText('bottom-pid-ki', '--');
                setText('bottom-pid-kd', '--');
            } else {
                if (data.topPidKp !== undefined) {
                    setText('top-pid-kp', Format.kp(data.topPidKp));
                    setText('top-pid-ki', Format.ki(data.topPidKi ?? 0));
                    setText('top-pid-kd', Format.kd(data.topPidKd ?? 0));
                }
                if (data.bottomPidKp !== undefined) {
                    setText('bottom-pid-kp', Format.kp(data.bottomPidKp));
                    setText('bottom-pid-ki', Format.ki(data.bottomPidKi ?? 0));
                    setText('bottom-pid-kd', Format.kd(data.bottomPidKd ?? 0));
                }
            }
        }

        // 5. Ramp Rate Calculation (°C/s)
        const now = Date.now();
        if (!store.hasFirstRampReading) {
            store.lastTopTemp = store.topTemp;
            store.lastBottomTemp = store.bottomTemp;
            store.lastRampCalcTime = now;
            store.hasFirstRampReading = true;
            setText('top-ramp', '+0.0');
            setText('bottom-ramp', '+0.0');
        } else {
            const dt = (now - store.lastRampCalcTime) / 1000.0;
            if (dt >= 1.0) {
                store.topRampRate = (store.topTemp - store.lastTopTemp) / dt;
                store.bottomRampRate = (store.bottomTemp - store.lastBottomTemp) / dt;

                const fmtRate = (r) => (r >= 0 ? `+${Format.temp(r)}` : Format.temp(r));
                setText('top-ramp', fmtRate(store.topRampRate));
                setText('bottom-ramp', fmtRate(store.bottomRampRate));

                store.lastTopTemp = store.topTemp;
                store.lastBottomTemp = store.bottomTemp;
                store.lastRampCalcTime = now;
            }
        }

        // 6. Elapsed Time
        store.elapsedSec = data.elapsed ?? 0;
        setText('elapsed-time', `${t('hud.runtime_prefix', 'Runtime:')} ${formatTime(store.elapsedSec)} min`);

        // 7. Time Above Liquidus (TAL: > 217 °C)
        store.talSec = (data.talSec !== undefined) ? data.talSec : store.talSec;
        setText('stat-tal', `${store.talSec}s`);

        // 8. Delta T (Top vs Bottom)
        const deltaT = Math.abs(store.topTemp - store.bottomTemp);
        setText('stat-deltat', `${Format.temp(deltaT)} °C`);

        // 9. Feed Chart with Live Data (during active process, cooling/done or autotune)
        if ((store.stateEnum >= 1 && store.stateEnum <= 5) || store.stateEnum === 7) {
            appendChartData(
                store.elapsedSec,
                store.topTemp,
                store.topSet,
                store.bottomTemp,
                store.bottomSet,
                data.stepMarkers
            );
        }

        // 10. Sync Actor Buttons with Backend Reality
        if (data.lamp !== undefined && data.lamp !== store.lampState) {
            store.lampState = data.lamp;
            updateActorIcons();
        }
        if (data.fan !== undefined && data.fan !== store.fanState) {
            store.fanState = data.fan;
            updateActorIcons();
        }

        // 11. Update 1:1 Live CLI Logs
        if (data.logs) {
            updateLogs(data.logs);
        }

        // Auto-reconcile FSM state if controller is in BACKUP (State 8) but UI is not on backup tab
        if (data.stateEnum === 8 && store.currentTab !== 'backup') {
            if (!store._reconcilingBackup) {
                store._reconcilingBackup = true;
                apiRequest('/api/control', 'POST', { action: 'exitBackup' })
                    .finally(() => { store._reconcilingBackup = false; });
            }
        }

        // 12. Autotune Progress & State Synchronization (Edge Detection)
        const isTuneActive = Boolean(data.tuneActive);
        const tuneRunningEl = $('autotune-running');
        const tuneBarEl = $('tune-progress');
        const tuneLblEl = $('lbl-tune-progress');
        const btnStartTune = $('btn-start-autotune');
        const btnStopTune = $('btn-stop-autotune');

        if (isTuneActive) {
            if (btnStartTune) btnStartTune.classList.add('u-hidden');
            if (btnStopTune) btnStopTune.classList.remove('u-hidden');
            if (tuneRunningEl) tuneRunningEl.classList.remove('u-hidden');
            const pct = Math.min(100, Math.max(0, Math.round(data.tuneProgress || 0)));
            if (tuneBarEl) tuneBarEl.style.width = `${pct}%`;
            if (tuneLblEl) tuneLblEl.innerText = `${pct}%`;
        } else {
            if (tuneRunningEl && !tuneRunningEl.classList.contains('u-hidden')) {
                tuneRunningEl.classList.add('u-hidden');
            }
            if (btnStartTune && btnStartTune.classList.contains('u-hidden')) {
                btnStartTune.classList.remove('u-hidden');
            }
            if (btnStopTune && !btnStopTune.classList.contains('u-hidden')) {
                btnStopTune.classList.add('u-hidden');
            }
        }

        // Falling Edge Detection: true -> false transition while browser was connected
        if (store.prevTuneActive === true && !isTuneActive) {
            if (data.tuneFinished && data.tuneKp && data.tuneKp > 0) {
                handleAutotuneCompleted(data);
            }
        }
        store.prevTuneActive = isTuneActive;

        // 13. Synchronize Control Buttons Interlock State
        updateControlButtonsState();
    }

    // ── 13. WEBSOCKET CLIENT & RESILIENT LIFECYCLE ──────────────────────────
    let wsInstance = null;
    let wsReconnectTimer = null;
    let wsReconnectAttempts = 0;
    let wsConnectAttemptTime = 0;

    function scheduleWsReconnect() {
        if (wsReconnectTimer) return;
        const delay = 1000;
        wsReconnectAttempts++;
        console.log(`[WS] Scheduling fast reconnect in ${delay}ms (attempt #${wsReconnectAttempts})...`);
        wsReconnectTimer = setTimeout(() => {
            wsReconnectTimer = null;
            initWebSocket();
        }, delay);
    }

    function initWebSocket() {
        if (wsInstance) {
            wsInstance.onopen = null;
            wsInstance.onmessage = null;
            wsInstance.onerror = null;
            wsInstance.onclose = null;
            try { wsInstance.close(); } catch (e) { }
            wsInstance = null;
        }

        const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsHost = window.location.host || '192.168.4.1';
        const wsUrl = `${wsProtocol}//${wsHost}/ws`;

        wsConnectAttemptTime = Date.now();
        console.log(`[WS] Connecting to ${wsUrl}...`);

        try {
            wsInstance = new WebSocket(wsUrl);

            wsInstance.onopen = () => {
                console.log('[WS] Connected successfully.');
                store.wsConnected = true;
                store.lastTelemetryTime = Date.now();
                wsReconnectAttempts = 0;
                if (wsReconnectTimer) {
                    clearTimeout(wsReconnectTimer);
                    wsReconnectTimer = null;
                }
                updateControlButtonsState();
            };

            wsInstance.onmessage = (event) => {
                try {
                    store.wsConnected = true;
                    store.lastTelemetryTime = Date.now();
                    const data = JSON.parse(event.data);
                    updateTelemetryHUD(data);
                } catch (err) {
                    console.error('[WS] JSON parse error:', err, event.data);
                }
            };

            wsInstance.onerror = (err) => {
                console.warn('[WS] Error:', err);
                store.wsConnected = false;
                updateControlButtonsState();
                scheduleWsReconnect();
            };

            wsInstance.onclose = () => {
                console.warn('[WS] Disconnected. Reconnecting...');
                store.wsConnected = false;
                const badge = $('state-badge');
                if (badge) {
                    badge.innerText = t('status.disconnected', 'DISCONNECTED');
                    badge.className = 'status-badge status-fault';
                }
                updateLogs(null);
                updateControlButtonsState();
                scheduleWsReconnect();
            };
        } catch (e) {
            console.error('[WS] Initialization failed:', e);
            store.wsConnected = false;
            const badge = $('state-badge');
            if (badge) {
                badge.innerText = t('status.disconnected', 'DISCONNECTED');
                badge.className = 'status-badge status-fault';
            }
            updateLogs(null);
            updateControlButtonsState();
            scheduleWsReconnect();
        }
    }

    // Telemetry Watchdog: Detects dead/unclean disconnects (power-off/reset/sleep) within 3.5s
    let telemetryWatchdog = null;
    function startTelemetryWatchdog() {
        if (telemetryWatchdog) clearInterval(telemetryWatchdog);
        telemetryWatchdog = setInterval(() => {
            const now = Date.now();
            if (store.lastTelemetryTime > 0 && (now - store.lastTelemetryTime > 3500)) {
                if (store.wsConnected) {
                    console.warn('[Watchdog] Telemetry timeout (>3.5s without packet). Marking DISCONNECTED...');
                    store.wsConnected = false;
                    updateLogs(null);
                }
                const badge = $('state-badge');
                const disText = t('status.disconnected', 'DISCONNECTED');
                if (badge && badge.innerText !== disText) {
                    badge.innerText = disText;
                    badge.className = 'status-badge status-fault';
                }
                updateControlButtonsState();

                // Only force-close if socket is OPEN but silent, NOT if currently CONNECTING
                if (wsInstance && wsInstance.readyState === WebSocket.OPEN && (now - store.lastTelemetryTime > 5000) && (now - wsConnectAttemptTime > 5000)) {
                    console.warn('[Watchdog] Active WebSocket silent for >5s. Force closing...');
                    wsInstance.onopen = null;
                    wsInstance.onmessage = null;
                    wsInstance.onerror = null;
                    wsInstance.onclose = null;
                    try {
                        wsInstance.close();
                    } catch (e) {}
                    wsInstance = null;
                    scheduleWsReconnect();
                }
            }
        }, 500);
    }

    // ── 14. EVENT LISTENERS & VISIBILITY LIFECYCLE ──────────────────────────
    // W3C Page Visibility API: Instantly resume WebSocket when mobile wakes or tab becomes visible
    document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') {
            const now = Date.now();
            if (!store.wsConnected || (now - store.lastTelemetryTime > 3500)) {
                console.log('[WS] Tab became active and telemetry is stale/disconnected. Triggering fast reconnect...');
                wsReconnectAttempts = 0;
                if (wsReconnectTimer) {
                    clearTimeout(wsReconnectTimer);
                    wsReconnectTimer = null;
                }
                initWebSocket();
            }
        }
    });

    // Browser Online Event: Instantly reconnect when Wi-Fi recovers
    window.addEventListener('online', () => {
        console.log('[WS] Network connection online event. Triggering fast reconnect...');
        wsReconnectAttempts = 0;
        if (wsReconnectTimer) {
            clearTimeout(wsReconnectTimer);
            wsReconnectTimer = null;
        }
        initWebSocket();
    });

    // ── 15. INITIALIZATION ──────────────────────────────────────────────────
    async function init() {
        console.log('[ReflowApp] Booting...');

        initTheme();
        initLanguage();
        initTabs();
        initChart();
        initBackupState();
        updateActorIcons();

        // 1. Initialize Control Buttons in locked state until WS telemetry is confirmed live
        updateControlButtonsState();

        // 2. Initialize Live WebSocket immediately for instant telemetry & status feedback
        initWebSocket();
        startTelemetryWatchdog();

        document.addEventListener('click', (e) => {
            const dropdown = $('status-log-dropdown');
            const badge = $('state-badge');
            if (dropdown && badge && !dropdown.contains(e.target) && !badge.contains(e.target)) {
                dropdown.classList.remove('active');
                store.statusLogOpen = false;
            }
        });

        // Clear invalid visual highlighting when user modifies input field
        document.addEventListener('input', (e) => {
            if (e.target && e.target.classList && e.target.classList.contains('input-invalid')) {
                e.target.classList.remove('input-invalid');
            }
        });

        // 2. Fetch System Status & Dynamic Limits (First priority HTTP request)
        await syncStatusWithController();

        // 3. Restore curve history on reload or multi-device sync
        await restoreHistory();

        // 4. Fetch available profiles list once (cached in store.profiles)
        const profiles = await fetchProfiles(true);

        // 5. Populate HUD profile select (pre-selects active profile or defaultProfile)
        await populateChartProfileSelect(profiles);

        // 6. Load Settings (populates settings tab & default profile select from cached profiles)
        await loadSettingsFromController();

        // 7. Security verification (prompt Wi-Fi setup if running on factory default credentials)
        checkWifiSecurity();
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }

    // ── 15. SETTINGS MANAGER ────────────────────────────────────────────────
    let currentSettings = null;

    async function loadSettingsFromController() {
        try {
            // 1. Fetch available profiles to populate default profile select (cached)
            try {
                const profiles = await fetchProfiles();
                const defaultSel = $('set-default-profile');
                if (defaultSel && Array.isArray(profiles)) {
                    defaultSel.innerHTML = `<option value="">${t('settings.opt_no_default_profile', '— Not Selected (Default) —')}</option>`;
                    profiles.forEach(p => {
                        const file = (typeof p === 'string') ? p : (p.file || p.name || '');
                        const name = (typeof p === 'string') ? p : (p.name || p.file || '');
                        if (file) {
                            const opt = document.createElement('option');
                            opt.value = file;
                            opt.textContent = name.replace(/\.json$/i, '');
                            defaultSel.appendChild(opt);
                        }
                    });
                }
            } catch (err) {
                console.warn('[Settings] Failed to fetch profiles for select:', err);
            }

            // 2. Fetch machine settings
            const data = await apiRequest('/api/settings', 'GET');
            if (!data) return;
            currentSettings = data;

            // System & Hardware
            if ($('set-sim')) $('set-sim').value = data.simulationMode ? 'true' : 'false';
            if ($('set-lang')) $('set-lang').value = data.language || store.currentLanguage || 'en';
            if ($('set-hw-buzzer')) $('set-hw-buzzer').value = data.hardwareBuzzerEnabled ? 'true' : 'false';
            if ($('set-default-profile') && data.defaultProfile !== undefined) {
                $('set-default-profile').value = data.defaultProfile;
            }

            // Temperature Limits & Calibration
            if ($('set-max-top')) $('set-max-top').value = Format.temp(data.maxTempTop ?? 250);
            if ($('set-max-bottom')) $('set-max-bottom').value = Format.temp(data.maxTempBottom ?? 250);
            if ($('set-ema-enabled')) $('set-ema-enabled').value = data.emaFilterEnabled ? 'true' : 'false';
            if ($('set-filter-alpha')) $('set-filter-alpha').value = Format.alpha(data.emaAlpha ?? 0.3);
            if ($('set-cj-top-offset')) $('set-cj-top-offset').value = Format.temp(data.topCjOffset ?? 0.0);
            if ($('set-cj-bottom-offset')) $('set-cj-bottom-offset').value = Format.temp(data.bottomCjOffset ?? 0.0);

            // Safety & Watchdogs
            if ($('set-stuck-ssr-thresh')) $('set-stuck-ssr-thresh').value = Format.ramp(data.stuckSsrRiseThreshold ?? 5.0);
            if ($('set-stuck-ssr-time')) $('set-stuck-ssr-time').value = Format.int(data.stuckSsrWindowSec ?? 30);
            if ($('set-norise-thresh')) $('set-norise-thresh').value = Format.ramp(data.noRiseThreshold ?? 2.0);
            if ($('set-norise-time')) $('set-norise-time').value = Format.int(data.noRiseTimeoutSec ?? 45);
            if ($('set-cooling-safe-temp')) $('set-cooling-safe-temp').value = Format.temp(data.coolingSafeTemp ?? 50.0);

            // Tolerances & Timings
            if ($('set-hold-low-tol')) $('set-hold-low-tol').value = Format.ramp(data.holdLowTolerance ?? 2.0);
            if ($('set-hold-high-tol')) $('set-hold-high-tol').value = Format.ramp(data.holdHighTolerance ?? 2.0);
            if ($('set-settle-time-s')) $('set-settle-time-s').value = Format.int(data.settleTimeS ?? 5);
            if ($('set-fan-delay')) $('set-fan-delay').value = Format.int(data.fanCoolingDelayS ?? 0);
            if ($('set-fan-duration')) $('set-fan-duration').value = Format.int(data.fanCoolingDurationS ?? 60);

            // SSR Burst-Fire
            if ($('set-burst-top')) $('set-burst-top').value = Format.int(data.topBurstWindowMs ?? 1000);
            if ($('set-burst-bottom')) $('set-burst-bottom').value = Format.int(data.bottomBurstWindowMs ?? 1000);

            // PID Standard
            if ($('set-pid-lib-enabled')) $('set-pid-lib-enabled').value = data.pidLibraryEnabled ? 'true' : 'false';
            if ($('set-top-kp')) $('set-top-kp').value = Format.kp(data.topKp ?? 2.0);
            if ($('set-top-ki')) $('set-top-ki').value = Format.ki(data.topKi ?? 0.05);
            if ($('set-top-kd')) $('set-top-kd').value = Format.kd(data.topKd ?? 1.0);
            if ($('set-bottom-kp')) $('set-bottom-kp').value = Format.kp(data.bottomKp ?? 2.0);
            if ($('set-bottom-ki')) $('set-bottom-ki').value = Format.ki(data.bottomKi ?? 0.04);
            if ($('set-bottom-kd')) $('set-bottom-kd').value = Format.kd(data.bottomKd ?? 1.0);

            // Store stock PID values & library enabled state for HUD & warning banners
            store.pidLibraryEnabled = (data.pidLibraryEnabled === true);
            store.topKp = data.topKp ?? 2.0;
            store.topKi = data.topKi ?? 0.05;
            store.topKd = data.topKd ?? 1.0;
            store.bottomKp = data.bottomKp ?? 2.0;
            store.bottomKi = data.bottomKi ?? 0.04;
            store.bottomKd = data.bottomKd ?? 1.0;
            updatePidLibraryDisabledBanner();

            // Immediately apply active Language & Theme from controller if different
            if (data.language && data.language !== store.currentLanguage) {
                setLanguage(data.language);
            }
            if (data.theme && data.theme !== store.currentTheme) {
                setTheme(data.theme);
            }

            console.log('[Settings] Loaded successfully from controller.');
        } catch (err) {
            console.error('[Settings] Failed to load settings:', err);
        }
    }

    async function saveSettingsToController() {
        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            await Modal.alert(
                t('modal.save_settings_locked_active', 'Failed to save settings: Reflow process or autotune is currently active. Configuration changes are only permitted in IDLE state.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Save Failed: Process Active')
                }
            );
            return;
        }

        try {
            const payload = {
                simulationMode: $('set-sim')?.value === 'true',
                language: $('set-lang')?.value || store.currentLanguage || 'en',
                theme: store.currentTheme,
                hardwareBuzzerEnabled: $('set-hw-buzzer')?.value === 'true',
                defaultProfile: $('set-default-profile')?.value || '',
                showZones: store.showZones,
                showTalLine: store.showTalLine,
                showStepMarkers: store.showStepMarkers,
                showPidGains: store.showPidGains,

                maxTempTop: Format.parseFloat($('set-max-top')?.value, 250),
                maxTempBottom: Format.parseFloat($('set-max-bottom')?.value, 250),
                minTempTop: 0.0,
                minTempBottom: 0.0,
                coolingSafeTemp: Format.parseFloat($('set-cooling-safe-temp')?.value, 50),

                enableStuckSsrCheck: true,
                stuckSsrRiseThreshold: Format.parseFloat($('set-stuck-ssr-thresh')?.value, 5),
                stuckSsrWindowSec: Format.parseInt($('set-stuck-ssr-time')?.value, 30),

                enableNoRiseCheck: true,
                noRiseThreshold: Format.parseFloat($('set-norise-thresh')?.value, 2),
                noRiseTimeoutSec: Format.parseInt($('set-norise-time')?.value, 45),

                holdLowTolerance: Format.parseFloat($('set-hold-low-tol')?.value, 2.0),
                holdHighTolerance: Format.parseFloat($('set-hold-high-tol')?.value, 2.0),
                settleTimeS: Format.parseInt($('set-settle-time-s')?.value, 5),

                fanCoolingDelayS: Format.parseInt($('set-fan-delay')?.value, 0),
                fanCoolingDurationS: Format.parseInt($('set-fan-duration')?.value, 60),

                emaFilterEnabled: $('set-ema-enabled')?.value === 'true',
                emaAlpha: Format.parseFloat($('set-filter-alpha')?.value, 0.3),
                faultStreakLimit: 3,
                topCjOffset: Format.parseFloat($('set-cj-top-offset')?.value, 0.0),
                bottomCjOffset: Format.parseFloat($('set-cj-bottom-offset')?.value, 0.0),

                topBurstWindowMs: Format.parseInt($('set-burst-top')?.value, 1000),
                bottomBurstWindowMs: Format.parseInt($('set-burst-bottom')?.value, 1000),

                pidLibraryEnabled: $('set-pid-lib-enabled')?.value === 'true',
                topKp: Format.parseFloat($('set-top-kp')?.value, 2.0),
                topKi: Format.parseFloat($('set-top-ki')?.value, 0.05),
                topKd: Format.parseFloat($('set-top-kd')?.value, 1.0),
                bottomKp: Format.parseFloat($('set-bottom-kp')?.value, 2.0),
                bottomKi: Format.parseFloat($('set-bottom-ki')?.value, 0.04),
                bottomKd: Format.parseFloat($('set-bottom-kd')?.value, 1.0),

                btnDebounceMs: 50
            };

            const val = validateSettingsPayload(payload);
            if (!val.valid) {
                if (val.fieldId) {
                    const el = $(val.fieldId);
                    if (el) {
                        el.classList.add('input-invalid');
                        el.focus();
                        if (el.select) el.select();
                    }
                }
                await Modal.alert(val.error, {
                    type: 'warning',
                    title: t('modal.invalid_settings_title', '⚠️ Invalid Machine Setting')
                });
                return;
            }

            const res = await apiRequest('/api/settings', 'POST', payload);
            if (res && res.success !== false) {
                document.querySelectorAll('.input-invalid').forEach(el => el.classList.remove('input-invalid'));
                store.pidLibraryEnabled = payload.pidLibraryEnabled;
                store.topKp = payload.topKp;
                store.topKi = payload.topKi;
                store.topKd = payload.topKd;
                store.bottomKp = payload.bottomKp;
                store.bottomKi = payload.bottomKi;
                store.bottomKd = payload.bottomKd;
                updatePidLibraryDisabledBanner();

                if (payload.theme && payload.theme !== store.currentTheme) {
                    setTheme(payload.theme);
                }
                if (payload.language && payload.language !== store.currentLanguage) {
                    setLanguage(payload.language);
                }
                await Modal.alert(t('toast.settings_saved', 'Machine Settings saved successfully to controller!'), { type: 'success', title: t('modal.profile_saved_title', 'Settings Saved') });
            } else {
                const errMsg = (res && res.error) ? res.error : t('toast.settings_error', 'Failed to save settings to controller.');
                await Modal.alert(errMsg, { type: 'danger', title: t('modal.save_failed_title', 'Save Failed') });
            }
        } catch (err) {
            console.error('[Settings] Save failed:', err);
            await Modal.alert(t('toast.settings_error', 'Failed to save settings to controller.'), { type: 'danger', title: t('modal.save_failed_title', 'Save Failed') });
        }
    }

    async function changeWifiPasswordFromSettings() {
        const passInput = $('set-wifi-new-pass');
        if (!passInput) return;
        const newPass = passInput.value.trim();
        if (newPass.length < 8) {
            await Modal.alert(t('toast.wifi_pass_short', 'Password must be at least 8 characters long.'), { type: 'warning', title: t('settings.sg_wifi', 'Wi-Fi Password') });
            return;
        }

        try {
            const res = await apiRequest('/api/settings/wifi', 'POST', { password: newPass });
            if (res && res.success !== false) {
                passInput.value = '';
                await Modal.alert(t('toast.wifi_pass_updated', 'Wi-Fi password updated successfully! Controller AP will restart with new credentials.'), { type: 'success', title: t('settings.sg_wifi', 'Password Updated') });
            } else {
                await Modal.alert(t('toast.settings_error', 'Failed to update Wi-Fi password.'), { type: 'danger', title: t('modal.save_failed_title', 'Update Failed') });
            }
        } catch (err) {
            console.error('[Settings] Wi-Fi password update failed:', err);
            await Modal.alert(t('toast.settings_error', 'Error updating Wi-Fi password.'), { type: 'danger', title: t('modal.error_title', 'Error') });
        }
    }

    async function checkWifiSecurity() {
        try {
            const status = await apiRequest('/api/security/status', 'GET');
            if (status && status.passwordChanged === false) {
                await showWifiSecurityModal();
            }
        } catch (err) {
            console.warn('[Security] Could not check Wi-Fi security status:', err);
        }
    }

    async function showWifiSecurityModal() {
        while (true) {
            const result = await Modal.custom({
                type: 'warning',
                title: t('modal.sec_title', '⚠️ Wi-Fi Security Notice'),
                body: t('modal.sec_msg', "The controller is running with the default Access Point password ('reflow123'). Please set a secure password or confirm keeping the default."),
                withInput: true,
                inputType: 'text',
                inputPlaceholder: t('settings.ph_wifi_pass', 'min. 8 characters'),
                buttons: [
                    { label: t('modal.sec_btn_keep', 'Keep Default'), cls: 'btn-outline', value: 'KEEP_DEFAULT' },
                    { label: t('modal.sec_btn_change', 'Set Password'), cls: 'btn-primary' }
                ]
            });

            if (result === 'KEEP_DEFAULT') {
                try {
                    await apiRequest('/api/security/wifi', 'POST', { keepDefault: true });
                    showToast(t('toast.wifi_pass_kept', 'Default Wi-Fi password confirmed.'));
                } catch (err) {
                    console.error('[Security] Failed to confirm default password:', err);
                }
                break;
            } else if (typeof result === 'string') {
                const trimmed = result.trim();
                if (trimmed.length < 8) {
                    await Modal.alert(
                        t('toast.wifi_pass_short', 'Password must be at least 8 characters long.'),
                        { type: 'warning', title: t('modal.sec_title', '⚠️ Wi-Fi Security Notice') }
                    );
                    continue;
                }
                try {
                    const res = await apiRequest('/api/security/wifi', 'POST', { password: trimmed });
                    if (res && res.success !== false) {
                        await Modal.alert(
                            t('toast.wifi_pass_updated', 'Wi-Fi password updated successfully! Controller AP will restart with new credentials.'),
                            { type: 'success', title: t('settings.sg_wifi', 'Password Updated') }
                        );
                    } else {
                        await Modal.alert(
                            t('toast.settings_error', 'Failed to update Wi-Fi password.'),
                            { type: 'danger', title: t('modal.save_failed_title', 'Update Failed') }
                        );
                    }
                } catch (err) {
                    console.error('[Security] Failed to update Wi-Fi password:', err);
                    await Modal.alert(
                        t('toast.settings_error', 'Failed to update Wi-Fi password.'),
                        { type: 'danger', title: t('modal.save_failed_title', 'Update Failed') }
                    );
                }
                break;
            } else {
                break;
            }
        }
    }

    function toggleZones(show) {
        store.showZones = (show !== undefined) ? !!show : !store.showZones;
        localStorage.setItem('reflow_show_zones', store.showZones);
        updateCheckboxStates();
        apiRequest('/api/theme', 'POST', { showZones: store.showZones });
        if (chartInstance) {
            chartInstance.update('none');
        }
    }

    function toggleTalLine(show) {
        store.showTalLine = (show !== undefined) ? !!show : !store.showTalLine;
        localStorage.setItem('reflow_show_tal', store.showTalLine);
        updateCheckboxStates();
        apiRequest('/api/theme', 'POST', { showTalLine: store.showTalLine });
        if (chartInstance) {
            chartInstance.update('none');
        }
    }

    function toggleStepMarkers(show) {
        store.showStepMarkers = (show !== undefined) ? !!show : !store.showStepMarkers;
        localStorage.setItem('reflow_show_markers', store.showStepMarkers);
        updateCheckboxStates();
        apiRequest('/api/theme', 'POST', { showStepMarkers: store.showStepMarkers });
        if (chartInstance) {
            chartInstance.update('none');
        }
    }

    function togglePidGainsDisplay(show) {
        store.showPidGains = (show !== undefined) ? !!show : !store.showPidGains;
        localStorage.setItem('reflow_show_pid_gains', store.showPidGains);
        updateCheckboxStates();
        apiRequest('/api/theme', 'POST', { showPidGains: store.showPidGains });
    }

    // ── 16. PROFILE MANAGER ─────────────────────────────────────────────────
    let currentProfileData = null;
    let isUnsavedDraft = false;

    async function fetchProfiles(forceReload = false) {
        if (!forceReload && store.profiles !== null && Array.isArray(store.profiles)) {
            return store.profiles;
        }
        try {
            const profiles = await apiRequest('/api/profiles', 'GET');
            if (Array.isArray(profiles)) {
                store.profiles = profiles;
            }
            return store.profiles || [];
        } catch (err) {
            console.error('[Profiles] Failed to fetch profile list:', err);
            return store.profiles || [];
        }
    }

    function invalidateProfileCache() {
        store.profiles = null;
    }

    function getProfileFilename(p) {
        if (!p) return '';
        if (typeof p === 'string') return p;
        return p.file || p.name || '';
    }

    function getProfileDisplayName(p) {
        if (!p) return '';
        const raw = (typeof p === 'string') ? p : (p.name || p.file || '');
        return raw.replace(/\.json$/i, '');
    }

    async function loadProfileList(selectFilename, forceReload = false) {
        try {
            const profiles = await fetchProfiles(forceReload);
            const sel = $('profile-select');
            if (!sel) return;

            sel.innerHTML = '';
            const placeholder = document.createElement('option');
            placeholder.value = '';
            placeholder.textContent = t('profile.select_placeholder', '— Select Profile to Edit —');
            placeholder.hidden = true;
            placeholder.disabled = true;
            sel.appendChild(placeholder);

            if (Array.isArray(profiles) && profiles.length > 0) {
                profiles.forEach(p => {
                    const file = getProfileFilename(p);
                    const name = getProfileDisplayName(p);
                    if (file) {
                        const opt = document.createElement('option');
                        opt.value = file;
                        opt.textContent = name;
                        sel.appendChild(opt);
                    }
                });

                // Only load a profile if explicitly requested (e.g. after save/create); otherwise start clean
                const target = selectFilename || '';
                if (target && sel.querySelector(`option[value="${target}"]`)) {
                    sel.value = target;
                    isUnsavedDraft = false;
                    await loadProfileData(target);
                } else {
                    sel.value = '';
                    currentProfileData = null;
                    isUnsavedDraft = false;
                    renderProfileTable('table-top', [], true);
                    renderProfileTable('table-bottom', [], false);
                }
            } else {
                sel.innerHTML = `<option value="">${t('profile.no_profiles', 'No Profiles Available')}</option>`;
                currentProfileData = null;
                isUnsavedDraft = false;
                renderProfileTable('table-top', [], true);
                renderProfileTable('table-bottom', [], false);
            }
        } catch (err) {
            console.error('[Profile] Failed to load profile list:', err);
        }
    }

    async function loadProfileData(filename) {
        if (!filename || filename === 'undefined') return;
        try {
            const data = await apiRequest(`/api/profiles/${encodeURIComponent(filename)}`, 'GET');
            if (data) {
                isUnsavedDraft = false;
                currentProfileData = {
                    name: data.name || filename.replace(/\.json$/i, ''),
                    file: data.file || filename,
                    stepsTop: Array.isArray(data.stepsTop) ? data.stepsTop : [],
                    stepsBottom: Array.isArray(data.stepsBottom) ? data.stepsBottom : []
                };

                renderProfileTable('table-top', currentProfileData.stepsTop, true);
                renderProfileTable('table-bottom', currentProfileData.stepsBottom, false);
                console.log(`[Profile Editor] Loaded profile "${currentProfileData.name}" (${currentProfileData.stepsTop.length} top, ${currentProfileData.stepsBottom.length} bot steps).`);
            }
        } catch (err) {
            console.error(`[Profile Editor] Failed to load profile "${filename}":`, err);
        }
    }

    function onProfileSelectChange() {
        const sel = $('profile-select');
        if (!sel || !sel.value || sel.value === 'undefined') {
            currentProfileData = null;
            renderProfileTable('table-top', [], true);
            renderProfileTable('table-bottom', [], false);
            return;
        }

        if (isUnsavedDraft && currentProfileData && currentProfileData.file === sel.value) {
            renderProfileTable('table-top', currentProfileData.stepsTop, true);
            renderProfileTable('table-bottom', currentProfileData.stepsBottom, false);
            return;
        }

        loadProfileData(sel.value);
    }

    function renderProfileTable(tableId, steps, isTop) {
        const tbody = $(tableId)?.querySelector('tbody');
        if (!tbody) return;

        tbody.innerHTML = '';
        if (!steps || !steps.length) {
            const msg = currentProfileData ? t('profile.no_steps', 'No steps defined. Click "+ Add Step" below.') : t('profile.no_profile_selected', 'No profile selected. Choose a profile from the dropdown above or click "+ New Profile".');
            tbody.innerHTML = `<tr><td colspan="5" class="muted u-p-2">${msg}</td></tr>`;
            return;
        }

        steps.forEach((s, idx) => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><strong>#${idx + 1}</strong></td>
                <td><input type="number" value="${Format.temp(s.temp)}" step="${VALIDATION_RESOLUTIONS.TEMPERATURE}" min="${VALIDATION_LIMITS.MIN_TEMPERATURE}" max="${VALIDATION_LIMITS.MAX_TEMPERATURE}" onchange="ReflowApp.updateProfileStepField(${isTop}, ${idx}, 'temp', this.value)"></td>
                <td><input type="number" value="${Format.int(s.time)}" step="${VALIDATION_RESOLUTIONS.TIME_SEC}" min="0" max="${VALIDATION_LIMITS.MAX_STEP_TIME_S}" onchange="ReflowApp.updateProfileStepField(${isTop}, ${idx}, 'time', this.value)"></td>
                <td><input type="number" value="${Format.ramp(s.ramp)}" step="${VALIDATION_RESOLUTIONS.RAMP_RATE}" min="${VALIDATION_LIMITS.MIN_RAMP_RATE}" max="${VALIDATION_LIMITS.MAX_RAMP_RATE}" onchange="ReflowApp.updateProfileStepField(${isTop}, ${idx}, 'ramp', this.value)"></td>
                <td><button type="button" class="btn-delete-step" onclick="ReflowApp.removeProfileStep(${isTop}, ${idx})" title="${t('profile.delete_step_title', 'Delete Step')}">🗑</button></td>
            `;
            tbody.appendChild(tr);
        });
    }

    function updateProfileStepField(isTop, idx, field, val) {
        const list = isTop ? currentProfileData.stepsTop : currentProfileData.stepsBottom;
        if (list && list[idx]) {
            list[idx][field] = (field === 'time') ? Format.parseInt(val, 0) : Format.parseFloat(val, 0);
        }
    }

    async function addProfileStep(isTop) {
        if (!currentProfileData) {
            await Modal.alert(t('modal.no_profile_preheat', 'Please select or create a profile first.'), {
                type: 'info',
                title: t('modal.no_profile_title', 'No Profile')
            });
            return;
        }
        const list = isTop ? currentProfileData.stepsTop : currentProfileData.stepsBottom;
        if (list.length >= VALIDATION_LIMITS.MAX_PROFILE_STEPS) {
            await Modal.alert(t('modal.profile_steps_limit_reached', 'Maximum of {{max}} steps per heater reached.', { max: VALIDATION_LIMITS.MAX_PROFILE_STEPS }), {
                type: 'warning',
                title: t('modal.profile_steps_limit_title', 'Step Limit Reached')
            });
            return;
        }
        const last = list.length > 0 ? list[list.length - 1] : null;
        const newTemp = last ? Math.min(250, last.temp + 20) : (isTop ? 150 : 150);
        const newTime = last ? last.time : 60;
        const newRamp = last ? last.ramp : 1.0;

        list.push({ temp: newTemp, time: newTime, ramp: newRamp });
        renderProfileTable(isTop ? 'table-top' : 'table-bottom', list, isTop);
    }

    function removeProfileStep(isTop, idx) {
        if (!currentProfileData) return;
        const list = isTop ? currentProfileData.stepsTop : currentProfileData.stepsBottom;
        if (list && list.length > idx) {
            list.splice(idx, 1);
            renderProfileTable(isTop ? 'table-top' : 'table-bottom', list, isTop);
        }
    }

    async function createNewProfile() {
        let defaultVal = '';
        while (true) {
            const name = await Modal.prompt(t('modal.prompt_new_profile', 'Enter a name for the new profile:'), {
                title: t('modal.prompt_new_profile_title', 'New Profile'),
                inputPlaceholder: t('modal.prompt_new_profile_ph', 'Custom Profile (max. 30 chars)'),
                inputValue: defaultVal,
                maxLength: 30,
                type: 'info',
                createLabel: t('profile.btn_new', '+ New Profile'),
                cancelLabel: t('modal.btn_cancel', 'Cancel')
            });
            if (name === null) return; // User cancelled

            const cleanName = name.trim();
            if (!cleanName) {
                await Modal.alert(t('modal.profile_name_empty', 'Profile name cannot be empty.'), {
                    type: 'warning',
                    title: t('modal.profile_name_invalid_title', 'Invalid Profile Name')
                });
                defaultVal = '';
                continue;
            }

            if (cleanName.length > 30) {
                await Modal.alert(t('modal.profile_name_too_long', 'Profile name is too long ({{len}} chars). Maximum allowed is 30 characters.', { len: cleanName.length }), {
                    type: 'warning',
                    title: t('modal.profile_name_invalid_title', 'Invalid Profile Name')
                });
                defaultVal = cleanName.substring(0, 30);
                continue;
            }

            const filename = cleanName.toLowerCase().replace(/[^a-z0-9_-]/g, '_') + '.json';

            isUnsavedDraft = true;
            currentProfileData = {
                name: cleanName,
                file: filename,
                stepsBottom: [
                    { temp: 50.0, time: 10, ramp: 1.0 }
                ],
                stepsTop: [
                    { temp: 50.0, time: 10, ramp: 1.0 }
                ]
            };

            const sel = $('profile-select');
            if (sel) {
                const opt = document.createElement('option');
                opt.value = filename;
                opt.textContent = `${cleanName} (Draft)`;
                sel.appendChild(opt);
                sel.value = filename;
            }

            renderProfileTable('table-top', currentProfileData.stepsTop, true);
            renderProfileTable('table-bottom', currentProfileData.stepsBottom, false);
            break;
        }
    }

    async function saveProfileToController() {
        if (store.stateEnum === 8) {
            await Modal.alert(
                t('modal.profile_save_backup_locked', 'Profile modification is locked during backup or restore operations.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Save Failed: Process Active')
                }
            );
            return;
        }

        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            await Modal.alert(
                t('modal.save_profile_locked_active', 'Failed to save profile: Reflow process or autotune is currently active. Profile modifications are only permitted in IDLE state.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Save Failed: Process Active')
                }
            );
            return;
        }

        if (!currentProfileData || !currentProfileData.name || !currentProfileData.file) {
            await Modal.alert(t('modal.invalid_profile_data', 'Invalid profile data.'), {
                type: 'danger',
                title: t('modal.invalid_profile_title', 'Invalid Profile')
            });
            return;
        }
        const val = validateProfilePayload(currentProfileData);
        if (!val.valid) {
            await Modal.alert(val.error, {
                type: 'warning',
                title: t('modal.invalid_profile_title', '⚠️ Invalid Profile')
            });
            return;
        }

        try {
            const res = await apiRequest('/api/profiles', 'POST', currentProfileData);
            if (res && res.success !== false) {
                invalidateProfileCache();
                isUnsavedDraft = false;
                await Modal.alert(t('modal.profile_saved', 'Profile "{{name}}" saved successfully!', { name: currentProfileData.name }), {
                    type: 'success',
                    title: t('modal.profile_saved_title', 'Profile Saved')
                });
                await loadProfileList(currentProfileData.file, true);
                await populateChartProfileSelect();
            } else {
                const errMsg = (res && res.error) ? res.error : t('modal.profile_save_failed', 'Failed to save profile to controller.');
                await Modal.alert(errMsg, {
                    type: 'danger',
                    title: t('modal.save_failed_title', 'Save Failed')
                });
            }
        } catch (err) {
            console.error('[Profile Editor] Save failed:', err);
            await Modal.alert(t('modal.profile_save_failed', 'Failed to save profile to controller.'), {
                type: 'danger',
                title: t('modal.save_failed_title', 'Save Failed')
            });
        }
    }

    async function deleteProfileFromController() {
        if (store.stateEnum === 8) {
            await Modal.alert(
                t('modal.profile_delete_backup_locked', 'Profile deletion is locked during backup or restore operations.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Delete Failed: Process Active')
                }
            );
            return;
        }

        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            await Modal.alert(
                t('modal.delete_profile_locked_active', 'Failed to delete profile: Reflow process or autotune is currently active. Profile deletions are only permitted in IDLE state.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Delete Failed: Process Active')
                }
            );
            return;
        }

        const sel = $('profile-select');
        const file = sel ? sel.value : (currentProfileData ? currentProfileData.file : '');
        if (!file) return;

        if (file === 'factory-profile.json') {
            await Modal.alert(t('modal.factory_profile_protected', 'The factory default profile cannot be deleted.'), {
                type: 'warning',
                title: t('modal.protected_profile_title', 'Protected Profile')
            });
            return;
        }

        if (isUnsavedDraft && currentProfileData && file === currentProfileData.file) {
            // Unsaved draft simply remove from UI
            isUnsavedDraft = false;
            currentProfileData = null;
            await loadProfileList(null, true);
            return;
        }

        const isRunningProfile = (store.stateEnum >= 1 && store.stateEnum <= 4) && (store.activeProfileFile === file);
        const confirmMsg = isRunningProfile
            ? t('modal.delete_active_profile_confirm', "Profile '{{file}}' is currently active in the running reflow process. The current process will finish safely from RAM, but the profile will be deleted from storage and unavailable for future runs. Are you sure you want to delete it?", { file: file })
            : t('modal.delete_profile_confirm', 'Are you sure you want to delete profile "{{file}}"?', { file: file });
        const confirmTitle = isRunningProfile
            ? t('modal.delete_active_profile_title', '⚠️ Delete Running Profile?')
            : t('modal.delete_profile_title', 'Delete Profile');

        const ok = await Modal.confirm(confirmMsg, {
            type: 'danger',
            title: confirmTitle,
            confirmLabel: t('modal.btn_delete', 'Delete'),
            cancelLabel: t('modal.btn_cancel', 'Cancel')
        });
        if (!ok) return;

        try {
            const res = await apiRequest(`/api/profiles/${encodeURIComponent(file)}`, 'DELETE');
            if (res && res.success !== false) {
                invalidateProfileCache();
                await Modal.alert(t('modal.profile_deleted', 'Profile "{{file}}" deleted successfully.', { file: file }), {
                    type: 'success',
                    title: t('modal.profile_deleted_title', 'Profile Deleted')
                });
                currentProfileData = null;
                await loadProfileList(null, true);
                await populateChartProfileSelect();
            } else {
                await Modal.alert(t('modal.profile_delete_failed', 'Failed to delete profile.'), {
                    type: 'danger',
                    title: t('modal.delete_failed_title', 'Delete Failed')
                });
            }
        } catch (err) {
            console.error('[Profile Editor] Delete failed:', err);
            await Modal.alert(t('toast.settings_error', 'Error deleting profile.'), {
                type: 'danger',
                title: t('modal.error_title', 'Error')
            });
        }
    }

    // ── CHART PROFILE DROPDOWN ──────────────────────────────────────────────
    async function populateChartProfileSelect(profilesList) {
        try {
            const profiles = (Array.isArray(profilesList) && profilesList.length > 0)
                ? profilesList
                : await fetchProfiles();
            const sel = $('chart-profile-select');
            if (!sel) return;
            sel.innerHTML = '';

            // Always prepend a neutral placeholder (value="" so sel.value='' works on all browsers)
            const placeholder = document.createElement('option');
            placeholder.value = '';
            placeholder.textContent = t('hud.select_profile_placeholder', '— Select Profile —');
            // hidden + disabled prevents user from selecting it again but allows sel.value='' to work
            placeholder.hidden = true;
            placeholder.disabled = true;
            sel.appendChild(placeholder);

            if (Array.isArray(profiles) && profiles.length > 0) {
                profiles.forEach(p => {
                    const file = getProfileFilename(p);
                    const name = getProfileDisplayName(p);
                    if (file) {
                        const opt = document.createElement('option');
                        opt.value = file;
                        opt.textContent = name;
                        sel.appendChild(opt);
                    }
                });
            }

            // Priority for pre-selection in Cockpit HUD:
            // 1. store.activeProfileFile (if already known from restoreHistory or manual choice)
            // 2. currentSettings.defaultProfile (Auto-Reload at Startup from settings)
            let chosen = store.activeProfileFile;
            if (!chosen && currentSettings && currentSettings.defaultProfile) {
                chosen = currentSettings.defaultProfile;
            }

            if (chosen && sel.querySelector(`option[value="${chosen}"]`)) {
                sel.value = chosen;
                store.activeProfileFile = chosen;
                store.activeProfileName = chosen.replace(/\.json$/i, '');
            } else {
                sel.value = '';  // explicitly show placeholder
            }
        } catch (err) {
            console.error('[ChartSelect] Failed to populate profile dropdown:', err);
        }
    }

    function onChartProfileSelectChange(file) {
        if (!file) return;
        store.activeProfileFile = file;
        store.activeProfileName = file.replace(/\.json$/i, '');
        console.log(`[ChartSelect] Profile selected: ${file}`);
    }

    // ── 17. PID LIBRARY & AUTOTUNE MANAGER (TunePID) ────────────────────────
    let pidLibData = {
        top: [],
        bottom: []
    };

    async function loadPidLibraryFromController() {
        try {
            const data = await apiRequest('/api/pidlibrary', 'GET');
            if (data && (Array.isArray(data.top) || Array.isArray(data.bottom))) {
                pidLibData = {
                    top: Array.isArray(data.top) ? data.top : [],
                    bottom: Array.isArray(data.bottom) ? data.bottom : []
                };
            }
            renderPidTable('table-pid-top', pidLibData.top, true);
            renderPidTable('table-pid-bottom', pidLibData.bottom, false);
            updatePidPreview();
            updatePidLibraryDisabledBanner();
            console.log('[TunePID] Loaded PID library from controller.');
        } catch (err) {
            console.error('[TunePID] Failed to load PID library:', err);
        }
    }

    function updatePidLibraryDisabledBanner() {
        const banner = $('tunepid-disabled-banner');
        if (!banner) return;
        banner.classList.toggle('u-hidden', !!store.pidLibraryEnabled);
    }

    function renderPidTable(tableId, points, isTop) {
        const tbody = $(tableId)?.querySelector('tbody');
        if (!tbody) return;

        tbody.innerHTML = '';
        if (!points || !points.length) {
            tbody.innerHTML = `<tr><td colspan="6" class="muted u-p-2">${t('tunepid.no_points', 'No PID points defined. Click "+ Add PID Point" below.')}</td></tr>`;
            return;
        }

        // Sort ascending by temperature
        points.sort((a, b) => a.temp - b.temp);

        points.forEach((pt, idx) => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><strong>#${idx + 1}</strong></td>
                <td><input type="number" value="${Format.temp(pt.temp)}" step="${VALIDATION_RESOLUTIONS.TEMPERATURE}" min="${VALIDATION_LIMITS.MIN_TEMPERATURE}" max="${VALIDATION_LIMITS.MAX_TEMPERATURE}" onchange="ReflowApp.updatePidField(${isTop}, ${idx}, 'temp', this.value)"></td>
                <td><input type="number" value="${Format.kp(pt.kp)}" step="${VALIDATION_RESOLUTIONS.PID_GAIN_KP}" min="0.00" max="100.00" onchange="ReflowApp.updatePidField(${isTop}, ${idx}, 'kp', this.value)"></td>
                <td><input type="number" value="${Format.ki(pt.ki)}" step="${VALIDATION_RESOLUTIONS.PID_GAIN_KI}" min="0.000" max="10.000" onchange="ReflowApp.updatePidField(${isTop}, ${idx}, 'ki', this.value)"></td>
                <td><input type="number" value="${Format.kd(pt.kd)}" step="${VALIDATION_RESOLUTIONS.PID_GAIN_KD}" min="0.00" max="100.00" onchange="ReflowApp.updatePidField(${isTop}, ${idx}, 'kd', this.value)"></td>
                <td><button type="button" class="btn-delete-step" onclick="ReflowApp.removePidStep(${isTop}, ${idx})" title="${t('tunepid.delete_point_title', 'Delete Point')}">🗑</button></td>
            `;
            tbody.appendChild(tr);
        });
    }

    function updatePidField(isTop, idx, field, val) {
        const list = isTop ? pidLibData.top : pidLibData.bottom;
        if (list && list[idx]) {
            list[idx][field] = Format.parseFloat(val, 0);
            updatePidPreview();
        }
    }

    async function addPidStep(isTop) {
        if (!pidLibData) return;
        const list = isTop ? pidLibData.top : pidLibData.bottom;
        if (list.length >= VALIDATION_LIMITS.MAX_PID_POINTS) {
            await Modal.alert(t('modal.pid_points_limit_reached', 'Maximum of {{max}} PID points per heater reached.', { max: VALIDATION_LIMITS.MAX_PID_POINTS }), {
                type: 'warning',
                title: t('modal.pid_points_limit_title', 'Point Limit Reached')
            });
            return;
        }
        const last = list.length > 0 ? list[list.length - 1] : null;
        const newTemp = last ? Math.min(260, last.temp + 30) : 40;
        const newKp = last ? last.kp : 2.0;
        const newKi = last ? last.ki : 0.5;
        const newKd = last ? last.kd : 1.0;

        list.push({ temp: newTemp, kp: newKp, ki: newKi, kd: newKd });
        renderPidTable(isTop ? 'table-pid-top' : 'table-pid-bottom', list, isTop);
        updatePidPreview();
    }

    function removePidStep(isTop, idx) {
        const list = isTop ? pidLibData.top : pidLibData.bottom;
        if (list && list.length > idx) {
            list.splice(idx, 1);
            renderPidTable(isTop ? 'table-pid-top' : 'table-pid-bottom', list, isTop);
            updatePidPreview();
        }
    }

    async function savePidLibraryToController() {
        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            await Modal.alert(
                t('modal.save_pid_locked_active', 'Failed to save PID Library: Reflow process or autotune is currently active. PID modifications are only permitted in IDLE state.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Save Failed: Process Active')
                }
            );
            return;
        }

        const val = validatePidLibraryPayload(pidLibData);
        if (!val.valid) {
            await Modal.alert(val.error, {
                type: 'warning',
                title: t('modal.invalid_pid_title', '⚠️ Invalid PID Library')
            });
            return;
        }

        try {
            const res = await apiRequest('/api/pidlibrary', 'POST', pidLibData);
            if (res && res.success !== false) {
                await Modal.alert(t('modal.pid_lib_saved', 'PID Library saved successfully to controller!'), {
                    type: 'success',
                    title: t('modal.pid_lib_saved_title', 'PID Library Saved')
                });
            } else {
                const errMsg = (res && res.error) ? res.error : t('modal.pid_lib_save_failed', 'Failed to save PID Library to controller.');
                await Modal.alert(errMsg, {
                    type: 'danger',
                    title: t('modal.save_failed_title', 'Save Failed')
                });
            }
        } catch (err) {
            console.error('[TunePID] Save failed:', err);
            await Modal.alert(t('modal.pid_lib_save_failed', 'Failed to save PID Library to controller.'), {
                type: 'danger',
                title: t('modal.save_failed_title', 'Save Failed')
            });
        }
    }

    function backupPidLibrary() {
        const dataStr = 'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify(pidLibData, null, 2));
        const a = document.createElement('a');
        a.href = dataStr;
        a.download = `pid_library_backup_${new Date().toISOString().slice(0, 10)}.json`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
    }

    function interpolateGains(points, targetTemp) {
        if (!points || !points.length) return { kp: 2.0, ki: 0.5, kd: 1.0 };
        if (points.length === 1) return points[0];

        if (targetTemp <= points[0].temp) return points[0];
        if (targetTemp >= points[points.length - 1].temp) return points[points.length - 1];

        for (let i = 0; i < points.length - 1; i++) {
            const p0 = points[i];
            const p1 = points[i + 1];
            if (targetTemp >= p0.temp && targetTemp <= p1.temp) {
                const ratio = (targetTemp - p0.temp) / (p1.temp - p0.temp || 1);
                return {
                    kp: p0.kp + ratio * (p1.kp - p0.kp),
                    ki: p0.ki + ratio * (p1.ki - p0.ki),
                    kd: p0.kd + ratio * (p1.kd - p0.kd)
                };
            }
        }
        return points[0];
    }

    function updatePidPreview() {
        const tVal = Format.parseFloat($('pid-preview-temp')?.value, 150);
        const topEmpty = !pidLibData.top || !pidLibData.top.length;
        const botEmpty = !pidLibData.bottom || !pidLibData.bottom.length;

        if (topEmpty) {
            setText('pid-preview-top', '—');
        } else {
            const topGains = interpolateGains(pidLibData.top, tVal);
            setText('pid-preview-top', `Kp=${Format.kp(topGains.kp)} / Ki=${Format.ki(topGains.ki)} / Kd=${Format.kd(topGains.kd)}`);
        }

        if (botEmpty) {
            setText('pid-preview-bottom', '—');
        } else {
            const botGains = interpolateGains(pidLibData.bottom, tVal);
            setText('pid-preview-bottom', `Kp=${Format.kp(botGains.kp)} / Ki=${Format.ki(botGains.ki)} / Kd=${Format.kd(botGains.kd)}`);
        }
    }

    async function startAutotune() {
        if (!store.wsConnected || (Date.now() - store.lastTelemetryTime > 3500)) {
            await Modal.alert(t('modal.ws_disconnected_msg', 'Live telemetry stream is disconnected. Starting Reflow, Preheat, or Autotune is locked for safety until reconnection.'), {
                type: 'warning',
                title: t('modal.ws_disconnected_title', 'Safety Interlock Active')
            });
            return;
        }

        if (store.stateEnum !== 0 && store.stateEnum !== 5) {
            await Modal.alert(
                t('modal.autotune_locked_msg', 'Reflow process or another operation is currently active: Autotune can only be started in IDLE state.'),
                {
                    type: 'warning',
                    title: t('modal.autotune_locked_title', '⚠️ Autotune Locked: Process Active')
                }
            );
            return;
        }

        const ch = $('tune-channel')?.value || 'top';
        const rawTemp = $('tune-temp')?.value?.trim();
        const temp = Format.parseFloat(rawTemp, NaN);
        if (!rawTemp || isNaN(temp)) {
            await Modal.alert(t('modal.autotune_temp_required', `Please enter a target temperature between ${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C.`, { min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE) }), {
                type: 'warning',
                title: t('modal.autotune_temp_required_title', 'Target Temperature Required')
            });
            $('tune-temp')?.focus();
            return;
        }

        if (temp < VALIDATION_LIMITS.MIN_TEMPERATURE || temp > VALIDATION_LIMITS.MAX_TEMPERATURE) {
            await Modal.alert(t('modal.autotune_temp_invalid', `Target temperature must be between ${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C (Entered: ${Format.temp(temp)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE), val: Format.temp(temp) }), {
                type: 'warning',
                title: t('modal.autotune_temp_invalid_title', 'Invalid Temperature Range')
            });
            $('tune-temp')?.focus();
            return;
        }

        const heaterName = ch === 'top' ? (store.currentLanguage === 'de' ? 'Ober' : 'Top') : (store.currentLanguage === 'de' ? 'Unter' : 'Bottom');
        const ok = await Modal.confirm(t('modal.autotune_confirm', 'Start PID Autotune for {{heater}} heater at {{temp}}°C?', { heater: heaterName, temp: temp }), {
            type: 'warning',
            title: t('modal.autotune_confirm_title', 'Start Autotune'),
            confirmLabel: t('modal.btn_start', 'Start'),
            cancelLabel: t('modal.btn_cancel', 'Cancel')
        });
        if (!ok) return;

        try {
            const res = await apiRequest('/api/control', 'POST', {
                action: 'autotune',
                channel: ch,
                targetTemp: temp
            });
            if (res && res.success !== false) {
                $('btn-start-autotune')?.classList.add('u-hidden');
                $('btn-stop-autotune')?.classList.remove('u-hidden');
                $('autotune-running')?.classList.remove('u-hidden');
                console.log(`[TunePID] Autotune started for ${ch} at ${temp}°C`);
            } else {
                await Modal.alert(t('modal.autotune_cmd_sent', 'Autotune command sent to controller.'), {
                    type: 'info',
                    title: t('modal.autotune_cmd_title', 'Autotune')
                });
            }
        } catch (err) {
            console.error('[TunePID] Start Autotune failed:', err);
            await Modal.alert(t('modal.autotune_start_error', 'Error starting Autotune.'), {
                type: 'danger',
                title: t('modal.autotune_start_error_title', 'Autotune Error')
            });
        }
    }

    async function stopAutotune() {
        try {
            await apiRequest('/api/control', 'POST', { action: 'stopAutotune' });
            $('btn-start-autotune')?.classList.remove('u-hidden');
            $('btn-stop-autotune')?.classList.add('u-hidden');
            $('autotune-running')?.classList.add('u-hidden');
            console.log('[TunePID] Autotune stopped.');
        } catch (err) {
            console.error('[TunePID] Stop failed:', err);
        }
    }

    async function handleAutotuneCompleted(data) {
        const isTop = data.tuneIsTop !== false;
        const targetTemp = parseFloat(data.tuneTargetTemp || 150);
        const kp = parseFloat(data.tuneKp || 0);
        const ki = parseFloat(data.tuneKi || 0);
        const kd = parseFloat(data.tuneKd || 0);

        if (kp > 0) {
            // Reload PID library from controller to get the newly auto-saved Flash data
            await loadPidLibraryFromController();

            const heaterName = isTop ? (store.currentLanguage === 'de' ? 'Ober' : 'Top') : (store.currentLanguage === 'de' ? 'Unter' : 'Bottom');
            await Modal.alert(
                t('modal.autotune_complete_body',
                  `New PID point (${Format.temp(targetTemp)}°C: Kp=${Format.kp(kp)}, Ki=${Format.ki(ki)}, Kd=${Format.kd(kd)}) for ${heaterName} heater was automatically saved to the controller's PID Library.\n\nYou can fine-tune these values in the table anytime; if edited manually, click 'Save PID Library' to store your changes.`,
                  { temp: Format.temp(targetTemp), kp: Format.kp(kp), ki: Format.ki(ki), kd: Format.kd(kd), heater: heaterName }
                ),
                { type: 'success', title: t('modal.autotune_complete_title', 'Autotune Complete & Saved!') }
            );
        }
    }

    // ── 18. BACKUP & RESTORE MANAGER ─────────────────────────────────────────
    function showBackupStatus(message, isError = false) {
        const banner = $('backup-status-banner');
        const icon = $('backup-status-icon');
        const text = $('backup-status-text');
        if (!banner || !text) return;

        text.textContent = message;
        if (icon) icon.textContent = isError ? '⚠️' : '✓';
        banner.classList.toggle('error', isError);
        banner.classList.remove('u-hidden');

        setTimeout(() => {
            banner.classList.add('u-hidden');
        }, 6000);
    }

    function makeCrcTable() {
        let c;
        const table = [];
        for (let n = 0; n < 256; n++) {
            c = n;
            for (let k = 0; k < 8; k++) {
                c = ((c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1));
            }
            table[n] = c;
        }
        return table;
    }
    const CRC_TABLE = makeCrcTable();

    function crc32(uint8Array) {
        let crc = 0 ^ (-1);
        for (let i = 0; i < uint8Array.length; i++) {
            crc = (crc >>> 8) ^ CRC_TABLE[(crc ^ uint8Array[i]) & 0xFF];
        }
        return (crc ^ (-1)) >>> 0;
    }

    function createZipArchive(files) {
        const localHeaders = [];
        const centralHeaders = [];
        let offset = 0;

        files.forEach(file => {
            const nameBytes = new TextEncoder().encode(file.name);
            const dataBytes = typeof file.data === 'string' ? new TextEncoder().encode(file.data) : file.data;
            const crc = crc32(dataBytes);
            const size = dataBytes.length;

            const localHeader = new Uint8Array(30 + nameBytes.length + size);
            const dvL = new DataView(localHeader.buffer);
            dvL.setUint32(0, 0x04034b50, true);
            dvL.setUint16(4, 20, true);
            dvL.setUint16(6, 0x0800, true);
            dvL.setUint16(8, 0, true);
            dvL.setUint16(10, 0, true);
            dvL.setUint16(12, 0, true);
            dvL.setUint32(14, crc, true);
            dvL.setUint32(18, size, true);
            dvL.setUint32(22, size, true);
            dvL.setUint16(26, nameBytes.length, true);
            dvL.setUint16(28, 0, true);
            localHeader.set(nameBytes, 30);
            localHeader.set(dataBytes, 30 + nameBytes.length);
            localHeaders.push(localHeader);

            const centralHeader = new Uint8Array(46 + nameBytes.length);
            const dvC = new DataView(centralHeader.buffer);
            dvC.setUint32(0, 0x02014b50, true);
            dvC.setUint16(4, 20, true);
            dvC.setUint16(6, 20, true);
            dvC.setUint16(8, 0x0800, true);
            dvC.setUint16(10, 0, true);
            dvC.setUint16(12, 0, true);
            dvC.setUint16(14, 0, true);
            dvC.setUint32(16, crc, true);
            dvC.setUint32(20, size, true);
            dvC.setUint32(24, size, true);
            dvC.setUint16(28, nameBytes.length, true);
            dvC.setUint16(30, 0, true);
            dvC.setUint16(32, 0, true);
            dvC.setUint16(34, 0, true);
            dvC.setUint16(36, 0, true);
            dvC.setUint32(38, 0, true);
            dvC.setUint32(42, offset, true);
            centralHeader.set(nameBytes, 46);
            centralHeaders.push(centralHeader);

            offset += localHeader.length;
        });

        const centralDirOffset = offset;
        let centralDirSize = 0;
        centralHeaders.forEach(ch => { centralDirSize += ch.length; });

        const eocd = new Uint8Array(22);
        const dvE = new DataView(eocd.buffer);
        dvE.setUint32(0, 0x06054b50, true);
        dvE.setUint16(4, 0, true);
        dvE.setUint16(6, 0, true);
        dvE.setUint16(8, files.length, true);
        dvE.setUint16(10, files.length, true);
        dvE.setUint32(12, centralDirSize, true);
        dvE.setUint32(16, centralDirOffset, true);
        dvE.setUint16(20, 0, true);

        const totalSize = centralDirOffset + centralDirSize + eocd.length;
        const zipBytes = new Uint8Array(totalSize);
        let cur = 0;
        localHeaders.forEach(lh => { zipBytes.set(lh, cur); cur += lh.length; });
        centralHeaders.forEach(ch => { zipBytes.set(ch, cur); cur += ch.length; });
        zipBytes.set(eocd, cur);

        return new Blob([zipBytes], { type: 'application/zip' });
    }

    function parseZipArchive(arrayBuffer) {
        const dv = new DataView(arrayBuffer);
        const u8 = new Uint8Array(arrayBuffer);
        const extracted = [];
        let pos = 0;

        while (pos < arrayBuffer.byteLength - 30) {
            const sig = dv.getUint32(pos, true);
            if (sig === 0x04034b50) {
                const compMethod = dv.getUint16(pos + 8, true);
                const uncompSize = dv.getUint32(pos + 22, true);
                const nameLen = dv.getUint16(pos + 26, true);
                const extraLen = dv.getUint16(pos + 28, true);

                const nameBytes = u8.subarray(pos + 30, pos + 30 + nameLen);
                const fileName = new TextDecoder().decode(nameBytes);
                const dataStart = pos + 30 + nameLen + extraLen;
                const dataBytes = u8.subarray(dataStart, dataStart + uncompSize);

                if (compMethod === 0) {
                    const contentStr = new TextDecoder().decode(dataBytes);
                    extracted.push({ name: fileName, text: contentStr });
                }
                pos = dataStart + uncompSize;
            } else {
                pos++;
            }
        }
        return extracted;
    }

    // ── Validation Helpers (Centralized Sanity Bounds) ──────────────────────
    function validateSettingsPayload(data) {
        if (!data || typeof data !== 'object' || Array.isArray(data)) {
            return { valid: false, error: t('val.invalid_json_format', 'Invalid JSON object format.') };
        }
        // Schema version guard – reject files from a newer firmware
        if (data.schemaVersion && typeof data.schemaVersion === 'number') {
            if (data.schemaVersion > SCHEMA_VERSIONS.MACHINE_SETTINGS) {
                return { valid: false, error: t('val.incompatible_schema', 'Incompatible file: Schema version (v{{ver}}) is newer than supported controller firmware (v{{cur}}). Please update firmware first.', { ver: data.schemaVersion, cur: SCHEMA_VERSIONS.MACHINE_SETTINGS }) };
            }
        }
        // Reject if it is a profile or PID library
        if (data.stepsTop !== undefined || data.stepsBottom !== undefined) {
            return { valid: false, error: t('val.mismatched_profile', 'Invalid file: Reflow Profile JSON cannot be imported as Machine Settings.') };
        }
        if (Array.isArray(data.top) && Array.isArray(data.bottom) && data.topKp === undefined && data.maxTempTop === undefined) {
            return { valid: false, error: t('val.mismatched_pid', 'Invalid file: PID Library JSON cannot be imported as Machine Settings.') };
        }
        // Must contain at least one known machine settings key
        const hasSettingsKey = data.maxTempTop !== undefined || data.maxTempBottom !== undefined ||
                               data.coolingSafeTemp !== undefined || data.topKp !== undefined ||
                               data.bottomKp !== undefined || data.simulationMode !== undefined ||
                               data.theme !== undefined || data.language !== undefined ||
                               data.settleTimeS !== undefined || data.topBurstWindowMs !== undefined ||
                               data.safetyLimits !== undefined || data.general !== undefined;
        if (!hasSettingsKey) {
            return { valid: false, error: t('val.mismatched_settings', 'Invalid file: Does not contain valid Machine Settings structure.') };
        }

        const maxTop = data.maxTempTop ?? data.safetyLimits?.maxTempTop;
        const maxBot = data.maxTempBottom ?? data.safetyLimits?.maxTempBottom;
        const safeCool = data.coolingSafeTemp ?? data.safetyLimits?.safeCoolingTemp;
        const stuckRise = data.stuckSsrRiseThreshold ?? data.safetyLimits?.stuckSsrRiseThreshold;
        const stuckTime = data.stuckSsrWindowSec ?? data.safetyLimits?.stuckSsrWindowSec;
        const noRiseThresh = data.noRiseThreshold ?? data.safetyLimits?.noRiseThreshold;
        const noRiseTime = data.noRiseTimeoutSec ?? data.safetyLimits?.noRiseTimeoutSec;
        const holdLow = data.holdLowTolerance ?? data.safetyLimits?.holdLowTolerance;
        const holdHigh = data.holdHighTolerance ?? data.safetyLimits?.holdHighTolerance;
        const settleS = data.settleTimeS;
        const fanDelay = data.fanCoolingDelayS;
        const fanDur = data.fanCoolingDurationS;
        const burstTop = data.topBurstWindowMs;
        const burstBot = data.bottomBurstWindowMs;
        const alpha = data.emaAlpha;
        const topCj = data.topCjOffset;
        const botCj = data.bottomCjOffset;

        if (maxTop !== undefined && (isNaN(maxTop) || maxTop < VALIDATION_LIMITS.MIN_TEMPERATURE || maxTop > VALIDATION_LIMITS.MAX_TEMPERATURE)) {
            return { valid: false, fieldId: 'set-max-top', error: t('val.max_temp_top_out_of_range', `Max Top Temperature must be between ${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C (Entered: ${Format.temp(maxTop)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE), val: Format.temp(maxTop) }) };
        }
        if (maxBot !== undefined && (isNaN(maxBot) || maxBot < VALIDATION_LIMITS.MIN_TEMPERATURE || maxBot > VALIDATION_LIMITS.MAX_TEMPERATURE)) {
            return { valid: false, fieldId: 'set-max-bottom', error: t('val.max_temp_bottom_out_of_range', `Max Bottom Temperature must be between ${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C (Entered: ${Format.temp(maxBot)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE), val: Format.temp(maxBot) }) };
        }
        if (safeCool !== undefined && (isNaN(safeCool) || safeCool < VALIDATION_LIMITS.MIN_SAFE_COOLING_TEMP || safeCool > VALIDATION_LIMITS.MAX_SAFE_COOLING_TEMP)) {
            return { valid: false, fieldId: 'set-cooling-safe-temp', error: t('val.safe_cooling_range', `Safe Cooling Temperature must be between ${Format.temp(VALIDATION_LIMITS.MIN_SAFE_COOLING_TEMP)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_SAFE_COOLING_TEMP)}°C (Entered: ${Format.temp(safeCool)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_SAFE_COOLING_TEMP), max: Format.temp(VALIDATION_LIMITS.MAX_SAFE_COOLING_TEMP), val: Format.temp(safeCool) }) };
        }
        if (stuckRise !== undefined && (isNaN(stuckRise) || stuckRise < 1.0 || stuckRise > 50.0)) {
            return { valid: false, fieldId: 'set-stuck-ssr-thresh', error: t('val.stuck_rise_range', `Stuck SSR Rise Threshold must be between 1.0°C and 50.0°C (Entered: ${Format.temp(stuckRise)}°C).`, { min: '1.0', max: '50.0', val: Format.temp(stuckRise) }) };
        }
        if (stuckTime !== undefined && (isNaN(stuckTime) || stuckTime < 3 || stuckTime > 120)) {
            return { valid: false, fieldId: 'set-stuck-ssr-time', error: t('val.stuck_time_range', `Stuck SSR Window must be between 3s and 120s (Entered: ${stuckTime}s).`, { min: 3, max: 120, val: stuckTime }) };
        }
        if (noRiseThresh !== undefined && (isNaN(noRiseThresh) || noRiseThresh < 0.5 || noRiseThresh > 50.0)) {
            return { valid: false, fieldId: 'set-norise-thresh', error: t('val.norise_thresh_range', `Heater No-Rise Threshold must be between 0.5°C and 50.0°C (Entered: ${Format.temp(noRiseThresh)}°C).`, { min: '0.5', max: '50.0', val: Format.temp(noRiseThresh) }) };
        }
        if (noRiseTime !== undefined && (isNaN(noRiseTime) || noRiseTime < 5 || noRiseTime > 120)) {
            return { valid: false, fieldId: 'set-norise-time', error: t('val.norise_time_range', `Heater No-Rise Timeout must be between 5s and 120s (Entered: ${noRiseTime}s).`, { min: 5, max: 120, val: noRiseTime }) };
        }
        if (holdLow !== undefined && (isNaN(holdLow) || holdLow < 0.5 || holdLow > 30.0)) {
            return { valid: false, fieldId: 'set-hold-low-tol', error: t('val.hold_low_range', `Hold Lower Tolerance must be between 0.5°C and 30.0°C (Entered: ${Format.temp(holdLow)}°C).`, { min: '0.5', max: '30.0', val: Format.temp(holdLow) }) };
        }
        if (holdHigh !== undefined && (isNaN(holdHigh) || holdHigh < 0.5 || holdHigh > 30.0)) {
            return { valid: false, fieldId: 'set-hold-high-tol', error: t('val.hold_high_range', `Hold Upper Tolerance must be between 0.5°C and 30.0°C (Entered: ${Format.temp(holdHigh)}°C).`, { min: '0.5', max: '30.0', val: Format.temp(holdHigh) }) };
        }
        if (settleS !== undefined && (isNaN(settleS) || settleS < VALIDATION_LIMITS.MIN_SETTLE_S || settleS > VALIDATION_LIMITS.MAX_SETTLE_S)) {
            return { valid: false, fieldId: 'set-settle-time-s', error: t('val.settle_range', `Settle Time must be between ${VALIDATION_LIMITS.MIN_SETTLE_S}s and ${VALIDATION_LIMITS.MAX_SETTLE_S}s (Entered: ${settleS}s).`, { min: VALIDATION_LIMITS.MIN_SETTLE_S, max: VALIDATION_LIMITS.MAX_SETTLE_S, val: settleS }) };
        }
        if (fanDelay !== undefined && (isNaN(fanDelay) || fanDelay < 0 || fanDelay > 300)) {
            return { valid: false, fieldId: 'set-fan-delay', error: t('val.fan_delay_range', `Fan Cooling Delay must be between 0s and 300s (Entered: ${fanDelay}s).`, { min: 0, max: 300, val: fanDelay }) };
        }
        if (fanDur !== undefined && (isNaN(fanDur) || fanDur < 10 || fanDur > 600)) {
            return { valid: false, fieldId: 'set-fan-duration', error: t('val.fan_dur_range', `Fan Cooling Duration must be between 10s and 600s (Entered: ${fanDur}s).`, { min: 10, max: 600, val: fanDur }) };
        }
        if (burstTop !== undefined && (isNaN(burstTop) || burstTop < VALIDATION_LIMITS.MIN_BURST_WINDOW_MS || burstTop > VALIDATION_LIMITS.MAX_BURST_WINDOW_MS)) {
            return { valid: false, fieldId: 'set-burst-top', error: t('val.burst_top_range', `Top Burst Window must be between ${VALIDATION_LIMITS.MIN_BURST_WINDOW_MS}ms and ${VALIDATION_LIMITS.MAX_BURST_WINDOW_MS}ms (Entered: ${burstTop}ms).`, { min: VALIDATION_LIMITS.MIN_BURST_WINDOW_MS, max: VALIDATION_LIMITS.MAX_BURST_WINDOW_MS, val: burstTop }) };
        }
        if (burstBot !== undefined && (isNaN(burstBot) || burstBot < VALIDATION_LIMITS.MIN_BURST_WINDOW_MS || burstBot > VALIDATION_LIMITS.MAX_BURST_WINDOW_MS)) {
            return { valid: false, fieldId: 'set-burst-bottom', error: t('val.burst_bot_range', `Bottom Burst Window must be between ${VALIDATION_LIMITS.MIN_BURST_WINDOW_MS}ms and ${VALIDATION_LIMITS.MAX_BURST_WINDOW_MS}ms (Entered: ${burstBot}ms).`, { min: VALIDATION_LIMITS.MIN_BURST_WINDOW_MS, max: VALIDATION_LIMITS.MAX_BURST_WINDOW_MS, val: burstBot }) };
        }
        if (alpha !== undefined && (isNaN(alpha) || alpha < 0.01 || alpha > 1.0)) {
            return { valid: false, fieldId: 'set-filter-alpha', error: t('val.alpha_range', `EMA Filter Alpha must be between 0.01 and 1.0 (Entered: ${Format.alpha(alpha)}).`, { min: '0.01', max: '1.0', val: Format.alpha(alpha) }) };
        }
        if (topCj !== undefined && (isNaN(topCj) || topCj < VALIDATION_LIMITS.MIN_CJ_OFFSET || topCj > VALIDATION_LIMITS.MAX_CJ_OFFSET)) {
            return { valid: false, fieldId: 'set-cj-top-offset', error: t('val.cj_offset_range', `Top CJ Offset must be between ${Format.temp(VALIDATION_LIMITS.MIN_CJ_OFFSET)}°C and +${Format.temp(VALIDATION_LIMITS.MAX_CJ_OFFSET)}°C (Entered: ${Format.temp(topCj)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_CJ_OFFSET), max: '+' + Format.temp(VALIDATION_LIMITS.MAX_CJ_OFFSET), val: Format.temp(topCj) }) };
        }
        if (botCj !== undefined && (isNaN(botCj) || botCj < VALIDATION_LIMITS.MIN_CJ_OFFSET || botCj > VALIDATION_LIMITS.MAX_CJ_OFFSET)) {
            return { valid: false, fieldId: 'set-cj-bottom-offset', error: t('val.cj_offset_range', `Bottom CJ Offset must be between ${Format.temp(VALIDATION_LIMITS.MIN_CJ_OFFSET)}°C and +${Format.temp(VALIDATION_LIMITS.MAX_CJ_OFFSET)}°C (Entered: ${Format.temp(botCj)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_CJ_OFFSET), max: '+' + Format.temp(VALIDATION_LIMITS.MAX_CJ_OFFSET), val: Format.temp(botCj) }) };
        }

        const topKp = data.topKp;
        const topKi = data.topKi;
        const topKd = data.topKd;
        const botKp = data.bottomKp;
        const botKi = data.bottomKi;
        const botKd = data.bottomKd;

        if (topKp !== undefined && (isNaN(topKp) || topKp < VALIDATION_LIMITS.MIN_PID_KP || topKp > VALIDATION_LIMITS.MAX_PID_KP)) {
            return { valid: false, fieldId: 'set-top-kp', error: t('val.pid_kp_out_of_range', `Top PID Kp (${Format.kp(topKp)}) is out of bounds (0.00–100.00).`, { heater: 'Top', step: 1, val: Format.kp(topKp), min: '0.00', max: '100.00' }) };
        }
        if (topKi !== undefined && (isNaN(topKi) || topKi < VALIDATION_LIMITS.MIN_PID_KI || topKi > VALIDATION_LIMITS.MAX_PID_KI)) {
            return { valid: false, fieldId: 'set-top-ki', error: t('val.pid_ki_out_of_range', `Top PID Ki (${Format.ki(topKi)}) is out of bounds (0.000–10.000).`, { heater: 'Top', step: 1, val: Format.ki(topKi), min: '0.000', max: '10.000' }) };
        }
        if (topKd !== undefined && (isNaN(topKd) || topKd < VALIDATION_LIMITS.MIN_PID_KD || topKd > VALIDATION_LIMITS.MAX_PID_KD)) {
            return { valid: false, fieldId: 'set-top-kd', error: t('val.pid_kd_out_of_range', `Top PID Kd (${Format.kd(topKd)}) is out of bounds (0.00–100.00).`, { heater: 'Top', step: 1, val: Format.kd(topKd), min: '0.00', max: '100.00' }) };
        }
        if (botKp !== undefined && (isNaN(botKp) || botKp < VALIDATION_LIMITS.MIN_PID_KP || botKp > VALIDATION_LIMITS.MAX_PID_KP)) {
            return { valid: false, fieldId: 'set-bottom-kp', error: t('val.pid_kp_out_of_range', `Bottom PID Kp (${Format.kp(botKp)}) is out of bounds (0.00–100.00).`, { heater: 'Bottom', step: 1, val: Format.kp(botKp), min: '0.00', max: '100.00' }) };
        }
        if (botKi !== undefined && (isNaN(botKi) || botKi < VALIDATION_LIMITS.MIN_PID_KI || botKi > VALIDATION_LIMITS.MAX_PID_KI)) {
            return { valid: false, fieldId: 'set-bottom-ki', error: t('val.pid_ki_out_of_range', `Bottom PID Ki (${Format.ki(botKi)}) is out of bounds (0.000–10.000).`, { heater: 'Bottom', step: 1, val: Format.ki(botKi), min: '0.000', max: '10.000' }) };
        }
        if (botKd !== undefined && (isNaN(botKd) || botKd < VALIDATION_LIMITS.MIN_PID_KD || botKd > VALIDATION_LIMITS.MAX_PID_KD)) {
            return { valid: false, fieldId: 'set-bottom-kd', error: t('val.pid_kd_out_of_range', `Bottom PID Kd (${Format.kd(botKd)}) is out of bounds (0.00–100.00).`, { heater: 'Bottom', step: 1, val: Format.kd(botKd), min: '0.00', max: '100.00' }) };
        }
        return { valid: true };
    }

    function validateProfilePayload(data) {
        if (!data || typeof data !== 'object' || Array.isArray(data)) {
            return { valid: false, error: t('val.invalid_json_format', 'Invalid JSON object format.') };
        }
        // Schema version guard – reject files from a newer firmware
        if (data.schemaVersion && typeof data.schemaVersion === 'number') {
            if (data.schemaVersion > SCHEMA_VERSIONS.REFLOW_PROFILE) {
                return { valid: false, error: t('val.incompatible_schema', 'Incompatible file: Schema version (v{{ver}}) is newer than supported controller firmware (v{{cur}}). Please update firmware first.', { ver: data.schemaVersion, cur: SCHEMA_VERSIONS.REFLOW_PROFILE }) };
            }
        }
        // Reject if it is machine settings or PID library
        if (data.maxTempTop !== undefined || data.holdLowTolerance !== undefined || data.topBurstWindowMs !== undefined || data.safetyLimits !== undefined) {
            return { valid: false, error: t('val.mismatched_settings', 'Invalid file: Machine Settings JSON cannot be imported as Reflow Profile.') };
        }
        if (Array.isArray(data.top) && Array.isArray(data.bottom) && data.stepsTop === undefined) {
            return { valid: false, error: t('val.mismatched_pid', 'Invalid file: PID Library JSON cannot be imported as Reflow Profile.') };
        }
        if (!data.name || typeof data.name !== 'string' || !data.name.trim()) {
            return { valid: false, error: t('val.missing_profile_name', 'Invalid profile: Missing profile name.') };
        }
        if (data.name.trim().length > 30) {
            return { valid: false, error: t('modal.profile_name_too_long', `Profile name exceeds limit of 30 characters (${data.name.trim().length} chars).`, { len: data.name.trim().length }) };
        }
        if (!Array.isArray(data.stepsTop) || !Array.isArray(data.stepsBottom)) {
            return { valid: false, error: t('val.missing_profile_steps', 'Invalid profile: Must contain stepsTop and stepsBottom arrays.') };
        }
        if (data.stepsTop.length === 0 && data.stepsBottom.length === 0) {
            return { valid: false, error: t('val.empty_profile_steps', 'Profile must contain at least one step.') };
        }
        if (data.stepsTop.length > VALIDATION_LIMITS.MAX_PROFILE_STEPS || data.stepsBottom.length > VALIDATION_LIMITS.MAX_PROFILE_STEPS) {
            return { valid: false, error: t('val.profile_steps_exceeded', 'Profile exceeds maximum of {{max}} steps per heater.', { max: VALIDATION_LIMITS.MAX_PROFILE_STEPS }) };
        }

        // Validate top steps (supports both 'temp' and 'targetTemp')
        for (let i = 0; i < data.stepsTop.length; i++) {
            const step = data.stepsTop[i];
            const temp = (typeof step.temp === 'number') ? step.temp : ((typeof step.targetTemp === 'number') ? step.targetTemp : null);
            const ramp = (typeof step.ramp === 'number') ? step.ramp : ((typeof step.rampRate === 'number') ? step.rampRate : null);
            const time = (typeof step.time === 'number') ? step.time : ((typeof step.holdTimeS === 'number') ? step.holdTimeS : null);

            if (temp === null || isNaN(temp) || temp < VALIDATION_LIMITS.MIN_TEMPERATURE || temp > VALIDATION_LIMITS.MAX_TEMPERATURE) {
                return { valid: false, error: t('val.step_temp_out_of_range', `Top step #${i + 1} Target Temp (${temp !== null ? Format.temp(temp) : 'invalid'}°C) is out of bounds (${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}–${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C).`, { heater: 'Top', step: i + 1, val: temp !== null ? Format.temp(temp) : 'invalid', min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE) }) };
            }
            if (ramp !== null && (isNaN(ramp) || ramp < VALIDATION_LIMITS.MIN_RAMP_RATE || ramp > VALIDATION_LIMITS.MAX_RAMP_RATE)) {
                return { valid: false, error: t('val.step_ramp_out_of_range', `Top step #${i + 1} Ramp Rate (${Format.ramp(ramp)}°C/s) is out of bounds (${Format.ramp(VALIDATION_LIMITS.MIN_RAMP_RATE)}–${Format.ramp(VALIDATION_LIMITS.MAX_RAMP_RATE)}°C/s).`, { heater: 'Top', step: i + 1, val: Format.ramp(ramp), min: Format.ramp(VALIDATION_LIMITS.MIN_RAMP_RATE), max: Format.ramp(VALIDATION_LIMITS.MAX_RAMP_RATE) }) };
            }
            if (time !== null && (isNaN(time) || time < 0 || time > VALIDATION_LIMITS.MAX_STEP_TIME_S)) {
                return { valid: false, error: t('val.step_time_out_of_range', `Top step #${i + 1} Hold Time (${time}s) exceeds maximum of ${VALIDATION_LIMITS.MAX_STEP_TIME_S}s.`, { heater: 'Top', step: i + 1, val: time, max: VALIDATION_LIMITS.MAX_STEP_TIME_S }) };
            }
        }
        // Validate bottom steps (supports both 'temp' and 'targetTemp')
        for (let i = 0; i < data.stepsBottom.length; i++) {
            const step = data.stepsBottom[i];
            const temp = (typeof step.temp === 'number') ? step.temp : ((typeof step.targetTemp === 'number') ? step.targetTemp : null);
            const ramp = (typeof step.ramp === 'number') ? step.ramp : ((typeof step.rampRate === 'number') ? step.rampRate : null);
            const time = (typeof step.time === 'number') ? step.time : ((typeof step.holdTimeS === 'number') ? step.holdTimeS : null);

            if (temp === null || isNaN(temp) || temp < VALIDATION_LIMITS.MIN_TEMPERATURE || temp > VALIDATION_LIMITS.MAX_TEMPERATURE) {
                return { valid: false, error: t('val.step_temp_out_of_range', `Bottom step #${i + 1} Target Temp (${temp !== null ? Format.temp(temp) : 'invalid'}°C) is out of bounds (${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}–${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C).`, { heater: 'Bottom', step: i + 1, val: temp !== null ? Format.temp(temp) : 'invalid', min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE) }) };
            }
            if (ramp !== null && (isNaN(ramp) || ramp < VALIDATION_LIMITS.MIN_RAMP_RATE || ramp > VALIDATION_LIMITS.MAX_RAMP_RATE)) {
                return { valid: false, error: t('val.step_ramp_out_of_range', `Bottom step #${i + 1} Ramp Rate (${Format.ramp(ramp)}°C/s) is out of bounds (${Format.ramp(VALIDATION_LIMITS.MIN_RAMP_RATE)}–${Format.ramp(VALIDATION_LIMITS.MAX_RAMP_RATE)}°C/s).`, { heater: 'Bottom', step: i + 1, val: Format.ramp(ramp), min: Format.ramp(VALIDATION_LIMITS.MIN_RAMP_RATE), max: Format.ramp(VALIDATION_LIMITS.MAX_RAMP_RATE) }) };
            }
            if (time !== null && (isNaN(time) || time < 0 || time > VALIDATION_LIMITS.MAX_STEP_TIME_S)) {
                return { valid: false, error: t('val.step_time_out_of_range', `Bottom step #${i + 1} Hold Time (${time}s) exceeds maximum of ${VALIDATION_LIMITS.MAX_STEP_TIME_S}s.`, { heater: 'Bottom', step: i + 1, val: time, max: VALIDATION_LIMITS.MAX_STEP_TIME_S }) };
            }
        }
        return { valid: true };
    }

    function validatePidLibraryPayload(data) {
        if (!data || typeof data !== 'object' || Array.isArray(data)) {
            return { valid: false, error: t('val.invalid_json_format', 'Invalid JSON object format.') };
        }
        // Schema version guard – reject files from a newer firmware
        if (data.schemaVersion && typeof data.schemaVersion === 'number') {
            if (data.schemaVersion > SCHEMA_VERSIONS.PID_LIBRARY) {
                return { valid: false, error: t('val.incompatible_schema', 'Incompatible file: Schema version (v{{ver}}) is newer than supported controller firmware (v{{cur}}). Please update firmware first.', { ver: data.schemaVersion, cur: SCHEMA_VERSIONS.PID_LIBRARY }) };
            }
        }
        if (data.maxTempTop !== undefined || data.holdLowTolerance !== undefined || data.topBurstWindowMs !== undefined) {
            return { valid: false, error: t('val.mismatched_settings', 'Invalid file: Machine Settings JSON cannot be imported as PID Library.') };
        }
        if (data.stepsTop !== undefined || data.stepsBottom !== undefined) {
            return { valid: false, error: t('val.mismatched_profile', 'Invalid file: Reflow Profile JSON cannot be imported as PID Library.') };
        }
        if (!Array.isArray(data.top) || !Array.isArray(data.bottom)) {
            return { valid: false, error: t('val.missing_pid_arrays', 'Invalid PID Library: Must contain top and bottom point arrays.') };
        }
        if (data.top.length === 0 && data.bottom.length === 0) {
            return { valid: false, error: t('val.missing_pid_arrays', 'Invalid PID Library: Must contain at least one point.') };
        }
        if (data.top.length > VALIDATION_LIMITS.MAX_PID_POINTS || data.bottom.length > VALIDATION_LIMITS.MAX_PID_POINTS) {
            return { valid: false, error: t('val.pid_points_exceeded', 'PID library exceeds maximum of {{max}} points per heater.', { max: VALIDATION_LIMITS.MAX_PID_POINTS }) };
        }
        for (let i = 0; i < data.top.length; i++) {
            const pt = data.top[i];
            if (typeof pt.temp !== 'number' || isNaN(pt.temp) || pt.temp < VALIDATION_LIMITS.MIN_TEMPERATURE || pt.temp > VALIDATION_LIMITS.MAX_TEMPERATURE) {
                return { valid: false, error: t('val.pid_temp_out_of_range', `Top PID point #${i + 1} Temp (${Format.temp(pt.temp)}°C) is out of bounds (${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}–${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C).`, { heater: 'Top', step: i + 1, val: Format.temp(pt.temp), min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE) }) };
            }
            if (typeof pt.kp !== 'number' || isNaN(pt.kp) || pt.kp < 0 || pt.kp > 100) {
                return { valid: false, error: t('val.pid_kp_out_of_range', `Top PID point #${i + 1} Kp (${Format.kp(pt.kp)}) is out of bounds (0.00–100.00).`, { heater: 'Top', step: i + 1, val: Format.kp(pt.kp), min: '0.00', max: '100.00' }) };
            }
            if (typeof pt.ki !== 'number' || isNaN(pt.ki) || pt.ki < 0 || pt.ki > 10) {
                return { valid: false, error: t('val.pid_ki_out_of_range', `Top PID point #${i + 1} Ki (${Format.ki(pt.ki)}) is out of bounds (0.000–10.000).`, { heater: 'Top', step: i + 1, val: Format.ki(pt.ki), min: '0.000', max: '10.000' }) };
            }
            if (typeof pt.kd !== 'number' || isNaN(pt.kd) || pt.kd < 0 || pt.kd > 100) {
                return { valid: false, error: t('val.pid_kd_out_of_range', `Top PID point #${i + 1} Kd (${Format.kd(pt.kd)}) is out of bounds (0.00–100.00).`, { heater: 'Top', step: i + 1, val: Format.kd(pt.kd), min: '0.00', max: '100.00' }) };
            }
        }
        for (let i = 0; i < data.bottom.length; i++) {
            const pt = data.bottom[i];
            if (typeof pt.temp !== 'number' || isNaN(pt.temp) || pt.temp < VALIDATION_LIMITS.MIN_TEMPERATURE || pt.temp > VALIDATION_LIMITS.MAX_TEMPERATURE) {
                return { valid: false, error: t('val.pid_temp_out_of_range', `Bottom PID point #${i + 1} Temp (${Format.temp(pt.temp)}°C) is out of bounds (${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}–${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C).`, { heater: 'Bottom', step: i + 1, val: Format.temp(pt.temp), min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE) }) };
            }
            if (typeof pt.kp !== 'number' || isNaN(pt.kp) || pt.kp < 0 || pt.kp > 100) {
                return { valid: false, error: t('val.pid_kp_out_of_range', `Bottom PID point #${i + 1} Kp (${Format.kp(pt.kp)}) is out of bounds (0.00–100.00).`, { heater: 'Bottom', step: i + 1, val: Format.kp(pt.kp), min: '0.00', max: '100.00' }) };
            }
            if (typeof pt.ki !== 'number' || isNaN(pt.ki) || pt.ki < 0 || pt.ki > 10) {
                return { valid: false, error: t('val.pid_ki_out_of_range', `Bottom PID point #${i + 1} Ki (${Format.ki(pt.ki)}) is out of bounds (0.000–10.000).`, { heater: 'Bottom', step: i + 1, val: Format.ki(pt.ki), min: '0.000', max: '10.000' }) };
            }
            if (typeof pt.kd !== 'number' || isNaN(pt.kd) || pt.kd < 0 || pt.kd > 100) {
                return { valid: false, error: t('val.pid_kd_out_of_range', `Bottom PID point #${i + 1} Kd (${Format.kd(pt.kd)}) is out of bounds (0.00–100.00).`, { heater: 'Bottom', step: i + 1, val: Format.kd(pt.kd), min: '0.00', max: '100.00' }) };
            }
        }
        return { valid: true };
    }

    async function syncStatusWithController() {
        try {
            const status = await apiRequest('/api/status', 'GET');
            if (status) {
                const formatV = (ver) => ver ? (String(ver).startsWith('v') ? ver : `v${ver}`) : '—';

                if (status.firmwareVersion) {
                    store.firmwareVersion = status.firmwareVersion;
                    const el = $('sys-fw-version');
                    if (el) el.textContent = formatV(status.firmwareVersion);
                }
                if (status.idfVersion) {
                    store.idfVersion = status.idfVersion;
                    const el = $('sys-idf-version');
                    if (el) el.textContent = formatV(status.idfVersion);
                }
                if (status.buildDate || status.buildTime) {
                    const el = $('sys-build-time');
                    if (el) el.textContent = `${status.buildDate || ''} ${status.buildTime || ''}`.trim() || '—';
                }

                // Warn user if firmware downgrade rejected newer configuration schema on flash
                if (status.schemaIncompatible && !store.schemaIncompatibleNotified) {
                    store.schemaIncompatibleNotified = true;
                    Modal.alert(
                        t('modal.schema_downgrade_msg', 'Configuration files from a newer firmware version were found on the controller. For safety reasons, these were not loaded and the controller is currently running with safe factory defaults.\n\nOptions:\n• Update back to the newer firmware to keep using your existing configuration.\n• Or restore your existing files (settings.json, profiles, etc.) from your backup to ensure a clean migration.\n• Or save your settings in the respective tab to overwrite them with the current defaults.'),
                        {
                            type: 'warning',
                            title: t('modal.schema_downgrade_title', '⚠️ Incompatible Configuration Data Detected (Downgrade)')
                        }
                    );
                }

                // Dynamically synchronize validation limits from controller firmware (Single Source of Truth)
                if (status.limits && typeof status.limits === 'object') {
                    for (const [k, v] of Object.entries(status.limits)) {
                        if (typeof v === 'number') {
                            VALIDATION_LIMITS[k] = Math.round((v + Number.EPSILON) * 1000) / 1000;
                        } else {
                            VALIDATION_LIMITS[k] = v;
                        }
                    }
                    console.log('[ReflowApp] Dynamic validation limits synced from firmware:', VALIDATION_LIMITS);
                }

                // Dynamically synchronize input step resolutions from controller firmware
                if (status.resolutions && typeof status.resolutions === 'object') {
                    for (const [k, v] of Object.entries(status.resolutions)) {
                        if (typeof v === 'number') {
                            VALIDATION_RESOLUTIONS[k] = Math.round((v + Number.EPSILON) * 1000) / 1000;
                        } else {
                            VALIDATION_RESOLUTIONS[k] = v;
                        }
                    }
                    console.log('[ReflowApp] Dynamic validation resolutions synced from firmware:', VALIDATION_RESOLUTIONS);
                }

                applyValidationLimitsToDOM();

                if (typeof status.backupTaken === 'boolean') {
                    store.backupTaken = status.backupTaken;
                    updateBackupStageVisibility();
                }
                // Auto-reconcile FSM state if controller is in BACKUP (State 8) but UI is not on backup tab
                if (status.state === 8 && store.currentTab !== 'backup') {
                    console.log('[ReflowApp] Auto-exiting BACKUP state because active tab is', store.currentTab);
                    apiRequest('/api/control', 'POST', { action: 'exitBackup' }).catch(() => {});
                }
            }
        } catch (err) {
            console.warn('[ReflowApp] Could not sync status with controller:', err);
        }
    }

    /**
     * Dynamically applies central validation boundaries & step resolutions to all DOM input elements.
     * Ensures any change in C++ backend limits or resolutions automatically updates all HTML inputs.
     */
    function applyValidationLimitsToDOM() {
        const setAttr = (id, min, max, step) => {
            const el = $(id);
            if (el) {
                if (min !== undefined && min !== null) el.min = String(min);
                if (max !== undefined && max !== null) el.max = String(max);
                if (step !== undefined && step !== null) el.step = String(step);
            }
        };

        // Settings inputs
        setAttr('set-max-top', VALIDATION_LIMITS.MIN_TEMPERATURE, VALIDATION_LIMITS.MAX_TEMPERATURE, VALIDATION_RESOLUTIONS.TEMPERATURE);
        setAttr('set-max-bottom', VALIDATION_LIMITS.MIN_TEMPERATURE, VALIDATION_LIMITS.MAX_TEMPERATURE, VALIDATION_RESOLUTIONS.TEMPERATURE);
        setAttr('set-filter-alpha', VALIDATION_LIMITS.MIN_EMA_ALPHA, VALIDATION_LIMITS.MAX_EMA_ALPHA, VALIDATION_RESOLUTIONS.FILTER_ALPHA);
        setAttr('set-cj-top-offset', VALIDATION_LIMITS.MIN_CJ_OFFSET, VALIDATION_LIMITS.MAX_CJ_OFFSET, VALIDATION_RESOLUTIONS.OFFSET_TEMP);
        setAttr('set-cj-bottom-offset', VALIDATION_LIMITS.MIN_CJ_OFFSET, VALIDATION_LIMITS.MAX_CJ_OFFSET, VALIDATION_RESOLUTIONS.OFFSET_TEMP);
        setAttr('set-stuck-ssr-thresh', VALIDATION_LIMITS.MIN_STUCK_SSR_RISE, VALIDATION_LIMITS.MAX_STUCK_SSR_RISE, VALIDATION_RESOLUTIONS.TEMPERATURE);
        setAttr('set-stuck-ssr-time', VALIDATION_LIMITS.MIN_STUCK_SSR_SEC, VALIDATION_LIMITS.MAX_STUCK_SSR_SEC, VALIDATION_RESOLUTIONS.TIME_SEC);
        setAttr('set-norise-thresh', VALIDATION_LIMITS.MIN_NO_RISE_THRESH, VALIDATION_LIMITS.MAX_NO_RISE_THRESH, VALIDATION_RESOLUTIONS.TEMPERATURE);
        setAttr('set-norise-time', VALIDATION_LIMITS.MIN_NO_RISE_TIMEOUT_S, VALIDATION_LIMITS.MAX_NO_RISE_TIMEOUT_S, VALIDATION_RESOLUTIONS.TIME_SEC);
        setAttr('set-cooling-safe-temp', VALIDATION_LIMITS.MIN_SAFE_COOLING_TEMP, VALIDATION_LIMITS.MAX_SAFE_COOLING_TEMP, VALIDATION_RESOLUTIONS.TEMPERATURE);
        setAttr('set-hold-low-tol', VALIDATION_LIMITS.MIN_HOLD_TOLERANCE, VALIDATION_LIMITS.MAX_HOLD_TOLERANCE, VALIDATION_RESOLUTIONS.TOLERANCE_TEMP);
        setAttr('set-hold-high-tol', VALIDATION_LIMITS.MIN_HOLD_TOLERANCE, VALIDATION_LIMITS.MAX_HOLD_TOLERANCE, VALIDATION_RESOLUTIONS.TOLERANCE_TEMP);
        setAttr('set-settle-time-s', VALIDATION_LIMITS.MIN_SETTLE_S, VALIDATION_LIMITS.MAX_SETTLE_S, VALIDATION_RESOLUTIONS.TIME_SEC);
        setAttr('set-fan-delay', VALIDATION_LIMITS.MIN_FAN_DELAY_S, VALIDATION_LIMITS.MAX_FAN_DELAY_S, VALIDATION_RESOLUTIONS.TIME_SEC);
        setAttr('set-fan-duration', VALIDATION_LIMITS.MIN_FAN_DURATION_S, VALIDATION_LIMITS.MAX_FAN_DURATION_S, VALIDATION_RESOLUTIONS.TIME_SEC);
        setAttr('set-burst-top', VALIDATION_LIMITS.MIN_BURST_WINDOW_MS, VALIDATION_LIMITS.MAX_BURST_WINDOW_MS, VALIDATION_RESOLUTIONS.TIME_MS);
        setAttr('set-burst-bottom', VALIDATION_LIMITS.MIN_BURST_WINDOW_MS, VALIDATION_LIMITS.MAX_BURST_WINDOW_MS, VALIDATION_RESOLUTIONS.TIME_MS);
        setAttr('set-top-kp', VALIDATION_LIMITS.MIN_PID_KP, VALIDATION_LIMITS.MAX_PID_KP, VALIDATION_RESOLUTIONS.PID_GAIN_KP);
        setAttr('set-top-ki', VALIDATION_LIMITS.MIN_PID_KI, VALIDATION_LIMITS.MAX_PID_KI, VALIDATION_RESOLUTIONS.PID_GAIN_KI);
        setAttr('set-top-kd', VALIDATION_LIMITS.MIN_PID_KD, VALIDATION_LIMITS.MAX_PID_KD, VALIDATION_RESOLUTIONS.PID_GAIN_KD);
        setAttr('set-bottom-kp', VALIDATION_LIMITS.MIN_PID_KP, VALIDATION_LIMITS.MAX_PID_KP, VALIDATION_RESOLUTIONS.PID_GAIN_KP);
        setAttr('set-bottom-ki', VALIDATION_LIMITS.MIN_PID_KI, VALIDATION_LIMITS.MAX_PID_KI, VALIDATION_RESOLUTIONS.PID_GAIN_KI);
        setAttr('set-bottom-kd', VALIDATION_LIMITS.MIN_PID_KD, VALIDATION_LIMITS.MAX_PID_KD, VALIDATION_RESOLUTIONS.PID_GAIN_KD);

        // Autotune and PID Preview inputs
        setAttr('tune-temp', VALIDATION_LIMITS.MIN_TEMPERATURE, VALIDATION_LIMITS.MAX_TEMPERATURE, VALIDATION_RESOLUTIONS.TEMPERATURE);
        setAttr('pid-preview-temp', VALIDATION_LIMITS.MIN_TEMPERATURE, VALIDATION_LIMITS.MAX_TEMPERATURE, VALIDATION_RESOLUTIONS.TEMPERATURE);
    }

    function updateBackupStageVisibility() {
        const stage = $('backup-orb-stage');
        const portal = $('backup-restore-portal');
        const backBtn = $('btn-backup-back-to-portal');

        if (store.backupTaken) {
            if (stage) stage.classList.add('u-hidden');
            if (portal) portal.classList.remove('u-hidden');
            if (backBtn) backBtn.classList.remove('u-hidden');
        } else {
            if (stage) {
                stage.classList.remove('u-hidden');
                stage.classList.remove('stage-exit');
            }
            if (portal) portal.classList.add('u-hidden');
            if (backBtn) backBtn.classList.add('u-hidden');
        }
    }

    async function backupFullSystemZip() {
        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            await Modal.alert(
                t('modal.backup_locked_msg', 'Reflow process or autotune is currently active: Backup and maintenance functions are only available in IDLE state.'),
                {
                    type: 'warning',
                    title: t('modal.backup_locked_title', '⚠️ Maintenance & Backup Locked: Process Active')
                }
            );
            return;
        }

        const orb = $('btn-backup-orb');
        if (orb) {
            orb.classList.remove('orb-dive');
            void orb.offsetWidth;
            orb.classList.add('orb-dive');
        }

        try {
            const filesToPack = [];

            // 1. Settings
            try {
                const settings = await apiRequest('/api/settings', 'GET');
                if (settings) {
                    filesToPack.push({
                        name: 'settings.json',
                        data: JSON.stringify(settings, null, 2)
                    });
                }
            } catch (err) {
                console.warn('[Backup] Could not fetch settings:', err);
            }

            // 2. PID Library
            try {
                const pidlib = await apiRequest('/api/pidlibrary', 'GET');
                if (pidlib) {
                    filesToPack.push({
                        name: 'pid_library.json',
                        data: JSON.stringify(pidlib, null, 2)
                    });
                }
            } catch (err) {
                console.warn('[Backup] Could not fetch pid library:', err);
            }

            // 3. All Profiles
            try {
                const profList = await apiRequest('/api/profiles', 'GET');
                if (Array.isArray(profList)) {
                    for (const profInfo of profList) {
                        const fileName = typeof profInfo === 'string' ? profInfo : profInfo.file;
                        if (!fileName) continue;
                        try {
                            const profData = await apiRequest(`/api/profiles/${encodeURIComponent(fileName)}`, 'GET');
                            if (profData) {
                                filesToPack.push({
                                    name: fileName,
                                    data: JSON.stringify(profData, null, 2)
                                });
                            }
                        } catch (pErr) {
                            console.warn(`[Backup] Could not fetch profile ${fileName}:`, pErr);
                        }
                    }
                }
            } catch (err) {
                console.warn('[Backup] Could not fetch profiles list:', err);
            }

            if (!filesToPack.length) {
                showBackupStatus(t('val.no_files_backup', 'No files found to backup from controller.'), true);
                return;
            }

            // Create ZIP Blob
            const zipBlob = createZipArchive(filesToPack);
            const now = new Date();
            const dateStr = now.toISOString().slice(0, 10);
            const timeStr = String(now.getHours()).padStart(2, '0') + String(now.getMinutes()).padStart(2, '0');
            const zipName = `reflow_backup_${dateStr}_${timeStr}.zip`;

            const url = URL.createObjectURL(zipBlob);
            const a = document.createElement('a');
            a.href = url;
            a.download = zipName;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);

            // Inform controller that backup was pulled in this instance
            try {
                await apiRequest('/api/backup/complete', 'POST');
            } catch (bErr) {
                console.warn('[Backup] Failed to notify controller of backup completion:', bErr);
            }
            store.backupTaken = true;

            const backBtn = $('btn-backup-back-to-portal');
            if (backBtn) backBtn.classList.remove('u-hidden');

            showBackupStatus(t('toast.backup_success', '✓ 1:1 LittleFS ZIP backup downloaded successfully ({{count}} files)!', { count: filesToPack.length }));

            // Animate Orb out and reveal Restore Card Grid Portal
            setTimeout(() => {
                const stage = $('backup-orb-stage');
                const portal = $('backup-restore-portal');
                if (stage) stage.classList.add('stage-exit');
                setTimeout(() => {
                    if (stage) stage.classList.add('u-hidden');
                    if (portal) portal.classList.remove('u-hidden');
                }, 400);
            }, 900);
        } catch (err) {
            console.error('[Backup] Backup ZIP failed:', err);
            showBackupStatus(t('toast.backup_failed', 'Failed to generate ZIP backup.'), true);
        } finally {
            setTimeout(() => {
                orb?.classList.remove('orb-dive');
            }, 1300);
        }
    }

    function initBackupState() {
        updateBackupStageVisibility();
    }

    function showBackupRestorePortal() {
        const stage = $('backup-orb-stage');
        const portal = $('backup-restore-portal');
        if (stage) stage.classList.add('u-hidden');
        if (portal) portal.classList.remove('u-hidden');
    }

    function showBackupOrbStage() {
        const stage = $('backup-orb-stage');
        const portal = $('backup-restore-portal');
        const orb = $('btn-backup-orb');
        const backBtn = $('btn-backup-back-to-portal');

        if (portal) portal.classList.add('u-hidden');
        if (stage) {
            stage.classList.remove('u-hidden');
            void stage.offsetWidth;
            stage.classList.remove('stage-exit');
        }
        if (orb) orb.classList.remove('orb-dive');
        if (backBtn && store.backupTaken) {
            backBtn.classList.remove('u-hidden');
        }
    }

    function readJsonFile(file, callback) {
        if (!file) return;
        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            Modal.alert(
                t('modal.backup_locked_msg', 'Reflow process or autotune is currently active: Backup and maintenance functions are only available in IDLE state.'),
                {
                    type: 'warning',
                    title: t('modal.backup_locked_title', '⚠️ Maintenance & Backup Locked: Process Active')
                }
            );
            return;
        }
        if (file.size > VALIDATION_LIMITS.MAX_JSON_SIZE_BYTES) {
            const err = t('val.file_too_large', `File size (${Math.round(file.size / 1024)} KB) exceeds limit of ${Math.round(VALIDATION_LIMITS.MAX_JSON_SIZE_BYTES / 1024)} KB.`);
            showBackupStatus(err, true);
            Modal.alert(err, {
                type: 'warning',
                title: t('modal.upload_failed_title', '⚠️ Upload Failed')
            });
            return;
        }
        const reader = new FileReader();
        reader.onload = async (e) => {
            try {
                const parsed = JSON.parse(e.target.result);
                callback(parsed);
            } catch (err) {
                console.error('[Restore] Parse error:', err);
                const syntaxErr = t('val.invalid_json_syntax', 'Invalid JSON syntax in file.');
                showBackupStatus(syntaxErr, true);
                await Modal.alert(syntaxErr, {
                    type: 'danger',
                    title: t('modal.invalid_json_title', '⚠️ Invalid JSON File')
                });
            }
        };
        reader.readAsText(file);
    }

    function restorePidLibraryFromFile(event) {
        const file = event.target.files[0];
        if (!file) return;
        readJsonFile(file, async (data) => {
            const val = validatePidLibraryPayload(data);
            if (!val.valid) {
                showBackupStatus(val.error, true);
                await Modal.alert(val.error, {
                    type: 'warning',
                    title: t('modal.invalid_pid_title', '⚠️ Invalid PID Library')
                });
                event.target.value = '';
                return;
            }
            const ok = await Modal.confirm(
                t('modal.restore_pid_confirm', 'Restore PID Library with {{top}} top and {{bottom}} bottom points?', {
                    top: data.top?.length || 0,
                    bottom: data.bottom?.length || 0
                }), {
                    type: 'warning',
                    title: t('modal.restore_pid_title', 'Restore PID Library'),
                    confirmLabel: t('modal.btn_restore', 'Restore'),
                    cancelLabel: t('modal.btn_cancel', 'Cancel')
                }
            );
            if (ok) {
                try {
                    const res = await apiRequest('/api/pidlibrary', 'POST', data);
                    if (res && res.success !== false) {
                        showBackupStatus(t('toast.pid_restored', '✓ PID Library restored successfully!'));
                        await Modal.alert(t('toast.pid_restored', '✓ PID Library restored successfully!'), {
                            type: 'success',
                            title: t('modal.restore_success_title', 'Restore Successful')
                        });
                        await loadPidLibraryFromController();
                    } else {
                        const errMsg = (res && res.error) ? res.error : t('modal.restore_pid_failed_msg', 'Failed to restore PID Library to controller.');
                        showBackupStatus(errMsg, true);
                        await Modal.alert(errMsg, {
                            type: 'danger',
                            title: t('modal.restore_failed_title', 'Restore Failed')
                        });
                    }
                } catch (err) {
                    console.error('[Restore] Failed to restore PID library:', err);
                    const errMsg = t('modal.restore_pid_failed_msg', 'Failed to restore PID Library to controller.');
                    showBackupStatus(errMsg, true);
                    await Modal.alert(errMsg, {
                        type: 'danger',
                        title: t('modal.restore_failed_title', 'Restore Failed')
                    });
                }
            }
            event.target.value = '';
        });
    }

    function restoreSettingsFromFile(event) {
        const file = event.target.files[0];
        if (!file) return;
        readJsonFile(file, async (data) => {
            const val = validateSettingsPayload(data);
            if (!val.valid) {
                showBackupStatus(val.error, true);
                await Modal.alert(val.error, {
                    type: 'warning',
                    title: t('modal.invalid_settings_title', '⚠️ Invalid Machine Setting')
                });
                event.target.value = '';
                return;
            }
            const ok = await Modal.confirm(
                t('modal.restore_settings_confirm', 'Restore Machine Settings from selected file?'), {
                    type: 'warning',
                    title: t('modal.restore_settings_title', 'Restore Settings'),
                    confirmLabel: t('modal.btn_restore', 'Restore'),
                    cancelLabel: t('modal.btn_cancel', 'Cancel')
                }
            );
            if (ok) {
                try {
                    const res = await apiRequest('/api/settings', 'POST', data);
                    if (res && res.success !== false) {
                        showBackupStatus(t('toast.settings_restored', '✓ Machine Settings restored successfully!'));
                        await Modal.alert(t('toast.settings_restored', '✓ Machine Settings restored successfully!'), {
                            type: 'success',
                            title: t('modal.restore_success_title', 'Restore Successful')
                        });
                        await loadSettingsFromController();
                    } else {
                        const errMsg = (res && res.error) ? res.error : t('modal.restore_settings_failed_msg', 'Failed to restore machine settings to controller.');
                        showBackupStatus(errMsg, true);
                        await Modal.alert(errMsg, {
                            type: 'danger',
                            title: t('modal.restore_failed_title', 'Restore Failed')
                        });
                    }
                } catch (err) {
                    console.error('[Restore] Failed to restore settings:', err);
                    const errMsg = t('modal.restore_settings_failed_msg', 'Failed to restore machine settings to controller.');
                    showBackupStatus(errMsg, true);
                    await Modal.alert(errMsg, {
                        type: 'danger',
                        title: t('modal.restore_failed_title', 'Restore Failed')
                    });
                }
            }
            event.target.value = '';
        });
    }

    function importProfileFromFile(event) {
        const file = event.target.files[0];
        if (!file) return;
        readJsonFile(file, async (data) => {
            const val = validateProfilePayload(data);
            if (!val.valid) {
                showBackupStatus(val.error, true);
                await Modal.alert(val.error, {
                    type: 'warning',
                    title: t('modal.invalid_profile_title', '⚠️ Invalid Profile')
                });
                event.target.value = '';
                return;
            }
            if (!data.file) {
                data.file = data.name.toLowerCase().replace(/[^a-z0-9_-]/g, '_') + '.json';
            }
            const ok = await Modal.confirm(
                t('modal.import_profile_confirm', 'Import Profile "{{name}}" ({{file}})?', {
                    name: data.name,
                    file: data.file
                }), {
                    type: 'info',
                    title: t('modal.import_profile_title', 'Import Profile'),
                    confirmLabel: t('modal.btn_import', 'Import'),
                    cancelLabel: t('modal.btn_cancel', 'Cancel')
                }
            );
            if (ok) {
                try {
                    const res = await apiRequest('/api/profiles', 'POST', data);
                    if (res && res.success !== false) {
                        invalidateProfileCache();
                        showBackupStatus(t('toast.profile_imported', '✓ Profile "{{name}}" imported successfully!', { name: data.name }));
                        await Modal.alert(t('toast.profile_imported', '✓ Profile "{{name}}" imported successfully!', { name: data.name }), {
                            type: 'success',
                            title: t('modal.profile_saved_title', 'Profile Imported')
                        });
                        await loadProfileList(data.file, true);
                        await populateChartProfileSelect();
                    } else {
                        const errMsg = (res && res.error) ? res.error : t('modal.import_profile_failed_msg', 'Failed to import profile to controller.');
                        showBackupStatus(errMsg, true);
                        await Modal.alert(errMsg, {
                            type: 'danger',
                            title: t('modal.import_failed_title', 'Import Failed')
                        });
                    }
                } catch (err) {
                    console.error('[Restore] Failed to import profile:', err);
                    const errMsg = t('modal.import_profile_failed_msg', 'Failed to import profile to controller.');
                    showBackupStatus(errMsg, true);
                    await Modal.alert(errMsg, {
                        type: 'danger',
                        title: t('modal.import_failed_title', 'Import Failed')
                    });
                }
            }
            event.target.value = '';
        });
    }

    async function restoreZipArchiveFromFile(event) {
        const file = event.target.files[0];
        if (!file) return;

        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            await Modal.alert(
                t('modal.backup_locked_msg', 'Reflow process or autotune is currently active: Backup and maintenance functions are only available in IDLE state.'),
                {
                    type: 'warning',
                    title: t('modal.backup_locked_title', '⚠️ Maintenance & Backup Locked: Process Active')
                }
            );
            event.target.value = '';
            return;
        }

        if (file.size > VALIDATION_LIMITS.MAX_ZIP_SIZE_BYTES) {
            const err = t('val.file_too_large', `File size (${Math.round(file.size / 1024)} KB) exceeds limit of ${Math.round(VALIDATION_LIMITS.MAX_ZIP_SIZE_BYTES / 1024)} KB.`);
            showBackupStatus(err, true);
            await Modal.alert(err, {
                type: 'warning',
                title: t('modal.upload_failed_title', '⚠️ ZIP Archive Too Large')
            });
            event.target.value = '';
            return;
        }

        const reader = new FileReader();
        reader.onload = async (e) => {
            try {
                const arrayBuffer = e.target.result;
                const extractedFiles = parseZipArchive(arrayBuffer);

                if (!extractedFiles.length) {
                    const emptyErr = t('val.empty_zip', 'ZIP archive is empty or invalid.');
                    showBackupStatus(emptyErr, true);
                    await Modal.alert(emptyErr, {
                        type: 'danger',
                        title: t('modal.invalid_zip_title', '⚠️ Invalid ZIP Archive')
                    });
                    return;
                }

                const ok = await Modal.confirm(
                    t('modal.restore_zip_confirm', 'Restore {{count}} files from "{{file}}" to the controller?', {
                        count: extractedFiles.length,
                        file: file.name
                    }), {
                        type: 'warning',
                        title: t('modal.restore_zip_title', 'Full ZIP Restore'),
                        confirmLabel: t('modal.btn_restore_all', 'Restore All'),
                        cancelLabel: t('modal.btn_cancel', 'Cancel')
                    }
                );
                if (!ok) {
                    event.target.value = '';
                    return;
                }

                let successCount = 0;
                let failCount = 0;

                for (const item of extractedFiles) {
                    const baseName = item.name.split('/').pop();
                    if (!baseName || !baseName.endsWith('.json')) continue;
                    try {
                        const json = JSON.parse(item.text);

                        if (baseName === 'settings.json') {
                            const val = validateSettingsPayload(json);
                            if (val.valid) {
                                const res = await apiRequest('/api/settings', 'POST', json);
                                if (res && res.success !== false) successCount++; else failCount++;
                            } else {
                                console.warn(`[Restore ZIP] Invalid settings.json: ${val.error}`);
                                failCount++;
                            }
                        } else if (baseName === 'pid_library.json') {
                            const val = validatePidLibraryPayload(json);
                            if (val.valid) {
                                const res = await apiRequest('/api/pidlibrary', 'POST', json);
                                if (res && res.success !== false) successCount++; else failCount++;
                            } else {
                                console.warn(`[Restore ZIP] Invalid pid_library.json: ${val.error}`);
                                failCount++;
                            }
                        } else {
                            const val = validateProfilePayload(json);
                            if (val.valid) {
                                if (!json.file) json.file = baseName;
                                const res = await apiRequest('/api/profiles', 'POST', json);
                                if (res && res.success !== false) successCount++; else failCount++;
                            } else {
                                console.warn(`[Restore ZIP] Invalid profile ${baseName}: ${val.error}`);
                                failCount++;
                            }
                        }
                    } catch (itemErr) {
                        console.error(`[Restore] Error processing ${item.name}:`, itemErr);
                        failCount++;
                    }
                }

                invalidateProfileCache();
                await loadSettingsFromController();
                await loadPidLibraryFromController();
                await loadProfileList(null, true);
                await populateChartProfileSelect();

                if (failCount > 0) {
                    const warnMsg = t('toast.restore_zip_warning', 'Restored {{count}} files, but {{skipped}} files were skipped or invalid.', {
                        count: successCount,
                        skipped: failCount
                    });
                    showBackupStatus(warnMsg, true);
                    await Modal.alert(warnMsg, {
                        type: 'warning',
                        title: t('modal.restore_partial_title', '⚠️ Restore Complete (With Warnings)')
                    });
                } else {
                    const succMsg = t('toast.restore_zip_complete', '✓ Full ZIP Restore Complete: {{count}} files restored successfully!', {
                        count: successCount,
                        skipped: ''
                    });
                    showBackupStatus(succMsg);
                    await Modal.alert(succMsg, {
                        type: 'success',
                        title: t('modal.restore_success_title', 'Restore Successful')
                    });
                }
            } catch (err) {
                console.error('[Restore] ZIP restore error:', err);
                const failMsg = t('toast.restore_zip_failed', 'Error restoring from ZIP archive.');
                showBackupStatus(failMsg, true);
                await Modal.alert(failMsg, {
                    type: 'danger',
                    title: t('modal.restore_failed_title', 'Restore Failed')
                });
            }
            event.target.value = '';
        };
        reader.readAsArrayBuffer(file);
    }

    // =========================================================================
    // 1-Click Web OTA Firmware Update
    // =========================================================================
    let selectedOtaFile = null;

    async function onOtaFileSelected(files) {
        if (!files || files.length === 0) return;
        const file = files[0];
        const input = $('ota-file-input');

        if (!file.name.toLowerCase().endsWith('.bin')) {
            if (input) input.value = '';
            Modal.alert(t('ota.error_invalid_file', 'Please select a valid .bin firmware binary file.'), {
                type: 'warning',
                title: t('modal.invalid_file_type', 'Invalid File Type')
            });
            return;
        }

        // Safety state check: only allow in IDLE (0), DONE (5), BACKUP (8)
        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            if (input) input.value = '';
            await Modal.alert(
                t('ota.error_locked', 'Firmware update locked: A reflow process or autotune is currently active. Please return to IDLE state first.'),
                {
                    type: 'warning',
                    title: t('modal.backup_locked_title', '⚠️ Process Active')
                }
            );
            return;
        }

        const sizeStr = `${(file.size / (1024 * 1024)).toFixed(2)} MB`;
        const confirmed = await Modal.confirm(
            t('ota.confirm_msg', 'Are you sure you want to flash "{{file}}" ({{size}})? The controller will automatically restart after flashing.')
                .replace('{{file}}', file.name)
                .replace('{{size}}', sizeStr),
            {
                type: 'warning',
                title: t('ota.confirm_title', '⚡ Confirm Firmware Update'),
                confirmText: t('ota.btn_flash', 'Flash Firmware'),
                cancelText: t('ota.btn_cancel', 'Cancel')
            }
        );

        if (!confirmed) {
            if (input) input.value = '';
            return;
        }

        selectedOtaFile = file;
        startOtaUpload();
    }

    function cancelOtaSelection() {
        selectedOtaFile = null;
        const input = $('ota-file-input');
        if (input) input.value = '';
        const progressEl = $('ota-progress-container');
        if (progressEl) progressEl.classList.add('u-hidden');
    }

    async function startOtaUpload() {
        if (!selectedOtaFile) return;

        const progressEl = $('ota-progress-container');
        const barEl = $('ota-progress-bar');
        const pctEl = $('ota-progress-pct');
        const statusEl = $('ota-progress-status');
        const btnUpload = $('btn-ota-upload-label');

        if (progressEl) progressEl.classList.remove('u-hidden');
        if (btnUpload) btnUpload.style.pointerEvents = 'none';
        if (barEl) {
            barEl.style.width = '0%';
            barEl.classList.remove('pulsing');
        }
        if (pctEl) pctEl.textContent = '0%';
        if (statusEl) statusEl.textContent = t('ota.status_uploading', 'Uploading firmware...');

        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/ota', true);
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');

        xhr.upload.onprogress = (evt) => {
            if (evt.lengthComputable) {
                const pct = Math.round((evt.loaded / evt.total) * 100);
                if (barEl) barEl.style.width = `${pct}%`;
                if (pctEl) pctEl.textContent = `${pct}%`;
                if (pct >= 100 && statusEl) {
                    statusEl.textContent = t('ota.status_flashing', 'Flashing OTA partition...');
                    if (barEl) barEl.classList.add('pulsing');
                }
            }
        };

        xhr.onload = async () => {
            if (xhr.status >= 200 && xhr.status < 300) {
                if (statusEl) statusEl.textContent = t('ota.status_rebooting', 'Update successful! Rebooting controller...');
                if (barEl) barEl.style.width = '100%';
                if (pctEl) pctEl.textContent = '100%';

                let countdown = 8;
                Modal.show({
                    title: t('ota.status_rebooting', 'Rebooting...'),
                    message: t('ota.reconnect_msg', 'Controller is rebooting... Reconnecting in {{sec}}s...').replace('{{sec}}', countdown),
                    type: 'info',
                    buttons: []
                });

                const timer = setInterval(() => {
                    countdown--;
                    const msgEl = document.querySelector('.modal-body p');
                    if (msgEl) {
                        msgEl.textContent = t('ota.reconnect_msg', 'Controller is rebooting... Reconnecting in {{sec}}s...').replace('{{sec}}', Math.max(0, countdown));
                    }
                    if (countdown <= 0) {
                        clearInterval(timer);
                        // Poll /api/status until device is back online
                        pollAfterReboot();
                    }
                }, 1000);
            } else {
                if (btnUpload) btnUpload.style.pointerEvents = 'auto';
                const input = $('ota-file-input');
                if (input) input.value = '';
                let errMsg = xhr.statusText || 'OTA upload failed';
                try {
                    const resp = JSON.parse(xhr.responseText);
                    if (resp.message) errMsg = resp.message;
                } catch (_) {}
                Modal.alert(`Error (${xhr.status}): ${errMsg}`, {
                    type: 'error',
                    title: 'OTA Update Failed'
                });
            }
        };

        xhr.onerror = () => {
            if (btnUpload) btnUpload.style.pointerEvents = 'auto';
            const input = $('ota-file-input');
            if (input) input.value = '';
            Modal.alert('Network error during firmware upload.', {
                type: 'error',
                title: 'Upload Error'
            });
        };

        xhr.send(selectedOtaFile);
    }

    async function pollAfterReboot() {
        let attempts = 0;
        const maxAttempts = 20;

        const checkInterval = setInterval(async () => {
            attempts++;
            try {
                const res = await fetch('/api/status', { cache: 'no-store' });
                if (res.ok) {
                    clearInterval(checkInterval);
                    Modal.closeAll();
                    await Modal.alert(
                        t('ota.success_msg', 'The new firmware was flashed successfully and the controller is back online!'),
                        {
                            type: 'success',
                            title: t('ota.success_title', '🎉 Firmware Update Complete')
                        }
                    );
                    window.location.reload();
                }
            } catch (_) {
                if (attempts >= maxAttempts) {
                    clearInterval(checkInterval);
                    Modal.closeAll();
                    window.location.reload();
                }
            }
        }, 1500);
    }

    return {
        store,
        STRINGS,
        init,
        setTheme,
        toggleTheme,
        toggleStatusLog,
        switchTab,
        toggleLamp,
        toggleFan,
        startReflow,
        startPreheat,
        stopReflow,
        skipStep,
        updateStatusBadge,
        updateActorIcons,
        updateTelemetryHUD,
        exportCSV,
        exportPNG,
        resetChart,
        toggleZones,
        toggleTalLine,
        toggleStepMarkers,
        togglePidGainsDisplay,
        loadSettingsFromController,
        saveSettingsToController,
        changeWifiPasswordFromSettings,
        checkWifiSecurity,
        showWifiSecurityModal,
        fetchProfiles,
        invalidateProfileCache,
        loadProfileList,
        loadProfileData,
        onProfileSelectChange,
        addProfileStep,
        removeProfileStep,
        updateProfileStepField,
        createNewProfile,
        saveProfileToController,
        deleteProfileFromController,
        populateChartProfileSelect,
        onChartProfileSelectChange,
        loadPidLibraryFromController,
        renderPidTable,
        updatePidField,
        addPidStep,
        removePidStep,
        savePidLibraryToController,
        backupPidLibrary,
        updatePidPreview,
        updatePidLibraryDisabledBanner,
        startAutotune,
        stopAutotune,
        backupFullSystemZip,
        showBackupOrbStage,
        showBackupRestorePortal,
        initBackupState,
        restoreZipArchiveFromFile,
        restorePidLibraryFromFile,
        restoreSettingsFromFile,
        importProfileFromFile,
        showBackupStatus,
        onOtaFileSelected,
        cancelOtaSelection,
        startOtaUpload,
        t,
        setLanguage,
        applyTranslations
    };
})();

// Global window bindings for HTML onclick
window.setTheme = ReflowApp.setTheme;
window.toggleTheme = ReflowApp.toggleTheme;
window.setLanguage = ReflowApp.setLanguage;
window.applyTranslations = ReflowApp.applyTranslations;
window.t = ReflowApp.t;
window.toggleStatusLog = ReflowApp.toggleStatusLog;
window.switchTab = ReflowApp.switchTab;
window.toggleLamp = ReflowApp.toggleLamp;
window.toggleFan = ReflowApp.toggleFan;
window.startReflow = ReflowApp.startReflow;
window.startPreheat = ReflowApp.startPreheat;
window.stopReflow = ReflowApp.stopReflow;
window.skipStep = ReflowApp.skipStep;
window.exportCSV = ReflowApp.exportCSV;
window.exportPNG = ReflowApp.exportPNG;
window.toggleZones = ReflowApp.toggleZones;
window.toggleTalLine = ReflowApp.toggleTalLine;
window.toggleStepMarkers = ReflowApp.toggleStepMarkers;
window.togglePidGainsDisplay = ReflowApp.togglePidGainsDisplay;
window.loadSettingsFromController = ReflowApp.loadSettingsFromController;
window.saveSettingsToController = ReflowApp.saveSettingsToController;
window.changeWifiPasswordFromSettings = ReflowApp.changeWifiPasswordFromSettings;
window.fetchProfiles = ReflowApp.fetchProfiles;
window.invalidateProfileCache = ReflowApp.invalidateProfileCache;
window.loadProfileList = ReflowApp.loadProfileList;
window.loadProfileData = ReflowApp.loadProfileData;
window.onProfileSelectChange = ReflowApp.onProfileSelectChange;
window.addProfileStep = ReflowApp.addProfileStep;
window.removeProfileStep = ReflowApp.removeProfileStep;
window.createNewProfile = ReflowApp.createNewProfile;
window.saveProfileToController = ReflowApp.saveProfileToController;
window.deleteProfileFromController = ReflowApp.deleteProfileFromController;
window.populateChartProfileSelect = ReflowApp.populateChartProfileSelect;
window.onChartProfileSelectChange = ReflowApp.onChartProfileSelectChange;
window.loadPidLibraryFromController = ReflowApp.loadPidLibraryFromController;
window.updatePidField = ReflowApp.updatePidField;
window.addPidStep = ReflowApp.addPidStep;
window.removePidStep = ReflowApp.removePidStep;
window.savePidLibraryToController = ReflowApp.savePidLibraryToController;
window.backupPidLibrary = ReflowApp.backupPidLibrary;
window.updatePidPreview = ReflowApp.updatePidPreview;
window.updatePidLibraryDisabledBanner = ReflowApp.updatePidLibraryDisabledBanner;
window.startAutotune = ReflowApp.startAutotune;
window.stopAutotune = ReflowApp.stopAutotune;
window.backupFullSystemZip = ReflowApp.backupFullSystemZip;
window.showBackupOrbStage = ReflowApp.showBackupOrbStage;
window.showBackupRestorePortal = ReflowApp.showBackupRestorePortal;
window.restoreZipArchiveFromFile = ReflowApp.restoreZipArchiveFromFile;
window.restorePidLibraryFromFile = ReflowApp.restorePidLibraryFromFile;
window.restoreSettingsFromFile = ReflowApp.restoreSettingsFromFile;
window.importProfileFromFile = ReflowApp.importProfileFromFile;
window.showBackupStatus = ReflowApp.showBackupStatus;
window.onOtaFileSelected = ReflowApp.onOtaFileSelected;
window.cancelOtaSelection = ReflowApp.cancelOtaSelection;
window.startOtaUpload = ReflowApp.startOtaUpload;

