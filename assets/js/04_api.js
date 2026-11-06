/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 04_api.js
 * @brief Layer 2 Transport: REST client, HTTP error translation, and network boundary handling.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 3. REST API CLIENT ──────────────────────────────────────────────────
async function parseErrorResponse(res) {
    let raw = '';
    try {
        raw = (await res.text()).trim();
    } catch {
        return `HTTP ${res.status}`;
    }

    if (!raw) {
        return `HTTP ${res.status}`;
    }

    try {
        const json = JSON.parse(raw);
        return json.error || raw;
    } catch {
        return raw;
    }
}

async function apiRequest(endpoint, method = 'GET', data = null, timeoutMs = 5000) {
    const controller = typeof AbortController !== 'undefined' ? new AbortController() : null;
    let timer = null;

    const options = {
        method: method,
        headers: {},
    };

    if (controller && timeoutMs > 0) {
        options.signal = controller.signal;
        timer = setTimeout(() => {
            controller.abort();
        }, timeoutMs);
    }

    if (data && (method === 'POST' || method === 'PUT' || method === 'DELETE')) {
        options.headers['Content-Type'] = 'application/json';
        options.body = JSON.stringify(data);
    }

    try {
        const res = await fetch(endpoint, options);
        if (timer) clearTimeout(timer);

        if (!res.ok) {
            console.warn(`[API] ${method} ${endpoint} HTTP ${res.status}`);
            const errDetail = await parseErrorResponse(res);
            return { success: false, error: errDetail, status: res.status };
        }

        const contentType = res.headers.get('content-type');
        if (contentType && contentType.includes('application/json')) {
            return await res.json();
        }
        return await res.text();
    } catch (err) {
        if (timer) clearTimeout(timer);
        const isTimeout = err.name === 'AbortError';
        const errorMsg = isTimeout
            ? `Request timeout (${Math.round(timeoutMs / 1000)}s)`
            : err.message || 'Network error';
        console.error(`[API Error] ${method} ${endpoint}:`, errorMsg);
        return { success: false, error: errorMsg, status: 0, isTimeout: isTimeout };
    }
}

/**
 * Translates known backend English error strings into localized UI messages.
 * Keeps backend REST errors standard-compliant while presenting native German in the UI.
 */
function localizeApiError(rawError, fallbackKey = null, fallbackDefault = '') {
    if (!rawError || typeof rawError !== 'string') {
        return fallbackKey ? t(fallbackKey, fallbackDefault) : '';
    }
    if (
        rawError.includes('Cannot overwrite protected factory profile') ||
        rawError.includes('factory profile')
    ) {
        return t(
            'modal.factory_profile_readonly',
            "The factory default profile (factory-profile.json) is read-only and cannot be overwritten. Please create a new profile using '+ New Profile' or use a different filename."
        );
    }
    if (rawError.includes('Storage limit of 90% reached')) {
        return t(
            'modal.storage_limit_rejected',
            'Storage limit of 90% reached. Save rejected to preserve flash wear leveling.'
        );
    }
    if (
        rawError.includes('Profile modification blocked during active process') ||
        rawError.includes('Profile modification blocked')
    ) {
        return t(
            'modal.save_profile_locked_active',
            'Profile modification blocked during active process.'
        );
    }
    if (rawError.includes('Settings modification blocked during active process')) {
        return t(
            'modal.save_settings_locked_active',
            'Settings modification blocked during active process.'
        );
    }
    if (
        rawError.includes('Wi-Fi password modification blocked during active process') ||
        rawError.includes('Wi-Fi password modification blocked')
    ) {
        return t(
            'modal.wifi_pass_locked_active',
            'Wi-Fi password modification blocked during active process.'
        );
    }
    return rawError;
}
