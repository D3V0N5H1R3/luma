// Generate graphicalui_assets.hpp from vendored libraries and framework files.
//
// Embeds:
//   - lit-html IIFE (rendering engine)
//   - Pico CSS classless (base styles)
//   - uPlot IIFE + CSS (charting)
//   - Custom GUI override CSS
//   - lit-html based renderer
//   - Chart renderer (uPlot bridge)
//   - Subscription manager
//   - Lucide icon data + renderer
//
// Each asset is compressed with zlib (deflate) to reduce binary size.
// The C++ side decompresses on first use via miniz (mz_uncompress).
//
// Uses byte arrays instead of raw string literals to avoid the
// MSVC 16,380-char limit (Pico CSS alone is 71 KB).

import fs from 'fs';
import path from 'path';
import zlib from 'zlib';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, '..');

// ── Configuration ──────────────────────────────────────

const filesToEmbed = {
    lit_html_js:          'external/lit-html/lit-html.iife.js',
    pico_css:             'external/pico-css/pico.classless.min.css',
    uplot_js:             'external/uplot/uPlot.iife.min.js',
    uplot_css:            'external/uplot/uPlot.min.css',
    gui_overrides_css:    'external/gui-framework/gui-overrides.css',
    gui_renderer_js:      'external/gui-framework/gui-renderer.js',
    gui_charts_js:        'external/gui-framework/gui-charts.js',
    gui_subscriptions_js: 'external/gui-framework/gui-subscriptions.js',
    lucide_icons_1:       'external/lucide/lucide-icons-part1.json',
    lucide_icons_2:       'external/lucide/lucide-icons-part2.json',
};

// The WIDGET_RENDERERS table is split across renderers/*.js fragments (mirroring
// the C++ graphicalui_widgets_{basic,layout,advanced,interaction}.cpp split).
// They are concatenated into the single gui_renderer_js asset, replacing the
// marker inside the renderer IIFE so the module-private helpers stay private.
// The C++ dev-asset loader (graphicalui_serialization.cpp) mirrors this order.
const rendererFragments = [
    'external/gui-framework/renderers/basic.js',
    'external/gui-framework/renderers/layout.js',
    'external/gui-framework/renderers/advanced.js',
    'external/gui-framework/renderers/interaction.js',
];
const RENDERER_FRAGMENT_MARKER = '// __GUI_WIDGET_RENDERER_FRAGMENTS__';

const outputPath = path.join(rootDir, 'core/runtime/stdlib/io/graphicalui_assets.hpp');

// ── Read a required source file (trimmed) or exit ──────

function readRequired(relPath, kind = 'file') {
    const absPath = path.join(rootDir, relPath);
    if (!fs.existsSync(absPath)) {
        console.error(`ERROR: missing ${kind}: ${absPath}`);
        process.exit(1);
    }
    return fs.readFileSync(absPath, 'utf8').trim();
}

// ── Read all source files ──────────────────────────────

function readSources() {
    const contents = {};
    for (const [key, relPath] of Object.entries(filesToEmbed)) {
        contents[key] = readRequired(relPath);
        console.log(`  ${key}: ${contents[key].length} chars`);
    }
    return contents;
}

// ── Splice per-category renderer fragments into gui_renderer_js ──

function spliceRendererFragments(contents) {
    let fragmentsBlob = '';
    for (const relPath of rendererFragments) {
        fragmentsBlob += readRequired(relPath, 'renderer fragment') + '\n';
    }
    if (!contents.gui_renderer_js.includes(RENDERER_FRAGMENT_MARKER)) {
        console.error('ERROR: renderer fragment marker not found in gui-renderer.js');
        process.exit(1);
    }
    // Function replacer keeps the fragment source literal (it is full of `${...}`).
    contents.gui_renderer_js = contents.gui_renderer_js.replace(
        RENDERER_FRAGMENT_MARKER, () => fragmentsBlob);
    console.log(`  gui_renderer_js (with fragments): ${contents.gui_renderer_js.length} chars`);
}

// ── Build Lucide icon data JS ──────────────────────────
// Icon rendering via lit-html svg templates is in gui-renderer.js.

function buildLucideJs(contents) {
    return [
        '    const __lucide_p1 = ' + contents.lucide_icons_1 + ';',
        '    const __lucide_p2 = ' + contents.lucide_icons_2 + ';',
        '    const __lucide_icons = Object.assign({}, __lucide_p1, __lucide_p2);',
    ].join('\n');
}

