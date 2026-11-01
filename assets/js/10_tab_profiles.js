/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 10_tab_profiles.js
 * @brief Layer 4 Domain Controller: Reflow profile CRUD, dual table editor, payload validation.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 16. PROFILE MANAGER ─────────────────────────────────────────────────
    let currentProfileData = null;
    let isUnsavedDraft = false;

    async function fetchProfiles(forceReload = false) {
        if (!forceReload && store.profiles !== null && Array.isArray(store.profiles)) {
            return store.profiles;
        }
        try {
            const profiles = await apiRequest('/api/profiles', 'GET');
            if (Array.isArray(profiles)) {
                store.profiles = profiles;
            }
            return store.profiles || [];
        } catch (err) {
            console.error('[Profiles] Failed to fetch profile list:', err);
            return store.profiles || [];
        }
    }

    function invalidateProfileCache() {
        store.profiles = null;
    }

    function getProfileFilename(p) {
        if (!p) return '';
        if (typeof p === 'string') return p;
        return p.file || p.name || '';
    }

    function getProfileDisplayName(p) {
        if (!p) return '';
        const raw = (typeof p === 'string') ? p : (p.name || p.file || '');
        return raw.replace(/\.json$/i, '');
    }

    async function loadProfileList(selectFilename, forceReload = false) {
        try {
            const profiles = await fetchProfiles(forceReload);
            const sel = $('profile-select');
            if (!sel) return;

            sel.innerHTML = '';
            const placeholder = document.createElement('option');
            placeholder.value = '';
            placeholder.textContent = t('profile.select_placeholder', '— Select Profile to Edit —');
            placeholder.hidden = true;
            placeholder.disabled = true;
            sel.appendChild(placeholder);

            if (Array.isArray(profiles) && profiles.length > 0) {
                profiles.forEach(p => {
                    const file = getProfileFilename(p);
                    const name = getProfileDisplayName(p);
                    if (file) {
                        const opt = document.createElement('option');
                        opt.value = file;
                        opt.textContent = name;
                        sel.appendChild(opt);
                    }
                });

                // Only load a profile if explicitly requested (e.g. after save/create); otherwise start clean
                const target = selectFilename || '';
                if (target && sel.querySelector(`option[value="${target}"]`)) {
                    sel.value = target;
                    isUnsavedDraft = false;
                    await loadProfileData(target);
                } else {
                    sel.value = '';
                    currentProfileData = null;
                    isUnsavedDraft = false;
                    renderProfileTable('table-top', [], true);
                    renderProfileTable('table-bottom', [], false);
                }
            } else {
                sel.innerHTML = `<option value="">${t('profile.no_profiles', 'No Profiles Available')}</option>`;
                currentProfileData = null;
                isUnsavedDraft = false;
                renderProfileTable('table-top', [], true);
                renderProfileTable('table-bottom', [], false);
            }
        } catch (err) {
            console.error('[Profile] Failed to load profile list:', err);
        }
    }

    async function loadProfileData(filename) {
        if (!filename || filename === 'undefined') return;
        try {
            const data = await apiRequest(`/api/profiles/${encodeURIComponent(filename)}`, 'GET');
            if (data) {
                isUnsavedDraft = false;
                currentProfileData = {
                    name: data.name || filename.replace(/\.json$/i, ''),
                    file: data.file || filename,
                    stepsTop: Array.isArray(data.stepsTop) ? data.stepsTop : [],
                    stepsBottom: Array.isArray(data.stepsBottom) ? data.stepsBottom : []
                };

                renderProfileTable('table-top', currentProfileData.stepsTop, true);
                renderProfileTable('table-bottom', currentProfileData.stepsBottom, false);
                console.log(`[Profile Editor] Loaded profile "${currentProfileData.name}" (${currentProfileData.stepsTop.length} top, ${currentProfileData.stepsBottom.length} bot steps).`);
            }
        } catch (err) {
            console.error(`[Profile Editor] Failed to load profile "${filename}":`, err);
        }
    }

    function onProfileSelectChange() {
        const sel = $('profile-select');
        if (!sel || !sel.value || sel.value === 'undefined') {
            currentProfileData = null;
            renderProfileTable('table-top', [], true);
            renderProfileTable('table-bottom', [], false);
            return;
        }

        if (isUnsavedDraft && currentProfileData && currentProfileData.file === sel.value) {
            renderProfileTable('table-top', currentProfileData.stepsTop, true);
            renderProfileTable('table-bottom', currentProfileData.stepsBottom, false);
            return;
        }

        loadProfileData(sel.value);
    }

    function renderProfileTable(tableId, steps, isTop) {
        const tbody = $(tableId)?.querySelector('tbody');
        if (!tbody) return;

        tbody.innerHTML = '';
        if (!steps || !steps.length) {
            const msg = currentProfileData ? t('profile.no_steps', 'No steps defined. Click "+ Add Step" below.') : t('profile.no_profile_selected', 'No profile selected. Choose a profile from the dropdown above or click "+ New Profile".');
            tbody.innerHTML = `<tr><td colspan="5" class="muted u-p-2">${msg}</td></tr>`;
            return;
        }

        steps.forEach((s, idx) => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><strong>#${idx + 1}</strong></td>
                <td><input type="number" value="${Format.temp(s.temp)}" step="${VALIDATION_RESOLUTIONS.TEMPERATURE}" min="${VALIDATION_LIMITS.MIN_TEMPERATURE}" max="${VALIDATION_LIMITS.MAX_TEMPERATURE}" onchange="ReflowApp.updateProfileStepField(${isTop}, ${idx}, 'temp', this.value)"></td>
                <td><input type="number" value="${Format.int(s.time)}" step="${VALIDATION_RESOLUTIONS.TIME_SEC}" min="0" max="${VALIDATION_LIMITS.MAX_STEP_TIME_S}" onchange="ReflowApp.updateProfileStepField(${isTop}, ${idx}, 'time', this.value)"></td>
                <td><input type="number" value="${Format.ramp(s.ramp)}" step="${VALIDATION_RESOLUTIONS.RAMP_RATE}" min="${VALIDATION_LIMITS.MIN_RAMP_RATE}" max="${VALIDATION_LIMITS.MAX_RAMP_RATE}" onchange="ReflowApp.updateProfileStepField(${isTop}, ${idx}, 'ramp', this.value)"></td>
                <td><button type="button" class="btn-delete-step" onclick="ReflowApp.removeProfileStep(${isTop}, ${idx})" title="${t('profile.delete_step_title', 'Delete Step')}">🗑</button></td>
            `;
            tbody.appendChild(tr);
        });
    }

    function updateProfileStepField(isTop, idx, field, val) {
        const list = isTop ? currentProfileData.stepsTop : currentProfileData.stepsBottom;
        if (list && list[idx]) {
            list[idx][field] = (field === 'time') ? Format.parseInt(val, 0) : Format.parseFloat(val, 0);
        }
    }

    async function addProfileStep(isTop) {
        if (!currentProfileData) {
            await Modal.alert(t('modal.no_profile_preheat', 'Please select or create a profile first.'), {
                type: 'info',
                title: t('modal.no_profile_title', 'No Profile')
            });
            return;
        }
        const list = isTop ? currentProfileData.stepsTop : currentProfileData.stepsBottom;
        if (list.length >= VALIDATION_LIMITS.MAX_PROFILE_STEPS) {
            await Modal.alert(t('modal.profile_steps_limit_reached', 'Maximum of {{max}} steps per heater reached.', { max: VALIDATION_LIMITS.MAX_PROFILE_STEPS }), {
                type: 'warning',
                title: t('modal.profile_steps_limit_title', 'Step Limit Reached')
            });
            return;
        }
        const last = list.length > 0 ? list[list.length - 1] : null;
        const newTemp = last ? Math.min(250, last.temp + 20) : (isTop ? 150 : 150);
        const newTime = last ? last.time : 60;
        const newRamp = last ? last.ramp : 1.0;

        list.push({ temp: newTemp, time: newTime, ramp: newRamp });
        renderProfileTable(isTop ? 'table-top' : 'table-bottom', list, isTop);
    }

    function removeProfileStep(isTop, idx) {
        if (!currentProfileData) return;
        const list = isTop ? currentProfileData.stepsTop : currentProfileData.stepsBottom;
        if (list && list.length > idx) {
            list.splice(idx, 1);
            renderProfileTable(isTop ? 'table-top' : 'table-bottom', list, isTop);
        }
    }

    async function createNewProfile() {
        let defaultVal = '';
        while (true) {
            const name = await Modal.prompt(t('modal.prompt_new_profile', 'Enter a name for the new profile:'), {
                title: t('modal.prompt_new_profile_title', 'New Profile'),
                inputPlaceholder: t('modal.prompt_new_profile_ph', 'Custom Profile (max. 30 chars)'),
                inputValue: defaultVal,
                maxLength: 30,
                type: 'info',
                createLabel: t('profile.btn_new', '+ New Profile'),
                cancelLabel: t('modal.btn_cancel', 'Cancel')
            });
            if (name === null) return; // User cancelled

            const cleanName = name.trim();
            if (!cleanName) {
                await Modal.alert(t('modal.profile_name_empty', 'Profile name cannot be empty.'), {
                    type: 'warning',
                    title: t('modal.profile_name_invalid_title', 'Invalid Profile Name')
                });
                defaultVal = '';
                continue;
            }

            if (cleanName.length > 30) {
                await Modal.alert(t('modal.profile_name_too_long', 'Profile name is too long ({{len}} chars). Maximum allowed is 30 characters.', { len: cleanName.length }), {
                    type: 'warning',
                    title: t('modal.profile_name_invalid_title', 'Invalid Profile Name')
                });
                defaultVal = cleanName.substring(0, 30);
                continue;
            }

            const filename = cleanName.toLowerCase().replace(/[^a-z0-9_-]/g, '_') + '.json';

            isUnsavedDraft = true;
            currentProfileData = {
                name: cleanName,
                file: filename,
                stepsBottom: [
                    { temp: 50.0, time: 10, ramp: 1.0 }
                ],
                stepsTop: [
                    { temp: 50.0, time: 10, ramp: 1.0 }
                ]
            };

            const sel = $('profile-select');
            if (sel) {
                const opt = document.createElement('option');
                opt.value = filename;
                opt.textContent = `${cleanName} (Draft)`;
                sel.appendChild(opt);
                sel.value = filename;
            }

            renderProfileTable('table-top', currentProfileData.stepsTop, true);
            renderProfileTable('table-bottom', currentProfileData.stepsBottom, false);
            break;
        }
    }

    async function saveProfileToController() {
        if (store.stateEnum === 8) {
            await Modal.alert(
                t('modal.profile_save_backup_locked', 'Profile modification is locked during backup or restore operations.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Save Failed: Process Active')
                }
            );
            return;
        }

        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            await Modal.alert(
                t('modal.save_profile_locked_active', 'Failed to save profile: Reflow process or autotune is currently active. Profile modifications are only permitted in IDLE state.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Save Failed: Process Active')
                }
            );
            return;
        }

        if (!currentProfileData || !currentProfileData.name || !currentProfileData.file) {
            await Modal.alert(t('modal.invalid_profile_data', 'Invalid profile data.'), {
                type: 'danger',
                title: t('modal.invalid_profile_title', 'Invalid Profile')
            });
            return;
        }

        if (currentProfileData.file === 'factory-profile.json' || currentProfileData.file === 'factory-profile') {
            await Modal.alert(
                t('modal.factory_profile_readonly', 'The factory default profile (factory-profile.json) is read-only and cannot be overwritten. Please create a new profile using \'+ New Profile\' or use a different filename.'),
                {
                    type: 'warning',
                    title: t('modal.factory_profile_readonly_title', 'Factory Profile Read-Only')
                }
            );
            return;
        }
        const val = validateProfilePayload(currentProfileData);
        if (!val.valid) {
            await Modal.alert(val.error, {
                type: 'warning',
                title: t('modal.invalid_profile_title', '⚠️ Invalid Profile')
            });
            return;
        }

        try {
            const res = await apiRequest('/api/profiles', 'POST', currentProfileData);
            if (res && res.success !== false) {
                invalidateProfileCache();
                isUnsavedDraft = false;
                await Modal.alert(t('modal.profile_saved', 'Profile "{{name}}" saved successfully!', { name: currentProfileData.name }), {
                    type: 'success',
                    title: t('modal.profile_saved_title', 'Profile Saved')
                });
                await loadProfileList(currentProfileData.file, true);
                await populateChartProfileSelect();
            } else {
                const errMsg = localizeApiError(res && res.error, 'modal.profile_save_failed', 'Failed to save profile to controller.');
                await Modal.alert(errMsg, {
                    type: 'danger',
                    title: t('modal.save_failed_title', 'Save Failed')
                });
            }
        } catch (err) {
            console.error('[Profile Editor] Save failed:', err);
            await Modal.alert(t('modal.profile_save_failed', 'Failed to save profile to controller.'), {
                type: 'danger',
                title: t('modal.save_failed_title', 'Save Failed')
            });
        }
    }

    async function deleteProfileFromController() {
        if (store.stateEnum === 8) {
            await Modal.alert(
                t('modal.profile_delete_backup_locked', 'Profile deletion is locked during backup or restore operations.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Delete Failed: Process Active')
                }
            );
            return;
        }

        if (store.stateEnum !== 0 && store.stateEnum !== 5 && store.stateEnum !== 8) {
            await Modal.alert(
                t('modal.delete_profile_locked_active', 'Failed to delete profile: Reflow process or autotune is currently active. Profile deletions are only permitted in IDLE state.'),
                {
                    type: 'danger',
                    title: t('modal.save_locked_active_title', '⚠️ Delete Failed: Process Active')
                }
            );
            return;
        }

        const sel = $('profile-select');
        const file = sel ? sel.value : (currentProfileData ? currentProfileData.file : '');
        if (!file) return;

        if (file === 'factory-profile.json') {
            await Modal.alert(t('modal.factory_profile_protected', 'The factory default profile cannot be deleted.'), {
                type: 'warning',
                title: t('modal.protected_profile_title', 'Protected Profile')
            });
            return;
        }

        if (isUnsavedDraft && currentProfileData && file === currentProfileData.file) {
            // Unsaved draft simply remove from UI
            isUnsavedDraft = false;
            currentProfileData = null;
            await loadProfileList(null, true);
            return;
        }

        const isRunningProfile = (store.stateEnum >= 1 && store.stateEnum <= 4) && (store.activeProfileFile === file);
        const confirmMsg = isRunningProfile
            ? t('modal.delete_active_profile_confirm', "Profile '{{file}}' is currently active in the running reflow process. The current process will finish safely from RAM, but the profile will be deleted from storage and unavailable for future runs. Are you sure you want to delete it?", { file: file })
            : t('modal.delete_profile_confirm', 'Are you sure you want to delete profile "{{file}}"?', { file: file });
        const confirmTitle = isRunningProfile
            ? t('modal.delete_active_profile_title', '⚠️ Delete Running Profile?')
            : t('modal.delete_profile_title', 'Delete Profile');

        const ok = await Modal.confirm(confirmMsg, {
            type: 'danger',
            title: confirmTitle,
            confirmLabel: t('modal.btn_delete', 'Delete'),
            cancelLabel: t('modal.btn_cancel', 'Cancel')
        });
        if (!ok) return;

        try {
            const res = await apiRequest(`/api/profiles/${encodeURIComponent(file)}`, 'DELETE');
            if (res && res.success !== false) {
                invalidateProfileCache();
                await Modal.alert(t('modal.profile_deleted', 'Profile "{{file}}" deleted successfully.', { file: file }), {
                    type: 'success',
                    title: t('modal.profile_deleted_title', 'Profile Deleted')
                });
                currentProfileData = null;
                await loadProfileList(null, true);
                await populateChartProfileSelect();
            } else {
                await Modal.alert(t('modal.profile_delete_failed', 'Failed to delete profile.'), {
                    type: 'danger',
                    title: t('modal.delete_failed_title', 'Delete Failed')
                });
            }
        } catch (err) {
            console.error('[Profile Editor] Delete failed:', err);
            await Modal.alert(t('toast.settings_error', 'Error deleting profile.'), {
                type: 'danger',
                title: t('modal.error_title', 'Error')
            });
        }
    }

    // ── CHART PROFILE DROPDOWN ──────────────────────────────────────────────
    async function populateChartProfileSelect(profilesList) {
        try {
            const profiles = (Array.isArray(profilesList) && profilesList.length > 0)
                ? profilesList
                : await fetchProfiles();
            const sel = $('chart-profile-select');
            if (!sel) return;
            sel.innerHTML = '';

            // Always prepend a neutral placeholder (value="" so sel.value='' works on all browsers)
            const placeholder = document.createElement('option');
            placeholder.value = '';
            placeholder.textContent = t('hud.select_profile_placeholder', '— Select Profile —');
            // hidden + disabled prevents user from selecting it again but allows sel.value='' to work
            placeholder.hidden = true;
            placeholder.disabled = true;
            sel.appendChild(placeholder);

            if (Array.isArray(profiles) && profiles.length > 0) {
                profiles.forEach(p => {
                    const file = getProfileFilename(p);
                    const name = getProfileDisplayName(p);
                    if (file) {
                        const opt = document.createElement('option');
                        opt.value = file;
                        opt.textContent = name;
                        sel.appendChild(opt);
                    }
                });
            }

            // Priority for pre-selection in Cockpit HUD:
            // 1. store.activeProfileFile (if already known from restoreHistory or manual choice)
            // 2. currentSettings.defaultProfile (Auto-Reload at Startup from settings)
            let chosen = store.activeProfileFile;
            if (!chosen && currentSettings && currentSettings.defaultProfile) {
                chosen = currentSettings.defaultProfile;
            }

            if (chosen && sel.querySelector(`option[value="${chosen}"]`)) {
                sel.value = chosen;
                store.activeProfileFile = chosen;
                store.activeProfileName = chosen.replace(/\.json$/i, '');
            } else {
                sel.value = '';  // explicitly show placeholder
            }
        } catch (err) {
            console.error('[ChartSelect] Failed to populate profile dropdown:', err);
        }
    }

    function onChartProfileSelectChange(file) {
        if (!file) return;
        store.activeProfileFile = file;
        store.activeProfileName = file.replace(/\.json$/i, '');
        console.log(`[ChartSelect] Profile selected: ${file}`);
    }

function validateProfilePayload(data) {
        if (!data || typeof data !== 'object' || Array.isArray(data)) {
            return { valid: false, error: t('val.invalid_json_format', 'Invalid JSON object format.') };
        }
        // Schema version guard – reject files from a newer firmware
        if (data.schemaVersion && typeof data.schemaVersion === 'number') {
            if (data.schemaVersion > SCHEMA_VERSIONS.REFLOW_PROFILE) {
                return { valid: false, error: t('val.incompatible_schema', 'Incompatible file: Schema version (v{{ver}}) is newer than supported controller firmware (v{{cur}}). Please update firmware first.', { ver: data.schemaVersion, cur: SCHEMA_VERSIONS.REFLOW_PROFILE }) };
            }
        }
        // Reject if it is machine settings or PID library
        if (data.maxTempTop !== undefined || data.holdLowTolerance !== undefined || data.topBurstWindowMs !== undefined || data.safetyLimits !== undefined) {
            return { valid: false, error: t('val.mismatched_settings', 'Invalid file: Machine Settings JSON cannot be imported as Reflow Profile.') };
        }
        if (Array.isArray(data.top) && Array.isArray(data.bottom) && data.stepsTop === undefined) {
            return { valid: false, error: t('val.mismatched_pid', 'Invalid file: PID Library JSON cannot be imported as Reflow Profile.') };
        }
        if (!data.name || typeof data.name !== 'string' || !data.name.trim()) {
            return { valid: false, error: t('val.missing_profile_name', 'Invalid profile: Missing profile name.') };
        }
        if (data.name.trim().length > 30) {
            return { valid: false, error: t('modal.profile_name_too_long', `Profile name exceeds limit of 30 characters (${data.name.trim().length} chars).`, { len: data.name.trim().length }) };
        }
        if (!Array.isArray(data.stepsTop) || !Array.isArray(data.stepsBottom)) {
            return { valid: false, error: t('val.missing_profile_steps', 'Invalid profile: Must contain stepsTop and stepsBottom arrays.') };
        }
        if (data.stepsTop.length === 0 && data.stepsBottom.length === 0) {
            return { valid: false, error: t('val.empty_profile_steps', 'Profile must contain at least one step.') };
        }
        if (data.stepsTop.length > VALIDATION_LIMITS.MAX_PROFILE_STEPS || data.stepsBottom.length > VALIDATION_LIMITS.MAX_PROFILE_STEPS) {
            return { valid: false, error: t('val.profile_steps_exceeded', 'Profile exceeds maximum of {{max}} steps per heater.', { max: VALIDATION_LIMITS.MAX_PROFILE_STEPS }) };
        }

        // Validate top steps (supports both 'temp' and 'targetTemp')
        for (let i = 0; i < data.stepsTop.length; i++) {
            const step = data.stepsTop[i];
            const temp = (typeof step.temp === 'number') ? step.temp : ((typeof step.targetTemp === 'number') ? step.targetTemp : null);
            const ramp = (typeof step.ramp === 'number') ? step.ramp : ((typeof step.rampRate === 'number') ? step.rampRate : null);
            const time = (typeof step.time === 'number') ? step.time : ((typeof step.holdTimeS === 'number') ? step.holdTimeS : null);

            if (temp === null || isNaN(temp) || temp < VALIDATION_LIMITS.MIN_TEMPERATURE || temp > VALIDATION_LIMITS.MAX_TEMPERATURE) {
                return { valid: false, error: t('val.step_temp_out_of_range', `Top step #${i + 1} Target Temp (${temp !== null ? Format.temp(temp) : 'invalid'}°C) is out of bounds (${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}–${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C).`, { heater: 'Top', step: i + 1, val: temp !== null ? Format.temp(temp) : 'invalid', min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE) }) };
            }
            if (ramp !== null && (isNaN(ramp) || ramp < VALIDATION_LIMITS.MIN_RAMP_RATE || ramp > VALIDATION_LIMITS.MAX_RAMP_RATE)) {
                return { valid: false, error: t('val.step_ramp_out_of_range', `Top step #${i + 1} Ramp Rate (${Format.ramp(ramp)}°C/s) is out of bounds (${Format.ramp(VALIDATION_LIMITS.MIN_RAMP_RATE)}–${Format.ramp(VALIDATION_LIMITS.MAX_RAMP_RATE)}°C/s).`, { heater: 'Top', step: i + 1, val: Format.ramp(ramp), min: Format.ramp(VALIDATION_LIMITS.MIN_RAMP_RATE), max: Format.ramp(VALIDATION_LIMITS.MAX_RAMP_RATE) }) };
            }
            if (time !== null && (isNaN(time) || time < 0 || time > VALIDATION_LIMITS.MAX_STEP_TIME_S)) {
                return { valid: false, error: t('val.step_time_out_of_range', `Top step #${i + 1} Hold Time (${time}s) exceeds maximum of ${VALIDATION_LIMITS.MAX_STEP_TIME_S}s.`, { heater: 'Top', step: i + 1, val: time, max: VALIDATION_LIMITS.MAX_STEP_TIME_S }) };
            }
        }
        // Validate bottom steps (supports both 'temp' and 'targetTemp')
        for (let i = 0; i < data.stepsBottom.length; i++) {
            const step = data.stepsBottom[i];
            const temp = (typeof step.temp === 'number') ? step.temp : ((typeof step.targetTemp === 'number') ? step.targetTemp : null);
            const ramp = (typeof step.ramp === 'number') ? step.ramp : ((typeof step.rampRate === 'number') ? step.rampRate : null);
            const time = (typeof step.time === 'number') ? step.time : ((typeof step.holdTimeS === 'number') ? step.holdTimeS : null);

            if (temp === null || isNaN(temp) || temp < VALIDATION_LIMITS.MIN_TEMPERATURE || temp > VALIDATION_LIMITS.MAX_TEMPERATURE) {
                return { valid: false, error: t('val.step_temp_out_of_range', `Bottom step #${i + 1} Target Temp (${temp !== null ? Format.temp(temp) : 'invalid'}°C) is out of bounds (${Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE)}–${Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE)}°C).`, { heater: 'Bottom', step: i + 1, val: temp !== null ? Format.temp(temp) : 'invalid', min: Format.temp(VALIDATION_LIMITS.MIN_TEMPERATURE), max: Format.temp(VALIDATION_LIMITS.MAX_TEMPERATURE) }) };
            }
            if (ramp !== null && (isNaN(ramp) || ramp < VALIDATION_LIMITS.MIN_RAMP_RATE || ramp > VALIDATION_LIMITS.MAX_RAMP_RATE)) {
                return { valid: false, error: t('val.step_ramp_out_of_range', `Bottom step #${i + 1} Ramp Rate (${Format.ramp(ramp)}°C/s) is out of bounds (${Format.ramp(VALIDATION_LIMITS.MIN_RAMP_RATE)}–${Format.ramp(VALIDATION_LIMITS.MAX_RAMP_RATE)}°C/s).`, { heater: 'Bottom', step: i + 1, val: Format.ramp(ramp), min: Format.ramp(VALIDATION_LIMITS.MIN_RAMP_RATE), max: Format.ramp(VALIDATION_LIMITS.MAX_RAMP_RATE) }) };
            }
            if (time !== null && (isNaN(time) || time < 0 || time > VALIDATION_LIMITS.MAX_STEP_TIME_S)) {
                return { valid: false, error: t('val.step_time_out_of_range', `Bottom step #${i + 1} Hold Time (${time}s) exceeds maximum of ${VALIDATION_LIMITS.MAX_STEP_TIME_S}s.`, { heater: 'Bottom', step: i + 1, val: time, max: VALIDATION_LIMITS.MAX_STEP_TIME_S }) };
            }
        }
        return { valid: true };
    }
