# External Dependencies

This directory holds the third-party libraries vendored into the Luma source
tree, together with the first-party GraphicalUi browser-side code that depends
on them. Each subdirectory carries its own `DIRECTORY.md` recording provenance and
license; this file is the inventory and the vendoring policy.

## Vendored third-party libraries

| Library                          | Version                        | License                        | Consumed by                        |
| -------------------------------- | ------------------------------ | ------------------------------ | ---------------------------------- |
| [`mbedtls`](mbedtls/DIRECTORY.md)   | 3.6.6 LTS                      | Apache-2.0 OR GPL-2.0-or-later | `Hash`, `Http`, `Random`           |
| [`miniz`](miniz/DIRECTORY.md)       | 3.1.0                          | MIT                            | `Compression`, `GraphicalUi`       |
| [`webview`](webview/DIRECTORY.md)   | 0.12.0                         | MIT                            | `GraphicalUi` (native window host) |
| [`lit-html`](lit-html/DIRECTORY.md) | 3.2.1                          | BSD-3-Clause                   | `GraphicalUi` (template renderer)  |
| [`lucide`](lucide/DIRECTORY.md)     | 0.473.0                        | ISC                            | `GraphicalUi` (SVG icons)          |
| [`pico-css`](pico-css/DIRECTORY.md) | 2.1.1                          | MIT                            | `GraphicalUi` (base styles)        |
| [`uplot`](uplot/DIRECTORY.md)       | 1.6.31                         | MIT                            | `GraphicalUi` (charts)             |

## First-party code

| Directory                                  | License | Purpose                                                          |
| ------------------------------------------ | ------- | ---------------------------------------------------------------- |
| [`gui-framework`](gui-framework/DIRECTORY.md) | MIT     | Browser-side glue for `GraphicalUi` (renderer, charts, subs, CSS) |

`gui-framework/` is **not** vendored — it is Luma source code that lives here so
it can be embedded alongside the web libraries it builds on.

## How dependencies are built in

- **`mbedtls`, `miniz`** — compiled into the `luma` runtime as static libraries
  by [`cmake/LumaMbedTLS.cmake`](../cmake/LumaMbedTLS.cmake) and
  [`cmake/LumaMiniz.cmake`](../cmake/LumaMiniz.cmake), linked from
  `core/runtime/CMakeLists.txt`.
- **`webview`** — header-only; the platform backend (WebView2 on Windows, WebKit
  on macOS, WebKitGTK on Linux) is resolved by
  [`cmake/LumaWebView.cmake`](../cmake/LumaWebView.cmake) and its headers are
  included by the GraphicalUi module (`core/runtime/stdlib/io/graphicalui_*`).
- **GraphicalUi web assets** (`lit-html`, `pico-css`, `uplot`, `lucide`, and the
  first-party `gui-framework` scripts/styles) — compressed and embedded into
  `core/runtime/stdlib/io/graphicalui_assets.hpp` by
  `scripts/generate_gui_assets.mjs`, then decompressed at runtime with `miniz`.

## Vendoring policy

- Each third-party subdirectory ships a `DIRECTORY.md` (upstream source URL,
  version, vendor date, license) and a `LICENSE` file where the upstream
  provides a standalone one. Minified single-file assets (`lit-html`,
  `pico-css`, `uplot`) retain their license banner in the asset itself instead
  of a standalone `LICENSE` file.
- Vendor only the minimal subset of files Luma needs — drop tests, docs,
  examples, and build scaffolding from upstream.
- To update a library: replace the files, bump the **Version** and
  **Vendored** date in its `DIRECTORY.md`, regenerate
  `graphicalui_assets.hpp` if it is a GraphicalUi asset, and rebuild.

> **Note:** `external/**` is excluded from the repository linters (markdownlint,
> clang-format, etc.) so vendored files stay byte-for-byte upstream.
