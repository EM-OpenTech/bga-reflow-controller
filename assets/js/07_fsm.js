/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 07_fsm.js
 * @brief Layer 3 Telemetry Engine: Finite State Machine UI bindings, status badges, actor overrides, interlocks.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 4. STATUS LOG DROPDOWN ──────────────────────────────────────────────
function toggleStatusLog() {
    const dropdown = $('status-log-dropdown');
    if (!dropdown) return;
    store.statusLogOpen = !store.statusLogOpen;
    dropdown.classList.toggle('active', store.statusLogOpen);
    if (store.statusLogOpen && !store.wsConnected) {
        updateLogs(null);
    }
}

function updateLogs(logs) {
    const list = $('status-log-list');
    if (!list) return;

    // 1. Connection check
    if (isConnectingPhase) {
        list.innerHTML = `<div class="status-log-item status-log-item--info">${t('status.log_connecting', 'Connecting to controller...')}</div>`;
        return;
    }

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
        if (
            lower.includes('handshake done') ||
            lower.includes('httpd') ||
            lower.includes('static file') ||
            lower.includes('serving file') ||
            lower.includes('400 bad request') ||
            lower.includes('wifi:<ba-add>') ||
            lower.includes('wifi:') ||
            lower.includes('session') ||
            lower.includes('websocket client') ||
            lower.includes('new websocket') ||
            lower.includes('max websocket clients')
        ) {
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
    const hasErrorsOrWarnings = filtered.some(
        line =>
            line.startsWith('E ') ||
            line.startsWith('W ') ||
            line.includes('ERROR') ||
            line.includes('CRITICAL') ||
            line.includes('FAULT') ||
            line.includes('WARN')
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

        if (
            line.startsWith('E ') ||
            line.includes('CRITICAL') ||
            line.includes('ERROR') ||
            line.includes('FAULT')
        ) {
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

// ── 7. STATUS BADGE & CONNECTION STATE MACHINE ──────────────────────────
function startConnectingPhase(durationMs = 2500) {
    isConnectingPhase = true;
    if (connectingTimer) {
        clearTimeout(connectingTimer);
    }
    updateConnectionBadge();
    updateLogs(null);
    connectingTimer = setTimeout(() => {
        isConnectingPhase = false;
        connectingTimer = null;
        updateConnectionBadge();
        updateLogs(null);
    }, durationMs);
}

function updateConnectionBadge() {
    const badge = $('state-badge');
    if (!badge) return;

    // 1. Fixed Connecting Phase (2.5s on load or reconnect)
    if (isConnectingPhase) {
        badge.innerText = t('status.connecting', 'CONNECTING...');
        badge.className = 'status-badge status-connecting';
        return;
    }

    // 2. Operational: Connected and telemetry active -> BEREIT / IDLE (Grün)
    if (store.wsConnected) {
        if (store.stateStr) {
            updateStatusBadge(store.stateEnum, store.stateStr);
            return;
        }
        badge.innerText = t('status.connected', 'CONNECTED');
        badge.className = 'status-badge status-idle';
        return;
    }

    // 3. Disconnected: No active connection to ESP32 -> GETRENNT / DISCONNECTED (Rot)
    badge.innerText = t('status.disconnected', 'DISCONNECTED');
    badge.className = 'status-badge status-fault';
}

function updateStatusBadge(stateEnum, stateStr, preheatDone) {
    const badge = $('state-badge');
    if (!badge) return;

    if (
        stateEnum === undefined ||
        stateEnum === null ||
        isNaN(Number(stateEnum)) ||
        Number(stateEnum) < 0 ||
        Number(stateEnum) > 8
    ) {
        store.stateEnum = -1;
        store.stateStr = STRINGS.STATE_UNKNOWN;
        if (!isConnectingPhase) {
            badge.innerText = t('state.unknown', STRINGS.STATE_UNKNOWN);
            badge.className = 'status-badge status-fault';
        }
        return;
    }

    store.stateEnum = Number(stateEnum);
    store.stateStr = stateStr || STRINGS.STATE_IDLE;

    // Hold CONNECTING visual during the fixed 2.5s latch
    if (isConnectingPhase) {
        return;
    }

    const localizedState = t(`state.${store.stateEnum}`, store.stateStr);
    badge.innerText = localizedState.toUpperCase();
    badge.className = 'status-badge';

    // 0=IDLE -> status-idle
    // 1=PREHEAT -> status-preheat
    // 2=SOAK, 3=REFLOW, 7=AUTOTUNE, 8=BACKUP -> status-active
    // 4=COOLING, 5=DONE -> status-cooldown
    // 6=FAULT -> status-fault
    switch (store.stateEnum) {
        case 0:
            badge.classList.add('status-idle');
            break;
        case 1:
            badge.classList.add('status-preheat');
            break;
        case 2:
        case 3:
        case 7:
        case 8:
            badge.classList.add('status-active');
            break;
        case 4:
        case 5:
            badge.classList.add('status-cooldown');
            break;
        case 6:
            badge.classList.add('status-fault');
            break;
        default:
            badge.classList.add('status-unknown');
            break;
    }
    // Preheat Done blink overlay
    if (preheatDone && store.stateEnum === 1) {
        badge.classList.remove('status-preheat');
        badge.classList.add('status-preheat-done');
    }

    // Screen WakeLock: Keep display awake during active heating phases (Preheat, Soak, Reflow, Cooling, Autotune)
    syncWakeLockWithState(store.stateEnum);
}

// ── 7b. SCREEN WAKE LOCK MANAGER ────────────────────────────────────────
let _wakeLockSentinel = null;

async function acquireWakeLock() {
    if ('wakeLock' in navigator && !_wakeLockSentinel) {
        try {
            _wakeLockSentinel = await navigator.wakeLock.request('screen');
            _wakeLockSentinel.addEventListener('release', () => {
                _wakeLockSentinel = null;
            });
            console.log('[WakeLock] Screen WakeLock acquired for heating phase.');
        } catch (err) {
            console.warn('[WakeLock] Could not acquire Screen WakeLock:', err?.name, err?.message);
            _wakeLockSentinel = null;
        }
    }
}

async function releaseWakeLock() {
    if (_wakeLockSentinel) {
        try {
            await _wakeLockSentinel.release();
            console.log('[WakeLock] Screen WakeLock released.');
        } catch (err) {
            console.warn('[WakeLock] Could not release Screen WakeLock:', err);
        }
        _wakeLockSentinel = null;
    }
}

function syncWakeLockWithState(stateEnum) {
    const isHeating = (stateEnum >= 1 && stateEnum <= 4) || stateEnum === 7;
    if (isHeating) {
        acquireWakeLock();
    } else {
        releaseWakeLock();
    }
}

// ── 8. ACTOR OVERRIDES (LAMP & FAN) ─────────────────────────────────────
function updateActorIcons() {
    const lampBtn = $('icon-lamp');
    if (lampBtn) {
        lampBtn.classList.toggle('icon-lamp-on', store.lampState);
        lampBtn.classList.toggle('icon-off', !store.lampState);
        lampBtn.classList.toggle('active', store.lampState);
        lampBtn.setAttribute(
            'title',
            store.lampState
                ? t('ctrl.lamp_on', 'Worklight (ON)')
                : t('ctrl.lamp_off', 'Worklight (OFF)')
        );
    }

    const fanBtn = $('icon-fan');
    if (fanBtn) {
        fanBtn.classList.toggle('icon-fan-on', store.fanState);
        fanBtn.classList.toggle('icon-off', !store.fanState);
        fanBtn.classList.toggle('active', store.fanState);
        fanBtn.setAttribute(
            'title',
            store.fanState
                ? t('ctrl.fan_on', 'Cooling Fan (ON)')
                : t('ctrl.fan_off', 'Cooling Fan (OFF)')
        );

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
    const isLive = Boolean(
        store.wsConnected &&
        store.lastTelemetryTime > 0 &&
        Date.now() - store.lastTelemetryTime < 3500
    );
    const btnStart = $('btn-start');
    const btnPreheat = $('btn-preheat');
    const btnStop = $('btn-stop');
    const btnStartTune = $('btn-start-autotune');

    if (!isLive) {
        const tooltipDis = t(
            'ctrl.tooltip_disconnected',
            '⚠️ Telemetry disconnected - starting a process is locked until connection is restored.'
        );
        if (btnStart) {
            btnStart.disabled = true;
            btnStart.setAttribute('title', tooltipDis);
        }
        if (btnPreheat) {
            btnPreheat.disabled = true;
            btnPreheat.setAttribute('title', tooltipDis);
        }
        if (btnStartTune) {
            btnStartTune.disabled = true;
            btnStartTune.setAttribute('title', tooltipDis);
        }
        // STOP is never disabled – always allow emergency stop
        if (btnStop) btnStop.disabled = false;
        return;
    }

    // STOP is never disabled – regardless of state always allow emergency stop
    if (btnStop) btnStop.disabled = false;

    const st = store.stateEnum;
    // 0=IDLE, 1=PREHEAT, 2=SOAK, 3=REFLOW, 4=COOLING, 5=DONE, 6=FAULT, 7=AUTOTUNE, 8=BACKUP
    const canPreheat = st === 0 || st === 4 || st === 5;
    const canStart = st === 0 || st === 1 || st === 5;
    const canTune = st === 0 || st === 5;

    if (btnPreheat) {
        btnPreheat.disabled = !canPreheat;
        if (st === 1) {
            btnPreheat.setAttribute(
                'title',
                t('ctrl.preheat_running_hint', 'Preheat is currently in progress')
            );
        } else {
            btnPreheat.removeAttribute('title');
        }
    }
    if (btnStart) {
        btnStart.disabled = !canStart;
        if (st === 0 || st === 5) {
            btnStart.setAttribute(
                'title',
                t('ctrl.start_idle_hint', 'Step 2: Starts Reflow after preheating is complete')
            );
        } else if (st === 1) {
            btnStart.setAttribute(
                'title',
                t('ctrl.start_preheat_hint', 'Advance from Preheat to Reflow')
            );
        } else {
            btnStart.removeAttribute('title');
        }
    }
    if (btnStartTune) {
        btnStartTune.disabled = !canTune;
        btnStartTune.removeAttribute('title');
    }

    // Morph Stop button into Reset Fault action when in FAULT state
    if (btnStop) {
        btnStop.disabled = false;
        if (st === 6) {
            btnStop.textContent = t('ctrl.reset_fault', '🔄 RESET FAULT');
            btnStop.setAttribute(
                'title',
                t('ctrl.reset_fault_hint', 'Clear fault state and restore system to IDLE')
            );
            btnStop.classList.remove('btn-danger');
            btnStop.classList.add('btn-warning');
        } else {
            btnStop.textContent = t('ctrl.stop', '■ STOP');
            btnStop.removeAttribute('title');
            btnStop.classList.remove('btn-warning');
            btnStop.classList.add('btn-danger');
        }
    }

    // Settings & Configuration modifications are locked during active heating (PREHEAT=1, SOAK=2, REFLOW=3, COOLING=4),
    // AUTOTUNE (7), and BACKUP (8) to guarantee an immutable state during backup generation and restore.
    // Permitted strictly in quiescent states: IDLE (0), DONE (5), and FAULT (6).
    const isModificationBlocked =
        st === 1 || st === 2 || st === 3 || st === 4 || st === 7 || st === 8;
    const btnUpdateWifi = $('btn-update-wifi-pass');
    const inputWifiPass = $('set-wifi-new-pass');
    const btnSaveSettings = $('btn-save-settings');
    const btnSaveProfile = $('btn-save-profile');
    const btnDeleteProfile = $('btn-delete-profile');
    const btnSavePid = $('btn-save-pidlib');

    if (btnUpdateWifi) {
        btnUpdateWifi.disabled = isModificationBlocked;
        if (isModificationBlocked) {
            btnUpdateWifi.setAttribute(
                'title',
                t(
                    'modal.wifi_pass_locked_active',
                    'Wi-Fi password update blocked during active process or maintenance.'
                )
            );
        } else {
            btnUpdateWifi.removeAttribute('title');
        }
    }
    if (inputWifiPass) {
        inputWifiPass.disabled = isModificationBlocked;
    }
    if (btnSaveSettings) {
        btnSaveSettings.disabled = isModificationBlocked;
        if (isModificationBlocked) {
            btnSaveSettings.setAttribute(
                'title',
                t(
                    'modal.save_settings_locked_active',
                    'Settings modification blocked during active process or maintenance.'
                )
            );
        } else {
            btnSaveSettings.removeAttribute('title');
        }
    }
    if (btnSaveProfile) {
        btnSaveProfile.disabled = isModificationBlocked;
    }
    if (btnDeleteProfile) {
        btnDeleteProfile.disabled = isModificationBlocked;
    }
    if (btnSavePid) {
        btnSavePid.disabled = isModificationBlocked;
    }

    // Backup & Maintenance controls are ONLY permitted when controller is in BACKUP state (8).
    // Locked in all other states (IDLE=0, PREHEAT=1..4, FAULT=6, AUTOTUNE=7).
    const isBackupPermitted = st === 8;
    const btnOrb = $('btn-backup-orb');
    if (btnOrb) {
        btnOrb.disabled = !isBackupPermitted;
        btnOrb.classList.toggle('orb-disabled', !isBackupPermitted);
        if (!isBackupPermitted) {
            btnOrb.setAttribute(
                'title',
                t(
                    'backup.orb_locked_hint',
                    'Maintenance mode (BACKUP state) required to generate backup'
                )
            );
        } else {
            btnOrb.setAttribute(
                'title',
                t('backup.orb_title', 'Click the blue orb to download all files')
            );
        }
        const orbWrapper = btnOrb.closest('.backup-orb-wrapper');
        if (orbWrapper) {
            orbWrapper.classList.toggle('wrapper-disabled', !isBackupPermitted);
        }
    }

    const btnBackToPortal = $('btn-backup-back-to-portal');
    if (btnBackToPortal) {
        btnBackToPortal.disabled = !isBackupPermitted;
    }

    const btnCreateAnother = $('btn-backup-create-another');
    if (btnCreateAnother) {
        btnCreateAnother.disabled = !isBackupPermitted;
    }

    const backupInputs = [
        'ota-file-input',
        'zip-file-input',
        'settings-file-input',
        'profile-file-input',
        'pid-file-input',
    ];
    backupInputs.forEach(id => {
        const input = $(id);
        if (input) {
            input.disabled = !isBackupPermitted;
            const parentLabel = input.closest('label');
            if (parentLabel) {
                parentLabel.classList.toggle('btn-disabled', !isBackupPermitted);
            }
        }
    });
}

async function startPreheat() {
    const btnPreheat = $('btn-preheat');
    if (btnPreheat?.disabled) return;

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
    if (
        store.stateEnum === 1 ||
        store.stateEnum === 2 ||
        store.stateEnum === 3 ||
        store.stateEnum === 7 ||
        store.stateEnum === 8
    ) {
        console.warn(
            '[FSM] Cannot start preheat: A process is already running in state',
            store.stateStr
        );
        return;
    }
    const profName = $('chart-profile-select')?.value || store.activeProfileFile;
    if (!profName) {
        await Modal.alert(
            t('modal.no_profile_preheat', 'Please select a profile before starting Preheat.'),
            {
                type: 'warning',
                title: t('modal.no_profile_title', 'No Profile Selected'),
            }
        );
        return;
    }

    if (btnPreheat) btnPreheat.disabled = true;
    try {
        const res = await apiRequest('/api/control', 'POST', {
            action: 'preheat',
            profile: profName,
        });
        if (res && res.success) {
            console.log(`[FSM] Preheat started with ${profName}`);
        } else if (res && !res.success) {
            await Modal.alert(res.error || t('modal.preheat_failed', 'Could not start Preheat.'), {
                type: 'error',
                title: t('modal.error_title', 'Action Failed'),
            });
        }
    } finally {
        updateControlButtonsState();
    }
}

async function startReflow() {
    const btnStart = $('btn-start');
    if (btnStart?.disabled) return;

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

    // In IDLE state (0, 5, 6): Guide the user to start Preheat first with a 1-click action
    if (store.stateEnum === 0 || store.stateEnum === 5 || store.stateEnum === 6) {
        const profName = $('chart-profile-select')?.value || store.activeProfileFile;
        if (!profName) {
            await Modal.alert(
                t('modal.no_profile_preheat', 'Please select a profile before starting Preheat.'),
                {
                    type: 'warning',
                    title: t('modal.no_profile_title', 'No Profile Selected'),
                }
            );
            return;
        }
        const cleanProf = profName.replace(/\.json$/i, '');
        const ok = await Modal.confirm(
            t(
                'modal.preheat_required_body',
                'A BGA reflow cycle begins with preheating the PCB (bottom heater).\n\nWould you like to start Preheat now with profile "{{profile}}"?',
                { profile: cleanProf }
            ),
            {
                type: 'info',
                title: t('modal.preheat_required_title', 'Preheat Required'),
                confirmLabel: t('modal.btn_start_preheat', 'Start Preheat'),
                cancelLabel: t('modal.btn_cancel', 'Cancel'),
            }
        );
        if (ok) {
            await startPreheat();
        }
        return;
    }

    if (btnStart) btnStart.disabled = true;
    try {
        const res = await apiRequest('/api/control', 'POST', { action: 'reflow' });
        if (res && res.success) {
            console.log('[FSM] Reflow started.');
        } else if (res && !res.success) {
            await Modal.alert(res.error || t('modal.reflow_failed', 'Could not start Reflow.'), {
                type: 'error',
                title: t('modal.error_title', 'Action Failed'),
            });
        }
    } finally {
        updateControlButtonsState();
    }
}

async function stopReflow() {
    if (store.stateEnum === 6) {
        const res = await apiRequest('/api/control', 'POST', { action: 'resetFault' });
        if (res && res.success) {
            console.log('[FSM] Fault cleared.');
        }
        return;
    }
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

function updateSafetyBypassBadge() {
    const safetyBadge = $('safety-bypass-badge');
    if (safetyBadge) {
        safetyBadge.classList.toggle('u-hidden', store.enableSafetyWatchdog !== false);
    }
}

async function syncStatusWithController() {
    try {
        const status = await apiRequest('/api/status', 'GET');
        if (!status) return;

        const formatV = ver => (ver ? (String(ver).startsWith('v') ? ver : `v${ver}`) : '—');

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
            if (el)
                el.textContent =
                    `${status.buildDate || ''} ${status.buildTime || ''}`.trim() || '—';
        }

        // LittleFS Flash Storage diagnostics & Wear Leveling guard
        if (status.storage && typeof status.storage === 'object') {
            store.storage = status.storage;
            const el = $('sys-storage-usage');
            if (el) {
                if (!status.storage.mounted) {
                    el.textContent = '— (Unmounted)';
                    el.className = 'sys-info-val sys-info-val--danger';
                } else {
                    const usedKb = (status.storage.usedBytes / 1024).toFixed(1);
                    const totalMb = (status.storage.totalBytes / (1024 * 1024)).toFixed(2);
                    const pct =
                        status.storage.usedPercent !== undefined
                            ? status.storage.usedPercent.toFixed(1)
                            : '0.0';
                    el.textContent = `${usedKb} KB / ${totalMb} MB (${pct}%)`;
                    if (status.storage.isCritical) {
                        el.className = 'sys-info-val sys-info-val--danger';
                    } else if (status.storage.isWarning) {
                        el.className = 'sys-info-val sys-info-val--warning';
                    } else {
                        el.className = 'sys-info-val';
                    }
                }
            }

            // Storage wear leveling warning / critical alert modal
            if (status.storage.isCritical) {
                if (!store.storageCriticalNotified) {
                    store.storageCriticalNotified = true;
                    Modal.alert(
                        t(
                            'modal.storage_critical_msg',
                            'Flash storage usage has reached {{pct}}% (limit: {{limit}}%). Creating new profiles or files is now blocked to ensure wear leveling and prevent filesystem corruption.\n\nPlease delete unnecessary profiles or export backups to free up flash memory.',
                            {
                                pct: status.storage.usedPercent,
                                limit: status.storage.criticalThreshold,
                            }
                        ),
                        {
                            type: 'danger',
                            title: t(
                                'modal.storage_critical_title',
                                '⚠️ Flash Storage Limit Reached (Wear Leveling Protection)'
                            ),
                        }
                    );
                }
            } else {
                // Reset notification flag when storage usage is reduced below critical threshold
                store.storageCriticalNotified = false;
            }
        }

        // Warn user if firmware downgrade rejected newer configuration schema on flash
        if (status.schemaIncompatible && !store.schemaIncompatibleNotified) {
            store.schemaIncompatibleNotified = true;
            Modal.alert(
                t(
                    'modal.schema_downgrade_msg',
                    'Configuration files from a newer firmware version were found on the controller. For safety reasons, these were not loaded and the controller is currently running with safe factory defaults.\n\nOptions:\n• Update back to the newer firmware to keep using your existing configuration.\n• Or restore your existing files (settings.json, profiles, etc.) from your backup to ensure a clean migration.\n• Or save your settings in the respective tab to overwrite them with the current defaults.'
                ),
                {
                    type: 'warning',
                    title: t(
                        'modal.schema_downgrade_title',
                        '⚠️ Incompatible Configuration Data Detected (Downgrade)'
                    ),
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
            console.log(
                '[ReflowApp] Dynamic validation limits synced from firmware:',
                VALIDATION_LIMITS
            );
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
            console.log(
                '[ReflowApp] Dynamic validation resolutions synced from firmware:',
                VALIDATION_RESOLUTIONS
            );
        }

        applyValidationLimitsToDOM();

        if (typeof status.backupTaken === 'boolean') {
            store.backupTaken = status.backupTaken;
            updateBackupStageVisibility();
        }
    } catch (err) {
        console.warn('[ReflowApp] Could not sync status with controller:', err);
    }
}
