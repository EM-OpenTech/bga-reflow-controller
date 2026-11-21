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
                    const file = typeof p === 'string' ? p : p.file || p.name || '';
                    const name = typeof p === 'string' ? p : p.name || p.file || '';
                    if (file) {
                        const opt = document.createElement('option');
                        opt.value = file;
                        opt.textContent = Format.profileName(name);
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
        if ($('set-sw-buzzer')) {
            const swEnabled = data.softwareBuzzerEnabled !== false;
            $('set-sw-buzzer').value = swEnabled ? 'true' : 'false';
            store.softwareBuzzerEnabled = swEnabled;
        }
        if ($('set-hw-buzzer'))
            $('set-hw-buzzer').value = data.hardwareBuzzerEnabled ? 'true' : 'false';
        if ($('set-default-profile') && data.defaultProfile !== undefined) {
            $('set-default-profile').value = data.defaultProfile;
        }

        // Temperature Limits & Calibration
        if ($('set-max-top')) $('set-max-top').value = Format.temp(data.maxTempTop ?? 250);
        if ($('set-max-bottom')) $('set-max-bottom').value = Format.temp(data.maxTempBottom ?? 250);
        if ($('set-ema-enabled'))
            $('set-ema-enabled').value = data.emaFilterEnabled ? 'true' : 'false';
        if ($('set-filter-alpha')) $('set-filter-alpha').value = Format.alpha(data.emaAlpha ?? 0.3);
        if ($('set-cj-top-offset'))
            $('set-cj-top-offset').value = Format.temp(data.topCjOffset ?? 0.0);
        if ($('set-cj-bottom-offset'))
            $('set-cj-bottom-offset').value = Format.temp(data.bottomCjOffset ?? 0.0);

        // Safety & Watchdogs
        if ($('set-safety-enabled'))
            $('set-safety-enabled').value = data.enableSafetyWatchdog !== false ? 'true' : 'false';
        if ($('set-stuck-ssr-thresh'))
            $('set-stuck-ssr-thresh').value = Format.ramp(data.stuckSsrRiseThreshold ?? 5.0);
        if ($('set-stuck-ssr-time'))
            $('set-stuck-ssr-time').value = Format.int(data.stuckSsrWindowSec ?? 30);
        if ($('set-norise-thresh'))
            $('set-norise-thresh').value = Format.ramp(data.noRiseThreshold ?? 2.0);
        if ($('set-norise-time'))
            $('set-norise-time').value = Format.int(data.noRiseTimeoutSec ?? 45);
        if ($('set-cooling-safe-temp'))
            $('set-cooling-safe-temp').value = Format.temp(data.coolingSafeTemp ?? 50.0);
        if ($('set-preheat-timeout')) {
            $('set-preheat-timeout').value = Format.int(data.preheatHoldTimeoutMin ?? 0);
        }
        store.preheatHoldTimeoutMin = Number(data.preheatHoldTimeoutMin) || 0;

        // Tolerances & Timings
        if ($('set-hold-low-tol'))
            $('set-hold-low-tol').value = Format.ramp(data.holdLowTolerance ?? 2.0);
        if ($('set-hold-high-tol'))
            $('set-hold-high-tol').value = Format.ramp(data.holdHighTolerance ?? 2.0);
        if ($('set-settle-time-s'))
            $('set-settle-time-s').value = Format.int(data.settleTimeS ?? 5);
        if ($('set-fan-delay')) $('set-fan-delay').value = Format.int(data.fanCoolingDelayS ?? 0);
        if ($('set-fan-duration'))
            $('set-fan-duration').value = Format.int(data.fanCoolingDurationS ?? 60);

        // SSR Burst-Fire
        if ($('set-burst-top'))
            $('set-burst-top').value = Format.int(data.topBurstWindowMs ?? 1000);
        if ($('set-burst-bottom'))
            $('set-burst-bottom').value = Format.int(data.bottomBurstWindowMs ?? 1000);

        // PID Standard
        if ($('set-pid-lib-enabled'))
            $('set-pid-lib-enabled').value = data.pidLibraryEnabled ? 'true' : 'false';
        if ($('set-top-kp')) $('set-top-kp').value = Format.kp(data.topKp ?? 2.0);
        if ($('set-top-ki')) $('set-top-ki').value = Format.ki(data.topKi ?? 0.05);
        if ($('set-top-kd')) $('set-top-kd').value = Format.kd(data.topKd ?? 1.0);
        if ($('set-bottom-kp')) $('set-bottom-kp').value = Format.kp(data.bottomKp ?? 2.0);
        if ($('set-bottom-ki')) $('set-bottom-ki').value = Format.ki(data.bottomKi ?? 0.04);
        if ($('set-bottom-kd')) $('set-bottom-kd').value = Format.kd(data.bottomKd ?? 1.0);

        // Store stock PID values & library enabled state for HUD & warning banners
        store.pidLibraryEnabled = data.pidLibraryEnabled === true;
        store.topKp = data.topKp ?? 2.0;
        store.topKi = data.topKi ?? 0.05;
        store.topKd = data.topKd ?? 1.0;
        store.bottomKp = data.bottomKp ?? 2.0;
        store.bottomKi = data.bottomKi ?? 0.04;
        store.bottomKd = data.bottomKd ?? 1.0;
        store.enableSafetyWatchdog = data.enableSafetyWatchdog !== false;
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

function onSoftwareBuzzerChange() {
    const isEnabled = $('set-sw-buzzer')?.value === 'true';
    store.softwareBuzzerEnabled = isEnabled;
}

async function saveSettingsToController() {
    const btnSave = $('btn-save-settings');
    if (btnSave?.disabled || isModificationBlocked()) return;

    if (btnSave) btnSave.disabled = true;
    try {
        const swBuzzerActive = $('set-sw-buzzer')?.value === 'true';
        store.softwareBuzzerEnabled = swBuzzerActive;
        const payload = {
            simulationMode: $('set-sim')?.value === 'true',
            language: $('set-lang')?.value || store.currentLanguage || 'en',
            theme: store.currentTheme,
            softwareBuzzerEnabled: swBuzzerActive,
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
            preheatHoldTimeoutMin: Format.parseInt($('set-preheat-timeout')?.value, 0),

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
            bottomKd: Format.parseFloat($('set-bottom-kd')?.value, 1.0),
        };

        const val = validateSettingsPayload(payload);
        if (!val.valid) {
            const invalidEl = val.fieldId ? $(val.fieldId) : null;
            if (invalidEl) {
                invalidEl.classList.add('input-invalid');
                invalidEl.focus();
                invalidEl.select?.();
            }
            await Modal.alert(val.error, {
                type: 'warning',
                title: t('modal.invalid_settings_title', '⚠️ Invalid Machine Setting'),
            });
            return;
        }

        const res = await apiRequest('/api/settings', 'POST', payload);
        if (res && res.success !== false) {
            document
                .querySelectorAll('.input-invalid')
                .forEach(el => el.classList.remove('input-invalid'));
            store.pidLibraryEnabled = payload.pidLibraryEnabled;
            store.topKp = payload.topKp;
            store.topKi = payload.topKi;
            store.topKd = payload.topKd;
            store.bottomKp = payload.bottomKp;
            store.bottomKi = payload.bottomKi;
            store.bottomKd = payload.bottomKd;
            store.enableSafetyWatchdog = payload.enableSafetyWatchdog !== false;
            store.preheatHoldTimeoutMin = payload.preheatHoldTimeoutMin;
            updatePidLibraryDisabledBanner();
            updateSafetyBypassBadge();

            if (payload.theme && payload.theme !== store.currentTheme) {
                setTheme(payload.theme);
            }
            if (payload.language && payload.language !== store.currentLanguage) {
                setLanguage(payload.language);
            }
            currentSettings = payload;
            if (payload.defaultProfile && !isChartProfileBlocked(store.stateEnum)) {
                store.activeProfileFile = payload.defaultProfile;
                store.activeProfileName = Format.profileName(payload.defaultProfile);
                await populateChartProfileSelect();
            }
            await Modal.alert(
                t('toast.settings_saved', 'Machine Settings saved successfully to controller!'),
                { type: 'success', title: t('modal.profile_saved_title', 'Settings Saved') }
            );
        } else {
            const errMsg = localizeApiError(
                res && res.error,
                'toast.settings_error',
                'Failed to save settings to controller.'
            );
            await Modal.alert(errMsg, {
                type: 'danger',
                title: t('modal.save_failed_title', 'Save Failed'),
            });
        }
    } catch (err) {
        console.error('[Settings] Save failed:', err);
        await Modal.alert(t('toast.settings_error', 'Failed to save settings to controller.'), {
            type: 'danger',
            title: t('modal.save_failed_title', 'Save Failed'),
        });
    } finally {
        if (btnSave) btnSave.disabled = false;
    }
}

async function changeWifiPasswordFromSettings() {
    const btnUpdate = $('btn-update-wifi-pass');
    if (btnUpdate?.disabled || isModificationBlocked()) return;

    const passInput = $('set-wifi-new-pass');
    if (!passInput) return;
    const newPass = passInput.value.trim();
    if (newPass.length < 8) {
        await Modal.alert(
            t('toast.wifi_pass_short', 'Password must be at least 8 characters long.'),
            { type: 'warning', title: t('settings.sg_wifi', 'Wi-Fi Password') }
        );
        return;
    }

    // 1. Confirmation Modal: "Really change password? Yes / Cancel"
    const confirmed = await Modal.confirm(
        t(
            'modal.wifi_pass_confirm_msg',
            'Are you sure you want to change the Wi-Fi password?\n\nYour device will be disconnected immediately and you must reconnect using the new password.'
        ),
        {
            title: t('modal.wifi_pass_confirm_title', 'Change Wi-Fi Password?'),
            confirmText: t('btn.change_password', 'Change Password'),
            cancelText: t('btn.cancel', 'Cancel'),
            type: 'warning',
        }
    );
    if (!confirmed) return;

    // 2. Lock button & update text during active transmission
    if (btnUpdate) {
        btnUpdate.disabled = true;
        btnUpdate.textContent = t('btn.updating_pass', 'Updating...');
    }

    const showReconnectAlert = () =>
        Modal.alert(
            t(
                'modal.wifi_pass_reconnect_msg',
                "Wi-Fi password has been updated successfully!\n\nYour device has been disconnected from the controller.\nPlease open your device Wi-Fi settings and reconnect to 'BGA Reflow Controller' using your new password."
            ),
            {
                type: 'success',
                title: t(
                    'modal.wifi_pass_reconnect_title',
                    '🔑 Password Changed – Please Reconnect'
                ),
            }
        );

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
                await showReconnectAlert();
                return;
            }
            const errMsg = localizeApiError(
                res.error,
                'toast.settings_error',
                'Failed to update Wi-Fi password.'
            );
            await Modal.alert(errMsg, {
                type: 'danger',
                title: t('modal.save_failed_title', 'Update Failed'),
            });
            return;
        }

        // Success: Notify user to reconnect to the Access Point
        await showReconnectAlert();
    } catch (err) {
        console.warn(
            '[Settings] Wi-Fi password applied, network connection dropped as expected:',
            err
        );
        passInput.value = '';
        if (btnUpdate) {
            btnUpdate.disabled = false;
            btnUpdate.textContent = t('settings.btn_wifi_update', 'Update Password');
        }
        // Applying a new Wi-Fi password restarts the SoftAP immediately, severing active TCP sockets.
        // A network drop here is expected confirmation that the AP restarted with new credentials!
        await showReconnectAlert();
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
            body: t(
                'modal.sec_msg',
                "The controller is running with the default Access Point password ('reflow123'). Please set a secure password or confirm keeping the default."
            ),
            withInput: true,
            inputType: 'text',
            inputPlaceholder: t('settings.ph_wifi_pass', 'min. 8 characters'),
            buttons: [
                {
                    label: t('modal.sec_btn_keep', 'Keep Default'),
                    cls: 'btn-outline',
                    value: 'KEEP_DEFAULT',
                },
                { label: t('modal.sec_btn_change', 'Set Password'), cls: 'btn-primary' },
            ],
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
                        t(
                            'modal.wifi_pass_reconnect_msg',
                            "Wi-Fi password has been updated successfully!\n\nYour device has been disconnected from the controller.\nPlease open your device Wi-Fi settings and reconnect to 'BGA Reflow Controller' using your new password."
                        ),
                        {
                            type: 'success',
                            title: t(
                                'modal.wifi_pass_reconnect_title',
                                '🔑 Password Changed – Please Reconnect'
                            ),
                        }
                    );
                } else {
                    const errMsg = localizeApiError(
                        res && res.error,
                        'toast.settings_error',
                        'Failed to update Wi-Fi password.'
                    );
                    await Modal.alert(errMsg, {
                        type: 'danger',
                        title: t('modal.save_failed_title', 'Update Failed'),
                    });
                }
            } catch (err) {
                console.warn('[Security] SoftAP restarted with new credentials:', err);
                await Modal.alert(
                    t(
                        'modal.wifi_pass_reconnect_msg',
                        "Wi-Fi password has been updated successfully!\n\nYour device has been disconnected from the controller.\nPlease open your device Wi-Fi settings and reconnect to 'BGA Reflow Controller' using your new password."
                    ),
                    {
                        type: 'success',
                        title: t(
                            'modal.wifi_pass_reconnect_title',
                            '🔑 Password Changed – Please Reconnect'
                        ),
                    }
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
    if (
        typeof data.schemaVersion === 'number' &&
        data.schemaVersion > SCHEMA_VERSIONS.MACHINE_SETTINGS
    ) {
        return {
            valid: false,
            error: t(
                'val.incompatible_schema',
                'Incompatible file: Schema version (v{{ver}}) is newer than supported controller firmware (v{{cur}}). Please update firmware first.',
                { ver: data.schemaVersion, cur: SCHEMA_VERSIONS.MACHINE_SETTINGS }
            ),
        };
    }
    // Reject if it is a profile or PID library
    if (data.stepsTop !== undefined || data.stepsBottom !== undefined) {
        return {
            valid: false,
            error: t(
                'val.mismatched_profile',
                'Invalid file: Reflow Profile JSON cannot be imported as Machine Settings.'
            ),
        };
    }
    if (
        Array.isArray(data.top) &&
        Array.isArray(data.bottom) &&
        data.topKp === undefined &&
        data.maxTempTop === undefined
    ) {
        return {
            valid: false,
            error: t(
                'val.mismatched_pid',
                'Invalid file: PID Library JSON cannot be imported as Machine Settings.'
            ),
        };
    }
    // Must contain at least one known machine settings key
    const hasSettingsKey =
        data.maxTempTop !== undefined ||
        data.maxTempBottom !== undefined ||
        data.coolingSafeTemp !== undefined ||
        data.topKp !== undefined ||
        data.bottomKp !== undefined ||
        data.simulationMode !== undefined ||
        data.theme !== undefined ||
        data.language !== undefined ||
        data.settleTimeS !== undefined ||
        data.topBurstWindowMs !== undefined ||
        data.safetyLimits !== undefined ||
        data.general !== undefined;
    if (!hasSettingsKey) {
        return {
            valid: false,
            error: t(
                'val.mismatched_settings',
                'Invalid file: Does not contain valid Machine Settings structure.'
            ),
        };
    }

    const maxTop = data.maxTempTop ?? data.safetyLimits?.maxTempTop;
    const maxBot = data.maxTempBottom ?? data.safetyLimits?.maxTempBottom;
    const safeCool = data.coolingSafeTemp ?? data.safetyLimits?.safeCoolingTemp;
    const preheatTimeout = data.preheatHoldTimeoutMin;
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

    function checkRange(val, min, max, fieldId, key, fallback, vars) {
        if (val !== undefined && (!Number.isFinite(val) || val < min || val > max)) {
            return { valid: false, fieldId, error: t(key, fallback, vars) };
        }
        return null;
    }

    const rules = [
        checkRange(maxTop, VALIDATION_LIMITS.MIN_TEMPERATURE, VALIDATION_LIMITS.MAX_TEMPERATURE, 'set-max-top', 'val.max_temp_top_out_of_range', `Max Top Temperature must be between ${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C (Entered: ${Format.temp(maxTop)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE), val: Format.temp(maxTop) }),
        checkRange(maxBot, VALIDATION_LIMITS.MIN_TEMPERATURE, VALIDATION_LIMITS.MAX_TEMPERATURE, 'set-max-bottom', 'val.max_temp_bottom_out_of_range', `Max Bottom Temperature must be between ${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C (Entered: ${Format.temp(maxBot)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE), val: Format.temp(maxBot) }),
        checkRange(safeCool, VALIDATION_LIMITS.MIN_SAFE_COOLING_TEMP, VALIDATION_LIMITS.MAX_SAFE_COOLING_TEMP, 'set-cooling-safe-temp', 'val.safe_cooling_range', `Safe Cooling Temperature must be between ${Format.temp(VALIDATION_LIMITS.MIN_SAFE_COOLING_TEMP)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_SAFE_COOLING_TEMP)}°C (Entered: ${Format.temp(safeCool)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_SAFE_COOLING_TEMP), max: Format.temp(VALIDATION_LIMITS.MAX_SAFE_COOLING_TEMP), val: Format.temp(safeCool) }),
        checkRange(preheatTimeout, VALIDATION_LIMITS.MIN_PREHEAT_HOLD_TIMEOUT_MIN, VALIDATION_LIMITS.MAX_PREHEAT_HOLD_TIMEOUT_MIN, 'set-preheat-timeout', 'val.preheat_timeout_range', `Preheat Done Timeout must be between ${VALIDATION_LIMITS.MIN_PREHEAT_HOLD_TIMEOUT_MIN} and ${VALIDATION_LIMITS.MAX_PREHEAT_HOLD_TIMEOUT_MIN} minutes (Entered: ${preheatTimeout}).`, { min: VALIDATION_LIMITS.MIN_PREHEAT_HOLD_TIMEOUT_MIN, max: VALIDATION_LIMITS.MAX_PREHEAT_HOLD_TIMEOUT_MIN, val: preheatTimeout }),
        checkRange(stuckRise, 1.0, 50.0, 'set-stuck-ssr-thresh', 'val.stuck_rise_range', `Stuck SSR Rise Threshold must be between 1.0°C and 50.0°C (Entered: ${Format.temp(stuckRise)}°C).`, { min: '1.0', max: '50.0', val: Format.temp(stuckRise) }),
        checkRange(stuckTime, 3, 120, 'set-stuck-ssr-time', 'val.stuck_time_range', `Stuck SSR Window must be between 3s and 120s (Entered: ${stuckTime}s).`, { min: 3, max: 120, val: stuckTime }),
        checkRange(noRiseThresh, 0.5, 50.0, 'set-norise-thresh', 'val.norise_thresh_range', `Heater No-Rise Threshold must be between 0.5°C and 50.0°C (Entered: ${Format.temp(noRiseThresh)}°C).`, { min: '0.5', max: '50.0', val: Format.temp(noRiseThresh) }),
        checkRange(noRiseTime, 5, 120, 'set-norise-time', 'val.norise_time_range', `Heater No-Rise Timeout must be between 5s and 120s (Entered: ${noRiseTime}s).`, { min: 5, max: 120, val: noRiseTime }),
        checkRange(holdLow, 0.5, 30.0, 'set-hold-low-tol', 'val.hold_low_range', `Hold Lower Tolerance must be between 0.5°C and 30.0°C (Entered: ${Format.temp(holdLow)}°C).`, { min: '0.5', max: '30.0', val: Format.temp(holdLow) }),
        checkRange(holdHigh, 0.5, 30.0, 'set-hold-high-tol', 'val.hold_high_range', `Hold Upper Tolerance must be between 0.5°C and 30.0°C (Entered: ${Format.temp(holdHigh)}°C).`, { min: '0.5', max: '30.0', val: Format.temp(holdHigh) }),
        checkRange(settleS, VALIDATION_LIMITS.MIN_SETTLE_S, VALIDATION_LIMITS.MAX_SETTLE_S, 'set-settle-time-s', 'val.settle_range', `Settle Time must be between ${VALIDATION_LIMITS.MIN_SETTLE_S}s and ${VALIDATION_LIMITS.MAX_SETTLE_S}s (Entered: ${settleS}s).`, { min: VALIDATION_LIMITS.MIN_SETTLE_S, max: VALIDATION_LIMITS.MAX_SETTLE_S, val: settleS }),
        checkRange(fanDelay, 0, 300, 'set-fan-delay', 'val.fan_delay_range', `Fan Cooling Delay must be between 0s and 300s (Entered: ${fanDelay}s).`, { min: 0, max: 300, val: fanDelay }),
        checkRange(fanDur, VALIDATION_LIMITS.MIN_FAN_DURATION_S, VALIDATION_LIMITS.MAX_FAN_DURATION_S, 'set-fan-duration', 'val.fan_dur_range', `Fan Cooling Duration must be between ${VALIDATION_LIMITS.MIN_FAN_DURATION_S}s and ${VALIDATION_LIMITS.MAX_FAN_DURATION_S}s (Entered: ${fanDur}s).`, { min: VALIDATION_LIMITS.MIN_FAN_DURATION_S, max: VALIDATION_LIMITS.MAX_FAN_DURATION_S, val: fanDur }),
        checkRange(burstTop, VALIDATION_LIMITS.MIN_BURST_WINDOW_MS, VALIDATION_LIMITS.MAX_BURST_WINDOW_MS, 'set-burst-top', 'val.burst_top_range', `Top Burst Window must be between ${VALIDATION_LIMITS.MIN_BURST_WINDOW_MS}ms and ${VALIDATION_LIMITS.MAX_BURST_WINDOW_MS}ms (Entered: ${burstTop}ms).`, { min: VALIDATION_LIMITS.MIN_BURST_WINDOW_MS, max: VALIDATION_LIMITS.MAX_BURST_WINDOW_MS, val: burstTop }),
        checkRange(burstBot, VALIDATION_LIMITS.MIN_BURST_WINDOW_MS, VALIDATION_LIMITS.MAX_BURST_WINDOW_MS, 'set-burst-bottom', 'val.burst_bot_range', `Bottom Burst Window must be between ${VALIDATION_LIMITS.MIN_BURST_WINDOW_MS}ms and ${VALIDATION_LIMITS.MAX_BURST_WINDOW_MS}ms (Entered: ${burstBot}ms).`, { min: VALIDATION_LIMITS.MIN_BURST_WINDOW_MS, max: VALIDATION_LIMITS.MAX_BURST_WINDOW_MS, val: burstBot }),
        checkRange(alpha, 0.01, 1.0, 'set-filter-alpha', 'val.alpha_range', `EMA Filter Alpha must be between 0.01 and 1.0 (Entered: ${Format.alpha(alpha)}).`, { min: '0.01', max: '1.0', val: Format.alpha(alpha) }),
        checkRange(topCj, VALIDATION_LIMITS.MIN_CJ_OFFSET, VALIDATION_LIMITS.MAX_CJ_OFFSET, 'set-cj-top-offset', 'val.cj_offset_range', `Top CJ Offset must be between ${Format.temp(VALIDATION_LIMITS.MIN_CJ_OFFSET)}°C and +${Format.temp(VALIDATION_LIMITS.MAX_CJ_OFFSET)}°C (Entered: ${Format.temp(topCj)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_CJ_OFFSET), max: '+' + Format.temp(VALIDATION_LIMITS.MAX_CJ_OFFSET), val: Format.temp(topCj) }),
        checkRange(botCj, VALIDATION_LIMITS.MIN_CJ_OFFSET, VALIDATION_LIMITS.MAX_CJ_OFFSET, 'set-cj-bottom-offset', 'val.cj_offset_range', `Bottom CJ Offset must be between ${Format.temp(VALIDATION_LIMITS.MIN_CJ_OFFSET)}°C and +${Format.temp(VALIDATION_LIMITS.MAX_CJ_OFFSET)}°C (Entered: ${Format.temp(botCj)}°C).`, { min: Format.temp(VALIDATION_LIMITS.MIN_CJ_OFFSET), max: '+' + Format.temp(VALIDATION_LIMITS.MAX_CJ_OFFSET), val: Format.temp(botCj) }),
        checkRange(data.topKp, VALIDATION_LIMITS.MIN_PID_KP, VALIDATION_LIMITS.MAX_PID_KP, 'set-top-kp', 'val.pid_kp_out_of_range', `Top PID Kp (${Format.kp(data.topKp)}) is out of bounds (0.00–100.00).`, { heater: 'Top', step: 1, val: Format.kp(data.topKp), min: '0.00', max: '100.00' }),
        checkRange(data.topKi, VALIDATION_LIMITS.MIN_PID_KI, VALIDATION_LIMITS.MAX_PID_KI, 'set-top-ki', 'val.pid_ki_out_of_range', `Top PID Ki (${Format.ki(data.topKi)}) is out of bounds (0.000–10.000).`, { heater: 'Top', step: 1, val: Format.ki(data.topKi), min: '0.000', max: '10.000' }),
        checkRange(data.topKd, VALIDATION_LIMITS.MIN_PID_KD, VALIDATION_LIMITS.MAX_PID_KD, 'set-top-kd', 'val.pid_kd_out_of_range', `Top PID Kd (${Format.kd(data.topKd)}) is out of bounds (0.00–100.00).`, { heater: 'Top', step: 1, val: Format.kd(data.topKd), min: '0.00', max: '100.00' }),
        checkRange(data.bottomKp, VALIDATION_LIMITS.MIN_PID_KP, VALIDATION_LIMITS.MAX_PID_KP, 'set-bottom-kp', 'val.pid_kp_out_of_range', `Bottom PID Kp (${Format.kp(data.bottomKp)}) is out of bounds (0.00–100.00).`, { heater: 'Bottom', step: 1, val: Format.kp(data.bottomKp), min: '0.00', max: '100.00' }),
        checkRange(data.bottomKi, VALIDATION_LIMITS.MIN_PID_KI, VALIDATION_LIMITS.MAX_PID_KI, 'set-bottom-ki', 'val.pid_ki_out_of_range', `Bottom PID Ki (${Format.ki(data.bottomKi)}) is out of bounds (0.000–10.000).`, { heater: 'Bottom', step: 1, val: Format.ki(data.bottomKi), min: '0.000', max: '10.000' }),
        checkRange(data.bottomKd, VALIDATION_LIMITS.MIN_PID_KD, VALIDATION_LIMITS.MAX_PID_KD, 'set-bottom-kd', 'val.pid_kd_out_of_range', `Bottom PID Kd (${Format.kd(data.bottomKd)}) is out of bounds (0.00–100.00).`, { heater: 'Bottom', step: 1, val: Format.kd(data.bottomKd), min: '0.00', max: '100.00' }),
    ];

    for (const r of rules) {
        if (r) return r;
    }
    return { valid: true };
}
