/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 11_tab_settings.js
 * @brief Layer 4 Domain Controller: Machine settings form binding, Wi-Fi security, payload validation.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

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
            if ($('set-safety-enabled')) $('set-safety-enabled').value = (data.enableSafetyWatchdog !== false) ? 'true' : 'false';
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
            store.enableSafetyWatchdog = (data.enableSafetyWatchdog !== false);
            updatePidLibraryDisabledBanner();
            updateSafetyBypassBadge();

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
        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8 && store.stateEnum !== 6) {
            await Modal.alert(
                t('modal.save_settings_locked_active', 'Failed to save settings: Reflow process or autotune is currently active. Configuration changes are not permitted during an active heating process.'),
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

                enableSafetyWatchdog: $('set-safety-enabled')?.value !== 'false',
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
                bottomKd: Format.parseFloat($('set-bottom-kd')?.value, 1.0)
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
                store.enableSafetyWatchdog = payload.enableSafetyWatchdog !== false;
                updatePidLibraryDisabledBanner();
                updateSafetyBypassBadge();

                if (payload.theme && payload.theme !== store.currentTheme) {
                    setTheme(payload.theme);
                }
                if (payload.language && payload.language !== store.currentLanguage) {
                    setLanguage(payload.language);
                }
                await Modal.alert(t('toast.settings_saved', 'Machine Settings saved successfully to controller!'), { type: 'success', title: t('modal.profile_saved_title', 'Settings Saved') });
            } else {
                const errMsg = localizeApiError(res && res.error, 'toast.settings_error', 'Failed to save settings to controller.');
                await Modal.alert(errMsg, { type: 'danger', title: t('modal.save_failed_title', 'Save Failed') });
            }
        } catch (err) {
            console.error('[Settings] Save failed:', err);
            await Modal.alert(t('toast.settings_error', 'Failed to save settings to controller.'), { type: 'danger', title: t('modal.save_failed_title', 'Save Failed') });
        }
    }

    async function changeWifiPasswordFromSettings() {
        if (store.stateEnum !== 0 && store.stateEnum !== 5) {
            await Modal.alert(
                t('modal.wifi_pass_locked_active', 'Failed to update Wi-Fi password: A process or maintenance operation is currently active. Wi-Fi credentials can only be modified in IDLE state.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Process Active')
                }
            );
            return;
        }

        const passInput = $('set-wifi-new-pass');
        if (!passInput) return;
        const newPass = passInput.value.trim();
        if (newPass.length < 8) {
            await Modal.alert(t('toast.wifi_pass_short', 'Password must be at least 8 characters long.'), { type: 'warning', title: t('settings.sg_wifi', 'Wi-Fi Password') });
            return;
        }

        // 1. Confirmation Modal: "Really change password? Yes / Cancel"
        const confirmed = await Modal.confirm(
            t('modal.wifi_pass_confirm_msg', 'Are you sure you want to change the Wi-Fi password?\n\nYour device will be disconnected immediately and you must reconnect using the new password.'),
            {
                title: t('modal.wifi_pass_confirm_title', 'Change Wi-Fi Password?'),
                confirmText: t('btn.change_password', 'Change Password'),
                cancelText: t('btn.cancel', 'Cancel'),
                type: 'warning'
            }
        );
        if (!confirmed) return;

        // 2. Lock button & update text during active transmission
        const btnUpdate = $('btn-update-wifi-pass');
        if (btnUpdate) {
            btnUpdate.disabled = true;
            btnUpdate.textContent = t('btn.updating_pass', 'Updating...');
        }

        try {
            const res = await apiRequest('/api/security/wifi', 'POST', { password: newPass });
            passInput.value = '';
            if (btnUpdate) {
                btnUpdate.disabled = false;
                btnUpdate.textContent = t('settings.btn_wifi_update', 'Update Password');
            }

            if (res && res.success === false) {
                // If status === 0, the browser's fetch dropped because the ESP32 SoftAP restarted with the new password
                if (res.status === 0) {
                    await Modal.alert(
                        t('modal.wifi_pass_reconnect_msg', 'Wi-Fi password has been updated successfully!\n\nYour device has been disconnected from the controller.\nPlease open your device Wi-Fi settings and reconnect to \'BGA Reflow Controller\' using your new password.'),
                        {
                            type: 'success',
                            title: t('modal.wifi_pass_reconnect_title', '🔑 Password Changed – Please Reconnect')
                        }
                    );
                    return;
                }
                const errMsg = localizeApiError(res.error, 'toast.settings_error', 'Failed to update Wi-Fi password.');
                await Modal.alert(errMsg, { type: 'danger', title: t('modal.save_failed_title', 'Update Failed') });
                return;
            }

            // Success: Notify user to reconnect to the Access Point
            await Modal.alert(
                t('modal.wifi_pass_reconnect_msg', 'Wi-Fi password has been updated successfully!\n\nYour device has been disconnected from the controller.\nPlease open your device Wi-Fi settings and reconnect to \'BGA Reflow Controller\' using your new password.'),
                {
                    type: 'success',
                    title: t('modal.wifi_pass_reconnect_title', '🔑 Password Changed – Please Reconnect')
                }
            );
        } catch (err) {
            console.warn('[Settings] Wi-Fi password applied, network connection dropped as expected:', err);
            passInput.value = '';
            if (btnUpdate) {
                btnUpdate.disabled = false;
                btnUpdate.textContent = t('settings.btn_wifi_update', 'Update Password');
            }
            // Applying a new Wi-Fi password restarts the SoftAP immediately, severing active TCP sockets.
            // A network drop here is expected confirmation that the AP restarted with new credentials!
            await Modal.alert(
                t('modal.wifi_pass_reconnect_msg', 'Wi-Fi password has been updated successfully!\n\nYour device has been disconnected from the controller.\nPlease open your device Wi-Fi settings and reconnect to \'BGA Reflow Controller\' using your new password.'),
                {
                    type: 'success',
                    title: t('modal.wifi_pass_reconnect_title', '🔑 Password Changed – Please Reconnect')
                }
            );
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
                    if ((res && res.success !== false) || (res && res.status === 0)) {
                        await Modal.alert(
                            t('modal.wifi_pass_reconnect_msg', 'Wi-Fi password has been updated successfully!\n\nYour device has been disconnected from the controller.\nPlease open your device Wi-Fi settings and reconnect to \'BGA Reflow Controller\' using your new password.'),
                            { type: 'success', title: t('modal.wifi_pass_reconnect_title', '🔑 Password Changed – Please Reconnect') }
                        );
                    } else {
                        const errMsg = localizeApiError(res && res.error, 'toast.settings_error', 'Failed to update Wi-Fi password.');
                        await Modal.alert(
                            errMsg,
                            { type: 'danger', title: t('modal.save_failed_title', 'Update Failed') }
                        );
                    }
                } catch (err) {
                    console.warn('[Security] SoftAP restarted with new credentials:', err);
                    await Modal.alert(
                        t('modal.wifi_pass_reconnect_msg', 'Wi-Fi password has been updated successfully!\n\nYour device has been disconnected from the controller.\nPlease open your device Wi-Fi settings and reconnect to \'BGA Reflow Controller\' using your new password.'),
                        { type: 'success', title: t('modal.wifi_pass_reconnect_title', '🔑 Password Changed – Please Reconnect') }
                    );
                }
                break;
            } else {
                break;
            }
        }
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
        if (fanDur !== undefined && (isNaN(fanDur) || fanDur < VALIDATION_LIMITS.MIN_FAN_DURATION_S || fanDur > VALIDATION_LIMITS.MAX_FAN_DURATION_S)) {
            return { valid: false, fieldId: 'set-fan-duration', error: t('val.fan_dur_range', `Fan Cooling Duration must be between ${VALIDATION_LIMITS.MIN_FAN_DURATION_S}s and ${VALIDATION_LIMITS.MAX_FAN_DURATION_S}s (Entered: ${fanDur}s).`, { min: VALIDATION_LIMITS.MIN_FAN_DURATION_S, max: VALIDATION_LIMITS.MAX_FAN_DURATION_S, val: fanDur }) };
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
