/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 13_tab_backup.js
 * @brief Layer 4 Domain Controller: System backup ZIP, restore portal, orb hero stage, OTA firmware upload.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 18. BACKUP & RESTORE MANAGER ─────────────────────────────────────────
function showBackupStatus(message, isError = false) {
    const banner = $('backup-status-banner');
    const icon = $('backup-status-icon');
    const text = $('backup-status-text');
    if (!banner || !text) return;

    text.textContent = message;
    if (icon) icon.textContent = isError ? '⚠️' : '✓';
    banner.classList.toggle('error', isError);
    banner.classList.remove('u-hidden');

    setTimeout(() => {
        banner.classList.add('u-hidden');
    }, 6000);
}

function makeCrcTable() {
    let c;
    const table = [];
    for (let n = 0; n < 256; n++) {
        c = n;
        for (let k = 0; k < 8; k++) {
            c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        }
        table[n] = c;
    }
    return table;
}
const CRC_TABLE = makeCrcTable();

function crc32(uint8Array) {
    let crc = 0 ^ -1;
    for (let i = 0; i < uint8Array.length; i++) {
        crc = (crc >>> 8) ^ CRC_TABLE[(crc ^ uint8Array[i]) & 0xff];
    }
    return (crc ^ -1) >>> 0;
}

function createZipArchive(files) {
    const localHeaders = [];
    const centralHeaders = [];
    let offset = 0;

    files.forEach(file => {
        const nameBytes = new TextEncoder().encode(file.name);
        const dataBytes =
            typeof file.data === 'string' ? new TextEncoder().encode(file.data) : file.data;
        const crc = crc32(dataBytes);
        const size = dataBytes.length;

        const localHeader = new Uint8Array(30 + nameBytes.length + size);
        const dvL = new DataView(localHeader.buffer);
        dvL.setUint32(0, 0x04034b50, true);
        dvL.setUint16(4, 20, true);
        dvL.setUint16(6, 0x0800, true);
        dvL.setUint16(8, 0, true);
        dvL.setUint16(10, 0, true);
        dvL.setUint16(12, 0, true);
        dvL.setUint32(14, crc, true);
        dvL.setUint32(18, size, true);
        dvL.setUint32(22, size, true);
        dvL.setUint16(26, nameBytes.length, true);
        dvL.setUint16(28, 0, true);
        localHeader.set(nameBytes, 30);
        localHeader.set(dataBytes, 30 + nameBytes.length);
        localHeaders.push(localHeader);

        const centralHeader = new Uint8Array(46 + nameBytes.length);
        const dvC = new DataView(centralHeader.buffer);
        dvC.setUint32(0, 0x02014b50, true);
        dvC.setUint16(4, 20, true);
        dvC.setUint16(6, 20, true);
        dvC.setUint16(8, 0x0800, true);
        dvC.setUint16(10, 0, true);
        dvC.setUint16(12, 0, true);
        dvC.setUint16(14, 0, true);
        dvC.setUint32(16, crc, true);
        dvC.setUint32(20, size, true);
        dvC.setUint32(24, size, true);
        dvC.setUint16(28, nameBytes.length, true);
        dvC.setUint16(30, 0, true);
        dvC.setUint16(32, 0, true);
        dvC.setUint16(34, 0, true);
        dvC.setUint16(36, 0, true);
        dvC.setUint32(38, 0, true);
        dvC.setUint32(42, offset, true);
        centralHeader.set(nameBytes, 46);
        centralHeaders.push(centralHeader);

        offset += localHeader.length;
    });

    const centralDirOffset = offset;
    let centralDirSize = 0;
    centralHeaders.forEach(ch => {
        centralDirSize += ch.length;
    });

    const eocd = new Uint8Array(22);
    const dvE = new DataView(eocd.buffer);
    dvE.setUint32(0, 0x06054b50, true);
    dvE.setUint16(4, 0, true);
    dvE.setUint16(6, 0, true);
    dvE.setUint16(8, files.length, true);
    dvE.setUint16(10, files.length, true);
    dvE.setUint32(12, centralDirSize, true);
    dvE.setUint32(16, centralDirOffset, true);
    dvE.setUint16(20, 0, true);

    const totalSize = centralDirOffset + centralDirSize + eocd.length;
    const zipBytes = new Uint8Array(totalSize);
    let cur = 0;
    localHeaders.forEach(lh => {
        zipBytes.set(lh, cur);
        cur += lh.length;
    });
    centralHeaders.forEach(ch => {
        zipBytes.set(ch, cur);
        cur += ch.length;
    });
    zipBytes.set(eocd, cur);

    return new Blob([zipBytes], { type: 'application/zip' });
}

