/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 05_modal.js
 * @brief Layer 2 Base UI: Asynchronous modal system (alerts, confirms, prompts, custom dialogs).
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 3. MODAL SYSTEM (replaces native alert/confirm) ─────────────────────
    const Modal = (() => {
        const ICONS = {
            info: { cls: 'icon-info', char: 'ℹ' },
            success: { cls: 'icon-success', char: '✓' },
            warning: { cls: 'icon-warning', char: '⚠' },
            danger: { cls: 'icon-danger', char: '✕' }
        };
        const TITLES = {
            info: 'Info', success: 'Success', warning: 'Warning', danger: 'Error'
        };

        let _resolve = null;

        function _show({ type = 'info', title, body, buttons, withInput = false, inputPlaceholder = '', inputType = 'text', inputValue = '', maxLength = null }) {
            const overlay = $('modal-overlay');
            const iconEl = $('modal-icon');
            const titleEl = $('modal-title');
            const bodyEl = $('modal-body');
            const inputEl = $('modal-input');
            const actionsEl = $('modal-actions');
            if (!overlay) return Promise.resolve(null);

            const ic = ICONS[type] || ICONS.info;
            iconEl.className = `modal-icon ${ic.cls}`;
            iconEl.textContent = ic.char;
            titleEl.textContent = title || TITLES[type] || 'Info';
            bodyEl.textContent = body;

            if (withInput) {
                inputEl.value = inputValue || '';
                inputEl.placeholder = inputPlaceholder || '';
                inputEl.type = inputType || 'text';
                if (maxLength) {
                    inputEl.maxLength = maxLength;
                } else {
                    inputEl.removeAttribute('maxlength');
                }
                inputEl.classList.remove('u-hidden');
                setTimeout(() => inputEl.focus(), 50);
            } else {
                inputEl.classList.add('u-hidden');
            }

            actionsEl.innerHTML = '';
            buttons.forEach(btn => {
                const el = document.createElement('button');
                el.type = 'button';
                el.className = `btn ${btn.cls || 'btn-outline'}`;
                el.textContent = btn.label;
                el.onclick = () => {
                    _close();
                    if (_resolve) {
                        _resolve(btn.value !== undefined ? btn.value : (withInput ? inputEl.value : true));
                        _resolve = null;
                    }
                };
                actionsEl.appendChild(el);
            });

            overlay.classList.add('active');

            return new Promise(resolve => { _resolve = resolve; });
        }

        function _close() {
            const overlay = document.getElementById('modal-overlay');
            if (overlay) overlay.classList.remove('active');
        }

        // Strict modal mode: Backdrop clicks are ignored.
        // Modals can only be closed by explicitly clicking one of the modal action buttons (OK, Cancel, Confirm, etc.).

        return {
            alert(body, { type = 'info', title } = {}) {
                return _show({
                    type, title, body,
                    buttons: [{ label: t('modal.btn_ok', 'OK'), cls: 'btn-primary', value: true }]
                });
            },
            confirm(body, { type = 'warning', title, confirmLabel, cancelLabel } = {}) {
                return _show({
                    type, title, body,
                    buttons: [
                        { label: cancelLabel || t('modal.btn_cancel', 'Cancel'), cls: 'btn-outline', value: false },
                        { label: confirmLabel || t('modal.btn_confirm', 'Confirm'), cls: type === 'danger' ? 'btn-danger' : 'btn-primary', value: true }
                    ]
                });
            },
            prompt(body, { type = 'info', title, inputPlaceholder = '', inputType = 'text', inputValue = '', maxLength = null, createLabel, cancelLabel } = {}) {
                return _show({
                    type, title, body, withInput: true, inputPlaceholder, inputType, inputValue, maxLength,
                    buttons: [
                        { label: cancelLabel || t('modal.btn_cancel', 'Cancel'), cls: 'btn-outline', value: null },
                        { label: createLabel || t('profile.btn_new', 'Create'), cls: 'btn-primary' }
                    ]
                });
            },
            _prompt(body, opts) {
                return this.prompt(body, opts);
            },
            custom(opts) {
                return _show(opts);
            }
        };
    })();
