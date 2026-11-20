/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 14_app.js
 * @brief Layer 5 Bootstrap: Application lifecycle bootstrap, theme toggling, navigation, global API export.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 5. THEME & DISPLAY PREFERENCES ─────────────────────────────────────
async function initTheme() {
    const savedTheme = localStorage.getItem('reflow_theme') || 'light';
    const savedZones = localStorage.getItem('reflow_show_zones');
    const savedTal = localStorage.getItem('reflow_show_tal');
    const savedMarkers = localStorage.getItem('reflow_show_markers');
    const savedPidGains = localStorage.getItem('reflow_show_pid_gains');

    if (savedZones !== null) store.showZones = savedZones === 'true';
    if (savedTal !== null) store.showTalLine = savedTal === 'true';
    if (savedMarkers !== null) store.showStepMarkers = savedMarkers === 'true';
    if (savedPidGains !== null) store.showPidGains = savedPidGains === 'true';

    setTheme(savedTheme);
    updateCheckboxStates();

    // Sync with ESP32 NVS settings in background
    try {
        const data = await apiRequest('/api/theme', 'GET');
        if (data) {
            if (data.theme && data.theme !== store.currentTheme) {
                setTheme(data.theme);
            }
            if (data.showZones !== undefined) {
                store.showZones = !!data.showZones;
            }
            if (data.showTalLine !== undefined) {
                store.showTalLine = !!data.showTalLine;
            }
            if (data.showStepMarkers !== undefined) {
                store.showStepMarkers = !!data.showStepMarkers;
            }
            if (data.showPidGains !== undefined) {
                store.showPidGains = !!data.showPidGains;
            }
            updateCheckboxStates();
            if (chartInstance) chartInstance.update('none');
        }
    } catch (e) {
        console.warn('[Theme/Prefs] Failed to sync preferences with controller:', e);
    }
}

function updateCheckboxStates() {
    const chkZones = $('chk-zones');
    if (chkZones) chkZones.checked = store.showZones;
    const chkTal = $('chk-tal');
    if (chkTal) chkTal.checked = store.showTalLine;
    const chkMarkers = $('chk-markers');
    if (chkMarkers) chkMarkers.checked = store.showStepMarkers;
    const chkPid = $('chk-pid-gains');
    if (chkPid) chkPid.checked = store.showPidGains;

    const topTag = $('top-pid-tag');
    const botTag = $('bottom-pid-tag');
    if (topTag) topTag.classList.toggle('u-hidden', !store.showPidGains);
    if (botTag) botTag.classList.toggle('u-hidden', !store.showPidGains);
}

function setTheme(theme) {
    store.currentTheme = theme;
    document.documentElement.setAttribute('data-theme', theme);
    localStorage.setItem('reflow_theme', theme);

    const btn = $('theme-toggle');
    if (btn) {
        btn.setAttribute(
            'title',
            theme === 'dark' ? 'Switch to Light Mode' : 'Switch to Dark Mode'
        );
    }

    if (chartInstance) {
        updateChartTheme(theme);
    }
}

function toggleTheme() {
    const next = store.currentTheme === 'dark' ? 'light' : 'dark';
    setTheme(next);
    apiRequest('/api/theme', 'POST', { theme: next });
}

function toggleZones(show) {
    store.showZones = show !== undefined ? !!show : !store.showZones;
    localStorage.setItem('reflow_show_zones', store.showZones);
    updateCheckboxStates();
    apiRequest('/api/theme', 'POST', { showZones: store.showZones });
    if (chartInstance) {
        chartInstance.update('none');
    }
}

function toggleTalLine(show) {
    store.showTalLine = show !== undefined ? !!show : !store.showTalLine;
    localStorage.setItem('reflow_show_tal', store.showTalLine);
    updateCheckboxStates();
    apiRequest('/api/theme', 'POST', { showTalLine: store.showTalLine });
    if (chartInstance) {
        chartInstance.update('none');
    }
}

