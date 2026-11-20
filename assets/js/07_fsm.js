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

function getLogItemClass(line) {
    if (
        line.startsWith('E ') ||
        line.includes('CRITICAL') ||
        line.includes('ERROR') ||
        line.includes('FAULT')
    ) {
        return 'status-log-item status-log-item--error';
    }
    if (line.startsWith('W ') || line.includes('WARN') || line.includes('timeout')) {
        return 'status-log-item status-log-item--warn';
    }
    return 'status-log-item status-log-item--info';
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
        item.className = getLogItemClass(line);
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
    if (!badge) {
        return;
    }

    // 1. Fixed Connecting Phase (2.5s on load or reconnect)
    if (isConnectingPhase) {
        badge.innerText = t('status.connecting', 'CONNECTING...');
        badge.className = 'status-badge status-connecting';
        return;
    }

    // 2. Operational: Connected and telemetry active with state
    if (store.wsConnected && store.stateStr) {
        updateStatusBadge(store.stateEnum, store.stateStr);
        return;
    }

    // 3. Operational: Connected without state yet
    if (store.wsConnected) {
        badge.innerText = t('status.connected', 'CONNECTED');
        badge.className = 'status-badge status-idle';
        return;
    }

    // 4. Disconnected: No active connection to ESP32 -> GETRENNT / DISCONNECTED (Rot)
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

    const nextState = Number(stateEnum);
    const prevState = store.stateEnum;

    // Rising edge detection for DONE state: play Bambu Lab completion melody
    // Trigger only when transitioning from an active heating/cooling state (1..4) into DONE (5)
    if (prevState !== null && prevState >= 1 && prevState <= 4 && nextState === 5) {
        Sound.playDoneMelody();
    }

    // Rising edge detection for Preheat Done: play cheerful notification chirp
    if (!store.preheatDone && Boolean(preheatDone) && nextState === 1) {
        Sound.playPreheatDone();
    }

    store.prevStateEnum = prevState;
    store.stateEnum = nextState;
    store.stateStr = stateStr || STRINGS.STATE_IDLE;
    store.preheatDone = Boolean(preheatDone);

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
    if (isHeatingProcess(stateEnum)) {
        acquireWakeLock();
        return;
    }
    releaseWakeLock();
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
    if (!isConnectionLive()) {
        return;
    }
    const next = !store.lampState;
    store.lampState = next;
    updateActorIcons();
    apiRequest('/api/overrides', 'POST', { lamp: next });
}

function toggleFan() {
    if (!isConnectionLive()) {
        return;
    }
    const next = !store.fanState;
    store.fanState = next;
    updateActorIcons();
    apiRequest('/api/overrides', 'POST', { fan: next });
}

// ── 9. PROCESS CONTROLS & SAFETY INTERLOCK ─────────────────────────────
function setElementTooltip(element, titleText) {
    if (!element) {
        return;
    }
    if (typeof titleText === 'string' && titleText.length > 0) {
        element.setAttribute('title', titleText);
        return;
    }
    element.removeAttribute('title');
}

function getPreheatButtonTooltip(stateEnum) {
    if (stateEnum === 1) {
        return t('ctrl.preheat_running_hint', 'Preheat is currently in progress');
    }
    if (stateEnum === 2 || stateEnum === 3) {
        return t('ctrl.preheat_locked_reflow_hint', 'Disabled during active Reflow cycle');
    }
    if (stateEnum === 6) {
        return t('ctrl.locked_fault_hint', 'Disabled due to safety fault. Reset fault first.');
    }
    if (stateEnum === 7) {
        return t('ctrl.locked_autotune_hint', 'Disabled during Autotune');
    }
    if (stateEnum === 8) {
        return t('ctrl.locked_backup_hint', 'Disabled in Backup mode');
    }
    return t(
        'ctrl.preheat_idle_hint',
        'Step 1: Start bottom preheater to prepare board'
    );
}

