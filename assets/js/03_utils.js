/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 03_utils.js
 * @brief Layer 1 Foundation: Mathematical formatting (DIN 1333), time formatting, DOM setters.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

const Format = {
    round: (val, decimals = 1, fallback = '--') => {
        if (val === null || val === undefined || val === '') {
            return fallback;
        }
        const num = Number(val);
        if (!Number.isFinite(num)) {
            return fallback;
        }
        const factor = Math.pow(10, decimals);
        return (Math.round((num + Number.EPSILON) * factor) / factor).toFixed(decimals);
    },
    temp: val => Format.round(val, 1, '0.0'),
    ramp: val => Format.round(val, 2, '0.00'),
    kp: val => Format.round(val, 2, '0.00'),
    kd: val => Format.round(val, 2, '0.00'),
    ki: val => Format.round(val, 3, '0.000'),
    alpha: val => Format.round(val, 3, '0.000'),
    int: val => {
        if (val === null || val === undefined || val === '') return '0';
        const num = Number(val);
        return Number.isFinite(num) ? Math.round(num).toString() : '0';
    },
    pct: val => {
        if (val === null || val === undefined || val === '') return '0';
        const num = Number(val);
        return Number.isFinite(num) ? Math.round(num).toString() : '0';
    },
    parseFloat: (val, fallback = 0) => {
        if (val === null || val === undefined || val === '') return fallback;
        const clean = String(val).replace(',', '.').trim();
        const num = Number.parseFloat(clean);
        return Number.isFinite(num) ? num : fallback;
    },
    parseInt: (val, fallback = 0) => {
        if (val === null || val === undefined || val === '') return fallback;
        const clean = String(val).replace(',', '.').trim();
        const num = Number.parseInt(clean, 10);
        return Number.isFinite(num) ? num : fallback;
    },
    profileName: val => {
        if (!val || typeof val !== 'string') return '';
        return val.replace(/\.json$/i, '');
    },
};

const ICONS = {
    TRASH: '<svg viewBox="0 0 24 24" class="icon-trash" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3 6h18M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M10 11v6M14 11v6"/></svg>',
};

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

function isConnectionLive() {
    return Boolean(
        store.wsConnected &&
        store.lastTelemetryTime > 0 &&
        ((Date.now() - store.lastTelemetryTime) < 3500)
    );
}

function isHeatingProcess(state = store.stateEnum) {
    return (state >= 1 && state <= 4) || state === 7;
}

function isModificationBlocked(state = store.stateEnum) {
    if (!isConnectionLive()) {
        return true;
    }
    return state === 1 || state === 2 || state === 3 || state === 4 || state === 7 || state === 8;
}

function isChartProfileBlocked(state = store.stateEnum) {
    if (!isConnectionLive()) {
        return true;
    }
    return state !== 0 && state !== 5;
}

// ── Web Audio Synthesizer (Software Buzzer) ─────────────────────────────────
let _audioCtx = null;

function getAudioContext() {
    if (!_audioCtx && typeof window !== 'undefined') {
        const AudioCtxClass = window.AudioContext || window.webkitAudioContext;
        if (AudioCtxClass) {
            _audioCtx = new AudioCtxClass();
        }
    }
    if (_audioCtx && _audioCtx.state === 'suspended') {
        _audioCtx.resume().catch(() => {});
    }
    return _audioCtx;
}

if (typeof document !== 'undefined') {
    const unlockAudio = () => {
        if (_audioCtx && _audioCtx.state === 'suspended') {
            _audioCtx.resume().catch(() => {});
        }
    };
    document.addEventListener('pointerdown', unlockAudio, { passive: true });
    document.addEventListener('keydown', unlockAudio, { passive: true });
}

function playChime(ctx, notes, waveType = 'triangle', baseVol = 0.80) {
    let start = ctx.currentTime + 0.02;
    notes.forEach((n) => {
        const osc = ctx.createOscillator();
        const gain = ctx.createGain();
        osc.type = waveType;
        osc.frequency.setValueAtTime(n.freq, start);

        // Soft Attack (8ms) to eliminate harsh clicks
        gain.gain.setValueAtTime(0.0001, start);
        gain.gain.linearRampToValueAtTime(baseVol * (n.gain || 1.0), start + 0.008);

        // Exponential natural resonance decay
        gain.gain.exponentialRampToValueAtTime(0.0001, start + n.ringSec);

        osc.connect(gain);
        gain.connect(ctx.destination);
        osc.start(start);
        osc.stop(start + n.ringSec);

        start += n.offsetSec;
    });
}

const Sound = {
    playTone: (freq, durationMs, type = 'triangle', gainVal = 0.80) => {
        if (!store.softwareBuzzerEnabled) return;
        const ctx = getAudioContext();
        if (!ctx) return;
        try {
            const osc = ctx.createOscillator();
            const gainNode = ctx.createGain();
            const durSec = durationMs / 1000;
            osc.type = type;
            osc.frequency.setValueAtTime(freq, ctx.currentTime);
            gainNode.gain.setValueAtTime(0.0001, ctx.currentTime);
            gainNode.gain.linearRampToValueAtTime(gainVal, ctx.currentTime + 0.006);
            gainNode.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + durSec);
            osc.connect(gainNode);
            gainNode.connect(ctx.destination);
            osc.start();
            osc.stop(ctx.currentTime + durSec);
        } catch (_) {}
    },
    playDoneMelody: () => {
        if (!store.softwareBuzzerEnabled) return;
        const ctx = getAudioContext();
        if (!ctx) return;
        try {
            // High-End Labor Chime (D5 -> A5 -> D6, Keysight/Agilent Vibe)
            playChime(ctx, [
                { freq: 587.33, offsetSec: 0.09, ringSec: 0.60, gain: 0.8 }, // D5
                { freq: 880.00, offsetSec: 0.10, ringSec: 0.70, gain: 0.9 }, // A5
                { freq: 1174.66, offsetSec: 0.00, ringSec: 0.85, gain: 1.0 }, // D6
            ], 'triangle', 0.80);
        } catch (_) {}
    },
    playPreheatDone: () => {
        if (!store.softwareBuzzerEnabled) return;
        const ctx = getAudioContext();
        if (!ctx) return;
        try {
            // Dual-Bell (F5 -> C6 Quinte, AirDrop-Style, harmonic overlap)
            playChime(ctx, [
                { freq: 698.46, offsetSec: 0.12, ringSec: 0.65, gain: 0.9 }, // F5
                { freq: 1046.50, offsetSec: 0.00, ringSec: 0.80, gain: 1.0 }, // C6
            ], 'triangle', 0.80);
        } catch (_) {}
    },
};