function toggleStepMarkers(show) {
    store.showStepMarkers = show !== undefined ? !!show : !store.showStepMarkers;
    localStorage.setItem('reflow_show_markers', store.showStepMarkers);
    updateCheckboxStates();
    apiRequest('/api/theme', 'POST', { showStepMarkers: store.showStepMarkers });
    if (chartInstance) {
        chartInstance.update('none');
    }
}

function togglePidGainsDisplay(show) {
    store.showPidGains = show !== undefined ? !!show : !store.showPidGains;
    localStorage.setItem('reflow_show_pid_gains', store.showPidGains);
    updateCheckboxStates();
    apiRequest('/api/theme', 'POST', { showPidGains: store.showPidGains });
}

// ── 6. NAVIGATION & TAB SWITCHING ───────────────────────────────────────
function initTabs() {
    const tabs = ['chart', 'profile', 'settings', 'tunepid', 'backup'];

    // Restore last active tab across browser reloads & sessions
    const savedTab = localStorage.getItem('reflow_active_tab');
    if (savedTab && tabs.includes(savedTab)) {
        switchTab(savedTab, true);
    } else {
        switchTab('chart', true);
    }
}

async function switchTab(tabName, isInitialBoot = false) {
    // State safety check for backup tab
    if (tabName === 'backup' && !isInitialBoot) {
        const isAllowedState =
            isConnectionLive() &&
            (store.stateEnum === 0 || store.stateEnum === 5 || store.stateEnum === 8);
        if (!isAllowedState) {
            return;
        }
    }

    const previousTab = store.currentTab;
    if (previousTab === 'chart' && tabName !== 'chart') {
        dismissChartTooltip();
    }
    store.currentTab = tabName;
    localStorage.setItem('reflow_active_tab', tabName);

    const tabs = ['chart', 'profile', 'settings', 'tunepid', 'backup'];
    tabs.forEach(name => {
        const btn = $(`nav-tab-${name}`);
        const panel = $(`panel-${name}`);
        const isActive = name === tabName;

        if (btn) {
            btn.classList.toggle('active', isActive);
            btn.setAttribute('aria-selected', isActive ? 'true' : 'false');
        }
        if (panel) {
            panel.classList.toggle('active', isActive);
        }
    });

    // FSM BACKUP State transition lifecycle (immediate execution without hold delay)
    if (tabName === 'backup') {
        if (previousTab !== 'backup' && store.stateEnum !== 8 && !isInitialBoot) {
            store.stateEnum = 8;
            updateStatusBadge(8, 'BACKUP');
            updateControlButtonsState();
            apiRequest('/api/control', 'POST', { action: 'enterBackup' }).catch(err => {
                console.warn('[ReflowApp] Failed to enter backup state:', err);
            });
        }
    } else if (previousTab === 'backup' && tabName !== 'backup' && store.stateEnum === 8) {
        try {
            const res = await apiRequest('/api/control', 'POST', { action: 'exitBackup' });
            if (res && res.success !== false) {
                store.stateEnum = 0;
                updateStatusBadge(0, 'IDLE');
                updateControlButtonsState();
            }
        } catch (err) {
            console.warn('[ReflowApp] Failed to exit backup state:', err);
        }
    }

    // Trigger chart resize / re-render safely after DOM reflow when switching back to chart tab
    switch (tabName) {
        case 'chart':
            if (chartInstance) {
                requestAnimationFrame(() => {
                    if (chartInstance && store.currentTab === 'chart') {
                        chartInstance.resize();
                        chartInstance.update('none');
                    }
                });
            }
            break;
        case 'settings':
            loadSettingsFromController();
            break;
        case 'profile':
            loadProfileList(null, true);
            break;
        case 'tunepid':
            loadPidLibraryFromController();
            updatePidPreview();
            updatePidLibraryDisabledBanner();
            break;
        default:
            break;
    }
}