function getStartButtonTooltip(stateEnum, preheatDone) {
    if (stateEnum === 1 && preheatDone) {
        return t(
            'ctrl.start_preheat_done_hint',
            'Preheat complete! Click START to begin Reflow profile'
        );
    }
    if (stateEnum === 1) {
        return t(
            'ctrl.start_preheating_locked_hint',
            'Preheating in progress... START unlocks when preheat temperature is reached'
        );
    }
    if (stateEnum === 2 || stateEnum === 3) {
        return t('ctrl.start_running_hint', 'Reflow profile is currently running');
    }
    if (stateEnum === 4) {
        return t('ctrl.start_cooling_hint', 'Cooling phase in progress');
    }
    if (stateEnum === 6) {
        return t('ctrl.locked_fault_hint', 'Disabled due to safety fault. Reset fault first.');
    }
    if (stateEnum === 7) {
        return t('ctrl.locked_autotune_hint', 'Disabled during Autotune');
    }
    if (stateEnum === 8) {
        return t('ctrl.locked_backup_hint', 'Disabled in Backup mode');
    }
    return t('ctrl.start_idle_hint', 'Step 2: Starts Reflow after preheating is complete');
}

function getAutotuneButtonTooltip(stateEnum) {
    if (stateEnum === 7) {
        return t('ctrl.tune_running_hint', 'PID Autotune is currently in progress');
    }
    if (stateEnum === 1 || stateEnum === 2 || stateEnum === 3 || stateEnum === 4) {
        return t('ctrl.tune_locked_heating_hint', 'Disabled during active heating or cooling cycle');
    }
    if (stateEnum === 6) {
        return t('ctrl.locked_fault_hint', 'Disabled due to safety fault. Reset fault first.');
    }
    if (stateEnum === 8) {
        return t('ctrl.locked_backup_hint', 'Disabled in Backup mode');
    }
    return t('ctrl.tune_idle_hint', 'Start PID Autotune for the selected heater');
}

function getStopButtonTooltip(stateEnum) {
    if (stateEnum === 6) {
        return t('ctrl.reset_fault_hint', 'Clear fault state and restore system to IDLE');
    }
    return t('ctrl.stop_hint', 'Emergency Stop: Immediately turn off all heaters');
}

function updateStopButton(btnStop, stateEnum) {
    if (!btnStop) {
        return;
    }
    btnStop.disabled = false;
    setElementTooltip(btnStop, getStopButtonTooltip(stateEnum));
    if (stateEnum === 6) {
        btnStop.textContent = t('ctrl.reset_fault', '↺ RESET FAULT');
        btnStop.classList.remove('btn-danger');
        btnStop.classList.add('btn-warning');
        return;
    }
    btnStop.textContent = t('ctrl.stop', '■ STOP');
    btnStop.classList.remove('btn-warning');
    btnStop.classList.add('btn-danger');
}

function getSkipButtonTooltip(stateEnum) {
    if (stateEnum === 1 || stateEnum === 2 || stateEnum === 3) {
        return t('ctrl.skip_step_hint', 'Skip to the next profile step');
    }
    return t('ctrl.skip_disabled_hint', 'Step skipping only available during active reflow phases');
}

