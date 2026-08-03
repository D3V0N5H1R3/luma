# Lucide

Community-maintained SVG icon library (a fork of Feather) with 1600+ icons.

- **Source:** <https://github.com/lucide-icons/lucide>
- **Version:** 0.473.0
- **Vendored:** 2026-05-15
- **License:** ISC (see [LICENSE](LICENSE))

The `GraphicalUi` module renders crisp scalable icons via `GraphicalUi.icon()`,
replacing text/emoji placeholders. Only a curated subset of icons is embedded —
not the full library — to keep the binary small.

## Files

- `lucide.min.js` — upstream UMD bundle (the vendored library; all icons).
- `extract_icons.js` — first-party script that selects the curated subset (the
  `wanted` list) and writes the JSON files below.
- `lucide-subset.json` — the extracted subset (reference output).
- `lucide-icons-part1.json`, `lucide-icons-part2.json` — the subset split into
  two halves (to stay under the MSVC string-literal limit); these are the files
  embedded into `graphicalui_assets.hpp`.
- `LICENSE` — upstream license text.

To change which icons are bundled, edit the `wanted` list in `extract_icons.js`,
re-run it, then regenerate the assets header.
