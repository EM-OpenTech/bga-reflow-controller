/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 01_store.js
 * @brief Layer 1 Foundation: Central reactive state store, validation limits, and DOM helpers.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

'use strict';

const ReflowApp = window.ReflowApp || {};
window.ReflowApp = ReflowApp;

let wsInstance = null;
let wsReconnectAttempts = 0;
let isConnectingPhase = true;
let connectingTimer = null;

// ── 1. STATE STORE ──────────────────────────────────────────────────────
const store = {
    // Actor States (Direct from Backend)
    lampState: false,
    fanState: false,

    // System State (0=IDLE, 1=PREHEAT, 2=SOAK, 3=REFLOW, 4=COOLING, 5=DONE, 6=FAULT, 7=AUTOTUNE, 8=BACKUP)
    stateEnum: 0,
    stateStr: 'IDLE',
    prevStateEnum: null,
    preheatDone: false,

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
    currentTab: (function () {
        try {
            const s = localStorage.getItem('reflow_active_tab');
            return s && ['chart', 'profile', 'settings', 'tunepid', 'backup'].includes(s)
                ? s
                : 'chart';
        } catch (e) {
            return 'chart';
        }
    })(),
    currentTheme: 'light',
    currentLanguage: localStorage.getItem('reflow_lang') || 'en',
    softwareBuzzerEnabled: true,
    statusLogOpen: false,
    wsConnected: false,
    backupTaken: false,
    showZones: true,
    showTalLine: true,
    showStepMarkers: true,
    showPidGains: false,
    lastTelemetryTime: 0,

    // Stock PID Settings & Library Mode
    pidLibrary: null,
    pidLibraryEnabled: false,
    topKp: 2.0,
    topKi: 0.05,
    topKd: 1.0,
    bottomKp: 2.0,
    bottomKi: 0.04,
    bottomKd: 1.0,

    // Downgrade / Incompatibility flag
    schemaIncompatibleNotified: false,

    // Autotune Lifecycle & Abort Tracking
    userStoppedAutotune: false,
    lastTuneTargetTemp: 0,
    lastTuneIsTop: true,
};

// Centralized Global Validation Limits & Sanity Bounds (Re-usable for future UI inputs)
const VALIDATION_LIMITS = {
    MIN_TEMPERATURE: 20.0,
    MAX_TEMPERATURE: 300.0,
    MIN_RAMP_RATE: 0.1,
    MAX_RAMP_RATE: 10.0,
    MAX_STEP_TIME_S: 600,
    MIN_SAFE_COOLING_TEMP: 20.0,
    MAX_SAFE_COOLING_TEMP: 100.0,
    MIN_STUCK_SSR_RISE: 1.0,
    MAX_STUCK_SSR_RISE: 50.0,
    MIN_STUCK_SSR_SEC: 3,
    MAX_STUCK_SSR_SEC: 120,
    MIN_NO_RISE_THRESH: 0.5,
    MAX_NO_RISE_THRESH: 50.0,
    MIN_NO_RISE_TIMEOUT_S: 5,
    MAX_NO_RISE_TIMEOUT_S: 120,
    MIN_HOLD_TOLERANCE: 0.5,
    MAX_HOLD_TOLERANCE: 30.0,
    MIN_SETTLE_S: 1,
    MAX_SETTLE_S: 60,
    MIN_FAN_DELAY_S: 0,
    MAX_FAN_DELAY_S: 300,
    MIN_FAN_DURATION_S: 0,
    MAX_FAN_DURATION_S: 600,
    MIN_BURST_WINDOW_MS: 500,
    MAX_BURST_WINDOW_MS: 4000,
    MIN_EMA_ALPHA: 0.01,
    MAX_EMA_ALPHA: 1.0,
    MIN_FAULT_STREAK: 1,
    MAX_FAULT_STREAK: 20,
    MIN_CJ_OFFSET: -8.0,
    MAX_CJ_OFFSET: 7.9,
    MIN_PID_KP: 0.0,
    MAX_PID_KP: 100.0,
    MIN_PID_KI: 0.0,
    MAX_PID_KI: 10.0,
    MIN_PID_KD: 0.0,
    MAX_PID_KD: 100.0,
    MAX_PROFILE_STEPS: 10,
    MAX_PID_POINTS: 15,
    MAX_HISTORY_POINTS: 10800,
    MAX_JSON_SIZE_BYTES: 65536, // 64 KB
    MAX_ZIP_SIZE_BYTES: 524288, // 512 KB
};