// ── 15. INITIALIZATION ──────────────────────────────────────────────────
async function init() {
    console.log('[ReflowApp] Booting...');

    initTheme();
    initLanguage();
    initTabs();
    initChart();
    initBackupState();
    updateActorIcons();
    applyValidationLimitsToDOM();

    // 1. Initialize Control Buttons in locked state until WS telemetry is confirmed live
    updateControlButtonsState();

    // 2. Start fixed 2.5s Connecting Phase & Initialize Live WebSocket
    startConnectingPhase(2500);
    initWebSocket();
    startTelemetryWatchdog();

    document.addEventListener('click', e => {
        const dropdown = $('status-log-dropdown');
        const badge = $('state-badge');
        if (dropdown && badge && !dropdown.contains(e.target) && !badge.contains(e.target)) {
            dropdown.classList.remove('active');
            store.statusLogOpen = false;
        }
    });

    // Clear invalid visual highlighting when user modifies input field
    document.addEventListener('input', e => {
        if (e.target?.classList?.contains('input-invalid')) {
            e.target.classList.remove('input-invalid');
        }
    });

    // Tab Protection: Prevent accidental tab closing or navigation while heating process is active (240°C peak)
    window.addEventListener('beforeunload', e => {
        if (isHeatingProcess()) {
            e.preventDefault();
            e.returnValue = '';
            return '';
        }
    });

    // 2. Fetch System Status & Dynamic Limits (First priority HTTP request)
    await syncStatusWithController();

    // 3. Restore curve history on reload or multi-device sync
    await restoreHistory();

    // 4. Fetch available profiles list once (cached in store.profiles)
    const profiles = await fetchProfiles(true);

    // 5. Load Settings (populates settings tab & sets currentSettings including defaultProfile)
    await loadSettingsFromController();

    // 6. Populate HUD profile select (pre-selects active profile or defaultProfile)
    await populateChartProfileSelect(profiles);

    // 7. Security verification (prompt Wi-Fi setup if running on factory default credentials)
    checkWifiSecurity();
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}

Object.assign(ReflowApp, {
    store,
    STRINGS,
    init,
    isConnectionLive,
    isHeatingProcess,
    isModificationBlocked,
    isChartProfileBlocked,
    setTheme,
    toggleTheme,
    toggleStatusLog,
    switchTab,
    toggleLamp,
    toggleFan,
    startReflow,
    startPreheat,
    stopReflow,
    skipStep,
    updateStatusBadge,
    updateConnectionBadge,
    startConnectingPhase,
    updateActorIcons,
    updateTelemetryHUD,
    exportCSV,
    exportPNG,
    resetChart,
    toggleZones,
    toggleTalLine,
    toggleStepMarkers,
    togglePidGainsDisplay,
    loadSettingsFromController,
    saveSettingsToController,
    onSoftwareBuzzerChange,
    Sound,
    changeWifiPasswordFromSettings,
    checkWifiSecurity,
    showWifiSecurityModal,
    fetchProfiles,
    invalidateProfileCache,
    loadProfileList,
    loadProfileData,
    onProfileSelectChange,
    addProfileStep,
    removeProfileStep,
    updateProfileStepField,
    createNewProfile,
    saveProfileToController,
    deleteProfileFromController,
    populateChartProfileSelect,
    onChartProfileSelectChange,
    loadPidLibraryFromController,
    renderPidTable,
    updatePidField,
    addPidStep,
    removePidStep,
    savePidLibraryToController,
    updatePidPreview,
    updatePidLibraryDisabledBanner,
    startAutotune,
    stopAutotune,
    handleAutotuneAborted,
    backupFullSystemZip,
    showBackupOrbStage,
    showBackupRestorePortal,
    exitBackupMode,
    initBackupState,
    restoreZipArchiveFromFile,
    restorePidLibraryFromFile,
    restoreSettingsFromFile,
    importProfileFromFile,
    showBackupStatus,
    onOtaFileSelected,
    startOtaUpload,
    t,
    setLanguage,
    applyTranslations,
});