function parseZipArchive(arrayBuffer) {
    const dv = new DataView(arrayBuffer);
    const u8 = new Uint8Array(arrayBuffer);
    const extracted = [];
    let pos = 0;

    while (pos < arrayBuffer.byteLength - 30) {
        const sig = dv.getUint32(pos, true);
        if (sig === 0x04034b50) {
            const compMethod = dv.getUint16(pos + 8, true);
            const uncompSize = dv.getUint32(pos + 22, true);
            const nameLen = dv.getUint16(pos + 26, true);
            const extraLen = dv.getUint16(pos + 28, true);

            const nameBytes = u8.subarray(pos + 30, pos + 30 + nameLen);
            const fileName = new TextDecoder().decode(nameBytes);
            const dataStart = pos + 30 + nameLen + extraLen;
            const dataBytes = u8.subarray(dataStart, dataStart + uncompSize);

            if (compMethod === 0) {
                const contentStr = new TextDecoder().decode(dataBytes);
                extracted.push({ name: fileName, text: contentStr });
            }
            pos = dataStart + uncompSize;
        } else {
            pos++;
        }
    }
    return extracted;
}

function updateBackupStageVisibility() {
    const stage = $('backup-orb-stage');
    const portal = $('backup-restore-portal');
    const backBtn = $('btn-backup-back-to-portal');

    if (store.backupTaken) {
        if (stage) stage.classList.add('u-hidden');
        if (portal) portal.classList.remove('u-hidden');
        if (backBtn) backBtn.classList.remove('u-hidden');
    } else {
        if (stage) {
            stage.classList.remove('u-hidden');
            stage.classList.remove('stage-exit');
        }
        if (portal) portal.classList.add('u-hidden');
        if (backBtn) backBtn.classList.add('u-hidden');
    }
}

