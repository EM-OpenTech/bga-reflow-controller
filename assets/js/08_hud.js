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
        const topSensorOk = (data.topSensorOk !== false) && (data.topTemp !== null && data.topTemp !== undefined);
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

        if (((store.stateEnum >= 1 && store.stateEnum <= 4) || store.stateEnum === 7) && topSensorOk) {
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
        const bottomSensorOk = (data.bottomSensorOk !== false) && (data.bottomTemp !== null && data.bottomTemp !== undefined);
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

        if (((store.stateEnum >= 1 && store.stateEnum <= 4) || store.stateEnum === 7) && bottomSensorOk) {
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
        } else if (topSensorOk && bottomSensorOk) {
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
        } else {
            setText('top-ramp', '--');
            setText('bottom-ramp', '--');
        }

        // 6. Elapsed Time
        store.elapsedSec = data.elapsed ?? 0;
        setText('elapsed-time', `${t('hud.runtime_prefix', 'Runtime:')} ${formatTime(store.elapsedSec)} min`);

        // 7. Time Above Liquidus (TAL: > 217 °C)
        store.talSec = (data.talSec !== undefined) ? data.talSec : store.talSec;
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