// ── Helper: compress string → C++ byte array ──────────
// Returns the formatted declaration plus the raw/compressed sizes so the caller
// can accumulate totals without any shared mutable state.

function toCompressedByteArray(str, varName) {
    const raw = Buffer.from(str, 'utf8');
    const compressed = zlib.deflateSync(raw, { level: 9 });

    const hexLines = [];
    for (let i = 0; i < compressed.length; i += 16) {
        const slice = compressed.subarray(i, Math.min(i + 16, compressed.length));
        const hexes = Array.from(slice).map(b => '0x' + b.toString(16).padStart(2, '0'));
        hexLines.push('    ' + hexes.join(','));
    }
    const ratio = ((1 - compressed.length / raw.length) * 100).toFixed(1);
    console.log(`    ${varName}: ${raw.length} → ${compressed.length} bytes (${ratio}% reduction)`);
    const text = [
        `inline constexpr unsigned char ${varName}_compressed_data[] = {`,
        hexLines.join(',\n'),
        '};',
        '',
        `inline constexpr std::size_t ${varName}_compressed_size = ${compressed.length};`,
        `inline constexpr std::size_t ${varName}_uncompressed_size = ${raw.length};`,
    ].join('\n');
    return { text, rawBytes: raw.length, compressedBytes: compressed.length };
}

// ── Generate the header and write it to disk ───────────

function emitHeader(assets) {
    const output = [
        `// Auto-generated from external/ resources.`,
        `// Do not edit — regenerate with: node scripts/generate_gui_assets.mjs`,
        ``,
        `#pragma once`,
        ``,
        `#include <cstddef>`,
        `#include <stdexcept>`,
        `#include <string>`,
        ``,
        `#include "miniz.h"`,
        ``,
        `namespace luma::gui_assets {`,
        ``,
        `// Decompress a zlib-compressed asset into a std::string.`,
        `[[nodiscard]] inline std::string decompress(const unsigned char* data,`,
        `                                            std::size_t compressed_size,`,
        `                                            std::size_t uncompressed_size) {`,
        `    std::string out(uncompressed_size, '\\0');`,
        `    mz_ulong dest_len = static_cast<mz_ulong>(uncompressed_size);`,
        `    const int status = mz_uncompress(`,
        `        reinterpret_cast<unsigned char*>(out.data()), &dest_len,`,
        `        data, static_cast<mz_ulong>(compressed_size)`,
        `    );`,
        `    if (status != MZ_OK) {`,
        `        throw std::runtime_error("failed to decompress embedded GUI asset");`,
        `    }`,
        `    out.resize(dest_len);`,
        `    return out;`,
        `}`,
        ``,
    ];

    let totalRawBytes = 0;
    let totalCompressedBytes = 0;
    for (const [name, content] of Object.entries(assets)) {
        const { text, rawBytes, compressedBytes } = toCompressedByteArray(content, name);
        totalRawBytes += rawBytes;
        totalCompressedBytes += compressedBytes;
        output.push(`// ${name}`);
        output.push(text);
        output.push('');
        output.push(`[[nodiscard]] inline std::string ${name}_string() {`);
        output.push(`    return decompress(${name}_compressed_data, ${name}_compressed_size, ${name}_uncompressed_size);`);
        output.push('}');
        output.push('');
    }

    output.push('} // namespace luma::gui_assets');
    output.push('');

    const outputStr = output.join('\n');
    fs.writeFileSync(outputPath, outputStr);
    console.log(`\nGenerated graphicalui_assets.hpp: ${outputStr.length} chars`);
    console.log(`Total raw asset size: ${(totalRawBytes / 1024).toFixed(1)} KB`);
    console.log(`Total compressed asset size: ${(totalCompressedBytes / 1024).toFixed(1)} KB`);
    console.log(`Compression ratio: ${((1 - totalCompressedBytes / totalRawBytes) * 100).toFixed(1)}%`);
}

// ── Orchestrate ────────────────────────────────────────

function main() {
    const contents = readSources();
    spliceRendererFragments(contents);
    const lucideIconsJs = buildLucideJs(contents);

    const assets = {
        lit_html_js:          contents.lit_html_js,
        pico_css:             contents.pico_css,
        uplot_js:             contents.uplot_js,
        uplot_css:            contents.uplot_css,
        gui_overrides_css:    contents.gui_overrides_css,
        gui_renderer_js:      contents.gui_renderer_js,
        gui_charts_js:        contents.gui_charts_js,
        gui_subscriptions_js: contents.gui_subscriptions_js,
        lucide_icons_js:      lucideIconsJs,
    };

    emitHeader(assets);
}

main();