function updateControlActionButtons(stateEnum, isLive) {
    const btnStart = $('btn-start');
    const btnPreheat = $('btn-preheat');
    const btnStop = $('btn-stop');
    const btnStartTune = $('btn-start-autotune');
    const btnSkip = $('btn-skip');
    const navBackup = $('nav-tab-backup');
    const chartSelect = $('chart-profile-select');
    const btnLamp = $('icon-lamp');
    const btnFan = $('icon-fan');

    updateStopButton(btnStop, stateEnum);

    if (!isLive) {
        const tooltipDis = t(
            'ctrl.tooltip_disconnected',
            '⚠️ Telemetry disconnected - starting a process is locked until connection is restored.'
        );
        const tooltipDisAction = t(
            'ctrl.tooltip_disconnected_action',
            '⚠️ Controller disconnected – modifications are locked until connection is restored.'
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
        if (btnSkip) {
            btnSkip.disabled = true;
            btnSkip.setAttribute('title', tooltipDis);
        }
        if (navBackup) {
            navBackup.disabled = true;
            navBackup.setAttribute('title', tooltipDisAction);
        }
        if (chartSelect) {
            chartSelect.disabled = true;
            chartSelect.setAttribute('title', tooltipDisAction);
        }
        if (btnLamp) {
            btnLamp.disabled = true;
            btnLamp.setAttribute('title', tooltipDisAction);
        }
        if (btnFan) {
            btnFan.disabled = true;
            btnFan.setAttribute('title', tooltipDisAction);
        }
        return;
    }

    const canPreheat = stateEnum === 0 || stateEnum === 4 || stateEnum === 5;
    const canStart = stateEnum === 0 || stateEnum === 5 || (stateEnum === 1 && store.preheatDone);
    const canTune = stateEnum === 0 || stateEnum === 5;
    const canSkip = stateEnum === 1 || stateEnum === 2 || stateEnum === 3;
    const canBackupTab = stateEnum === 0 || stateEnum === 5 || stateEnum === 8;

    if (btnPreheat) {
        btnPreheat.disabled = !canPreheat;
        setElementTooltip(btnPreheat, getPreheatButtonTooltip(stateEnum));
    }
    if (btnStart) {
        btnStart.disabled = !canStart;
        setElementTooltip(btnStart, getStartButtonTooltip(stateEnum, store.preheatDone));
    }
    if (btnStartTune) {
        btnStartTune.disabled = !canTune;
        setElementTooltip(btnStartTune, getAutotuneButtonTooltip(stateEnum));
    }
    if (btnSkip) {
        btnSkip.disabled = !canSkip;
        setElementTooltip(btnSkip, getSkipButtonTooltip(stateEnum));
    }
    if (navBackup) {
        navBackup.disabled = !canBackupTab;
        setElementTooltip(
            navBackup,
            canBackupTab
                ? ''
                : t(
                      'nav.backup_locked_hint',
                      'Backup is locked during active heating or autotune'
                  )
        );
    }
    if (chartSelect) {
        const isBlocked = isChartProfileBlocked(stateEnum);
        chartSelect.disabled = isBlocked;
        setElementTooltip(
            chartSelect,
            isBlocked
                ? (!isLive
                    ? t(
                          'ctrl.tooltip_disconnected_action',
                          '⚠️ Controller disconnected – modifications are locked until connection is restored.'
                      )
                    : (stateEnum === 6
                        ? t(
                              'ctrl.locked_fault_hint',
                              'Disabled due to safety fault. Reset fault first.'
                          )
                        : t(
                              'ctrl.tune_locked_heating_hint',
                              'Disabled during active heating or cooling cycle'
                          )))
                : ''
        );
    }
    if (btnLamp) {
        btnLamp.disabled = false;
        btnLamp.removeAttribute('title');
    }
    if (btnFan) {
        btnFan.disabled = false;
        btnFan.removeAttribute('title');
    }
}

function getLockTooltip(isModificationBlocked, isLive, activeMsg) {
    if (!isModificationBlocked) {
        return '';
    }
    if (!isLive) {
        return t(
            'ctrl.tooltip_disconnected_action',
            '⚠️ Controller disconnected – modifications are locked until connection is restored.'
        );
    }
    return activeMsg || '';
}

function updateSettingsLocking(isModificationBlocked, isLive) {
    const btnUpdateWifi = $('btn-update-wifi-pass');
    const inputWifiPass = $('set-wifi-new-pass');
    const btnSaveSettings = $('btn-save-settings');
    const btnSaveProfile = $('btn-save-profile');
    const btnDeleteProfile = $('btn-delete-profile');
    const btnSavePid = $('btn-save-pidlib');
    const selectTuneChannel = $('tune-channel');
    const inputTuneTemp = $('tune-temp');
    const selectTuneMethod = $('tune-method');
    const selectProfile = $('profile-select');
    const btnNewProfile = $('btn-new-profile');
    const btnAddStepTop = $('btn-add-step-top');
    const btnAddStepBottom = $('btn-add-step-bottom');
    const btnAddPidTop = $('btn-add-pid-top');
    const btnAddPidBottom = $('btn-add-pid-bottom');

    if (btnUpdateWifi) {
        btnUpdateWifi.disabled = isModificationBlocked;
        setElementTooltip(
            btnUpdateWifi,
            getLockTooltip(
                isModificationBlocked,
                isLive,
                t(
                    'modal.wifi_pass_locked_active',
                    'Wi-Fi password update blocked during active process or maintenance.'
                )
            )
        );
    }
    if (inputWifiPass) {
        inputWifiPass.disabled = isModificationBlocked;
    }
    if (btnSaveSettings) {
        btnSaveSettings.disabled = isModificationBlocked;
        setElementTooltip(
            btnSaveSettings,
            getLockTooltip(
                isModificationBlocked,
                isLive,
                t(
                    'modal.save_settings_locked_active',
                    'Settings modification blocked during active process or maintenance.'
                )
            )
        );
    }
    if (btnSaveProfile) {
        btnSaveProfile.disabled = isModificationBlocked;
        setElementTooltip(
            btnSaveProfile,
            getLockTooltip(
                isModificationBlocked,
                isLive,
                t(
                    'modal.save_profile_locked_active',
                    'Profile modification blocked during active process or maintenance.'
                )
            )
        );
    }
    if (btnDeleteProfile) {
        btnDeleteProfile.disabled = isModificationBlocked;
        setElementTooltip(
            btnDeleteProfile,
            getLockTooltip(
                isModificationBlocked,
                isLive,
                t(
                    'modal.delete_profile_locked_active',
                    'Profile deletion blocked during active process or maintenance.'
                )
            )
        );
    }
    if (btnSavePid) {
        btnSavePid.disabled = isModificationBlocked;
        setElementTooltip(
            btnSavePid,
            getLockTooltip(
                isModificationBlocked,
                isLive,
                t(
                    'modal.save_pid_locked_active',
                    'PID modifications are not permitted during an active heating process.'
                )
            )
        );
    }
    if (selectTuneChannel) {
        selectTuneChannel.disabled = isModificationBlocked;
    }
    if (inputTuneTemp) {
        inputTuneTemp.disabled = isModificationBlocked;
    }
    if (selectTuneMethod) {
        selectTuneMethod.disabled = isModificationBlocked;
    }
    if (selectProfile) {
        // Profile selection is locked when disconnected; unlocked during active processes to allow read-only inspection
        selectProfile.disabled = !isLive;
        setElementTooltip(selectProfile, getLockTooltip(!isLive, isLive, ''));
    }
    if (btnNewProfile) {
        btnNewProfile.disabled = isModificationBlocked;
        setElementTooltip(btnNewProfile, getLockTooltip(isModificationBlocked, isLive, ''));
    }
    if (btnAddStepTop) {
        btnAddStepTop.disabled = isModificationBlocked;
        setElementTooltip(btnAddStepTop, getLockTooltip(isModificationBlocked, isLive, ''));
    }
    if (btnAddStepBottom) {
        btnAddStepBottom.disabled = isModificationBlocked;
        setElementTooltip(btnAddStepBottom, getLockTooltip(isModificationBlocked, isLive, ''));
    }
    if (btnAddPidTop) {
        btnAddPidTop.disabled = isModificationBlocked;
        setElementTooltip(btnAddPidTop, getLockTooltip(isModificationBlocked, isLive, ''));
    }
    if (btnAddPidBottom) {
        btnAddPidBottom.disabled = isModificationBlocked;
        setElementTooltip(btnAddPidBottom, getLockTooltip(isModificationBlocked, isLive, ''));
    }

    const tableElements = document.querySelectorAll(
        '.profile-table input, .profile-table .btn-delete-step, .pid-table input, .pid-table .btn-delete-step'
    );
    tableElements.forEach(el => {
        el.disabled = isModificationBlocked;
    });

    const settingsInputs = document.querySelectorAll(
        '#panel-settings input:not(#set-wifi-new-pass), #panel-settings select'
    );
    settingsInputs.forEach(el => {
        el.disabled = isModificationBlocked;
    });
}

function updateBackupLocking(isBackupPermitted) {
    const isLive = isConnectionLive();
    const btnOrb = $('btn-backup-orb');
    if (btnOrb) {
        btnOrb.disabled = !isBackupPermitted;
        btnOrb.classList.toggle('orb-disabled', !isBackupPermitted);
        setElementTooltip(
            btnOrb,
            isBackupPermitted
                ? t('backup.orb_title', 'Click the blue orb to download all files')
                : (!isLive
                      ? t(
                            'ctrl.tooltip_disconnected_action',
                            '⚠️ Controller disconnected – modifications are locked until connection is restored.'
                        )
                      : t(
                            'backup.orb_locked_hint',
                            'Maintenance mode (BACKUP state) required to generate backup'
                        ))
        );
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

function updateControlButtonsState() {
    const isLive = isConnectionLive();
    const st = store.stateEnum;
    const isBlocked = isModificationBlocked();
    // Backup & Maintenance controls are ONLY permitted when controller is in BACKUP state (8) and connection is live.
    const isBackupPermitted = isLive && st === 8;

    updateControlActionButtons(st, isLive);
    updateSettingsLocking(isBlocked, isLive);
    updateBackupLocking(isBackupPermitted);
}

async function startPreheat() {
    const btnPreheat = $('btn-preheat');
    if (btnPreheat?.disabled || !isConnectionLive()) return;

    if (store.stateEnum !== 0 && store.stateEnum !== 4 && store.stateEnum !== 5) {
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
        if (res?.success) {
            console.log(`[FSM] Preheat started with ${profName}`);
            return;
        }
        if (res && !res.success) {
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
    if (btnStart?.disabled || !isConnectionLive()) return;

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
        const cleanProf = Format.profileName(profName);
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

    // Safety Interlock: Reflow can only be advanced from PREHEAT once preheatDone is reached
    if (store.stateEnum === 1 && !store.preheatDone) {
        await Modal.alert(
            t(
                'modal.preheat_not_done_msg',
                'Preheating is still in progress. Starting Reflow is locked until the bottom preheat temperature is reached and settled.'
            ),
            {
                type: 'warning',
                title: t('modal.preheat_not_done_title', 'Preheating in Progress'),
            }
        );
        return;
    }

    if (btnStart) btnStart.disabled = true;
    try {
        const res = await apiRequest('/api/control', 'POST', { action: 'reflow' });
        if (res?.success) {
            console.log('[FSM] Reflow started.');
            return;
        }
        if (res && !res.success) {
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
    if (store.stateEnum === 7) {
        store.userStoppedAutotune = true;
    }
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
    const btnSkip = $('btn-skip');
    if (
        btnSkip?.disabled ||
        (store.stateEnum !== 1 && store.stateEnum !== 2 && store.stateEnum !== 3)
    ) {
        return;
    }
    const res = await apiRequest('/api/control', 'POST', { action: 'skip' });
    if (res?.success) {
        console.log('[FSM] Step skipped.');
        Sound.playTone(880, 80);
    }
}

function updateSafetyBypassBadge() {
    const safetyBadge = $('safety-bypass-badge');
    if (safetyBadge) {
        safetyBadge.classList.toggle('u-hidden', store.enableSafetyWatchdog !== false);
    }
}

function formatStorageUsage(storage) {
    if (!storage || !storage.mounted) {
        return { text: '— (Unmounted)', cssClass: 'sys-info-val sys-info-val--danger' };
    }

    const used_kb = (storage.usedBytes / 1024).toFixed(1);
    const total_mb = (storage.totalBytes / (1024 * 1024)).toFixed(2);
    const used_percent =
        storage.usedPercent !== undefined ? storage.usedPercent.toFixed(1) : '0.0';

    if (storage.isCritical) {
        return {
            text: `${used_kb} KB / ${total_mb} MB (${used_percent}%)`,
            cssClass: 'sys-info-val sys-info-val--danger',
        };
    }
    if (storage.isWarning) {
        return {
            text: `${used_kb} KB / ${total_mb} MB (${used_percent}%)`,
            cssClass: 'sys-info-val sys-info-val--warning',
        };
    }
    return {
        text: `${used_kb} KB / ${total_mb} MB (${used_percent}%)`,
        cssClass: 'sys-info-val',
    };
}

function checkStorageCriticalAlert(storage) {
    if (!storage?.isCritical) {
        store.storageCriticalNotified = false;
        return;
    }
    if (store.storageCriticalNotified) {
        return;
    }
    store.storageCriticalNotified = true;
    Modal.alert(
        t(
            'modal.storage_critical_msg',
            'Flash storage usage has reached {{pct}}% (limit: {{limit}}%). Creating new profiles or files is now blocked to ensure wear leveling and prevent filesystem corruption.\n\nPlease delete unnecessary profiles or export backups to free up flash memory.',
            {
                pct: storage.usedPercent,
                limit: storage.criticalThreshold,
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

function checkSchemaIncompatibility(status) {
    if (!status?.schemaIncompatible || store.schemaIncompatibleNotified) {
        return;
    }
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
                const info = formatStorageUsage(status.storage);
                el.textContent = info.text;
                el.className = info.cssClass;
            }
            checkStorageCriticalAlert(status.storage);
        }

        checkSchemaIncompatibility(status);

        // Dynamically synchronize validation limits from controller firmware (Single Source of Truth)
        if (status.limits && typeof status.limits === 'object') {
            for (const [k, v] of Object.entries(status.limits)) {
                VALIDATION_LIMITS[k] =
                    typeof v === 'number' ? Math.round((v + Number.EPSILON) * 1000) / 1000 : v;
            }
            console.log(
                '[ReflowApp] Dynamic validation limits synced from firmware:',
                VALIDATION_LIMITS
            );
        }

        // Dynamically synchronize input step resolutions from controller firmware
        if (status.resolutions && typeof status.resolutions === 'object') {
            for (const [k, v] of Object.entries(status.resolutions)) {
                VALIDATION_RESOLUTIONS[k] =
                    typeof v === 'number' ? Math.round((v + Number.EPSILON) * 1000) / 1000 : v;
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