async function backupFullSystemZip() {
    if (store.stateEnum !== 8) {
        await Modal.alert(
            t(
                'modal.backup_state_required',
                'Backup and restore operations are only permitted while the controller is in Maintenance/Backup state.'
            ),
            {
                type: 'warning',
                title: t('modal.backup_state_required_title', '⚠️ Maintenance Mode Required'),
            }
        );
        return;
    }

    const orb = $('btn-backup-orb');
    if (orb?.disabled) return;
    if (orb) {
        orb.disabled = true;
        orb.classList.remove('orb-dive');
        void orb.offsetWidth;
        orb.classList.add('orb-dive');
    }

    try {
        const filesToPack = [];

        // 1. Settings
        try {
            const settings = await apiRequest('/api/settings', 'GET');
            if (settings) {
                filesToPack.push({
                    name: 'settings.json',
                    data: JSON.stringify(settings, null, 2),
                });
            }
        } catch (err) {
            console.warn('[Backup] Could not fetch settings:', err);
        }

        // 2. PID Library
        try {
            const pidlib = await apiRequest('/api/pidlibrary', 'GET');
            if (pidlib) {
                filesToPack.push({
                    name: 'pid_library.json',
                    data: JSON.stringify(pidlib, null, 2),
                });
            }
        } catch (err) {
            console.warn('[Backup] Could not fetch pid library:', err);
        }

        // 3. All Profiles
        try {
            const profList = await apiRequest('/api/profiles', 'GET');
            if (Array.isArray(profList)) {
                for (const profInfo of profList) {
                    const fileName = typeof profInfo === 'string' ? profInfo : profInfo.file;
                    if (!fileName) continue;
                    try {
                        const profData = await apiRequest(
                            `/api/profiles/${encodeURIComponent(fileName)}`,
                            'GET'
                        );
                        if (profData) {
                            filesToPack.push({
                                name: fileName,
                                data: JSON.stringify(profData, null, 2),
                            });
                        }
                    } catch (pErr) {
                        console.warn(`[Backup] Could not fetch profile ${fileName}:`, pErr);
                    }
                }
            }
        } catch (err) {
            console.warn('[Backup] Could not fetch profiles list:', err);
        }

        if (!filesToPack.length) {
            showBackupStatus(
                t('val.no_files_backup', 'No files found to backup from controller.'),
                true
            );
            return;
        }

        // Create ZIP Blob
        const zipBlob = createZipArchive(filesToPack);
        const now = new Date();
        const dateStr = now.toISOString().slice(0, 10);
        const timeStr =
            String(now.getHours()).padStart(2, '0') + String(now.getMinutes()).padStart(2, '0');
        const zipName = `reflow_backup_${dateStr}_${timeStr}.zip`;

        const url = URL.createObjectURL(zipBlob);
        const a = document.createElement('a');
        a.href = url;
        a.download = zipName;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);

        // Inform controller that backup was pulled in this instance
        try {
            await apiRequest('/api/backup/complete', 'POST');
        } catch (bErr) {
            console.warn('[Backup] Failed to notify controller of backup completion:', bErr);
        }
        store.backupTaken = true;

        const backBtn = $('btn-backup-back-to-portal');
        if (backBtn) backBtn.classList.remove('u-hidden');

        showBackupStatus(
            t('toast.backup_success', '✓ ZIP backup downloaded successfully ({{count}} files)!', {
                count: filesToPack.length,
            })
        );

        // Animate Orb out and reveal Restore Card Grid Portal
        setTimeout(() => {
            const stage = $('backup-orb-stage');
            const portal = $('backup-restore-portal');
            if (stage) stage.classList.add('stage-exit');
            setTimeout(() => {
                if (stage) stage.classList.add('u-hidden');
                if (portal) portal.classList.remove('u-hidden');
            }, 400);
        }, 900);
    } catch (err) {
        console.error('[Backup] Backup ZIP failed:', err);
        showBackupStatus(t('toast.backup_failed', 'Failed to generate ZIP backup.'), true);
    } finally {
        if (orb) orb.disabled = false;
        setTimeout(() => {
            orb?.classList.remove('orb-dive');
        }, 1300);
    }
}

function initBackupState() {
    updateBackupStageVisibility();
}

async function exitBackupMode() {
    if (store.stateEnum === 8) {
        store.stateEnum = 0;
        updateStatusBadge(0, 'IDLE');
        try {
            await apiRequest('/api/control', 'POST', { action: 'exitBackup' });
            await syncStatusWithController();
        } catch (err) {
            console.warn('[Backup] Failed to exit backup state:', err);
        }
    }
    showBackupOrbStage();
}

function showBackupRestorePortal() {
    const stage = $('backup-orb-stage');
    const portal = $('backup-restore-portal');
    if (stage) stage.classList.add('u-hidden');
    if (portal) portal.classList.remove('u-hidden');
}

function showBackupOrbStage() {
    const stage = $('backup-orb-stage');
    const portal = $('backup-restore-portal');
    const orb = $('btn-backup-orb');
    const backBtn = $('btn-backup-back-to-portal');

    if (portal) portal.classList.add('u-hidden');
    if (stage) {
        stage.classList.remove('u-hidden');
        void stage.offsetWidth;
        stage.classList.remove('stage-exit');
    }
    if (orb) orb.classList.remove('orb-dive');
    if (backBtn && store.backupTaken) {
        backBtn.classList.remove('u-hidden');
    }
}

