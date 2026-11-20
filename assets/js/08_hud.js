/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 08_hud.js
 * @brief Layer 3 Telemetry Engine: Real-time Cockpit HUD gauges, TAL, delta T, settle/hold indicators.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 12. TELEMETRY HUD UPDATER (CALLED VIA WS) ───────────────────────────
function syncAutotuneHUD(data, isTuneActive) {
    const tuneRunningEl = $('autotune-running');
    const btnStartTune = $('btn-start-autotune');
    const btnStopTune = $('btn-stop-autotune');
    const chartBanner = $('chart-autotune-banner');
    const chartTuneBar = $('chart-tune-progress');
    const chartTuneLbl = $('chart-tune-percent');

    if (!isTuneActive) {
        chartBanner?.classList.add('u-hidden');
        tuneRunningEl?.classList.add('u-hidden');
        btnStartTune?.classList.remove('u-hidden');
        btnStopTune?.classList.add('u-hidden');
        return;
    }

    btnStartTune?.classList.add('u-hidden');
    btnStopTune?.classList.remove('u-hidden');
    tuneRunningEl?.classList.remove('u-hidden');
    chartBanner?.classList.remove('u-hidden');

    const pct = Math.min(100, Math.max(0, Math.round(data.tuneProgress || 0)));
    if (chartTuneBar) chartTuneBar.style.width = `${pct}%`;
    if (chartTuneLbl) chartTuneLbl.innerText = `${pct}%`;
}