// Global window bindings for HTML onclick
window.isConnectionLive = ReflowApp.isConnectionLive;
window.isHeatingProcess = ReflowApp.isHeatingProcess;
window.isModificationBlocked = ReflowApp.isModificationBlocked;
window.isChartProfileBlocked = ReflowApp.isChartProfileBlocked;
window.setTheme = ReflowApp.setTheme;
window.toggleTheme = ReflowApp.toggleTheme;
window.setLanguage = ReflowApp.setLanguage;
window.applyTranslations = ReflowApp.applyTranslations;
window.t = ReflowApp.t;
window.toggleStatusLog = ReflowApp.toggleStatusLog;
window.switchTab = ReflowApp.switchTab;
window.toggleLamp = ReflowApp.toggleLamp;
window.toggleFan = ReflowApp.toggleFan;
window.startReflow = ReflowApp.startReflow;
window.startPreheat = ReflowApp.startPreheat;
window.stopReflow = ReflowApp.stopReflow;
window.skipStep = ReflowApp.skipStep;
window.exportCSV = ReflowApp.exportCSV;
window.exportPNG = ReflowApp.exportPNG;
window.toggleZones = ReflowApp.toggleZones;
window.toggleTalLine = ReflowApp.toggleTalLine;
window.toggleStepMarkers = ReflowApp.toggleStepMarkers;
window.togglePidGainsDisplay = ReflowApp.togglePidGainsDisplay;
window.loadSettingsFromController = ReflowApp.loadSettingsFromController;
window.saveSettingsToController = ReflowApp.saveSettingsToController;
window.onSoftwareBuzzerChange = ReflowApp.onSoftwareBuzzerChange;
window.Sound = ReflowApp.Sound;
window.changeWifiPasswordFromSettings = ReflowApp.changeWifiPasswordFromSettings;
window.fetchProfiles = ReflowApp.fetchProfiles;
window.invalidateProfileCache = ReflowApp.invalidateProfileCache;
window.loadProfileList = ReflowApp.loadProfileList;
window.loadProfileData = ReflowApp.loadProfileData;
window.onProfileSelectChange = ReflowApp.onProfileSelectChange;
window.addProfileStep = ReflowApp.addProfileStep;
window.removeProfileStep = ReflowApp.removeProfileStep;
window.createNewProfile = ReflowApp.createNewProfile;
window.saveProfileToController = ReflowApp.saveProfileToController;
window.deleteProfileFromController = ReflowApp.deleteProfileFromController;
window.populateChartProfileSelect = ReflowApp.populateChartProfileSelect;
window.onChartProfileSelectChange = ReflowApp.onChartProfileSelectChange;
window.loadPidLibraryFromController = ReflowApp.loadPidLibraryFromController;
window.updatePidField = ReflowApp.updatePidField;
window.addPidStep = ReflowApp.addPidStep;
window.removePidStep = ReflowApp.removePidStep;
window.savePidLibraryToController = ReflowApp.savePidLibraryToController;
window.updatePidPreview = ReflowApp.updatePidPreview;
window.updatePidLibraryDisabledBanner = ReflowApp.updatePidLibraryDisabledBanner;
window.startAutotune = ReflowApp.startAutotune;
window.stopAutotune = ReflowApp.stopAutotune;
window.backupFullSystemZip = ReflowApp.backupFullSystemZip;
window.showBackupOrbStage = ReflowApp.showBackupOrbStage;
window.showBackupRestorePortal = ReflowApp.showBackupRestorePortal;
window.exitBackupMode = ReflowApp.exitBackupMode;
window.restoreZipArchiveFromFile = ReflowApp.restoreZipArchiveFromFile;
window.restorePidLibraryFromFile = ReflowApp.restorePidLibraryFromFile;
window.restoreSettingsFromFile = ReflowApp.restoreSettingsFromFile;
window.importProfileFromFile = ReflowApp.importProfileFromFile;
window.showBackupStatus = ReflowApp.showBackupStatus;
window.onOtaFileSelected = ReflowApp.onOtaFileSelected;
window.startOtaUpload = ReflowApp.startOtaUpload;