function readJsonFile(file, callback) {
    if (!file) return;
    if (
        store.stateEnum !== 0 &&
        store.stateEnum !== 5 &&
        store.stateEnum !== 8 &&
        store.stateEnum !== 6
    ) {
        Modal.alert(
            t(
                'modal.backup_locked_msg',
                'Reflow process or autotune is currently active: Backup and maintenance functions are only available in IDLE state.'
            ),
            {
                type: 'warning',
                title: t(
                    'modal.backup_locked_title',
                    '⚠️ Maintenance & Backup Locked: Process Active'
                ),
            }
        );
        return;
    }
    if (file.size > VALIDATION_LIMITS.MAX_JSON_SIZE_BYTES) {
        const err = t(
            'val.file_too_large',
            `File size (${Math.round(file.size / 1024)} KB) exceeds limit of ${Math.round(VALIDATION_LIMITS.MAX_JSON_SIZE_BYTES / 1024)} KB.`
        );
        showBackupStatus(err, true);
        Modal.alert(err, {
            type: 'warning',
            title: t('modal.upload_failed_title', '⚠️ Upload Failed'),
        });
        return;
    }
    const reader = new FileReader();
    reader.onload = async e => {
        try {
            const parsed = JSON.parse(e.target.result);
            callback(parsed);
        } catch (err) {
            console.error('[Restore] Parse error:', err);
            const syntaxErr = t('val.invalid_json_syntax', 'Invalid JSON syntax in file.');
            showBackupStatus(syntaxErr, true);
            await Modal.alert(syntaxErr, {
                type: 'danger',
                title: t('modal.invalid_json_title', '⚠️ Invalid JSON File'),
            });
        }
    };
    reader.readAsText(file);
}

function restorePidLibraryFromFile(event) {
    const file = event.target.files[0];
    if (!file) return;

    if (store.stateEnum !== 8) {
        Modal.alert(
            t(
                'modal.backup_state_required',
                'Backup and restore operations are only permitted while the controller is in Maintenance/Backup state.'
            ),
            {
                type: 'warning',
                title: t('modal.backup_state_required_title', '⚠️ Maintenance Mode Required'),
            }
        );
        event.target.value = '';
        return;
    }
    readJsonFile(file, async data => {
        const val = validatePidLibraryPayload(data);
        if (!val.valid) {
            showBackupStatus(val.error, true);
            await Modal.alert(val.error, {
                type: 'warning',
                title: t('modal.invalid_pid_title', '⚠️ Invalid PID Library'),
            });
            event.target.value = '';
            return;
        }
        const ok = await Modal.confirm(
            t(
                'modal.restore_pid_confirm',
                'Restore PID Library with {{top}} top and {{bottom}} bottom points?',
                {
                    top: data.top?.length || 0,
                    bottom: data.bottom?.length || 0,
                }
            ),
            {
                type: 'warning',
                title: t('modal.restore_pid_title', 'Restore PID Library'),
                confirmLabel: t('modal.btn_restore', 'Restore'),
                cancelLabel: t('modal.btn_cancel', 'Cancel'),
            }
        );
        if (!ok) {
            event.target.value = '';
            return;
        }

        try {
            const res = await apiRequest('/api/pidlibrary', 'POST', data);
            if (res && res.success !== false) {
                showBackupStatus(t('toast.pid_restored', '✓ PID Library restored successfully!'));
                await Modal.alert(t('toast.pid_restored', '✓ PID Library restored successfully!'), {
                    type: 'success',
                    title: t('modal.restore_success_title', 'Restore Successful'),
                });
                await loadPidLibraryFromController();
            } else {
                const errMsg =
                    res && res.error
                        ? res.error
                        : t(
                              'modal.restore_pid_failed_msg',
                              'Failed to restore PID Library to controller.'
                          );
                showBackupStatus(errMsg, true);
                await Modal.alert(errMsg, {
                    type: 'danger',
                    title: t('modal.restore_failed_title', 'Restore Failed'),
                });
            }
        } catch (err) {
            console.error('[Restore] Failed to restore PID library:', err);
            const errMsg = t(
                'modal.restore_pid_failed_msg',
                'Failed to restore PID Library to controller.'
            );
            showBackupStatus(errMsg, true);
            await Modal.alert(errMsg, {
                type: 'danger',
                title: t('modal.restore_failed_title', 'Restore Failed'),
            });
        }
        event.target.value = '';
    });
}

