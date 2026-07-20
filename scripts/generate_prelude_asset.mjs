// Generate gui_prelude_generated.hpp from the Solaris prelude source.
//
// Embeds core/analysis/prelude/gui_prelude.luma — the built-in Solaris GUI
// surface — into the analysis library as an uncompressed byte array, mirroring
// the byte-array embedding technique used by generate_gui_assets.mjs. Unlike
// the GUI assets, the prelude is left uncompressed so the analysis layer keeps
// no dependency on miniz or the runtime stdlib: the source is small (~41 KB)
// and byte arrays sidestep the MSVC string-literal length limit.
//
// The prelude is developer-authored Luma source; keeping it in a real .luma
// file lets the lexer, parser, type checker, and editor tooling see it as the
// language it is, rather than as text pasted into a C++ translation unit.

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, "..");

const sourcePath = path.join(rootDir, "core/analysis/prelude/gui_prelude.luma");
const outputPath = path.join(rootDir, "core/analysis/prelude/gui_prelude_generated.hpp");

// Read the prelude as raw bytes so the embedded array is byte-identical to the
// on-disk source (no trimming or newline translation).
const bytes = fs.readFileSync(sourcePath);

const hexLines = [];
for (let i = 0; i < bytes.length; i += 16) {
    const slice = bytes.subarray(i, Math.min(i + 16, bytes.length));
    const hexes = Array.from(slice).map((b) => "0x" + b.toString(16).padStart(2, "0"));
    hexLines.push("    " + hexes.join(",") + ",");
}

const output = [
    "// Auto-generated from core/analysis/prelude/gui_prelude.luma.",
    "// Do not edit — regenerate with: node scripts/generate_prelude_asset.mjs",
    "",
    "#ifndef LUMA_ANALYSIS_PRELUDE_GUI_PRELUDE_GENERATED_HPP",
    "#define LUMA_ANALYSIS_PRELUDE_GUI_PRELUDE_GENERATED_HPP",
    "",
    "#include <string_view>",
    "",
    "namespace luma::prelude {",
    "",
    "// The built-in Solaris GUI surface, embedded verbatim as raw bytes from",
    "// gui_prelude.luma. Uncompressed to keep the analysis library free of any",
    "// decompression dependency; a byte array (not a string literal) avoids the",
    "// MSVC 16 KB string-literal limit.",
    "inline constexpr unsigned char k_gui_prelude_source_data[] = {",
    hexLines.join("\n"),
    "};",
    "",
    "// std::string_view is char-based, so the immutable byte array is viewed",
    "// through a const char pointer. reinterpret_cast is not permitted in a",
    "// constant expression, so this view is initialised at runtime; its only use",
    "// builds a std::string from it on first access.",
    "inline const std::string_view k_gui_prelude_source{",
    "    reinterpret_cast<const char*>(k_gui_prelude_source_data),",
    "    sizeof(k_gui_prelude_source_data)};",
    "",
    "} // namespace luma::prelude",
    "",
    "#endif // LUMA_ANALYSIS_PRELUDE_GUI_PRELUDE_GENERATED_HPP",
    "",
].join("\n");

fs.writeFileSync(outputPath, output);
console.log(`Generated ${outputPath}: ${bytes.length} source bytes, ${output.length} chars`);
