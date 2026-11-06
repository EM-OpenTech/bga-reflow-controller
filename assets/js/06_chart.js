/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @file 06_chart.js
 * @brief Layer 3 Telemetry Engine: Chart.js configuration, live datasets, ring buffers, tooltips, exports.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

// ── 10. CHART.JS LIVE DIAGRAMM & PLUGINS ────────────────────────────────
let chartInstance = null;
let cachedStepMarkers = [];

// Custom Chart.js Plugin: Process Zones, TAL Line & Step-Markers
const reflowChartPlugin = {
    id: 'reflowChartPlugin',
    beforeDraw: chart => {
        const { ctx, chartArea, scales } = chart;
        if (!chartArea || !scales.x || !scales.y) return;

        const isDark = store.currentTheme === 'dark';

        // 1. Draw Process Target Temperature Zones (Horizontal Bands)
        if (store.showZones) {
            const zones = [
                {
                    label: t('chart.zone_preheat', 'PREHEAT (100-150°C)'),
                    min: 100,
                    max: 150,
                    color: isDark ? 'rgba(34, 197, 94, 0.05)' : 'rgba(34, 197, 94, 0.06)',
                    borderColor: isDark ? 'rgba(34, 197, 94, 0.15)' : 'rgba(34, 197, 94, 0.2)',
                },
                {
                    label: t('chart.zone_soak', 'SOAK (150-190°C)'),
                    min: 150,
                    max: 190,
                    color: isDark ? 'rgba(234, 179, 8, 0.05)' : 'rgba(234, 179, 8, 0.06)',
                    borderColor: isDark ? 'rgba(234, 179, 8, 0.15)' : 'rgba(234, 179, 8, 0.2)',
                },
                {
                    label: t('chart.zone_prereflow', 'PRE-REFLOW (190-217°C)'),
                    min: 190,
                    max: 217,
                    color: isDark ? 'rgba(249, 115, 22, 0.05)' : 'rgba(249, 115, 22, 0.06)',
                    borderColor: isDark ? 'rgba(249, 115, 22, 0.15)' : 'rgba(249, 115, 22, 0.2)',
                },
                {
                    label: t('chart.zone_reflow', 'REFLOW (217-245°C)'),
                    min: 217,
                    max: 255,
                    color: isDark ? 'rgba(239, 68, 68, 0.06)' : 'rgba(239, 68, 68, 0.07)',
                    borderColor: isDark ? 'rgba(239, 68, 68, 0.2)' : 'rgba(239, 68, 68, 0.25)',
                },
            ];

            ctx.save();
            zones.forEach(zone => {
                const yTop = scales.y.getPixelForValue(zone.max);
                const yBottom = scales.y.getPixelForValue(zone.min);
                const height = yBottom - yTop;

                // Fill zone
                ctx.fillStyle = zone.color;
                ctx.fillRect(chartArea.left, yTop, chartArea.width, height);

                // Top dashed border
                ctx.strokeStyle = zone.borderColor;
                ctx.lineWidth = 1;
                ctx.setLineDash([4, 4]);
                ctx.beginPath();
                ctx.moveTo(chartArea.left, yTop);
                ctx.lineTo(chartArea.right, yTop);
                ctx.stroke();

                // Zone text label positioned cleanly to the left of the top-right export buttons
                ctx.setLineDash([]);
                ctx.fillStyle = isDark ? 'rgba(255, 255, 255, 0.50)' : 'rgba(15, 23, 42, 0.50)';
                ctx.font = '9px monospace';
                ctx.textAlign = 'right';
                ctx.textBaseline = 'top';
                ctx.fillText(zone.label, chartArea.right - 8, yTop + 4);
            });
            ctx.restore();
        }

        // 2. Separate TAL Line (217°C Liquidus Threshold) - Light red, dashed
        if (store.showTalLine) {
            const talY = scales.y.getPixelForValue(217.0);
            if (talY >= chartArea.top && talY <= chartArea.bottom) {
                ctx.save();
                // Light red dashed line
                ctx.strokeStyle = isDark ? 'rgba(248, 113, 113, 0.65)' : 'rgba(239, 68, 68, 0.7)';
                ctx.lineWidth = 1.4;
                ctx.setLineDash([6, 4]);
                ctx.beginPath();
                ctx.moveTo(chartArea.left, talY);
                ctx.lineTo(chartArea.right, talY);
                ctx.stroke();

                // Label: TAL (>217°C) positioned right above the line
                ctx.setLineDash([]);
                ctx.fillStyle = isDark ? '#f87171' : '#dc2626';
                ctx.font = 'bold 9px monospace';
                ctx.textAlign = 'left';
                ctx.textBaseline = 'bottom';
                ctx.fillText('TAL (>217°C)', chartArea.left + 8, talY - 2);
                ctx.restore();
            }
        }
    },
    afterDatasetsDraw: chart => {
        if (!store.showStepMarkers) return;

        const { ctx, chartArea, scales } = chart;
        if (!ctx || !chartArea || !scales?.x || !scales?.y || !cachedStepMarkers.length) return;

        ctx.save();
        const light = store.currentTheme === 'light';

        cachedStepMarkers.forEach(m => {
            const time = m.time !== undefined ? m.time : m.timeS;
            const temp = m.temp !== undefined ? m.temp : m.targetTemp;
            const step = m.step !== undefined ? m.step : (m.stepIndex ?? 0);
            const isTop = !!m.isTop;

            if (time === undefined || temp === undefined) return;

            const x = scales.x.getPixelForValue(Number(time));
            if (!Number.isFinite(x) || x < chartArea.left || x > chartArea.right) return;

            const targetY = scales.y.getPixelForValue(Number(temp));

            // 1. Vertical Dashed Line (Monochrome / Neutral)
            ctx.setLineDash([5, 4]);
            ctx.strokeStyle = light ? 'rgba(15, 23, 42, 0.65)' : 'rgba(203, 213, 225, 0.65)';
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(x, chartArea.top);
            ctx.lineTo(x, chartArea.bottom);
            ctx.stroke();

            // 2. Point Marker on Curve at Target Temperature
            if (targetY >= chartArea.top && targetY <= chartArea.bottom) {
                ctx.setLineDash([]);
                ctx.fillStyle = light ? '#0f172a' : '#f8fafc';
                ctx.beginPath();
                ctx.arc(x, targetY, 3.5, 0, 2 * Math.PI);
                ctx.fill();

                ctx.strokeStyle = light ? '#ffffff' : '#0f172a';
                ctx.lineWidth = 1.5;
                ctx.stroke();
            }

            // 3. Monochrome Pill Badge at Top of Chart
            // Line 1: "Step 1"
            // Line 2: "Bot@150°C" / "Top@150°C"
            const line1 = t('chart.step_badge', 'Step {{num}}', { num: step + 1 });
            const line2 = `${isTop ? 'Top' : 'Bot'}@${Math.round(temp)}°C`;

            const lineH = 12;
            ctx.font = 'bold 10px Outfit, sans-serif';
            const w1 = ctx.measureText(line1).width;
            ctx.font = '11px Outfit, sans-serif';
            const w2 = ctx.measureText(line2).width;
            const padding = 6;
            const pillWidth = Math.max(w1, w2) + padding * 2;
            const pillHeight = lineH * 2 + 6;

            let pillX = x + 4;
            if (pillX + pillWidth > chartArea.right) pillX = x - pillWidth - 4;
            const pillY = chartArea.top + (isTop ? 6 : 38);

            // Pill Background & Border (Black/White / Dark Slate)
            ctx.fillStyle = light ? 'rgba(241, 245, 249, 0.95)' : 'rgba(15, 23, 42, 0.95)';
            ctx.strokeStyle = light ? 'rgba(203, 213, 225, 0.8)' : 'rgba(51, 65, 85, 0.8)';
            ctx.lineWidth = 1;

            if (ctx.roundRect) {
                ctx.beginPath();
                ctx.roundRect(pillX, pillY, pillWidth, pillHeight, 4);
                ctx.fill();
                ctx.stroke();
            } else {
                ctx.fillRect(pillX, pillY, pillWidth, pillHeight);
            }

            // Text
            ctx.fillStyle = light ? '#0f172a' : '#f8fafc';
            ctx.textAlign = 'left';
            ctx.textBaseline = 'top';

            ctx.font = 'bold 10px Outfit, sans-serif';
            ctx.fillText(line1, pillX + padding, pillY + 4);

            ctx.font = '11px Outfit, sans-serif';
            ctx.fillText(line2, pillX + padding, pillY + 4 + lineH);
        });

        ctx.restore();
    },
};