function restoreSettingsFromFile(event) {
    const file = event.target.files[0];
    if (!file) return;

    if (store.stateEnum !== 8) {
        Modal.alert(
            t(
                'modal.backup_state_required',
                'Backup and restore operations are only permitted while the controller is in Maintenance/Backup state.'
            ),
            {
                type: 'warning',
                title: t('modal.backup_state_required_title', '⚠️ Maintenance Mode Required'),
            }
        );
        event.target.value = '';
        return;
    }
    readJsonFile(file, async data => {
        const val = validateSettingsPayload(data);
        if (!val.valid) {
            showBackupStatus(val.error, true);
            await Modal.alert(val.error, {
                type: 'warning',
                title: t('modal.invalid_settings_title', '⚠️ Invalid Machine Setting'),
            });
            event.target.value = '';
            return;
        }
        const ok = await Modal.confirm(
            t('modal.restore_settings_confirm', 'Restore Machine Settings from selected file?'),
            {
                type: 'warning',
                title: t('modal.restore_settings_title', 'Restore Settings'),
                confirmLabel: t('modal.btn_restore', 'Restore'),
                cancelLabel: t('modal.btn_cancel', 'Cancel'),
            }
        );
        if (!ok) {
            event.target.value = '';
            return;
        }

        try {
            const res = await apiRequest('/api/settings', 'POST', data);
            if (res && res.success !== false) {
                showBackupStatus(
                    t('toast.settings_restored', '✓ Machine Settings restored successfully!')
                );
                await Modal.alert(
                    t('toast.settings_restored', '✓ Machine Settings restored successfully!'),
                    {
                        type: 'success',
                        title: t('modal.restore_success_title', 'Restore Successful'),
                    }
                );
                await loadSettingsFromController();
            } else {
                const errMsg =
                    res && res.error
                        ? res.error
                        : t(
                              'modal.restore_settings_failed_msg',
                              'Failed to restore machine settings to controller.'
                          );
                showBackupStatus(errMsg, true);
                await Modal.alert(errMsg, {
                    type: 'danger',
                    title: t('modal.restore_failed_title', 'Restore Failed'),
                });
            }
        } catch (err) {
            console.error('[Restore] Failed to restore settings:', err);
            const errMsg = t(
                'modal.restore_settings_failed_msg',
                'Failed to restore machine settings to controller.'
            );
            showBackupStatus(errMsg, true);
            await Modal.alert(errMsg, {
                type: 'danger',
                title: t('modal.restore_failed_title', 'Restore Failed'),
            });
        }
        event.target.value = '';
    });
}

function importProfileFromFile(event) {
    const file = event.target.files[0];
    if (!file) return;

    if (store.stateEnum !== 8) {
        Modal.alert(
            t(
                'modal.backup_state_required',
                'Backup and restore operations are only permitted while the controller is in Maintenance/Backup state.'
            ),
            {
                type: 'warning',
                title: t('modal.backup_state_required_title', '⚠️ Maintenance Mode Required'),
            }
        );
        event.target.value = '';
        return;
    }
    readJsonFile(file, async data => {
        const val = validateProfilePayload(data);
        if (!val.valid) {
            showBackupStatus(val.error, true);
            await Modal.alert(val.error, {
                type: 'warning',
                title: t('modal.invalid_profile_title', '⚠️ Invalid Profile'),
            });
            event.target.value = '';
            return;
        }
        if (!data.file) {
            data.file = data.name.toLowerCase().replace(/[^a-z0-9_-]/g, '_') + '.json';
        }
        const ok = await Modal.confirm(
            t('modal.import_profile_confirm', 'Import Profile "{{name}}" ({{file}})?', {
                name: data.name,
                file: data.file,
            }),
            {
                type: 'info',
                title: t('modal.import_profile_title', 'Import Profile'),
                confirmLabel: t('modal.btn_import', 'Import'),
                cancelLabel: t('modal.btn_cancel', 'Cancel'),
            }
        );
        if (!ok) {
            event.target.value = '';
            return;
        }

        try {
            const res = await apiRequest('/api/profiles', 'POST', data);
            if (res && res.success !== false) {
                invalidateProfileCache();
                showBackupStatus(
                    t('toast.profile_imported', '✓ Profile "{{name}}" imported successfully!', {
                        name: data.name,
                    })
                );
                await Modal.alert(
                    t('toast.profile_imported', '✓ Profile "{{name}}" imported successfully!', {
                        name: data.name,
                    }),
                    {
                        type: 'success',
                        title: t('modal.profile_saved_title', 'Profile Imported'),
                    }
                );
                await loadProfileList(data.file, true);
                await populateChartProfileSelect();
            } else {
                const errMsg = localizeApiError(
                    res && res.error,
                    'modal.import_profile_failed_msg',
                    'Failed to import profile to controller.'
                );
                showBackupStatus(errMsg, true);
                await Modal.alert(errMsg, {
                    type: 'danger',
                    title: t('modal.import_failed_title', 'Import Failed'),
                });
            }
        } catch (err) {
            console.error('[Restore] Failed to import profile:', err);
            const errMsg = t(
                'modal.import_profile_failed_msg',
                'Failed to import profile to controller.'
            );
            showBackupStatus(errMsg, true);
            await Modal.alert(errMsg, {
                type: 'danger',
                title: t('modal.import_failed_title', 'Import Failed'),
            });
        }
        event.target.value = '';
    });
}

