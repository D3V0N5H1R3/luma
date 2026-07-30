/* Unit tests for gui-charts.js module-private helpers.
 *
 * Covers the pure logic that shapes chart series, formats numbers, and builds
 * the text alternative (WCAG 1.1.1) used as the chart's aria-label — the parts
 * that are easy to break silently and carry accessibility weight.
 *
 * SPDX-License-Identifier: MIT
 */

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { loadFramework, plain } from "./gui-test-harness.mjs";

// buildValueSeries calls uPlot.paths.bars(...) as a free global. Record the
// config it is handed and hand back a sentinel so the wiring can be asserted.
let barsConfig;
const uPlot = {
    paths: {
        bars: (cfg) => {
            barsConfig = cfg;
            return "BARS_PATH";
        },
    },
};

const { internals: C } = loadFramework("gui-charts.js", {
    capture: [
        "withAlpha",
        "buildValueSeries",
        "buildMultiSeries",
        "formatNumber",
        "describeChart",
        "CHART_TYPE_LABELS",
        "COLORS",
    ],
    globals: { uPlot },
});

describe("COLORS palette", () => {
    it("exposes the eight-colour palette with the primary first", () => {
        assert.deepEqual(plain(C.COLORS), [
            "#4361ee", "#3a56d4", "#10b981", "#f59e0b",
            "#ef4444", "#8b5cf6", "#06b6d4", "#ec4899",
        ]);
    });
});

describe("withAlpha", () => {
    it("converts a hex colour to rgba with the given alpha", () => {
        assert.equal(C.withAlpha("#4361ee", 0.15), "rgba(67,97,238,0.15)");
    });

    it("handles pure black and white", () => {
        assert.equal(C.withAlpha("#000000", 1), "rgba(0,0,0,1)");
        assert.equal(C.withAlpha("#ffffff", 0.5), "rgba(255,255,255,0.5)");
    });
});

describe("buildValueSeries", () => {
    it("builds a bar series wired to uPlot.paths.bars with the sizing config", () => {
        const series = C.buildValueSeries("vertical_bar_chart", {});
        assert.equal(series.paths, "BARS_PATH");
        assert.deepEqual(plain(barsConfig), { size: [0.6, 100], gap: 2 });
        assert.deepEqual(plain({
            label: series.label,
            fill: series.fill,
            stroke: series.stroke,
            width: series.width,
        }), {
            label: "Value",
            fill: "#4361ee",
            stroke: "#4361ee",
            width: 0,
        });
    });

    it("treats horizontal_bar_chart the same as vertical_bar_chart", () => {
        const series = C.buildValueSeries("horizontal_bar_chart", {});
        assert.equal(series.paths, "BARS_PATH");
        assert.equal(series.width, 0);
        assert.equal(series.fill, "#4361ee");
    });

    it("uses opts.yLabel as the series label when provided", () => {
        const series = C.buildValueSeries("vertical_bar_chart", { yLabel: "Sales" });
        assert.equal(series.label, "Sales");
    });

    it("builds a scatter series with points and a null-path", () => {
        const series = C.buildValueSeries("scatter_plot", {});
        assert.equal(typeof series.paths, "function");
        assert.equal(series.paths(), null);
        assert.deepEqual(plain({
            label: series.label,
            stroke: series.stroke,
            width: series.width,
            points: series.points,
        }), {
            label: "Value",
            stroke: "#4361ee",
            width: 2,
            points: { show: true, size: 8, fill: "#4361ee" },
        });
    });

    it("builds a line series with no fill", () => {
        const series = C.buildValueSeries("line_chart", {});
        assert.equal(series.fill, undefined);
        assert.deepEqual(plain(series.points), { show: true, size: 6, fill: "#4361ee" });
        assert.equal(series.width, 2);
    });

    it("builds an area series with a translucent fill derived from COLORS[0]", () => {
        const series = C.buildValueSeries("area_chart", {});
        assert.equal(series.fill, "rgba(67,97,238,0.15)");
        assert.deepEqual(plain(series.points), { show: true, size: 6, fill: "#4361ee" });
    });
});

