# miniz

Single-file deflate/inflate (zlib/gzip) compression library.

- **Source:** <https://github.com/richgel999/miniz>
- **Version:** 3.1.0
- **Vendored:** 2026-05-15
- **License:** MIT (see [LICENSE](LICENSE))

Used by the `Compression` module for gzip and raw deflate/inflate, and by the
`GraphicalUi` module to decompress the embedded web assets at runtime.

## Files

- `miniz.c`, `miniz.h` — core library (deflate + inflate).
- `miniz_tdef.c`, `miniz_tdef.h` — deflate (compressor) implementation.
- `miniz_tinfl.c`, `miniz_tinfl.h` — inflate (decompressor) implementation.
- `miniz_common.h`, `miniz_export.h` — shared definitions and export macros.
- `miniz_zip.c`, `miniz_zip.h` — ZIP archive APIs (vendored but not compiled).
- `LICENSE` — upstream license text.

Only `miniz.c`, `miniz_tdef.c`, and `miniz_tinfl.c` are compiled (see
[`cmake/LumaMiniz.cmake`](../../cmake/LumaMiniz.cmake)); the build defines
`MINIZ_NO_STDIO` and `MINIZ_NO_ARCHIVE_APIS`, so the ZIP archive sources are
excluded.
