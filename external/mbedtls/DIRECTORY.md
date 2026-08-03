# Mbed TLS

C library implementing TLS, cryptographic primitives, and X.509 certificate
handling with no external dependencies.

- **Source:** <https://github.com/Mbed-TLS/mbedtls>
- **Version:** 3.6.6 LTS
- **Vendored:** 2026-05-14
- **License:** Apache-2.0 OR GPL-2.0-or-later (see [LICENSE](LICENSE))

Provides TLS, cryptographic primitives, and the CSPRNG (AES-CTR-DRBG) used by the
`Hash`, `Http`, and `Random` standard-library modules.

## Files

- `include/` — public Mbed TLS and PSA Crypto headers.
- `library/` — implementation sources.
- `LICENSE` — upstream license text.

Only the `include/` and `library/` directories are vendored from the release
tarball. The exact subset of `library/*.c` compiled into the runtime is listed
explicitly in [`cmake/LumaMbedTLS.cmake`](../../cmake/LumaMbedTLS.cmake).