let chartMouseX = null;
let chartMouseY = null;

// ── Custom HTML/CSS Tooltip Handler (4-Quadrant 2D Avoidance & Theme-Reactive) ──
function externalTooltipHandler(context) {
    const { chart, tooltip } = context;
    const parent = chart.canvas.parentNode;
    if (!parent) return;

    let tooltipEl = parent.querySelector('.chartjs-custom-tooltip');
    if (!tooltipEl) {
        tooltipEl = document.createElement('div');
        tooltipEl.className = 'chartjs-custom-tooltip';
        parent.appendChild(tooltipEl);
    }

    if (tooltip.opacity === 0) {
        tooltipEl.style.opacity = '0';
        return;
    }

    if (tooltip.body) {
        const titleLines = tooltip.title || [];
        const bodyLines = tooltip.body.map(b => b.lines);

        let innerHtml = '';
        titleLines.forEach(title => {
            innerHtml += `<div class="chart-tooltip-title">${title}</div>`;
        });

        innerHtml += '<div class="chart-tooltip-body">';
        bodyLines.forEach((body, i) => {
            const colors = (tooltip.labelColors && tooltip.labelColors[i]) || {};
            const bg = colors.borderColor || colors.backgroundColor || 'var(--accent)';
            const colorBox = `<span class="chart-tooltip-color-box" style="background:${bg}"></span>`;
            innerHtml += `<div class="chart-tooltip-row">${colorBox}<span>${body}</span></div>`;
        });
        innerHtml += '</div>';

        tooltipEl.innerHTML = innerHtml;
    }

    const containerWidth = parent.clientWidth;
    const containerHeight = parent.clientHeight;
    const tooltipWidth = tooltipEl.offsetWidth || 150;
    const tooltipHeight = tooltipEl.offsetHeight || 80;

    // Reference Position: Cursor mouse coordinates if available, otherwise data point caret
    const refX = Number.isFinite(chartMouseX) ? chartMouseX : tooltip.caretX;
    const refY = Number.isFinite(chartMouseY) ? chartMouseY : tooltip.caretY;

    // ── 4-Quadrant 2D Avoidance Logic ──
    // 1. Horizontal: Left half -> RIGHT of cursor, Right half -> LEFT of cursor
    const isLeftHalf = refX < containerWidth * 0.5;
    let posX = isLeftHalf ? refX + 16 : refX - tooltipWidth - 16;

    // 2. Vertical: Top half -> BELOW cursor, Bottom half -> ABOVE cursor
    const isTopHalf = refY < containerHeight * 0.5;
    let posY = isTopHalf ? refY + 16 : refY - tooltipHeight - 16;

    // Edge Clamping: Ensure tooltip never exceeds chart container borders
    posX = Math.max(8, Math.min(containerWidth - tooltipWidth - 8, posX));
    posY = Math.max(8, Math.min(containerHeight - tooltipHeight - 8, posY));

    tooltipEl.style.opacity = '1';
    tooltipEl.style.left = `${posX}px`;
    tooltipEl.style.top = `${posY}px`;
}