async function restoreZipArchiveFromFile(event) {
    const file = event.target.files[0];
    if (!file) return;

    if (store.stateEnum !== 8) {
        await Modal.alert(
            t(
                'modal.backup_state_required',
                'Backup and restore operations are only permitted while the controller is in Maintenance/Backup state.'
            ),
            {
                type: 'warning',
                title: t('modal.backup_state_required_title', '⚠️ Maintenance Mode Required'),
            }
        );
        event.target.value = '';
        return;
    }

    if (file.size > VALIDATION_LIMITS.MAX_ZIP_SIZE_BYTES) {
        const err = t(
            'val.file_too_large',
            `File size (${Math.round(file.size / 1024)} KB) exceeds limit of ${Math.round(VALIDATION_LIMITS.MAX_ZIP_SIZE_BYTES / 1024)} KB.`
        );
        showBackupStatus(err, true);
        await Modal.alert(err, {
            type: 'warning',
            title: t('modal.upload_failed_title', '⚠️ ZIP Archive Too Large'),
        });
        event.target.value = '';
        return;
    }

    const reader = new FileReader();
    reader.onload = async e => {
        try {
            const arrayBuffer = e.target.result;
            const extractedFiles = parseZipArchive(arrayBuffer);

            if (!extractedFiles.length) {
                const emptyErr = t('val.empty_zip', 'ZIP archive is empty or invalid.');
                showBackupStatus(emptyErr, true);
                await Modal.alert(emptyErr, {
                    type: 'danger',
                    title: t('modal.invalid_zip_title', '⚠️ Invalid ZIP Archive'),
                });
                return;
            }

            const ok = await Modal.confirm(
                t(
                    'modal.restore_zip_confirm',
                    'Restore {{count}} files from "{{file}}" to the controller?',
                    {
                        count: extractedFiles.length,
                        file: file.name,
                    }
                ),
                {
                    type: 'warning',
                    title: t('modal.restore_zip_title', 'Full ZIP Restore'),
                    confirmLabel: t('modal.btn_restore_all', 'Restore All'),
                    cancelLabel: t('modal.btn_cancel', 'Cancel'),
                }
            );
            if (!ok) {
                event.target.value = '';
                return;
            }

            let successCount = 0;
            let failCount = 0;

            for (const item of extractedFiles) {
                const baseName = item.name.split('/').pop();
                if (!baseName || !baseName.endsWith('.json')) continue;
                try {
                    const json = JSON.parse(item.text);

                    if (baseName === 'settings.json') {
                        const val = validateSettingsPayload(json);
                        if (val.valid) {
                            const res = await apiRequest('/api/settings', 'POST', json);
                            if (res && res.success !== false) successCount++;
                            else failCount++;
                        } else {
                            console.warn(`[Restore ZIP] Invalid settings.json: ${val.error}`);
                            failCount++;
                        }
                    } else if (baseName === 'pid_library.json') {
                        const val = validatePidLibraryPayload(json);
                        if (val.valid) {
                            const res = await apiRequest('/api/pidlibrary', 'POST', json);
                            if (res && res.success !== false) successCount++;
                            else failCount++;
                        } else {
                            console.warn(`[Restore ZIP] Invalid pid_library.json: ${val.error}`);
                            failCount++;
                        }
                    } else {
                        const val = validateProfilePayload(json);
                        if (val.valid) {
                            if (!json.file) json.file = baseName;
                            const res = await apiRequest('/api/profiles', 'POST', json);
                            if (res && res.success !== false) successCount++;
                            else failCount++;
                        } else {
                            console.warn(`[Restore ZIP] Invalid profile ${baseName}: ${val.error}`);
                            failCount++;
                        }
                    }
                } catch (itemErr) {
                    console.error(`[Restore] Error processing ${item.name}:`, itemErr);
                    failCount++;
                }
            }

            invalidateProfileCache();
            await loadSettingsFromController();
            await loadPidLibraryFromController();
            await loadProfileList(null, true);
            await populateChartProfileSelect();

            if (failCount > 0) {
                const warnMsg = t(
                    'toast.restore_zip_warning',
                    'Restored {{count}} files, but {{skipped}} files were skipped or invalid.',
                    {
                        count: successCount,
                        skipped: failCount,
                    }
                );
                showBackupStatus(warnMsg, true);
                await Modal.alert(warnMsg, {
                    type: 'warning',
                    title: t('modal.restore_partial_title', '⚠️ Restore Complete (With Warnings)'),
                });
            } else {
                const succMsg = t(
                    'toast.restore_zip_complete',
                    '✓ Full ZIP Restore Complete: {{count}} files restored successfully!',
                    {
                        count: successCount,
                        skipped: '',
                    }
                );
                showBackupStatus(succMsg);
                await Modal.alert(succMsg, {
                    type: 'success',
                    title: t('modal.restore_success_title', 'Restore Successful'),
                });
            }
        } catch (err) {
            console.error('[Restore] ZIP restore error:', err);
            const failMsg = t('toast.restore_zip_failed', 'Error restoring from ZIP archive.');
            showBackupStatus(failMsg, true);
            await Modal.alert(failMsg, {
                type: 'danger',
                title: t('modal.restore_failed_title', 'Restore Failed'),
            });
        }
        event.target.value = '';
    };
    reader.readAsArrayBuffer(file);
}