function updateTelemetryHUD(data) {
    if (!data) return;

    // State Machine Transition Detection: Reset peaks and chart ONLY when a new run starts live (PREHEAT or AUTOTUNE)
    const nextState = Number(data.stateEnum ?? 0);
    if (
        store.prevStateEnum !== null &&
        store.prevStateEnum === 0 &&
        (nextState === 1 || nextState === 7)
    ) {
        store.topPeakTemp = data.topTemp || 0.0;
        store.bottomPeakTemp = data.bottomTemp || 0.0;
        store.hasFirstRampReading = false;
        resetChart();
    }
    store.prevStateEnum = nextState;

    // 1. Status Badge
    updateStatusBadge(data.stateEnum, data.state, data.preheatDone);

    // 2. Active Profile Name – sync dropdown on initial connect or when running process changes profile
    const shouldSyncProfile =
        Boolean(data.profileFile) &&
        (!store.activeProfileFile || (nextState >= 1 && data.profileFile !== store.activeProfileFile));
    if (shouldSyncProfile) {
        store.activeProfileFile = data.profileFile;
        store.activeProfileName = Format.profileName(data.profileFile);
        const cSel = $('chart-profile-select');
        if (cSel && cSel.querySelector(`option[value="${data.profileFile}"]`)) {
            cSel.value = data.profileFile;
        }
    }
    // Lock profile select dropdown during active processes, fault, or offline
    const cSel2 = $('chart-profile-select');
    if (cSel2) cSel2.disabled = isChartProfileBlocked(nextState);

    // 3. Live Top Heater HUD
    const topSensorOk =
        data.topSensorOk !== false && data.topTemp !== null && data.topTemp !== undefined;
    store.topSensorOk = topSensorOk;
    if (topSensorOk) {
        store.topTemp = Number(data.topTemp);
        setText('top-temp', Format.temp(store.topTemp));
    } else {
        setText('top-temp', '--');
    }
    store.topSet = data.topSet ?? store.topSet;
    store.topPower = data.topPower ?? store.topPower;
    setText('top-set', Format.temp(store.topSet));
    setText('top-power', Format.int(store.topPower));

    if (isHeatingProcess() && topSensorOk) {
        if (store.topTemp > store.topPeakTemp) store.topPeakTemp = store.topTemp;
    }
    setText('top-peak', Format.temp(store.topPeakTemp));

    const isProcessActive = isHeatingProcess();

    // Settle / Hold Status Badge Helper
    const updateWaitBadge = (el, isSettling, isHolding, settleSec, holdSec) => {
        if (!el) return;
        if (!isProcessActive) {
            el.classList.add('u-hidden');
            return;
        }
        if (isSettling) {
            el.className = 'wait-alert wait-alert--settle';
            el.innerText = `⏳ Settle (${settleSec ?? 0}s)`;
            el.classList.remove('u-hidden');
        } else if (isHolding) {
            el.className = 'wait-alert wait-alert--hold';
            el.innerText = `⏱ Hold (${holdSec ?? 0}s)`;
            el.classList.remove('u-hidden');
        } else {
            el.classList.add('u-hidden');
        }
    };

    // Top Settle / Hold Badge
    updateWaitBadge(
        $('top-wait'),
        data.topSettling,
        data.topHolding,
        data.topSettleRemainSec,
        data.topHoldRemainSec
    );

    // 4. Live Bottom Heater HUD
    const bottomSensorOk =
        data.bottomSensorOk !== false && data.bottomTemp !== null && data.bottomTemp !== undefined;
    store.bottomSensorOk = bottomSensorOk;
    if (bottomSensorOk) {
        store.bottomTemp = Number(data.bottomTemp);
        setText('bottom-temp', Format.temp(store.bottomTemp));
    } else {
        setText('bottom-temp', '--');
    }
    store.bottomSet = data.bottomSet ?? store.bottomSet;
    store.bottomPower = data.bottomPower ?? store.bottomPower;
    setText('bottom-set', Format.temp(store.bottomSet));
    setText('bottom-power', Format.int(store.bottomPower));

    if (isHeatingProcess() && bottomSensorOk) {
        if (store.bottomTemp > store.bottomPeakTemp) store.bottomPeakTemp = store.bottomTemp;
    }
    setText('bottom-peak', Format.temp(store.bottomPeakTemp));

    // Bottom Settle / Hold Badge
    updateWaitBadge(
        $('bottom-wait'),
        data.bottomSettling,
        data.bottomHolding,
        data.bottomSettleRemainSec,
        data.bottomHoldRemainSec
    );

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
    } else if (topSensorOk && bottomSensorOk) {
        const dt = (now - store.lastRampCalcTime) / 1000.0;
        if (dt >= 1.0) {
            store.topRampRate = (store.topTemp - store.lastTopTemp) / dt;
            store.bottomRampRate = (store.bottomTemp - store.lastBottomTemp) / dt;

            const fmtRate = r => (r >= 0 ? `+${Format.temp(r)}` : Format.temp(r));
            setText('top-ramp', fmtRate(store.topRampRate));
            setText('bottom-ramp', fmtRate(store.bottomRampRate));

            store.lastTopTemp = store.topTemp;
            store.lastBottomTemp = store.bottomTemp;
            store.lastRampCalcTime = now;
        }
    } else {
        setText('top-ramp', '--');
        setText('bottom-ramp', '--');
    }

    // 6. Elapsed Time
    store.elapsedSec = data.elapsed ?? 0;
    setText(
        'elapsed-time',
        `${t('hud.runtime_prefix', 'Runtime:')} ${formatTime(store.elapsedSec)} min`
    );

    // 7. Time Above Liquidus (TAL: > 217 °C)
    store.talSec = data.talSec !== undefined ? data.talSec : store.talSec;
    setText('stat-tal', `${store.talSec}s`);

    // 8. Delta T (Top vs Bottom)
    if (topSensorOk && bottomSensorOk) {
        const deltaT = Math.abs(store.topTemp - store.bottomTemp);
        setText('stat-deltat', `${Format.temp(deltaT)} °C`);
    } else {
        setText('stat-deltat', `-- °C`);
    }

    // 9. Feed Chart with Live Data (during active process, cooling/done or autotune)
    if ((store.stateEnum >= 1 && store.stateEnum <= 5) || store.stateEnum === 7) {
        appendChartData(
            store.elapsedSec,
            topSensorOk ? store.topTemp : null,
            store.topSet,
            bottomSensorOk ? store.bottomTemp : null,
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

    // 12. Autotune Progress & State Synchronization (Edge Detection)
    const isTuneActive = Boolean(data.tuneActive);
    syncAutotuneHUD(data, isTuneActive);

    // Falling Edge Detection: true -> false transition while browser was connected
    if (store.prevTuneActive === true && !isTuneActive) {
        if (data.tuneFinished && data.tuneKp && data.tuneKp > 0) {
            handleAutotuneCompleted(data);
        } else if (store.userStoppedAutotune) {
            store.userStoppedAutotune = false;
            console.log('[TunePID] Autotune stopped by user.');
        } else if (typeof handleAutotuneAborted === 'function') {
            handleAutotuneAborted(data);
        }
    }
    store.prevTuneActive = isTuneActive;

    // 13. Synchronize Control Buttons Interlock State
    updateControlButtonsState();
}