function initChart() {
    const canvas = $('liveChart');
    if (!canvas) return;

    const isDark = store.currentTheme === 'dark';
    const gridColor = isDark ? 'rgba(255, 255, 255, 0.07)' : 'rgba(0, 0, 0, 0.06)';
    const tickColor = isDark ? 'rgba(255, 255, 255, 0.65)' : 'rgba(0, 0, 0, 0.65)';

    const config = {
        type: 'line',
        data: {
            datasets: [
                {
                    // 0: TOP HEATER (IST) – Blue
                    label: t('chart.ds_top_pv', 'Top PV'),
                    data: [],
                    borderColor: '#0284c7',
                    backgroundColor: 'rgba(2, 132, 199, 0.08)',
                    borderWidth: 2.2,
                    pointRadius: 0,
                    pointHoverRadius: 4,
                    tension: 0.15,
                    fill: false,
                },
                {
                    // 1: TOP SETPOINT – Blue Dashed
                    label: t('chart.ds_top_sp', 'Top SP'),
                    data: [],
                    borderColor: 'rgba(2, 132, 199, 0.55)',
                    borderWidth: 1.5,
                    borderDash: [5, 4],
                    pointRadius: 0,
                    pointHoverRadius: 0,
                    tension: 0,
                    fill: false,
                },
                {
                    // 2: BOTTOM HEATER (IST) – Green
                    label: t('chart.ds_bot_pv', 'Bottom PV'),
                    data: [],
                    borderColor: '#22c55e',
                    backgroundColor: 'rgba(34, 197, 94, 0.08)',
                    borderWidth: 2.2,
                    pointRadius: 0,
                    pointHoverRadius: 4,
                    tension: 0.15,
                    fill: false,
                },
                {
                    // 3: BOTTOM SETPOINT – Green Dashed
                    label: t('chart.ds_bot_sp', 'Bottom SP'),
                    data: [],
                    borderColor: 'rgba(34, 197, 94, 0.55)',
                    borderWidth: 1.5,
                    borderDash: [5, 4],
                    pointRadius: 0,
                    pointHoverRadius: 0,
                    tension: 0,
                    fill: false,
                },
            ],
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            interaction: {
                mode: 'index',
                intersect: false,
            },
            layout: {
                padding: { top: 14, right: 14, bottom: 4, left: 4 },
            },
            plugins: {
                legend: {
                    display: true,
                    position: 'top',
                    align: 'center',
                    labels: {
                        color: tickColor,
                        font: { family: 'monospace', size: 10, weight: 'bold' },
                        boxWidth: 14,
                        boxHeight: 2,
                        padding: 12,
                        usePointStyle: false,
                    },
                },
                tooltip: {
                    enabled: false,
                    external: externalTooltipHandler,
                    callbacks: {
                        title: items => {
                            if (!items.length) return '';
                            const sec = items[0].parsed.x;
                            return t('chart.tooltip_time', 'Time: {{time}} min ({{sec}}s)', {
                                time: formatTime(sec),
                                sec,
                            });
                        },
                        label: item => {
                            return `<strong>${item.dataset.label}:</strong> ${Format.temp(item.parsed.y)} °C`;
                        },
                    },
                },
            },
            scales: {
                x: {
                    type: 'linear',
                    title: {
                        display: true,
                        text: t('chart.axis_runtime', 'Runtime (min)'),
                        color: tickColor,
                        font: { family: 'monospace', size: 10, weight: 'bold' },
                    },
                    min: 0,
                    suggestedMax: 600, // Starts at 10 minutes (300s)
                    grid: { color: gridColor },
                    ticks: {
                        color: tickColor,
                        font: { family: 'monospace', size: 9 },
                        stepSize: 60, // 1-minute grid ticks
                        callback: val => {
                            const m = Math.floor(val / 60);
                            const s = val % 60;
                            return s === 0 ? `${m}m` : `${m}:${s.toString().padStart(2, '0')}`;
                        },
                    },
                },
                y: {
                    type: 'linear',
                    title: {
                        display: true,
                        text: t('chart.axis_temperature', 'Temperature (°C)'),
                        color: tickColor,
                        font: { family: 'monospace', size: 10, weight: 'bold' },
                    },
                    min: 0,
                    suggestedMax: 260,
                    grid: { color: gridColor },
                    ticks: {
                        color: tickColor,
                        font: { family: 'monospace', size: 9 },
                        stepSize: 20, // 0, 20, 40, 60, 80, 100...
                        callback: val => `${val}°C`,
                    },
                },
            },
        },
        plugins: [reflowChartPlugin],
    };

    // Track exact cursor coordinates on canvas for intelligent 4-quadrant tooltip positioning
    canvas.addEventListener('mousemove', e => {
        const rect = canvas.getBoundingClientRect();
        chartMouseX = e.clientX - rect.left;
        chartMouseY = e.clientY - rect.top;
    });

    canvas.addEventListener('mouseleave', () => {
        chartMouseX = null;
        chartMouseY = null;
    });

    chartInstance = new Chart(canvas, config);
}