describe("buildMultiSeries", () => {
    it("builds a bar series with an alpha-blended fill and the given colour", () => {
        const series = C.buildMultiSeries("vertical_bar_chart", "Sales", "#4361ee");
        assert.equal(series.label, "Sales");
        assert.equal(series.stroke, "#4361ee");
        assert.equal(series.fill, "rgba(67,97,238,0.6)");
        assert.equal(series.width, 0);
    });

    it("builds a line series in the given colour", () => {
        const series = C.buildMultiSeries("line_chart", "Costs", "#ef4444");
        assert.equal(series.label, "Costs");
        assert.equal(series.stroke, "#ef4444");
        assert.equal(series.width, 2);
        assert.deepEqual(plain(series.points), { show: true, size: 6, fill: "#ef4444" });
    });
});

describe("formatNumber", () => {
    it("renders integers without decimals", () => {
        assert.equal(C.formatNumber(5), "5");
        assert.equal(C.formatNumber(-3), "-3");
        assert.equal(C.formatNumber(0), "0");
    });

    it("renders non-integers with two decimals", () => {
        assert.equal(C.formatNumber(5.5), "5.50");
        assert.equal(C.formatNumber(5.234), "5.23");
    });

    it("stringifies non-finite and non-number inputs", () => {
        assert.equal(C.formatNumber(NaN), "NaN");
        assert.equal(C.formatNumber(Infinity), "Infinity");
        assert.equal(C.formatNumber(-Infinity), "-Infinity");
        assert.equal(C.formatNumber("abc"), "abc");
        assert.equal(C.formatNumber(null), "null");
    });
});

describe("CHART_TYPE_LABELS", () => {
    it("maps every chart type to a human-readable label", () => {
        assert.deepEqual(plain(C.CHART_TYPE_LABELS), {
            vertical_bar_chart: "Bar chart",
            horizontal_bar_chart: "Bar chart",
            line_chart: "Line chart",
            area_chart: "Area chart",
            pie_chart: "Pie chart",
            donut_chart: "Donut chart",
            scatter_plot: "Scatter plot",
        });
    });
});

describe("describeChart", () => {
    it("describes a scatter plot with pluralised point count and axis labels", () => {
        assert.equal(
            C.describeChart({
                type: "scatter_plot",
                x_values: [1, 2, 3],
                x_label: "Time",
                y_label: "Value",
            }),
            "3 points plotting Value against Time.",
        );
    });

    it("uses the singular 'point' for a single scatter value and default axes", () => {
        assert.equal(
            C.describeChart({ type: "scatter_plot", x_values: [1] }),
            "1 point plotting y against x.",
        );
    });

    it("reports zero points for an empty scatter plot", () => {
        assert.equal(
            C.describeChart({ type: "scatter_plot" }),
            "0 points plotting y against x.",
        );
    });

    it("pairs labels with values for categorical charts", () => {
        assert.equal(
            C.describeChart({
                type: "vertical_bar_chart",
                labels: ["A", "B"],
                values: [10, 20],
            }),
            "A: 10, B: 20.",
        );
    });

    it("substitutes 0 for missing or null values", () => {
        assert.equal(
            C.describeChart({
                type: "line_chart",
                labels: ["A", "B", "C"],
                values: [10, null],
            }),
            "A: 10, B: 0, C: 0.",
        );
    });

    it("returns 'No data.' when there are no labels", () => {
        assert.equal(C.describeChart({ type: "pie_chart" }), "No data.");
    });

    it("describes a multi-series chart with each series' value per category", () => {
        assert.equal(
            C.describeChart({
                type: "line_chart",
                labels: ["Q1", "Q2"],
                series_names: ["Sales", "Costs"],
                series_values: [[10, 20], [6, 9]],
            }),
            "Q1 (Sales 10, Costs 6); Q2 (Sales 20, Costs 9).",
        );
    });
});
