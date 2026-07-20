/* GraphicalUi chart renderer — uPlot based.
 *
 * Replaces the hand-built SVG chart rendering with uPlot for
 * line, area, bar, and scatter charts, and canvas-based rendering
 * for pie and donut charts.
 *
 * SPDX-License-Identifier: MIT
 */
(function() {
    "use strict";

    const COLORS = [
        "#4361ee", "#3a56d4", "#10b981", "#f59e0b",
        "#ef4444", "#8b5cf6", "#06b6d4", "#ec4899",
    ];

    const UPLOT_TYPES = new Set([
        "line_chart", "area_chart",
        "vertical_bar_chart", "horizontal_bar_chart",
    ]);

    // Named chart dimensions and ratios.
    const CHART_HEIGHT = 250;
    const DEFAULT_UPLOT_WIDTH = 400;
    const DEFAULT_PIE_WIDTH = 380;
    const PIE_MIN_HEIGHT = 200;
    const PIE_MAX_HEIGHT = 300;
    const PIE_HEIGHT_RATIO = 0.65;
    const DONUT_INNER_RATIO = 0.6;
    const PIE_LABEL_RADIUS_RATIO = 0.65;
    const MIN_SLICE_ANGLE_FOR_LABEL = 0.2;

    // Named series styling constants.
    const LINE_STROKE_WIDTH = 2;
    const LINE_POINT_SIZE = 6;
    const SCATTER_POINT_SIZE = 8;
    const BAR_WIDTH_FACTOR = 0.6;
    const BAR_MAX_WIDTH = 100;
    const BAR_GAP = 2;
    const AREA_FILL_ALPHA = 0.15;
    const AXIS_GAP = 5;

    // Named pie label and legend layout constants.
    const SLICE_LABEL_COLOR = "#ffffff";
    const SLICE_LABEL_FONT = "10px sans-serif";
    const CENTER_LABEL_FONT = "bold 16px sans-serif";
    const LEGEND_FONT = "11px sans-serif";
    const LEGEND_TOP = 20;
    const LEGEND_ROW_HEIGHT = 18;
    const LEGEND_SWATCH_SIZE = 10;
    const LEGEND_SWATCH_RADIUS = 2;
    const LEGEND_TEXT_OFFSET = 14;

    // Derive translucent fills from the palette instead of
    // hardcoding a duplicate colour, so the area fill tracks COLORS[0].
    function withAlpha(hex, alpha) {
        const r = parseInt(hex.slice(1, 3), 16);
        const g = parseInt(hex.slice(3, 5), 16);
        const b = parseInt(hex.slice(5, 7), 16);
        return "rgba(" + r + "," + g + "," + b + "," + alpha + ")";
    }

    // ── uPlot-based charts ─────────────────────────────
    // Note: only scatter_plot populates opts.xLabel / opts.yLabel.
    // Bar/line/area charts receive an empty opts object because the
    // Luma API does not expose axis titles for them.

    function buildValueSeries(type, opts) {
        const label = opts.yLabel || "Value";

        if (
            type === "vertical_bar_chart" ||
            type === "horizontal_bar_chart"
        ) {
            return {
                label: label,
                fill: COLORS[0],
                stroke: COLORS[0],
                width: 0,
                paths: uPlot.paths.bars({
                    size: [BAR_WIDTH_FACTOR, BAR_MAX_WIDTH], gap: BAR_GAP,
                }),
            };
        }

        if (type === "scatter_plot") {
            return {
                label: label,
                stroke: COLORS[0],
                width: LINE_STROKE_WIDTH,
                paths: () => null,
                points: {
                    show: true, size: SCATTER_POINT_SIZE, fill: COLORS[0],
                },
            };
        }

        return {
            label: label,
            stroke: COLORS[0],
            width: LINE_STROKE_WIDTH,
            fill: type === "area_chart"
                ? withAlpha(COLORS[0], AREA_FILL_ALPHA)
                : undefined,
            points: {
                show: true, size: LINE_POINT_SIZE, fill: COLORS[0],
            },
        };
    }

    // Subtle gridline colour that reads on both light and dark backgrounds.
    const GRID_COLOR = "rgba(128, 128, 128, 0.2)";

    function buildAxes(container, type, opts, textColor) {
        const axisStyle = {
            stroke: textColor,
            grid: { stroke: GRID_COLOR },
            ticks: { stroke: GRID_COLOR },
        };
        if (type === "scatter_plot") {
            return [
                { label: opts.xLabel || "", gap: AXIS_GAP, ...axisStyle },
                { label: opts.yLabel || "", gap: AXIS_GAP, ...axisStyle },
            ];
        }
        return [
            {
                label: opts.xLabel || "",
                // Read labels from the live per-render metadata rather than
                // closing over the array captured at instance creation: the
                // uPlot reuse path only calls setData, so a stale closure would
                // leave the x-axis showing the original categories after the
                // data (and __chartMeta.labels) change.
                values: (u, vals) => {
                    const lbls = (container.__chartMeta &&
                        container.__chartMeta.labels) || [];
                    return vals.map((v) => {
                        return v >= 0 && v < lbls.length
                            ? lbls[v]
                            : "";
                    });
                },
                gap: AXIS_GAP,
                ...axisStyle,
            },
            { label: opts.yLabel || "", gap: AXIS_GAP, ...axisStyle },
        ];
    }

    // ── Hover tooltip (shared by uPlot and pie/donut charts) ──
    // A single reused element positioned in viewport coordinates so it floats
    // above the chart near the pointer.
    function getChartTooltip() {
        let tip = document.getElementById("__gui_chart_tooltip");
        if (!tip) {
            tip = document.createElement("div");
            tip.id = "__gui_chart_tooltip";
            tip.className = "gui-chart-tooltip";
            tip.style.display = "none";
            document.body.appendChild(tip);
        }
        return tip;
    }

    function hideChartTooltip() {
        const tip = document.getElementById("__gui_chart_tooltip");
        if (tip) {
            tip.style.display = "none";
        }
    }

    function formatNumber(v) {
        if (typeof v !== "number" || !isFinite(v)) {
            return String(v);
        }
        return Number.isInteger(v) ? String(v) : v.toFixed(2);
    }

    function showChartTooltip(clientX, clientY, title, value, swatch) {
        const tip = getChartTooltip();
        // Build with textContent (not innerHTML) so author-supplied labels
        // containing <, >, or & render literally and cannot inject markup.
        tip.textContent = "";
        if (swatch) {
            const sw = document.createElement("span");
            sw.className = "gui-chart-tooltip-swatch";
            sw.style.background = swatch;
            tip.appendChild(sw);
        }
        const labelEl = document.createElement("span");
        labelEl.className = "gui-chart-tooltip-label";
        labelEl.textContent = title;
        tip.appendChild(labelEl);
        const valueEl = document.createElement("span");
        valueEl.className = "gui-chart-tooltip-value";
        valueEl.textContent = value;
        tip.appendChild(valueEl);
        tip.style.display = "block";
        // Offset from the pointer; clamp to the viewport so it never clips.
        const pad = 12;
        let left = clientX + pad;
        let top = clientY + pad;
        const rect = tip.getBoundingClientRect();
        if (left + rect.width > window.innerWidth) {
            left = clientX - rect.width - pad;
        }
        if (top + rect.height > window.innerHeight) {
            top = clientY - rect.height - pad;
        }
        tip.style.left = left + "px";
        tip.style.top = top + "px";
    }

    // uPlot plugin: report the hovered point's label and value in the shared
    // tooltip. Reads live chart metadata from the container so labels stay
    // correct across data updates.
    function tooltipPlugin(container) {
        return {
            hooks: {
                setCursor: (u) => {
                    const meta = container.__chartMeta || {};
                    const idx = u.cursor.idx;
                    if (idx == null) {
                        hideChartTooltip();
                        return;
                    }
                    const yVal = u.data[1][idx];
                    if (yVal == null) {
                        hideChartTooltip();
                        return;
                    }
                    let title;
                    if (meta.type === "scatter_plot") {
                        title = (meta.xLabel || "x") + " " +
                            formatNumber(u.data[0][idx]);
                    } else {
                        const lbls = meta.labels || [];
                        title = lbls[idx] != null
                            ? String(lbls[idx])
                            : String(u.data[0][idx]);
                    }
                    const valueText = (meta.yLabel ? meta.yLabel + ": " : "") +
                        formatNumber(yVal);
                    const oRect = u.over.getBoundingClientRect();
                    showChartTooltip(
                        oRect.left + u.cursor.left,
                        oRect.top + u.cursor.top,
                        title, valueText, COLORS[0],
                    );
                },
            },
        };
    }

    function renderUPlotChart(container, type, labels, values, opts) {
        const xData = [];
        for (let i = 0; i < labels.length; i++) {
            xData.push(i);
        }
        const data = (type === "scatter_plot")
            ? [opts.xValues || [], opts.yValues || []]
            : [xData, values];

        // Live metadata for the tooltip plugin, refreshed every render so
        // labels/axis titles stay correct when the data updates.
        container.__chartMeta = {
            type: type,
            labels: labels,
            xLabel: opts.xLabel || "",
            yLabel: opts.yLabel || "",
        };

        // Reuse existing uPlot instance — update data only.
        if (container.__uplot) {
            container.__uplot.setData(data);
            return;
        }

        const width = container.clientWidth || DEFAULT_UPLOT_WIDTH;
        const height = CHART_HEIGHT;

        // Axis text follows the theme foreground so labels stay readable on a
        // dark background instead of defaulting to near-black.
        const textColor = getComputedStyle(container).color || "#333";

        const uOpts = {
            width: width,
            height: height,
            series: [{ label: opts.xLabel || "X" }, buildValueSeries(type, opts)],
            axes: buildAxes(container, type, opts, textColor),
            cursor: { show: true },
            // Native legend is opt-in (it adds a values table below the plot);
            // the floating tooltip covers the common hover-to-read case.
            legend: { show: opts.legend === true },
            plugins: opts.tooltip === false ? [] : [tooltipPlugin(container)],
        };

        // Clear container and render.
        container.innerHTML = "";
        try {
            container.__uplot = new uPlot(uOpts, data, container);
            // Hide the tooltip when the pointer leaves the plot area.
            container.addEventListener("mouseleave", hideChartTooltip);
        } catch (e) {
            container.textContent =
                "[chart error: " + e.message + "]";
        }
    }

    // ── Pie / donut chart (canvas, responsive) ──────────

    function drawSlices(
        ctx, centerX, centerY, outerRadius, innerRadius,
        labels, values, total, isDonut
    ) {
        let currentAngle = -Math.PI / 2;
        const slices = [];

        for (let i = 0; i < labels.length; i++) {
            const sliceValue = values[i] || 0;
            const sliceAngle = (sliceValue / total) * 2 * Math.PI;
            const color = COLORS[i % COLORS.length];
            ctx.fillStyle = color;
            ctx.beginPath();
            ctx.moveTo(
                centerX + innerRadius * Math.cos(currentAngle),
                centerY + innerRadius * Math.sin(currentAngle)
            );
            ctx.arc(
                centerX, centerY, outerRadius,
                currentAngle, currentAngle + sliceAngle
            );
            if (isDonut) {
                ctx.arc(
                    centerX, centerY, innerRadius,
                    currentAngle + sliceAngle, currentAngle, true
                );
            } else {
                ctx.lineTo(centerX, centerY);
            }
            ctx.closePath();
            ctx.fill();

            slices.push({
                start: currentAngle,
                end: currentAngle + sliceAngle,
                label: labels[i],
                value: sliceValue,
                pct: Math.round(sliceValue / total * 100),
                color: color,
            });

            if (sliceAngle > MIN_SLICE_ANGLE_FOR_LABEL) {
                const labelRadius = isDonut
                    ? (outerRadius + innerRadius) / 2
                    : outerRadius * PIE_LABEL_RADIUS_RATIO;
                const labelX = centerX + labelRadius * Math.cos(
                    currentAngle + sliceAngle / 2
                );
                const labelY = centerY + labelRadius * Math.sin(
                    currentAngle + sliceAngle / 2
                );
                ctx.fillStyle = SLICE_LABEL_COLOR;
                ctx.font = SLICE_LABEL_FONT;
                ctx.textAlign = "center";
                ctx.textBaseline = "middle";
                ctx.fillText(
                    Math.round(sliceValue / total * 100) + "%",
                    labelX, labelY
                );
            }
            currentAngle += sliceAngle;
        }

        return slices;
    }

    // Attach pointer hit-testing to a pie/donut canvas so hovering a slice
    // surfaces its label, value, and share in the shared tooltip.
    function attachPieHover(
        canvas, slices, centerX, centerY, outerRadius, innerRadius, isDonut
    ) {
        canvas.addEventListener("mousemove", (e) => {
            const rect = canvas.getBoundingClientRect();
            const dx = (e.clientX - rect.left) - centerX;
            const dy = (e.clientY - rect.top) - centerY;
            const dist = Math.sqrt(dx * dx + dy * dy);
            if (dist > outerRadius || (isDonut && dist < innerRadius)) {
                hideChartTooltip();
                return;
            }
            // Canvas angles increase clockwise from the +x axis; slices begin at
            // -PI/2. Normalise the pointer angle into the slice sweep range.
            let angle = Math.atan2(dy, dx);
            while (angle < -Math.PI / 2) {
                angle += 2 * Math.PI;
            }
            const hit = slices.find((s) => angle >= s.start && angle < s.end);
            if (!hit) {
                hideChartTooltip();
                return;
            }
            showChartTooltip(
                e.clientX, e.clientY,
                String(hit.label),
                formatNumber(hit.value) + " (" + hit.pct + "%)",
                hit.color,
            );
        });
        canvas.addEventListener("mouseleave", hideChartTooltip);
    }

    function drawCenterLabel(ctx, centerX, centerY, label, textColor) {
        ctx.fillStyle = textColor;
        ctx.font = CENTER_LABEL_FONT;
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(label, centerX, centerY);
    }

    function drawLegend(ctx, labels, legendX, textColor) {
        ctx.font = LEGEND_FONT;
        ctx.textAlign = "left";
        ctx.textBaseline = "top";

        for (let i = 0; i < labels.length; i++) {
            const legendY = LEGEND_TOP + i * LEGEND_ROW_HEIGHT;
            ctx.fillStyle = COLORS[i % COLORS.length];
            ctx.beginPath();
            ctx.roundRect(
                legendX, legendY,
                LEGEND_SWATCH_SIZE, LEGEND_SWATCH_SIZE, LEGEND_SWATCH_RADIUS
            );
            ctx.fill();
            ctx.fillStyle = textColor;
            ctx.fillText(labels[i], legendX + LEGEND_TEXT_OFFSET, legendY);
        }
    }

    function renderPieChart(
        container, labels, values, isDonut, centerLabel, showTooltip
    ) {
        const canvas = document.createElement("canvas");
        const width = container.clientWidth || DEFAULT_PIE_WIDTH;
        const height = Math.max(
            PIE_MIN_HEIGHT,
            Math.min(width * PIE_HEIGHT_RATIO, PIE_MAX_HEIGHT)
        );
        canvas.width = width * 2;
        canvas.height = height * 2;
        canvas.style.width = width + "px";
        canvas.style.height = height + "px";
        const ctx = canvas.getContext("2d");
        // Guard against a missing 2D context.
        if (!ctx) {
            container.textContent =
                "[chart error: 2D canvas not supported]";
            return;
        }
        ctx.scale(2, 2);

        const centerX = Math.min(width * 0.35, 140);
        const centerY = height / 2;
        const outerRadius = Math.min(centerX - 20, height / 2 - 15);
        const innerRadius = isDonut
            ? outerRadius * DONUT_INNER_RATIO
            : 0;
        const total = values.reduce(
            (acc, value) => acc + (value || 0), 0
        ) || 1;
        const textColor =
            getComputedStyle(container).color || "#333";

        const slices = drawSlices(
            ctx, centerX, centerY, outerRadius, innerRadius,
            labels, values, total, isDonut
        );

        if (isDonut && centerLabel) {
            drawCenterLabel(ctx, centerX, centerY, centerLabel, textColor);
        }

        const legendX = Math.min(
            centerX + outerRadius + 20, width - 100
        );
        drawLegend(ctx, labels, legendX, textColor);

        // Hover tooltips are on by default; honour an explicit tooltip:false.
        if (showTooltip !== false) {
            attachPieHover(
                canvas, slices, centerX, centerY, outerRadius, innerRadius, isDonut
            );
        }

        container.innerHTML = "";
        container.appendChild(canvas);
    }

    // ── Public chart render dispatcher ──────────────────

    // Human-readable chart-type names for the generated text alternative.
    const CHART_TYPE_LABELS = {
        vertical_bar_chart: "Bar chart",
        horizontal_bar_chart: "Bar chart",
        line_chart: "Line chart",
        area_chart: "Area chart",
        pie_chart: "Pie chart",
        donut_chart: "Donut chart",
        scatter_plot: "Scatter plot",
    };

    // Build a concise text summary of the chart's data so the chart is not
    // conveyed by colour/shape alone (WCAG 1.1.1). Used as the container's
    // aria-label and as a visually hidden text alternative in the DOM.
    function describeChart(w) {
        const type = w.type;

        if (type === "scatter_plot") {
            const xs = w.x_values || [];
            const count = xs.length;
            const xl = w.x_label || "x";
            const yl = w.y_label || "y";
            return count + " point" + (count === 1 ? "" : "s") +
                " plotting " + yl + " against " + xl + ".";
        }

        const labels = w.labels || [];
        const values = w.values || [];
        const pairs = labels.map((label, i) => {
            return label + ": " + (values[i] != null ? values[i] : 0);
        });
        return pairs.length ? pairs.join(", ") + "." : "No data.";
    }

    function applyChartA11y(container, w) {
        const typeLabel = CHART_TYPE_LABELS[w.type] || "Chart";
        const summary = describeChart(w);
        const label = typeLabel + ". " + summary;
        container.setAttribute("role", "img");
        container.setAttribute("aria-label", label);

        // Reuse an existing summary node rather than appending a new one each
        // render: the uPlot reuse path does not clear the container, so blindly
        // appending would stack duplicate hidden summaries in the accessibility
        // tree (a DOM leak and a screen-reader nuisance).
        let sr = container.querySelector(":scope > .gui-visually-hidden");
        if (!sr) {
            sr = document.createElement("span");
            sr.className = "gui-visually-hidden";
            container.appendChild(sr);
        }
        sr.textContent = label;
    }

    /**
     * Renders a chart widget into the given container element.
     * @param {HTMLElement} container - The DOM element to render
     *   the chart into.
     * @param {object} w - The chart widget descriptor containing
     *   type, labels, values, and chart-specific options.
     */
    window.__gui_render_chart = function(container, w) {
        const type = w.type;
        const labels = w.labels || [];
        const values = w.values || [];
        const baseOpts = {
            xLabel: w.x_label || "",
            yLabel: w.y_label || "",
            legend: w.legend === true,
            tooltip: w.tooltip !== false,
        };

        if (UPLOT_TYPES.has(type)) {
            renderUPlotChart(container, type, labels, values, baseOpts);
        } else if (type === "scatter_plot") {
            renderUPlotChart(container, "scatter_plot", [], [], {
                xValues: w.x_values || [],
                yValues: w.y_values || [],
                xLabel: w.x_label || "",
                yLabel: w.y_label || "",
                legend: w.legend === true,
                tooltip: w.tooltip !== false,
            });
        } else if (type === "pie_chart") {
            renderPieChart(container, labels, values, false, undefined,
                w.tooltip !== false);
        } else if (type === "donut_chart") {
            renderPieChart(container, labels, values, true, w.center_label,
                w.tooltip !== false);
        } else {
            container.textContent =
                "[unknown chart type: " + type + "]";
            return;
        }

        applyChartA11y(container, w);
    };
})();