function updateChartTheme(theme) {
    if (!chartInstance) return;
    const isDark = theme === 'dark';
    const gridColor = isDark ? 'rgba(255, 255, 255, 0.07)' : 'rgba(0, 0, 0, 0.06)';
    const tickColor = isDark ? 'rgba(255, 255, 255, 0.65)' : 'rgba(0, 0, 0, 0.65)';

    chartInstance.options.scales.x.grid.color = gridColor;
    chartInstance.options.scales.x.ticks.color = tickColor;
    chartInstance.options.scales.x.title.color = tickColor;

    chartInstance.options.scales.y.grid.color = gridColor;
    chartInstance.options.scales.y.ticks.color = tickColor;
    chartInstance.options.scales.y.title.color = tickColor;

    chartInstance.options.plugins.legend.labels.color = tickColor;

    chartInstance.update('none');
}

function resetChart() {
    if (!chartInstance) return;
    chartInstance.data.datasets.forEach(ds => (ds.data = []));
    cachedStepMarkers = [];
    chartInstance.options.scales.x.suggestedMax = 600; // 10 minutes default
    chartInstance.update('none');
}

function appendChartData(elapsedSec, topTemp, topSet, bottomTemp, bottomSet, markers) {
    if (!chartInstance) return;

    // Dynamically expand X-axis in 2-minute blocks (120s) when process duration approaches right edge
    const currentMax = chartInstance.options.scales.x.suggestedMax || 600;
    if (elapsedSec >= currentMax - 10) {
        chartInstance.options.scales.x.suggestedMax = currentMax + 120; // +2 minutes
    }

    const ds0 = chartInstance.data.datasets[0].data;
    const last = ds0.length > 0 ? ds0[ds0.length - 1] : null;

    if (last && last.x === elapsedSec) {
        // Update current second measurement in-place
        last.y = topTemp;
        chartInstance.data.datasets[1].data[ds0.length - 1].y = topSet;
        chartInstance.data.datasets[2].data[ds0.length - 1].y = bottomTemp;
        chartInstance.data.datasets[3].data[ds0.length - 1].y = bottomSet;
    } else {
        // Push new second data point
        ds0.push({ x: elapsedSec, y: topTemp });
        chartInstance.data.datasets[1].data.push({ x: elapsedSec, y: topSet });
        chartInstance.data.datasets[2].data.push({ x: elapsedSec, y: bottomTemp });
        chartInstance.data.datasets[3].data.push({ x: elapsedSec, y: bottomSet });

        // Enforce ring buffer ceiling to match backend config::Limits::MAX_HISTORY_POINTS
        const maxPoints = VALIDATION_LIMITS.MAX_HISTORY_POINTS || 10800;
        if (ds0.length > maxPoints) {
            ds0.shift();
            chartInstance.data.datasets[1].data.shift();
            chartInstance.data.datasets[2].data.shift();
            chartInstance.data.datasets[3].data.shift();
        }
    }

    if (markers && Array.isArray(markers)) {
        cachedStepMarkers = markers;
    }

    // Render fast without layout animations
    chartInstance.update('none');
}

