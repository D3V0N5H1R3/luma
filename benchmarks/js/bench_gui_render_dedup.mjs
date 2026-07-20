// Headless render-loop benchmark for GraphicalUi render de-duplication (P01).
//
// Run:  node benchmarks/js/bench_gui_render_dedup.mjs
//
// WHAT THIS MEASURES
// ------------------
// The GraphicalUi render pipeline funnels every frame through the C++ host,
// which serializes the widget tree to JSON and evals `__gui_render({json})` in
// the webview.  Change-detection decides whether a frame actually needs to be
// re-rendered.  This benchmark isolates *only* the per-frame change-detection
// overhead — not lit-html's own DOM diffing, which is unchanged.
//
//   OLD (change-detection in the webview): the host always evals, so on every
//   flushed frame the webview JSON.parses the object literal into `widgetTree`,
//   then `__gui_flush_render` calls JSON.stringify(widgetTree) and compares the
//   result against the previous frame's string.  A no-op frame therefore costs
//   a full parse + a full re-serialize of the whole tree.
//
//   NEW (change-detection in the C++ host): the host already holds the
//   serialized JSON, so it compares that string against the previous frame and,
//   on a match, skips the eval entirely — no IPC, no parse, no stringify.
//
// The common hot case is a *no-op frame*: a timer/animation subscription fires
// (up to once per animation frame), `update` returns a model that renders to
// the same view, and nothing on screen changes.  That is the scenario measured
// below, across small and large trees.
//
// NOTE ON SCOPE: the *full* eval-skip (measured here) fires when the serialized
// tree is byte-identical — i.e. display-only views (labels, charts, progress)
// whose model change leaves the tree unchanged.  Interactive widgets embed a
// fresh per-render callback id, so their trees differ every frame and are still
// re-emitted; but even for those, moving change-detection to the host removes
// the redundant per-frame JSON.stringify the webview used to run on every
// flushed frame, which is the universal part of the win.

"use strict";

// ── Representative widget-tree generator ────────────────────────────────
// Mirrors the shape the C++ serializer emits: nested layout nodes carrying a
// type, a style dictionary, children, and per-widget ids/text.

function makeLeaf(i) {
    return {
        type: i % 3 === 0 ? "button" : "label",
        _id: `_gui_0_${i}`,
        text: `Item ${i} — the quick brown fox`,
        style: {
            font_size: "14px",
            color: "var(--gui-fg)",
            padding: "8px 12px",
            border_radius: "4px",
            font_weight: i % 2 === 0 ? "600" : "400",
        },
        _aria_label: `item ${i}`,
    };
}

function makeRow(rowIndex, cols) {
    const children = [];
    for (let c = 0; c < cols; c++) {
        children.push(makeLeaf(rowIndex * cols + c));
    }
    return {
        type: "row",
        _id: `_gui_0_row_${rowIndex}`,
        style: { gap: "12px", align_items: "center", justify_content: "space-between" },
        children,
    };
}

function makeTree(rows, cols) {
    const children = [];
    for (let r = 0; r < rows; r++) {
        children.push(makeRow(r, cols));
    }
    return {
        type: "column",
        _id: "_gui_0_root",
        style: { gap: "16px", padding: "24px" },
        children,
    };
}

// ── Pipeline models ─────────────────────────────────────────────────────
// The host always produces the serialized JSON (that cost is identical in both
// pipelines and is done once per frame regardless), so the benchmark starts
// from `hostJson` and measures only what each pipeline does *after* that.

// OLD: eval materializes the object, then the flush re-serializes it and
// compares strings.  Returns whether a re-render is needed.
function oldNoOpFrame(hostJson, prevJson) {
    const widgetTree = JSON.parse(hostJson); // webview eval of `__gui_render({...})`
    const treeJson = JSON.stringify(widgetTree); // __gui_flush_render change-detection
    return treeJson !== prevJson; // false on a no-op frame
}

// NEW: the host compares the string it already holds; no eval/parse/stringify.
function newNoOpFrame(hostJson, prevJson) {
    return hostJson !== prevJson; // false on a no-op frame
}

// ── Timing helper ───────────────────────────────────────────────────────

function timeIt(frames, fn) {
    // Warm up (JIT + caches) — not timed.
    let sink = 0;
    const warmup = Math.max(1, Math.floor(frames / 10));
    for (let i = 0; i < warmup; i++) {
        sink += fn() ? 1 : 0;
    }
    const start = process.hrtime.bigint();
    for (let i = 0; i < frames; i++) {
        sink += fn() ? 1 : 0;
    }
    const end = process.hrtime.bigint();
    // Defeat dead-code elimination.
    if (sink === Number.MAX_SAFE_INTEGER) {
        console.log("unreachable");
    }
    const totalNs = Number(end - start);
    return { frames, totalMs: totalNs / 1e6, perFrameUs: totalNs / frames / 1e3 };
}

function fmt(n, width = 10) {
    return n.toFixed(4).padStart(width);
}

function runScenario(name, tree, frames) {
    const hostJson = JSON.stringify(tree); // what the C++ host produces each frame
    // Distinct allocation with identical content: mirrors C++ comparing the new
    // frame's std::string against the stored prev_tree_json (a full size-check +
    // memcmp of two separate buffers), NOT a pointer-equality shortcut.
    const prevJson = Buffer.from(hostJson, "utf8").toString("utf8");
    const bytes = Buffer.byteLength(hostJson, "utf8");

    const oldRes = timeIt(frames, () => oldNoOpFrame(hostJson, prevJson));
    const newRes = timeIt(frames, () => newNoOpFrame(hostJson, prevJson));

    const speedup = oldRes.perFrameUs / newRes.perFrameUs;
    const savedMs = oldRes.totalMs - newRes.totalMs;

    console.log(`\n${name}  (${bytes} JSON bytes, ${frames} no-op frames)`);
    console.log(`  old (parse + stringify + compare):  ${fmt(oldRes.perFrameUs)} us/frame   ${fmt(oldRes.totalMs)} ms total`);
    console.log(`  new (string compare only):          ${fmt(newRes.perFrameUs)} us/frame   ${fmt(newRes.totalMs)} ms total`);
    console.log(`  -> ${speedup.toFixed(1)}x faster change-detection, ${savedMs.toFixed(2)} ms saved over the loop`);
}

function main() {
    console.log("GraphicalUi render de-duplication — no-op frame change-detection\n");
    console.log("Each 'frame' is one flushed render where the view is unchanged");
    console.log("(the common timer/animation-subscription case).");

    // Small tree (~counter-sized), medium and large trees (dashboard-sized).
    // Iteration counts are tuned so each OLD loop runs ~1 s.
    runScenario("small tree  (5x2 = 10 widgets)", makeTree(5, 2), 50000);
    runScenario("medium tree (20x5 = 100 widgets)", makeTree(20, 5), 8000);
    runScenario("large tree  (100x8 = 800 widgets)", makeTree(100, 8), 1000);

    console.log("\nThe old pipeline paid this parse+re-serialize on every flushed frame,");
    console.log("plus the IPC eval; the new pipeline skips the eval entirely on no-op frames.");
}

main();