// Centralized Domain Model Schema Versions (Firmware Compatibility Guards)
const SCHEMA_VERSIONS = {
    MACHINE_SETTINGS: 1,
    REFLOW_PROFILE: 1,
    PID_LIBRARY: 1,
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
    TIME_MS: 100,
};

const _domCache = Object.create(null);
const $ = id => {
    let el = _domCache[id];
    if (!el) {
        el = document.getElementById(id);
        if (el) _domCache[id] = el;
    }
    return el;
};

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
    setAttr(
        'set-max-top',
        VALIDATION_LIMITS.MIN_TEMPERATURE,
        VALIDATION_LIMITS.MAX_TEMPERATURE,
        VALIDATION_RESOLUTIONS.TEMPERATURE
    );
    setAttr(
        'set-max-bottom',
        VALIDATION_LIMITS.MIN_TEMPERATURE,
        VALIDATION_LIMITS.MAX_TEMPERATURE,
        VALIDATION_RESOLUTIONS.TEMPERATURE
    );
    setAttr(
        'set-filter-alpha',
        VALIDATION_LIMITS.MIN_EMA_ALPHA,
        VALIDATION_LIMITS.MAX_EMA_ALPHA,
        VALIDATION_RESOLUTIONS.FILTER_ALPHA
    );
    setAttr(
        'set-cj-top-offset',
        VALIDATION_LIMITS.MIN_CJ_OFFSET,
        VALIDATION_LIMITS.MAX_CJ_OFFSET,
        VALIDATION_RESOLUTIONS.OFFSET_TEMP
    );
    setAttr(
        'set-cj-bottom-offset',
        VALIDATION_LIMITS.MIN_CJ_OFFSET,
        VALIDATION_LIMITS.MAX_CJ_OFFSET,
        VALIDATION_RESOLUTIONS.OFFSET_TEMP
    );
    setAttr(
        'set-stuck-ssr-thresh',
        VALIDATION_LIMITS.MIN_STUCK_SSR_RISE,
        VALIDATION_LIMITS.MAX_STUCK_SSR_RISE,
        VALIDATION_RESOLUTIONS.TEMPERATURE
    );
    setAttr(
        'set-stuck-ssr-time',
        VALIDATION_LIMITS.MIN_STUCK_SSR_SEC,
        VALIDATION_LIMITS.MAX_STUCK_SSR_SEC,
        VALIDATION_RESOLUTIONS.TIME_SEC
    );
    setAttr(
        'set-norise-thresh',
        VALIDATION_LIMITS.MIN_NO_RISE_THRESH,
        VALIDATION_LIMITS.MAX_NO_RISE_THRESH,
        VALIDATION_RESOLUTIONS.TEMPERATURE
    );
    setAttr(
        'set-norise-time',
        VALIDATION_LIMITS.MIN_NO_RISE_TIMEOUT_S,
        VALIDATION_LIMITS.MAX_NO_RISE_TIMEOUT_S,
        VALIDATION_RESOLUTIONS.TIME_SEC
    );
    setAttr(
        'set-cooling-safe-temp',
        VALIDATION_LIMITS.MIN_SAFE_COOLING_TEMP,
        VALIDATION_LIMITS.MAX_SAFE_COOLING_TEMP,
        VALIDATION_RESOLUTIONS.TEMPERATURE
    );
    setAttr(
        'set-hold-low-tol',
        VALIDATION_LIMITS.MIN_HOLD_TOLERANCE,
        VALIDATION_LIMITS.MAX_HOLD_TOLERANCE,
        VALIDATION_RESOLUTIONS.TOLERANCE_TEMP
    );
    setAttr(
        'set-hold-high-tol',
        VALIDATION_LIMITS.MIN_HOLD_TOLERANCE,
        VALIDATION_LIMITS.MAX_HOLD_TOLERANCE,
        VALIDATION_RESOLUTIONS.TOLERANCE_TEMP
    );
    setAttr(
        'set-settle-time-s',
        VALIDATION_LIMITS.MIN_SETTLE_S,
        VALIDATION_LIMITS.MAX_SETTLE_S,
        VALIDATION_RESOLUTIONS.TIME_SEC
    );
    setAttr(
        'set-fan-delay',
        VALIDATION_LIMITS.MIN_FAN_DELAY_S,
        VALIDATION_LIMITS.MAX_FAN_DELAY_S,
        VALIDATION_RESOLUTIONS.TIME_SEC
    );
    setAttr(
        'set-fan-duration',
        VALIDATION_LIMITS.MIN_FAN_DURATION_S,
        VALIDATION_LIMITS.MAX_FAN_DURATION_S,
        VALIDATION_RESOLUTIONS.TIME_SEC
    );
    setAttr(
        'set-burst-top',
        VALIDATION_LIMITS.MIN_BURST_WINDOW_MS,
        VALIDATION_LIMITS.MAX_BURST_WINDOW_MS,
        VALIDATION_RESOLUTIONS.TIME_MS
    );
    setAttr(
        'set-burst-bottom',
        VALIDATION_LIMITS.MIN_BURST_WINDOW_MS,
        VALIDATION_LIMITS.MAX_BURST_WINDOW_MS,
        VALIDATION_RESOLUTIONS.TIME_MS
    );
    setAttr(
        'set-top-kp',
        VALIDATION_LIMITS.MIN_PID_KP,
        VALIDATION_LIMITS.MAX_PID_KP,
        VALIDATION_RESOLUTIONS.PID_GAIN_KP
    );
    setAttr(
        'set-top-ki',
        VALIDATION_LIMITS.MIN_PID_KI,
        VALIDATION_LIMITS.MAX_PID_KI,
        VALIDATION_RESOLUTIONS.PID_GAIN_KI
    );
    setAttr(
        'set-top-kd',
        VALIDATION_LIMITS.MIN_PID_KD,
        VALIDATION_LIMITS.MAX_PID_KD,
        VALIDATION_RESOLUTIONS.PID_GAIN_KD
    );
    setAttr(
        'set-bottom-kp',
        VALIDATION_LIMITS.MIN_PID_KP,
        VALIDATION_LIMITS.MAX_PID_KP,
        VALIDATION_RESOLUTIONS.PID_GAIN_KP
    );
    setAttr(
        'set-bottom-ki',
        VALIDATION_LIMITS.MIN_PID_KI,
        VALIDATION_LIMITS.MAX_PID_KI,
        VALIDATION_RESOLUTIONS.PID_GAIN_KI
    );
    setAttr(
        'set-bottom-kd',
        VALIDATION_LIMITS.MIN_PID_KD,
        VALIDATION_LIMITS.MAX_PID_KD,
        VALIDATION_RESOLUTIONS.PID_GAIN_KD
    );

    // Autotune and PID Preview inputs
    setAttr(
        'tune-temp',
        VALIDATION_LIMITS.MIN_TEMPERATURE,
        VALIDATION_LIMITS.MAX_TEMPERATURE,
        VALIDATION_RESOLUTIONS.TEMPERATURE
    );
    setAttr(
        'pid-preview-temp',
        VALIDATION_LIMITS.MIN_TEMPERATURE,
        VALIDATION_LIMITS.MAX_TEMPERATURE,
        VALIDATION_RESOLUTIONS.TEMPERATURE
    );
}