// ── 19. 1-Click Web OTA Firmware Update ─────────────────────────────────
let selectedOtaFile = null;

async function onOtaFileSelected(files) {
    if (!files || files.length === 0) return;
    const file = files[0];
    const input = $('ota-file-input');

    if (!file.name.toLowerCase().endsWith('.bin')) {
        if (input) input.value = '';
        Modal.alert(
            t('ota.error_invalid_file', 'Please select a valid .bin firmware binary file.'),
            {
                type: 'warning',
                title: t('modal.invalid_file_type', 'Invalid File Type'),
            }
        );
        return;
    }

    // Safety state check: only allow in BACKUP (8)
    if (store.stateEnum !== 8) {
        if (input) input.value = '';
        await Modal.alert(
            t(
                'modal.backup_state_required',
                'Backup and restore operations are only permitted while the controller is in Maintenance/Backup state.'
            ),
            {
                type: 'warning',
                title: t('modal.backup_state_required_title', '⚠️ Maintenance Mode Required'),
            }
        );
        return;
    }

    const sizeStr = `${(file.size / (1024 * 1024)).toFixed(2)} MB`;
    const confirmed = await Modal.confirm(
        t(
            'ota.confirm_msg',
            'Are you sure you want to flash "{{file}}" ({{size}})? The controller will automatically restart after flashing.'
        )
            .replace('{{file}}', file.name)
            .replace('{{size}}', sizeStr),
        {
            type: 'warning',
            title: t('ota.confirm_title', '⚡ Confirm Firmware Update'),
            confirmText: t('ota.btn_flash', 'Flash Firmware'),
            cancelText: t('ota.btn_cancel', 'Cancel'),
        }
    );

    if (!confirmed) {
        if (input) input.value = '';
        return;
    }

    selectedOtaFile = file;
    startOtaUpload();
}

