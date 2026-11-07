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
