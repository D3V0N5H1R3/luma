# External Dependencies

This directory holds the third-party libraries vendored into the Luma source
tree. Each subdirectory carries its own `DIRECTORY.md` recording provenance and
license; this file is the inventory and the vendoring policy.

## Vendored third-party libraries

| Library                          | Version                        | License                        | Consumed by                        |
| -------------------------------- | ------------------------------ | ------------------------------ | ---------------------------------- |
| [`mbedtls`](mbedtls/DIRECTORY.md)   | 3.6.6 LTS                      | Apache-2.0 OR GPL-2.0-or-later | `Hash`, `Http`, `Random`           |
| [`miniz`](miniz/DIRECTORY.md)       | 3.1.0                          | MIT                            | `Compression`                      |

## How dependencies are built in

- **`mbedtls`, `miniz`** — compiled into the `luma` runtime as static libraries
  by [`cmake/LumaMbedTLS.cmake`](../cmake/LumaMbedTLS.cmake) and
  [`cmake/LumaMiniz.cmake`](../cmake/LumaMiniz.cmake), linked from
  `core/runtime/CMakeLists.txt`.

## Vendoring policy

- Each third-party subdirectory ships a `DIRECTORY.md` (upstream source URL,
  version, vendor date, license) and a `LICENSE` file where the upstream
  provides a standalone one.
- Vendor only the minimal subset of files Luma needs — drop tests, docs,
  examples, and build scaffolding from upstream.
- To update a library: replace the files, bump the **Version** and
  **Vendored** date in its `DIRECTORY.md`, and rebuild.

> **Note:** `external/**` is excluded from the repository linters (markdownlint,
> clang-format, etc.) so vendored files stay byte-for-byte upstream.
