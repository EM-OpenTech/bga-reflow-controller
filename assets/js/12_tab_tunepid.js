/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 12_tab_tunepid.js
 * @brief Layer 4 Domain Controller: PID library management, autotune strip, gain interpolation preview.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 17. PID LIBRARY & AUTOTUNE MANAGER (TunePID) ────────────────────────
let pidLibData = {
    top: [],
    bottom: [],
};

async function loadPidLibraryFromController() {
    try {
        const data = await apiRequest('/api/pidlibrary', 'GET');
        if (data && (Array.isArray(data.top) || Array.isArray(data.bottom))) {
            pidLibData = {
                top: Array.isArray(data.top) ? data.top : [],
                bottom: Array.isArray(data.bottom) ? data.bottom : [],
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

// Updates the pulsing ⚠️ SAFETY BYPASSED badge from store (set on settings load, not via WS stream)

function updatePidLibraryDisabledBanner() {
    const banner = $('tunepid-disabled-banner');
    if (!banner) return;
    // When PID library is enabled, hide the disabled-warning banner; when disabled, show the warning.
    banner.classList.toggle('u-hidden', Boolean(store.pidLibraryEnabled));
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

    const isLocked =
        store.stateEnum === 1 ||
        store.stateEnum === 2 ||
        store.stateEnum === 3 ||
        store.stateEnum === 4 ||
        store.stateEnum === 7 ||
        store.stateEnum === 8;
    const dis = isLocked ? ' disabled' : '';

    points.forEach((pt, idx) => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
                <td><strong>#${idx + 1}</strong></td>
                <td><input type="number"${dis} value="${Format.temp(pt.temp)}" step="${VALIDATION_RESOLUTIONS.TEMPERATURE}" min="${VALIDATION_LIMITS.MIN_TEMPERATURE}" max="${VALIDATION_LIMITS.MAX_TEMPERATURE}" onchange="ReflowApp.updatePidField(${isTop}, ${idx}, 'temp', this.value)"></td>
                <td><input type="number"${dis} value="${Format.kp(pt.kp)}" step="${VALIDATION_RESOLUTIONS.PID_GAIN_KP}" min="0.00" max="100.00" onchange="ReflowApp.updatePidField(${isTop}, ${idx}, 'kp', this.value)"></td>
                <td><input type="number"${dis} value="${Format.ki(pt.ki)}" step="${VALIDATION_RESOLUTIONS.PID_GAIN_KI}" min="0.000" max="10.000" onchange="ReflowApp.updatePidField(${isTop}, ${idx}, 'ki', this.value)"></td>
                <td><input type="number"${dis} value="${Format.kd(pt.kd)}" step="${VALIDATION_RESOLUTIONS.PID_GAIN_KD}" min="0.00" max="100.00" onchange="ReflowApp.updatePidField(${isTop}, ${idx}, 'kd', this.value)"></td>
                <td><button type="button" class="btn-delete-step"${dis} onclick="ReflowApp.removePidStep(${isTop}, ${idx})" title="${t('tunepid.delete_point_title', 'Delete Point')}">${ICONS.TRASH}</button></td>
            `;
        tbody.appendChild(tr);
    });
}

function updatePidField(isTop, idx, field, val) {
    if (
        store.stateEnum === 1 ||
        store.stateEnum === 2 ||
        store.stateEnum === 3 ||
        store.stateEnum === 4 ||
        store.stateEnum === 7 ||
        store.stateEnum === 8
    ) {
        return;
    }
    const list = isTop ? pidLibData.top : pidLibData.bottom;
    if (list && list[idx]) {
        list[idx][field] = Format.parseFloat(val, 0);
        updatePidPreview();
    }
}

async function addPidStep(isTop) {
    if (
        store.stateEnum === 1 ||
        store.stateEnum === 2 ||
        store.stateEnum === 3 ||
        store.stateEnum === 4 ||
        store.stateEnum === 7 ||
        store.stateEnum === 8
    ) {
        return;
    }
    if (!pidLibData) return;
    const list = isTop ? pidLibData.top : pidLibData.bottom;
    if (list.length >= VALIDATION_LIMITS.MAX_PID_POINTS) {
        await Modal.alert(
            t(
                'modal.pid_points_limit_reached',
                'Maximum of {{max}} PID points per heater reached.',
                { max: VALIDATION_LIMITS.MAX_PID_POINTS }
            ),
            {
                type: 'warning',
                title: t('modal.pid_points_limit_title', 'Point Limit Reached'),
            }
        );
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
    if (
        store.stateEnum === 1 ||
        store.stateEnum === 2 ||
        store.stateEnum === 3 ||
        store.stateEnum === 4 ||
        store.stateEnum === 7 ||
        store.stateEnum === 8
    ) {
        return;
    }
    const list = isTop ? pidLibData.top : pidLibData.bottom;
    if (list && list.length > idx) {
        list.splice(idx, 1);
        renderPidTable(isTop ? 'table-pid-top' : 'table-pid-bottom', list, isTop);
        updatePidPreview();
    }
}

async function savePidLibraryToController() {
    const btnSave = $('btn-save-pidlib');
    if (btnSave?.disabled) return;

    if (store.stateEnum === 8) {
        await Modal.alert(
            t(
                'modal.save_pid_backup_locked',
                'PID Library modification is locked during backup or restore operations.'
            ),
            {
                type: 'danger',
                title: t('modal.save_locked_active_title', '⚠️ Save Failed: Maintenance Active'),
            }
        );
        return;
    }

    if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 6) {
        await Modal.alert(
            t(
                'modal.save_pid_locked_active',
                'Failed to save PID Library: Reflow process or autotune is currently active. PID modifications are not permitted during an active heating process.'
            ),
            {
                type: 'danger',
                title: t('modal.save_locked_active_title', '⚠️ Save Failed: Process Active'),
            }
        );
        return;
    }

    const val = validatePidLibraryPayload(pidLibData);
    if (!val.valid) {
        await Modal.alert(val.error, {
            type: 'warning',
            title: t('modal.invalid_pid_title', '⚠️ Invalid PID Library'),
        });
        return;
    }

    if (btnSave) btnSave.disabled = true;
    try {
        const res = await apiRequest('/api/pidlibrary', 'POST', pidLibData);
        if (res && res.success !== false) {
            await Modal.alert(
                t('modal.pid_lib_saved', 'PID Library saved successfully to controller!'),
                {
                    type: 'success',
                    title: t('modal.pid_lib_saved_title', 'PID Library Saved'),
                }
            );
        } else {
            const errMsg = localizeApiError(
                res && res.error,
                'modal.pid_lib_save_failed',
                'Failed to save PID Library to controller.'
            );
            await Modal.alert(errMsg, {
                type: 'danger',
                title: t('modal.save_failed_title', 'Save Failed'),
            });
        }
    } catch (err) {
        console.error('[TunePID] Save failed:', err);
        await Modal.alert(
            t('modal.pid_lib_save_failed', 'Failed to save PID Library to controller.'),
            {
                type: 'danger',
                title: t('modal.save_failed_title', 'Save Failed'),
            }
        );
    } finally {
        if (btnSave) btnSave.disabled = false;
    }
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
                kd: p0.kd + ratio * (p1.kd - p0.kd),
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
        setText(
            'pid-preview-top',
            `Kp=${Format.kp(topGains.kp)} / Ki=${Format.ki(topGains.ki)} / Kd=${Format.kd(topGains.kd)}`
        );
    }

    if (botEmpty) {
        setText('pid-preview-bottom', '—');
    } else {
        const botGains = interpolateGains(pidLibData.bottom, tVal);
        setText(
            'pid-preview-bottom',
            `Kp=${Format.kp(botGains.kp)} / Ki=${Format.ki(botGains.ki)} / Kd=${Format.kd(botGains.kd)}`
        );
    }
}

async function startAutotune() {
    const btnTune = $('btn-start-autotune');
    if (btnTune?.disabled) return;

    if (!store.wsConnected || Date.now() - store.lastTelemetryTime > 3500) {
        await Modal.alert(
            t(
                'modal.ws_disconnected_msg',
                'Live telemetry stream is disconnected. Starting Reflow, Preheat, or Autotune is locked for safety until reconnection.'
            ),
            {
                type: 'warning',
                title: t('modal.ws_disconnected_title', 'Safety Interlock Active'),
            }
        );
        return;
    }

    if (store.stateEnum !== 0 && store.stateEnum !== 5) {
        await Modal.alert(
            t(
                'modal.autotune_locked_msg',
                'Reflow process or another operation is currently active: Autotune can only be started in IDLE state.'
            ),
            {
                type: 'warning',
                title: t('modal.autotune_locked_title', '⚠️ Autotune Locked: Process Active'),
            }
        );
        return;
    }

    const ch = $('tune-channel')?.value || 'top';
    const rawTemp = $('tune-temp')?.value?.trim();
    const temp = Format.parseFloat(rawTemp, NaN);
    if (!rawTemp || !Number.isFinite(temp)) {
        await Modal.alert(
            t(
                'modal.autotune_temp_required',
                `Please enter a target temperature between ${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C.`,
                {
                    min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE),
                    max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE),
                }
            ),
            {
                type: 'warning',
                title: t('modal.autotune_temp_required_title', 'Target Temperature Required'),
            }
        );
        $('tune-temp')?.focus();
        return;
    }

    if (temp < VALIDATION_LIMITS.MIN_TEMPERATURE || temp > VALIDATION_LIMITS.MAX_TEMPERATURE) {
        await Modal.alert(
            t(
                'modal.autotune_temp_invalid',
                `Target temperature must be between ${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}°C and ${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C (Entered: ${Format.temp(temp)}°C).`,
                {
                    min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE),
                    max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE),
                    val: Format.temp(temp),
                }
            ),
            {
                type: 'warning',
                title: t('modal.autotune_temp_invalid_title', 'Invalid Temperature Range'),
            }
        );
        $('tune-temp')?.focus();
        return;
    }

    const heaterName =
        ch === 'top'
            ? store.currentLanguage === 'de'
                ? 'Ober'
                : 'Top'
            : store.currentLanguage === 'de'
              ? 'Unter'
              : 'Bottom';
    const ok = await Modal.confirm(
        t('modal.autotune_confirm', 'Start PID Autotune for {{heater}} heater at {{temp}}°C?', {
            heater: heaterName,
            temp: temp,
        }),
        {
            type: 'warning',
            title: t('modal.autotune_confirm_title', 'Start Autotune'),
            confirmLabel: t('modal.btn_start', 'Start'),
            cancelLabel: t('modal.btn_cancel', 'Cancel'),
        }
    );
    if (!ok) return;

    if (btnTune) btnTune.disabled = true;
    try {
        const res = await apiRequest('/api/control', 'POST', {
            action: 'autotune',
            channel: ch,
            targetTemp: temp,
        });
        if (res && res.success !== false) {
            $('btn-start-autotune')?.classList.add('u-hidden');
            $('btn-stop-autotune')?.classList.remove('u-hidden');
            $('autotune-running')?.classList.remove('u-hidden');
            console.log(`[TunePID] Autotune started for ${ch} at ${temp}°C`);
        } else {
            await Modal.alert(
                t('modal.autotune_cmd_sent', 'Autotune command sent to controller.'),
                {
                    type: 'info',
                    title: t('modal.autotune_cmd_title', 'Autotune'),
                }
            );
        }
    } catch (err) {
        console.error('[TunePID] Start Autotune failed:', err);
        await Modal.alert(t('modal.autotune_start_error', 'Error starting Autotune.'), {
            type: 'danger',
            title: t('modal.autotune_start_error_title', 'Autotune Error'),
        });
    } finally {
        if (btnTune) btnTune.disabled = false;
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
    const targetTemp = Format.parseFloat(data.tuneTargetTemp, 150);
    const kp = Format.parseFloat(data.tuneKp, 0);
    const ki = Format.parseFloat(data.tuneKi, 0);
    const kd = Format.parseFloat(data.tuneKd, 0);

    if (kp > 0) {
        // Reload PID library from controller to get the newly auto-saved Flash data
        await loadPidLibraryFromController();

        const heaterName = isTop
            ? store.currentLanguage === 'de'
                ? 'Ober'
                : 'Top'
            : store.currentLanguage === 'de'
              ? 'Unter'
              : 'Bottom';
        await Modal.alert(
            t(
                'modal.autotune_complete_body',
                `New PID point (${Format.temp(targetTemp)}°C: Kp=${Format.kp(kp)}, Ki=${Format.ki(ki)}, Kd=${Format.kd(kd)}) for ${heaterName} heater was automatically saved to the controller's PID Library.\n\nYou can fine-tune these values in the table anytime; if edited manually, click 'Save PID Library' to store your changes.`,
                {
                    temp: Format.temp(targetTemp),
                    kp: Format.kp(kp),
                    ki: Format.ki(ki),
                    kd: Format.kd(kd),
                    heater: heaterName,
                }
            ),
            {
                type: 'success',
                title: t('modal.autotune_complete_title', 'Autotune Complete & Saved!'),
            }
        );
    }
}

function validatePidLibraryPayload(data) {
    if (!data || typeof data !== 'object' || Array.isArray(data)) {
        return { valid: false, error: t('val.invalid_json_format', 'Invalid JSON object format.') };
    }
    // Schema version guard – reject files from a newer firmware
    if (
        typeof data.schemaVersion === 'number' &&
        data.schemaVersion > SCHEMA_VERSIONS.PID_LIBRARY
    ) {
        return {
            valid: false,
            error: t(
                'val.incompatible_schema',
                'Incompatible file: Schema version (v{{ver}}) is newer than supported controller firmware (v{{cur}}). Please update firmware first.',
                { ver: data.schemaVersion, cur: SCHEMA_VERSIONS.PID_LIBRARY }
            ),
        };
    }
    if (
        data.maxTempTop !== undefined ||
        data.holdLowTolerance !== undefined ||
        data.topBurstWindowMs !== undefined
    ) {
        return {
            valid: false,
            error: t(
                'val.mismatched_settings',
                'Invalid file: Machine Settings JSON cannot be imported as PID Library.'
            ),
        };
    }
    if (data.stepsTop !== undefined || data.stepsBottom !== undefined) {
        return {
            valid: false,
            error: t(
                'val.mismatched_profile',
                'Invalid file: Reflow Profile JSON cannot be imported as PID Library.'
            ),
        };
    }
    if (!Array.isArray(data.top) || !Array.isArray(data.bottom)) {
        return {
            valid: false,
            error: t(
                'val.missing_pid_arrays',
                'Invalid PID Library: Must contain top and bottom point arrays.'
            ),
        };
    }
    if (
        data.top.length > VALIDATION_LIMITS.MAX_PID_POINTS ||
        data.bottom.length > VALIDATION_LIMITS.MAX_PID_POINTS
    ) {
        return {
            valid: false,
            error: t(
                'val.pid_points_exceeded',
                'PID library exceeds maximum of {{max}} points per heater.',
                { max: VALIDATION_LIMITS.MAX_PID_POINTS }
            ),
        };
    }

    const validatePoint = (pt, i, heater) => {
        if (
            typeof pt.temp !== 'number' ||
            !Number.isFinite(pt.temp) ||
            pt.temp < VALIDATION_LIMITS.MIN_TEMPERATURE ||
            pt.temp > VALIDATION_LIMITS.MAX_TEMPERATURE
        ) {
            return t(
                'val.pid_temp_out_of_range',
                `${heater} PID point #${i + 1} Temp (${Format.temp(pt.temp)}°C) is out of bounds (${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}–${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C).`,
                {
                    heater,
                    step: i + 1,
                    val: Format.temp(pt.temp),
                    min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE),
                    max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE),
                }
            );
        }
        if (typeof pt.kp !== 'number' || !Number.isFinite(pt.kp) || pt.kp < 0 || pt.kp > 100) {
            return t(
                'val.pid_kp_out_of_range',
                `${heater} PID point #${i + 1} Kp (${Format.kp(pt.kp)}) is out of bounds (0.00–100.00).`,
                { heater, step: i + 1, val: Format.kp(pt.kp), min: '0.00', max: '100.00' }
            );
        }
        if (typeof pt.ki !== 'number' || !Number.isFinite(pt.ki) || pt.ki < 0 || pt.ki > 10) {
            return t(
                'val.pid_ki_out_of_range',
                `${heater} PID point #${i + 1} Ki (${Format.ki(pt.ki)}) is out of bounds (0.000–10.000).`,
                { heater, step: i + 1, val: Format.ki(pt.ki), min: '0.000', max: '10.000' }
            );
        }
        if (typeof pt.kd !== 'number' || !Number.isFinite(pt.kd) || pt.kd < 0 || pt.kd > 100) {
            return t(
                'val.pid_kd_out_of_range',
                `${heater} PID point #${i + 1} Kd (${Format.kd(pt.kd)}) is out of bounds (0.00–100.00).`,
                { heater, step: i + 1, val: Format.kd(pt.kd), min: '0.00', max: '100.00' }
            );
        }
        return null;
    };

    for (let i = 0; i < data.top.length; i++) {
        const err = validatePoint(data.top[i], i, 'Top');
        if (err) return { valid: false, error: err };
    }
    for (let i = 0; i < data.bottom.length; i++) {
        const err = validatePoint(data.bottom[i], i, 'Bottom');
        if (err) return { valid: false, error: err };
    }
    return { valid: true };
}
