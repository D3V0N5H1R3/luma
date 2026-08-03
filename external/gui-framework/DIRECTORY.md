# GraphicalUi Web Framework

First-party browser-side code for the `GraphicalUi` standard library module.
Unlike the sibling directories under `external/`, this is **not** vendored
third-party code — it is Luma source maintained under the project's MIT license.

These scripts run inside the webview host and bridge Luma's `GraphicalUi` API to
the DOM, building on the vendored lit-html, Pico CSS, uPlot, and Lucide libraries.

## Files

| File                   | Purpose                                                                                         |
| ---------------------- | ----------------------------------------------------------------------------------------------- |
| `gui-renderer.js`      | lit-html based renderer core: shared helpers, factories, disclosure controllers, the render loop, and theme API. The per-widget `WIDGET_RENDERERS` table is split into the `renderers/` fragments below and spliced in at a marker. |
| `renderers/basic.js`   | Renderer entries for basic & input widgets (label, button, text/number inputs, pickers, …).      |
| `renderers/layout.js`  | Renderer entries for layout containers, overlays & structural widgets (row/column, panel, dialog, table, …). |
| `renderers/advanced.js`| Renderer entries for advanced / composite widgets (virtual_list, form, wizard, toast, …).        |
| `renderers/interaction.js` | Renderer entries for the disclosure / roving-focus widgets (menu, popover, combobox).        |
| `gui-charts.js`        | Chart renderer bridging chart widgets to uPlot (line/area/bar/scatter) and canvas (pie/donut).   |
| `gui-subscriptions.js` | Manages timer, keyboard, resize, focus, and mouse subscriptions; coalesces high-frequency events. |
| `gui-overrides.css`    | Layout/widget styles and the Pico → `--gui-*` theme variable bridge that Pico does not cover.    |

The `renderers/*.js` fragments are **not** standalone modules: each is an
`Object.assign(WIDGET_RENDERERS, { … })` block concatenated into the
`gui-renderer.js` IIFE (at the `// __GUI_WIDGET_RENDERER_FRAGMENTS__` marker) so
they share the renderer's module-private helpers. The split mirrors the C++
`core/runtime/stdlib/io/graphicalui_widgets_{basic,layout,advanced,interaction}.cpp`
category files. When adding a widget, add its renderer entry to the matching
fragment (menu/popover/combobox live in `interaction.js`).

## Build integration

These files are compressed and embedded into
`core/runtime/stdlib/io/graphicalui_assets.hpp` by `scripts/generate_gui_assets.mjs`,
alongside the vendored web assets. The generator concatenates the `renderers/*.js`
fragments into the single `gui_renderer_js` asset (splicing them at the marker);
the dev-asset loader in `core/runtime/stdlib/io/graphicalui_serialization.cpp` mirrors
that concatenation so `LUMA_GUI_DEV_ASSETS` iteration matches the embedded build.
After editing any file here, regenerate the header and rebuild the runtime:

```bash
node scripts/generate_gui_assets.mjs
```

## Tests

Unit tests for these scripts live in [`tests/`](tests/DIRECTORY.md) and run under
Node's built-in test runner with no browser and no third-party dependencies:

```bash
node --test external/gui-framework/tests/*.test.mjs
```

A `node:vm` sandbox harness loads each browser IIFE with a fake DOM and
controllable fake timers and captures the module-private helpers, so the tests
exercise the real shipping code **without modifying it** — the embedded
`graphicalui_assets.hpp` blob stays byte-for-byte identical. The
`ci-gui-framework.yml` workflow runs the suite on every change here.

## Related

- GraphicalUi runtime module: `core/runtime/stdlib/io/graphicalui_*`
- Design guide: [`documents/Luma_GraphicalUi_Guide.md`](../../documents/Luma_GraphicalUi_Guide.md)
- Vendored web libraries: [`lit-html`](../lit-html/DIRECTORY.md),
  [`pico-css`](../pico-css/DIRECTORY.md), [`uplot`](../uplot/DIRECTORY.md),
  [`lucide`](../lucide/DIRECTORY.md)