async function startOtaUpload() {
    if (!selectedOtaFile) return;

    const progressEl = $('ota-progress-container');
    const barEl = $('ota-progress-bar');
    const pctEl = $('ota-progress-pct');
    const statusEl = $('ota-progress-status');
    const btnUpload = $('btn-ota-upload-label');

    if (progressEl) progressEl.classList.remove('u-hidden');
    if (btnUpload) btnUpload.style.pointerEvents = 'none';
    if (barEl) {
        barEl.style.width = '0%';
        barEl.classList.remove('pulsing');
    }
    if (pctEl) pctEl.textContent = '0%';
    if (statusEl) statusEl.textContent = t('ota.status_uploading', 'Uploading firmware...');

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota', true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.timeout = 60000; // 60s hard timeout for OTA flash upload

    xhr.ontimeout = () => {
        if (btnUpload) btnUpload.style.pointerEvents = 'auto';
        const input = $('ota-file-input');
        if (input) input.value = '';
        Modal.alert('Firmware upload timed out (>60s). Please check Wi-Fi connection and retry.', {
            type: 'error',
            title: 'Upload Timeout',
        });
    };

    xhr.upload.onprogress = evt => {
        if (evt.lengthComputable) {
            const pct = Math.round((evt.loaded / evt.total) * 100);
            if (barEl) barEl.style.width = `${pct}%`;
            if (pctEl) pctEl.textContent = `${pct}%`;
            if (pct >= 100 && statusEl) {
                statusEl.textContent = t('ota.status_flashing', 'Flashing OTA partition...');
                if (barEl) barEl.classList.add('pulsing');
            }
        }
    };

    xhr.onload = async () => {
        if (xhr.status >= 200 && xhr.status < 300) {
            if (statusEl)
                statusEl.textContent = t(
                    'ota.status_rebooting',
                    'Update successful! Rebooting controller...'
                );
            if (barEl) barEl.style.width = '100%';
            if (pctEl) pctEl.textContent = '100%';

            let countdown = 8;
            Modal.custom({
                title: t('ota.status_rebooting', 'Rebooting...'),
                body: t(
                    'ota.reconnect_msg',
                    'Controller is rebooting... Reconnecting in {{sec}}s...'
                ).replace('{{sec}}', countdown),
                type: 'info',
                buttons: [],
            });

            const timer = setInterval(() => {
                countdown--;
                const msgEl = $('modal-body');
                if (msgEl) {
                    msgEl.textContent = t(
                        'ota.reconnect_msg',
                        'Controller is rebooting... Reconnecting in {{sec}}s...'
                    ).replace('{{sec}}', Math.max(0, countdown));
                }
                if (countdown <= 0) {
                    clearInterval(timer);
                    // Poll /api/status until device is back online
                    pollAfterReboot();
                }
            }, 1000);
        } else {
            if (btnUpload) btnUpload.style.pointerEvents = 'auto';
            const input = $('ota-file-input');
            if (input) input.value = '';
            let errMsg = xhr.statusText || 'OTA upload failed';
            try {
                const resp = JSON.parse(xhr.responseText);
                if (resp.message) errMsg = resp.message;
            } catch (_) {}
            Modal.alert(`Error (${xhr.status}): ${errMsg}`, {
                type: 'error',
                title: 'OTA Update Failed',
            });
        }
    };

    xhr.onerror = () => {
        if (btnUpload) btnUpload.style.pointerEvents = 'auto';
        const input = $('ota-file-input');
        if (input) input.value = '';
        Modal.alert('Network error during firmware upload.', {
            type: 'error',
            title: 'Upload Error',
        });
    };

    xhr.send(selectedOtaFile);
}

async function pollAfterReboot() {
    let attempts = 0;
    const maxAttempts = 20;

    const checkInterval = setInterval(async () => {
        attempts++;
        try {
            const res = await fetch('/api/status', { cache: 'no-store' });
            if (res.ok) {
                clearInterval(checkInterval);
                Modal.closeAll();
                await Modal.alert(
                    t(
                        'ota.success_msg',
                        'The new firmware was flashed successfully and the controller is back online!'
                    ),
                    {
                        type: 'success',
                        title: t('ota.success_title', '🎉 Firmware Update Complete'),
                    }
                );
                window.location.reload();
            }
        } catch (_) {
            if (attempts >= maxAttempts) {
                clearInterval(checkInterval);
                Modal.closeAll();
                window.location.reload();
            }
        }
    }, 1500);
}