async function restoreHistory() {
    try {
        const data = await apiRequest('/api/history', 'GET');
        if (!data) return;

        if (data.stateEnum !== undefined) {
            store.stateEnum = Number(data.stateEnum);
            store.prevStateEnum = Number(data.stateEnum);
        }
        if (data.talSec !== undefined) {
            store.talSec = Number(data.talSec);
            setText('stat-tal', `${store.talSec}s`);
        }
        if (data.elapsedSec !== undefined) {
            store.elapsedSec = Number(data.elapsedSec);
            setText(
                'elapsed-time',
                `${t('hud.runtime_prefix', 'Runtime:')} ${formatTime(store.elapsedSec)} min`
            );
        }

        if (data.points && Array.isArray(data.points) && data.points.length > 0 && chartInstance) {
            console.log(
                `[History] Restoring ${data.points.length} telemetry points from controller...`
            );

            let maxTop = 0;
            let maxBot = 0;
            for (const p of data.points) {
                if (p[1] > maxTop) maxTop = p[1];
                if (p[2] > maxBot) maxBot = p[2];
            }
            store.topPeakTemp = maxTop;
            store.bottomPeakTemp = maxBot;
            setText('top-peak', Format.temp(store.topPeakTemp));
            setText('bottom-peak', Format.temp(store.bottomPeakTemp));

            chartInstance.data.datasets[0].data = data.points.map(p => ({ x: p[0], y: p[1] }));
            chartInstance.data.datasets[1].data = data.points.map(p => ({ x: p[0], y: p[3] }));
            chartInstance.data.datasets[2].data = data.points.map(p => ({ x: p[0], y: p[2] }));
            chartInstance.data.datasets[3].data = data.points.map(p => ({ x: p[0], y: p[4] }));

            if (data.markers && Array.isArray(data.markers)) {
                cachedStepMarkers = data.markers;
            }

            if (data.profileFile) {
                store.activeProfileFile = data.profileFile;
                store.activeProfileName = data.profileFile.replace(/\.json$/i, '');
                const cSel = $('chart-profile-select');
                if (cSel && cSel.querySelector(`option[value="${data.profileFile}"]`)) {
                    cSel.value = data.profileFile;
                }
            }

            const maxTime = data.points[data.points.length - 1][0] || 0;
            const currentMax = chartInstance.options.scales.x.suggestedMax || 600;
            if (maxTime >= currentMax - 10) {
                chartInstance.options.scales.x.suggestedMax = Math.ceil((maxTime + 60) / 120) * 120;
            }

            chartInstance.update('none');
        }
    } catch (e) {
        console.warn('[History] Failed to restore history from controller:', e);
    }
}

