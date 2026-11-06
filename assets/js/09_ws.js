/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 09_ws.js
 * @brief Layer 3 Telemetry Engine: Resilient WebSocket client, 3.5s watchdog, visibility wake lifecycle.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 13. WEBSOCKET CLIENT & RESILIENT LIFECYCLE ──────────────────────────
let wsReconnectTimer = null;

function scheduleWsReconnect() {
    if (wsReconnectTimer) return;
    wsReconnectAttempts++;
    const delay = Math.min(
        1000 + (wsReconnectAttempts > 2 ? (wsReconnectAttempts - 2) * 500 : 0),
        4000
    );
    console.log(
        `[WS] Scheduling fast reconnect in ${delay}ms (attempt #${wsReconnectAttempts})...`
    );
    wsReconnectTimer = setTimeout(() => {
        wsReconnectTimer = null;
        initWebSocket();
    }, delay);
}

function initWebSocket() {
    if (wsInstance) {
        wsInstance.onopen = null;
        wsInstance.onmessage = null;
        wsInstance.onerror = null;
        wsInstance.onclose = null;
        try {
            wsInstance.close();
        } catch (e) {}
        wsInstance = null;
    }

    const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsHost = window.location.host || '192.168.4.1';
    const wsUrl = `${wsProtocol}//${wsHost}/ws`;

    console.log(`[WS] Connecting to ${wsUrl}...`);

    try {
        wsInstance = new WebSocket(wsUrl);

        wsInstance.onopen = () => {
            console.log('[WS] Connected successfully.');
            store.wsConnected = true;
            store.lastTelemetryTime = Date.now();
            wsReconnectAttempts = 0;
            if (wsReconnectTimer) {
                clearTimeout(wsReconnectTimer);
                wsReconnectTimer = null;
            }
            updateConnectionBadge();
            updateLogs(null);
            updateControlButtonsState();
        };

        wsInstance.onmessage = event => {
            try {
                store.wsConnected = true;
                store.lastTelemetryTime = Date.now();
                const data = JSON.parse(event.data);
                updateTelemetryHUD(data);
            } catch (err) {
                console.error('[WS] JSON parse error:', err, event.data);
            }
        };

        wsInstance.onerror = err => {
            console.warn('[WS] Error:', err);
            store.wsConnected = false;
            updateConnectionBadge();
            updateLogs(null);
            updateControlButtonsState();
            scheduleWsReconnect();
        };

        wsInstance.onclose = () => {
            console.warn('[WS] Disconnected. Reconnecting...');
            store.wsConnected = false;
            updateConnectionBadge();
            updateLogs(null);
            updateControlButtonsState();
            scheduleWsReconnect();
        };
    } catch (e) {
        console.error('[WS] Initialization failed:', e);
        store.wsConnected = false;
        updateConnectionBadge();
        updateLogs(null);
        updateControlButtonsState();
        scheduleWsReconnect();
    }
}

// Telemetry Watchdog: Detects dead/unclean disconnects (power-off/reset/sleep) within 3.5s
let telemetryWatchdog = null;
function startTelemetryWatchdog() {
    if (telemetryWatchdog) clearInterval(telemetryWatchdog);
    telemetryWatchdog = setInterval(() => {
        const now = Date.now();
        if (store.lastTelemetryTime > 0 && now - store.lastTelemetryTime > 3500) {
            if (store.wsConnected) {
                console.warn(
                    '[Watchdog] Telemetry timeout (>3.5s without packet). Marking DISCONNECTED...'
                );
                store.wsConnected = false;
                updateLogs(null);
            }
            updateConnectionBadge();
            updateControlButtonsState();

            // Only force-close if socket is OPEN but silent
            if (
                wsInstance &&
                wsInstance.readyState === WebSocket.OPEN &&
                now - store.lastTelemetryTime > 5000
            ) {
                console.warn('[Watchdog] Active WebSocket silent for >5s. Force closing...');
                wsInstance.onopen = null;
                wsInstance.onmessage = null;
                wsInstance.onerror = null;
                wsInstance.onclose = null;
                try {
                    wsInstance.close();
                } catch (e) {}
                wsInstance = null;
                scheduleWsReconnect();
            }
        } else if (!store.wsConnected && !isConnectingPhase) {
            updateConnectionBadge();
            updateLogs(null);
        }
    }, 500);
}

// ── 14. EVENT LISTENERS & VISIBILITY LIFECYCLE ──────────────────────────
// W3C Page Visibility API: Instantly resume WebSocket when mobile wakes or tab becomes visible
document.addEventListener('visibilitychange', () => {
    if (document.visibilityState !== 'visible') return;

    // Re-acquire screen wake lock if heating phase is still active
    const isHeating = (store.stateEnum >= 1 && store.stateEnum <= 4) || store.stateEnum === 7;
    if (isHeating) {
        acquireWakeLock();
    }

    const now = Date.now();
    if (store.wsConnected && now - store.lastTelemetryTime <= 3500) return;

    console.log(
        '[WS] Tab became active and telemetry is stale/disconnected. Triggering fast reconnect...'
    );
    wsReconnectAttempts = 0;
    if (wsReconnectTimer) {
        clearTimeout(wsReconnectTimer);
        wsReconnectTimer = null;
    }
    initWebSocket();
});

// Browser Offline Event: Immediate switch to DISCONNECTED state when device disconnects from network
window.addEventListener('offline', () => {
    console.warn('[Network] Client network went offline. Setting status to DISCONNECTED...');
    isConnectingPhase = false;
    store.wsConnected = false;
    releaseWakeLock();
    updateConnectionBadge();
    updateLogs(null);
    updateControlButtonsState();
});

// Browser Online Event: Instantly reconnect when Wi-Fi recovers
window.addEventListener('online', () => {
    console.log('[Network] Client network online event. Triggering fast reconnect...');
    startConnectingPhase(2500);
    wsReconnectAttempts = 0;
    if (wsReconnectTimer) {
        clearTimeout(wsReconnectTimer);
        wsReconnectTimer = null;
    }
    initWebSocket();
});

// Cleanly close active WebSocket connection on page unload/refresh to immediately release server socket
window.addEventListener('beforeunload', () => {
    if (wsInstance) {
        wsInstance.onclose = null;
        wsInstance.onerror = null;
        try {
            wsInstance.close(1000, 'Page reload');
        } catch (e) {}
        wsInstance = null;
    }
});
