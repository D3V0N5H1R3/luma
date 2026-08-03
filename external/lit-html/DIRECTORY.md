# lit-html

Efficient, expressive, extensible HTML templating for JavaScript.

- **Source:** <https://github.com/lit/lit/tree/main/packages/lit-html>
- **Version:** 3.2.1
- **Vendored:** 2026-05-15
- **License:** BSD-3-Clause (retained in the `@license` banner of each file)

Used by the `GraphicalUi` renderer (`gui-renderer.js`) to build widget trees and
patch them efficiently into the DOM.

## Files

- `lit-html.js` — original ES module from npm (source for the IIFE wrapper).
- `lit-html.iife.js` — IIFE wrapper for inline `<script>` use, generated from
  `lit-html.js`; this is the file embedded into `graphicalui_assets.hpp`.