// ── 11. EXPORT MODULE (CSV & PNG) ───────────────────────────────────────
async function exportCSV() {
    if (!chartInstance) return;

    const ds0 = chartInstance.data.datasets[0].data; // Top Temp
    const ds1 = chartInstance.data.datasets[1].data; // Top Set
    const ds2 = chartInstance.data.datasets[2].data; // Bottom Temp
    const ds3 = chartInstance.data.datasets[3].data; // Bottom Set

    if (!ds0.length) {
        await Modal.alert(t('modal.no_data_export', 'No telemetry data recorded yet to export.'), {
            type: 'info',
            title: t('modal.no_data_title', 'No Data'),
        });
        return;
    }

    let csv = 'Time_s;Top_Temp_C;Top_Setpoint_C;Bottom_Temp_C;Bottom_Setpoint_C\r\n';
    for (let i = 0; i < ds0.length; i++) {
        const time = ds0[i].x;
        const topT = ds0[i] ? Format.temp(ds0[i].y) : '0.0';
        const topS = ds1[i] ? Format.temp(ds1[i].y) : '0.0';
        const botT = ds2[i] ? Format.temp(ds2[i].y) : '0.0';
        const botS = ds3[i] ? Format.temp(ds3[i].y) : '0.0';
        csv += `${time};${topT};${topS};${botT};${botS}\r\n`;
    }

    const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    const now = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
    a.href = url;
    a.download = `reflow_telemetry_${now}.csv`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}

function exportPNG() {
    if (!chartInstance) return;
    const canvas = $('liveChart');
    if (!canvas) return;

    // Render on offscreen canvas to guarantee solid background
    const isDark = store.currentTheme === 'dark';
    const offCanvas = document.createElement('canvas');
    offCanvas.width = canvas.width;
    offCanvas.height = canvas.height;
    const offCtx = offCanvas.getContext('2d');

    offCtx.fillStyle = isDark ? '#0f172a' : '#ffffff';
    offCtx.fillRect(0, 0, offCanvas.width, offCanvas.height);
    offCtx.drawImage(canvas, 0, 0);

    const url = offCanvas.toDataURL('image/png');
    const a = document.createElement('a');
    const now = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
    a.href = url;
    a.download = `reflow_chart_${now}.png`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
}
