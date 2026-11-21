/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 02_i18n.js
 * @brief Layer 1 Foundation: Localization dictionaries, string interpolation, and language switching.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

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

    DISCONNECTED: 'DISCONNECTED',
};

const DEFAULT_STRINGS = {
    'status.connecting': 'CONNECTING...',
    'status.connected': 'CONNECTED',
    'status.disconnected': 'DISCONNECTED',
    'status.badge_title': 'Click for System-Log',
    'status.log_disconnected': '✕ Controller disconnected. Waiting for connection...',
    'status.log_unknown_state': '⚠ Unknown controller state. Waiting for valid telemetry...',
    'status.log_all_ok': '✓ All systems operational (No faults recorded)',

    'hud.runtime_prefix': 'Runtime:',
    'hud.select_profile_placeholder': '— Select Profile —',
    'hud.safety_bypassed': '⚠️ SAFETY BYPASSED',
    'hud.safety_bypass_title':
        'Warning: Safety watchdog is disabled! Use at your own risk for testbenches only.',

    'state.0': 'IDLE',
    'state.1': 'PREHEAT',
    'state.2': 'SOAK',
    'state.3': 'REFLOW',
    'state.4': 'COOLING',
    'state.5': 'DONE',
    'state.6': 'FAULT',
    'state.7': 'AUTOTUNE',
    'state.8': 'BACKUP',
    'state.unknown': 'UNKNOWN',

    'ctrl.lamp_title': 'Worklight',
    'ctrl.lamp_on': 'Worklight (ON)',
    'ctrl.lamp_off': 'Worklight (OFF)',
    'ctrl.fan_title': 'Cooling Fan',
    'ctrl.fan_on': 'Cooling Fan (ON)',
    'ctrl.fan_off': 'Cooling Fan (OFF)',
    'ctrl.export_csv_title': 'Export CSV',
    'ctrl.export_png_title': 'Export PNG',
    'ctrl.tooltip_disconnected':
        '⚠️ Telemetry disconnected - starting a process is locked until connection is restored.',
    'ctrl.tooltip_disconnected_action':
        '⚠️ Controller disconnected – modifications are locked until connection is restored.',
    'ctrl.start_idle_hint': 'Step 2: Starts Reflow after preheating is complete',
    'ctrl.start_preheat_hint': 'Advance from Preheat to Reflow',
    'ctrl.start_preheat_done_hint': 'Preheat complete! Click START to begin Reflow profile',
    'ctrl.start_preheating_locked_hint':
        'Preheating in progress... START unlocks when preheat temperature is reached',
    'ctrl.skip_step_hint': 'Skip to the next profile step',
    'ctrl.skip_cooling_hint': 'End cooling phase and complete process',
    'ctrl.skip_preheat_done_hint': 'Preheat complete. Click START to begin Reflow.',
    'ctrl.skip_disabled_hint': 'Step skipping only available during active reflow phases',
    'ctrl.locked_done_hint': 'Process complete. Waiting for reset to IDLE...',
    'nav.backup_locked_hint': 'Backup is locked during active heating or autotune',

    'chart.axis_runtime': 'Runtime (min)',
    'chart.axis_temperature': 'Temperature (°C)',
    'chart.autotune_running': 'Autotuning...',
    'chart.ds_top_pv': 'Top PV',
    'chart.ds_top_sp': 'Top SP',
    'chart.ds_bot_pv': 'Bottom PV',
    'chart.ds_bot_sp': 'Bottom SP',
    'chart.tooltip_time': 'Time: {{time}} min ({{sec}}s)',
    'chart.step_badge': 'Step {{num}}',
    'chart.zone_preheat': 'PREHEAT (100-150°C)',
    'chart.zone_soak': 'SOAK (150-190°C)',
    'chart.zone_prereflow': 'PRE-REFLOW (190-217°C)',
    'chart.zone_reflow': 'REFLOW (217-245°C)',

    'profile.no_steps': 'No steps defined. Click "+ Add Step" below.',
    'profile.no_profile_selected':
        'No profile selected. Choose a profile from the dropdown above or click "+ New Profile".',
    'profile.select_placeholder': '— Select Profile to Edit —',
    'profile.no_profiles': 'No Profiles Available',
    'profile.delete_step_title': 'Delete Step',
    'profile.btn_new': '+ New Profile',

    'settings.opt_lang_en': 'English (Default)',
    'settings.opt_lang_de': 'Deutsch',
    'settings.lbl_sw_buzzer': 'Software Buzzer (Browser)',
    'settings.opt_no_default_profile': '— Not Selected (Default) —',
    'settings.lbl_safety_enabled': 'Safety Watchdog (Thermal & Fault Protection)',
    'settings.opt_safety_enabled': 'Enabled (Recommended)',
    'settings.opt_safety_disabled': 'Disabled (Testbench Only)',
    'settings.hint_safety_warning':
        '⚠️ Use at your own risk: Disabling bypasses thermal overtemp, sensor fault, and stuck-SSR watchdogs. For test environments and experiments only!',

    'tunepid.no_points': 'No PID points defined. Click "+ Add PID Point" below.',
    'tunepid.delete_point_title': 'Delete Point',

    'modal.ws_disconnected_title': 'Safety Interlock Active',
    'modal.ws_disconnected_msg':
        'Live telemetry stream is disconnected. Starting Reflow, Preheat, or Autotune is locked for safety until reconnection.',
    'modal.preheat_required_title': 'Preheat Required',
    'modal.preheat_required_body':
        'A BGA reflow cycle begins with preheating the PCB (bottom heater).\n\nWould you like to start Preheat now with profile "{{profile}}"?',
    'modal.btn_start_preheat': 'Start Preheat',
    'modal.confirm_stop_reflow_title': 'Stop Reflow?',
    'modal.confirm_stop_reflow_body':
        'Are you sure you want to stop the reflow process and enter cooldown?',
    'modal.confirm_stop_autotune_title': 'Stop Autotune?',
    'modal.confirm_stop_autotune_body': 'Are you sure you want to stop the running PID autotune?',
    'modal.no_profile_preheat': 'Please select a profile before starting Preheat.',
    'modal.no_profile_title': 'No Profile Selected',
    'modal.no_data_export': 'No telemetry data recorded yet to export.',
    'modal.no_data_title': 'No Data',
    'modal.invalid_profile_data': 'Invalid profile data.',
    'modal.invalid_profile_title': 'Invalid Profile',
    'modal.incomplete_profile_bottom': 'Bottom heater profile must contain at least 1 step.',
    'modal.incomplete_profile_top': 'Top heater profile must contain at least 1 step.',
    'modal.incomplete_profile_any': 'Profile must contain at least 1 step (Top or Bottom heater).',
    'modal.incomplete_profile_title': 'Incomplete Profile',
    'modal.profile_steps_limit_reached': 'Maximum of {{max}} steps per heater reached.',
    'modal.profile_steps_limit_title': 'Step Limit Reached',
    'modal.profile_steps_exceeded': 'Profile exceeds maximum limit of {{max}} steps per heater.',
    'modal.profile_saved': 'Profile "{{name}}" saved successfully!',
    'modal.profile_saved_title': 'Profile Saved',
    'modal.save_failed_title': 'Save Failed',
    'modal.profile_save_failed': 'Failed to save profile to controller.',
    'modal.factory_profile_protected': 'The factory default profile cannot be deleted.',
    'modal.factory_profile_readonly':
        "The factory default profile (factory-profile.json) is read-only and cannot be overwritten. Please create a new profile using '+ New Profile' or use a different filename.",
    'modal.factory_profile_readonly_title': 'Factory Profile Read-Only',
    'modal.protected_profile_title': 'Protected Profile',
    'modal.delete_profile_confirm': 'Are you sure you want to delete profile "{{file}}"?',
    'modal.delete_profile_title': 'Delete Profile',
    'modal.profile_deleted': 'Profile "{{file}}" deleted successfully.',
    'modal.profile_deleted_title': 'Profile Deleted',
    'modal.profile_delete_failed': 'Failed to delete profile.',
    'modal.delete_failed_title': 'Delete Failed',
    'modal.error_title': 'Error',
    'modal.pid_lib_saved': 'PID Library saved successfully to controller!',
    'modal.pid_lib_saved_title': 'PID Library Saved',
    'modal.pid_lib_save_failed': 'Failed to save PID Library to controller.',
    'modal.pid_points_limit_reached': 'Maximum of {{max}} PID points per heater reached.',
    'modal.pid_points_limit_title': 'Point Limit Reached',
    'modal.pid_points_exceeded': 'PID Library exceeds maximum limit of {{max}} points per heater.',
    'modal.empty_pid_points': 'PID Library must contain at least 1 point (Top or Bottom heater).',
    'modal.invalid_pid_title': 'Invalid PID Library',
    'modal.preheat_not_done_title': 'Preheating in Progress',
    'modal.preheat_not_done_msg':
        'Preheating is still in progress. Starting Reflow is locked until the bottom preheat temperature is reached and settled.',
    'modal.autotune_temp_required': 'Please enter a target temperature between 20°C and 300°C.',
    'modal.autotune_temp_required_title': 'Target Temperature Required',
    'modal.autotune_temp_invalid': 'Target temperature must be between 20°C and 300°C.',
    'modal.autotune_temp_invalid_title': 'Invalid Temperature Range',
    'modal.autotune_confirm': 'Start PID Autotune for {{heater}} heater at {{temp}}°C?',
    'modal.autotune_confirm_title': 'Start Autotune',
    'modal.autotune_cmd_sent': 'Autotune command sent to controller.',
    'modal.autotune_cmd_title': 'Autotune',
    'modal.autotune_start_error': 'Error starting Autotune.',
    'modal.autotune_start_error_title': 'Autotune Error',
    'modal.autotune_library_full':
        'PID library for {{heater}} heater is full ({{max}}/{{max}} points). Please delete an unneeded point in the table or tune an existing temperature.',
    'modal.autotune_library_full_title': '⚠️ PID Library Full',
    'modal.autotune_complete_body':
        "New PID point ({{temp}}°C: Kp={{kp}}, Ki={{ki}}, Kd={{kd}}) for {{heater}} heater was automatically saved to the controller's PID Library.\n\nYou can fine-tune these values in the table anytime; if edited manually, click 'Save PID Library' to store your changes.",
    'modal.autotune_complete_title': 'Autotune Complete & Saved!',
    'modal.autotune_complete_full_title': '⚠️ Autotune Completed (Not Saved)',
    'modal.autotune_complete_full_body':
        'Autotune finished for {{heater}} heater ({{temp}}°C: Kp={{kp}}, Ki={{ki}}, Kd={{kd}}).\n\n⚠️ However, the point could NOT be saved because the PID Library is at maximum capacity ({{max}}/{{max}} points). Please delete an existing point to free up space and apply these gains manually if desired.',
    'modal.autotune_aborted_title': '⚠️ PID Autotune Aborted',
    'modal.autotune_aborted_body':
        'PID Autotune for {{heater}} heater at {{target}}°C was aborted.\n\nReason: {{reason}}{{tip}}\n\nHeaters were immediately deactivated and cooling was initiated for safety.',
    'modal.autotune_abort_estop_reason':
        'Safety Emergency Stop: Temperature ({{curr}}°C) exceeded threshold ({{estop}}°C = target + 40°C).',
    'modal.autotune_abort_generic_reason':
        'Test aborted by safety watchdog or thermal limits.',
    'modal.autotune_abort_low_temp_tip':
        'Tip: At low target temperatures (e.g. ambient), heating inertia easily overshoots the +40°C safety cutoff. Tune at typical working temperatures (e.g. 100–220°C).',
    'modal.restore_pid_confirm':
        'Restore PID Library with {{top}} top and {{bottom}} bottom points?',
    'modal.restore_pid_title': 'Restore PID Library',
    'modal.restore_settings_confirm': 'Restore Machine Settings from selected file?',
    'modal.restore_settings_title': 'Restore Settings',
    'modal.import_profile_confirm': 'Import Profile "{{name}}" ({{file}})?',
    'modal.import_profile_title': 'Import Profile',
    'modal.restore_zip_confirm': 'Restore {{count}} files from "{{file}}" to the controller?',
    'modal.restore_zip_title': 'Full ZIP Restore',
    'modal.btn_restore': 'Restore',
    'modal.btn_restore_all': 'Restore All',
    'modal.btn_import': 'Import',
    'modal.btn_start': 'Start',
    'modal.prompt_new_profile': 'Enter a name for the new profile:',
    'modal.prompt_new_profile_title': 'New Profile',
    'modal.prompt_new_profile_ph': 'Custom Profile (max. 30 chars)',
    'modal.profile_name_too_long':
        'Profile name is too long ({{len}} chars). Maximum allowed is 30 characters.',
    'modal.profile_name_invalid_title': 'Invalid Profile Name',
    'modal.profile_name_empty': 'Profile name cannot be empty.',
    'modal.btn_ok': 'OK',
    'modal.btn_cancel': 'Cancel',
    'modal.btn_confirm': 'Confirm',
    'modal.btn_delete': 'Delete',
    'modal.btn_stop': 'Stop',
    'modal.upload_failed_title': '⚠️ Upload Failed',
    'modal.invalid_json_title': '⚠️ Invalid JSON File',
    'modal.invalid_zip_title': '⚠️ Invalid ZIP Archive',
    'modal.restore_failed_title': 'Restore Failed',
    'modal.restore_success_title': 'Restore Successful',
    'modal.restore_partial_title': '⚠️ Restore Complete (With Warnings)',
    'modal.import_failed_title': 'Import Failed',
    'modal.incompatible_schema_title': '⚠️ Incompatible File Version',
    'modal.restore_pid_failed_msg': 'Failed to restore PID Library to controller.',
    'modal.restore_settings_failed_msg': 'Failed to restore machine settings to controller.',
    'modal.import_profile_failed_msg': 'Failed to import profile to controller.',
    'modal.sec_title': '⚠️ Wi-Fi Security Notice',
    'modal.sec_msg':
        "The controller is running with the default Access Point password ('reflow123'). Please set a secure password or confirm keeping the default.",
    'modal.sec_btn_keep': 'Keep Default',
    'modal.sec_btn_change': 'Set Password',
    'modal.save_settings_locked_active':
        'Failed to save settings: Reflow process or autotune is currently active. Configuration changes are not permitted during an active heating process.',
    'modal.wifi_pass_locked_active':
        'Failed to update Wi-Fi password: A process or maintenance operation is currently active. Wi-Fi credentials can only be modified in IDLE state.',
    'modal.wifi_pass_confirm_title': 'Change Wi-Fi Password?',
    'modal.wifi_pass_confirm_msg':
        'Are you sure you want to change the Wi-Fi password?\n\nYour device will be disconnected immediately and you must reconnect using the new password.',
    'modal.wifi_pass_reconnect_title': '🔑 Password Changed – Please Reconnect',
    'modal.wifi_pass_reconnect_msg':
        "Wi-Fi password has been updated successfully!\n\nYour device has been disconnected from the controller.\nPlease open your device Wi-Fi settings and reconnect to 'BGA Reflow Controller' using your new password.",
    'btn.change_password': 'Change Password',
    'btn.updating_pass': 'Updating...',
    'modal.save_profile_locked_active':
        'Failed to save profile: Reflow process or autotune is currently active. Profile modifications are only permitted in IDLE state.',
    'modal.save_pid_locked_active':
        'Failed to save PID Library: Reflow process or autotune is currently active. PID modifications are not permitted during an active heating process.',
    'modal.delete_profile_locked_active':
        'Failed to delete profile: Reflow process or autotune is currently active. Profile deletions are only permitted in IDLE state.',
    'modal.delete_active_profile_title': '⚠️ Delete Running Profile?',
    'modal.delete_active_profile_confirm':
        "Profile '{{file}}' is currently active in the running reflow process. The current process will finish safely from RAM, but the profile will be deleted from storage and unavailable for future runs. Are you sure you want to delete it?",

    'toast.settings_saved': 'Machine Settings saved successfully to controller!',
    'toast.settings_error': 'Failed to save settings to controller.',
    'toast.profile_saved': "Profile '{{name}}' saved successfully.",
    'toast.profile_deleted': "Profile '{{name}}' deleted.",
    'toast.profile_loaded': "Profile '{{name}}' loaded.",
    'toast.autotune_started': 'Autotune started for {{heater}} at {{temp}}°C.',
    'toast.autotune_stopped': 'Autotune stopped.',
    'toast.pid_lib_saved': 'PID Library saved successfully.',
    'toast.pid_lib_error': 'Failed to save PID Library.',
    'toast.wifi_pass_updated':
        'Wi-Fi password updated successfully! Controller AP will restart with new credentials.',
    'toast.wifi_pass_short': 'Password must be at least 8 characters long.',
    'toast.wifi_pass_kept': 'Default Wi-Fi password confirmed.',
    'toast.backup_started': 'Creating backup...',
    'toast.backup_success': '✓ ZIP backup downloaded successfully ({{count}} files)!',
    'toast.backup_failed': 'Failed to generate ZIP backup.',
    'toast.restore_success': 'Restore successful! Reloading system...',
    'toast.restore_error': 'Restore failed.',
    'toast.pid_restored': '✓ PID Library restored successfully!',
    'toast.restore_pid_failed': 'Failed to restore PID Library.',
    'toast.settings_restored': '✓ Machine Settings restored successfully!',
    'toast.restore_settings_failed': 'Failed to restore settings.',
    'toast.profile_imported': '✓ Profile "{{name}}" imported successfully!',
    'toast.import_profile_failed': 'Failed to import profile.',
    'toast.restore_zip_complete':
        '✓ Full ZIP Restore Complete: {{count}} files restored successfully!{{skipped}}',
    'toast.restore_zip_failed': 'Error restoring from ZIP archive.',

    'backup.title': 'Backup & Restore',
    'backup.subtitle': 'Create a complete ZIP backup or restore individual system configurations.',
    'backup.system_info_title': 'System & Firmware Information',
    'backup.lbl_fw_version': 'Firmware Version',
    'backup.lbl_idf_version': 'ESP-IDF Framework',
    'backup.lbl_build_time': 'Build Date & Time',
    'backup.lbl_storage_usage': 'Used Flash Storage',
    'backup.status_success': 'Backup successful!',
    'backup.orb_title': 'Click the blue orb to download all files',
    'backup.orb_text': 'BACKUP',
    'backup.orb_hint': 'Click the blue orb to download all files',
    'backup.orb_locked_hint': 'Maintenance mode (BACKUP state) required to generate backup',
    'backup.session_saved': 'Active Instance: System backup verified & ready for maintenance',
    'backup.btn_create_another': '⟲ Download Fresh Backup',
    'backup.btn_back_to_portal': '← Back to Restore Options',
    'backup.card_zip_title': 'Full System ZIP Archive',
    'backup.card_zip_desc':
        'Restores all machine settings, PID libraries, and reflow profiles in a single atomic operation.',
    'backup.btn_upload_zip': 'Upload Backup',

    'backup.modular_title': 'Modular Component Management',

    'backup.card_settings_title': 'Machine Settings',
    'backup.card_settings_desc':
        'Restores system parameters, safety limits, SSR timings, and hardware defaults.',
    'backup.btn_upload_settings': 'Restore Settings',

    'backup.card_profile_title': 'Reflow Profile',
    'backup.card_profile_desc':
        'Imports or updates a single dual-heater temperature curve profile into flash memory.',
    'backup.btn_upload_profile': 'Import Profile',

    'backup.card_pid_title': 'PID Gain Library',
    'backup.card_pid_desc': 'Restores temperature-dependent Kp, Ki, Kd gain scheduling points.',
    'backup.btn_upload_pid': 'Restore PID Library',

    // OTA Firmware Update
    'ota.title': 'Firmware & System Update (OTA)',
    'ota.subtitle': 'Flash a new firmware binary to update both C++ firmware and web interface.',
    'ota.drop_main': 'Choose Firmware Binary (.bin)',
    'ota.drop_sub': 'or drag & drop file here',
    'ota.btn_flash': 'Flash Firmware',
    'ota.btn_cancel': 'Cancel',
    'ota.status_uploading': 'Uploading firmware...',
    'ota.status_flashing': 'Flashing OTA partition...',
    'ota.status_rebooting': 'Update successful! Rebooting controller...',
    'ota.warning_power': '⚠️ Do not turn off power or disconnect Wi-Fi while flashing!',
    'ota.confirm_title': '⚡ Confirm Firmware Update',
    'ota.confirm_msg':
        'Are you sure you want to flash "{{file}}" ({{size}})? The controller will automatically restart after flashing.',
    'ota.success_title': '🎉 Firmware Update Complete',
    'ota.success_msg':
        'The new firmware was flashed successfully and the controller is back online!',
    'ota.reconnect_msg': 'Controller is rebooting... Reconnecting in {{sec}}s...',
    'ota.error_invalid_file': 'Please select a valid .bin firmware binary file.',
    'ota.error_locked':
        'Firmware update locked: A reflow process or autotune is currently active. Please return to IDLE state first.',

    'modal.schema_downgrade_title': '⚠️ Incompatible Configuration Data Detected (Downgrade)',
    'modal.schema_downgrade_msg':
        'Configuration files from a newer firmware version were found on the controller. For safety reasons, these were not loaded and the controller is currently running with safe factory defaults.\n\nOptions:\n• Update back to the newer firmware to keep using your existing configuration.\n• Or restore your existing files (settings.json, profiles, etc.) from your backup to ensure a clean migration.\n• Or save your settings in the respective tab to overwrite them with the current defaults.',

    // Validation Errors
    'val.no_files_backup': 'No files found to backup from controller.',
    'val.empty_zip': 'ZIP archive is empty or invalid.',
    'val.no_restorable_files_zip':
        'ZIP archive contains no restorable user files (factory profile is read-only).',
    'modal.restore_empty_title': '⚠️ No Restorable Files',
    'val.invalid_json_syntax': 'Invalid JSON syntax in file.',
    'val.file_too_large': 'File exceeds maximum size limit ({{size}} KB).',
    'val.invalid_json': 'Invalid JSON syntax: Unable to parse file.',
    'val.mismatched_settings':
        'Invalid file: File contains profile or PID data instead of machine settings.',
    'val.mismatched_profile':
        'Invalid file: File contains machine settings or PID library instead of a reflow profile.',
    'val.mismatched_pid':
        'Invalid file: File contains machine settings or profile data instead of a PID library.',
    'val.invalid_temp_bounds':
        'Invalid temperature values: Must be between {{min}}°C and {{max}}°C.',
    'val.empty_profile_steps':
        'Profile curves must contain at least one step for Top and Bottom heaters.',
    'val.empty_pid_points': 'PID library must contain at least one valid PID point.',
    'val.incompatible_schema':
        'Incompatible file: Schema version (v{{ver}}) is newer than supported controller firmware (v{{cur}}). Please update firmware first.',
    'val.zip_no_valid_files': 'No valid controller configuration files found inside ZIP archive.',
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

    const text = window.LANG && window.LANG[key] !== undefined ? window.LANG[key] : defaultText;
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
        el.textContent =
            lang !== 'en' && window.LANG?.[key] ? window.LANG[key] : el.dataset.langDefault;
    });

    // 2. Input placeholders
    document.querySelectorAll('[data-lang-placeholder]').forEach(el => {
        const key = el.getAttribute('data-lang-placeholder');
        if (!el.dataset.langPlaceholderDefault) {
            el.dataset.langPlaceholderDefault = el.placeholder;
        }
        el.placeholder =
            lang !== 'en' && window.LANG?.[key]
                ? window.LANG[key]
                : el.dataset.langPlaceholderDefault;
    });

    // 3. Titles & Tooltips
    document.querySelectorAll('[data-lang-title]').forEach(el => {
        const key = el.getAttribute('data-lang-title');
        if (!el.dataset.langTitleDefault) {
            el.dataset.langTitleDefault = el.title;
        }
        el.title =
            lang !== 'en' && window.LANG?.[key] ? window.LANG[key] : el.dataset.langTitleDefault;
    });

    // 4. Update dynamic status badge text and title
    const badge = $('state-badge');
    if (badge) {
        badge.title = t('status.badge_title', 'Click for System-Log');
        updateConnectionBadge();
    }
    updateLogs(null);

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
    if (
        chartSel &&
        chartSel.options &&
        chartSel.options.length > 0 &&
        chartSel.options[0].value === ''
    ) {
        chartSel.options[0].textContent = t('hud.select_profile_placeholder', '— Select Profile —');
    }

    const profSel = $('profile-select');
    if (
        profSel &&
        profSel.options &&
        profSel.options.length > 0 &&
        profSel.options[0].value === ''
    ) {
        profSel.options[0].textContent = t(
            'profile.select_placeholder',
            '— Select Profile to Edit —'
        );
    }

    const defaultProfSel = $('set-default-profile');
    if (
        defaultProfSel &&
        defaultProfSel.options &&
        defaultProfSel.options.length > 0 &&
        defaultProfSel.options[0].value === ''
    ) {
        defaultProfSel.options[0].textContent = t(
            'settings.opt_no_default_profile',
            '— Not Selected (Default) —'
        );
    }

    // 9. Refresh Chart labels, axis titles and re-render
    if (chartInstance) {
        const scales = chartInstance.options?.scales;
        if (scales?.x?.title) scales.x.title.text = t('chart.axis_runtime', 'Runtime (min)');
        if (scales?.y?.title) scales.y.title.text = t('chart.axis_temperature', 'Temperature (°C)');

        const ds = chartInstance.data?.datasets;
        if (ds) {
            const labels = [
                t('chart.ds_top_pv', 'Top PV'),
                t('chart.ds_top_sp', 'Top SP'),
                t('chart.ds_bot_pv', 'Bottom PV'),
                t('chart.ds_bot_sp', 'Bottom SP'),
            ];
            ds.forEach((d, i) => {
                if (d && labels[i]) d.label = labels[i];
            });
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
    document.documentElement.lang = lang;

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
