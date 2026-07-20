# cmake/LumaMbedTLS.cmake — Vendored Mbed TLS static library (HTTPS support).
#
# Mbed TLS 3.6.6 LTS — vendored 2026-05-14 from
# https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.6
#
# Why Mbed TLS:
#   - Pure C with no external dependencies — ideal for vendoring.
#   - LTS releases provide long-term stability.
#   - Permissive Apache-2.0 licence compatible with Luma's licence.
#   - Supports TLS 1.2 + 1.3, X.509, and the PSA Crypto API.
#   - Cross-platform (Windows, macOS, Linux) without platform-specific TLS APIs.
#
# Defines the `mbedtls_lib` target only. Linking it into luma_core, defining
# LUMA_HAS_TLS, and adding any platform crypto libraries that luma_core itself
# needs are the caller's responsibility (see core/runtime/CMakeLists.txt).

include_guard(GLOBAL)

set(MBEDTLS_DIR ${PROJECT_SOURCE_DIR}/external/mbedtls/library)

add_library(mbedtls_lib STATIC
    # ─── Core cryptographic primitives ───
    ${MBEDTLS_DIR}/aes.c
    ${MBEDTLS_DIR}/aesce.c
    ${MBEDTLS_DIR}/aesni.c
    ${MBEDTLS_DIR}/aria.c
    ${MBEDTLS_DIR}/camellia.c
    ${MBEDTLS_DIR}/ccm.c
    ${MBEDTLS_DIR}/chacha20.c
    ${MBEDTLS_DIR}/chachapoly.c
    ${MBEDTLS_DIR}/cipher.c
    ${MBEDTLS_DIR}/cipher_wrap.c
    ${MBEDTLS_DIR}/cmac.c
    ${MBEDTLS_DIR}/des.c
    ${MBEDTLS_DIR}/gcm.c
    ${MBEDTLS_DIR}/nist_kw.c
    ${MBEDTLS_DIR}/poly1305.c

    # ─── Hashing ───
    ${MBEDTLS_DIR}/md.c
    ${MBEDTLS_DIR}/md5.c
    ${MBEDTLS_DIR}/ripemd160.c
    ${MBEDTLS_DIR}/sha1.c
    ${MBEDTLS_DIR}/sha256.c
    ${MBEDTLS_DIR}/sha3.c
    ${MBEDTLS_DIR}/sha512.c
    ${MBEDTLS_DIR}/hkdf.c

    # ─── Big number / modular arithmetic ───
    ${MBEDTLS_DIR}/bignum.c
    ${MBEDTLS_DIR}/bignum_core.c
    ${MBEDTLS_DIR}/bignum_mod.c
    ${MBEDTLS_DIR}/bignum_mod_raw.c

    # ─── Public-key cryptography ───
    ${MBEDTLS_DIR}/dhm.c
    ${MBEDTLS_DIR}/ecdh.c
    ${MBEDTLS_DIR}/ecdsa.c
    ${MBEDTLS_DIR}/ecjpake.c
    ${MBEDTLS_DIR}/ecp.c
    ${MBEDTLS_DIR}/ecp_curves.c
    ${MBEDTLS_DIR}/ecp_curves_new.c
    ${MBEDTLS_DIR}/rsa.c
    ${MBEDTLS_DIR}/rsa_alt_helpers.c
    ${MBEDTLS_DIR}/pk.c
    ${MBEDTLS_DIR}/pk_ecc.c
    ${MBEDTLS_DIR}/pk_wrap.c
    ${MBEDTLS_DIR}/pkparse.c
    ${MBEDTLS_DIR}/pkwrite.c
    ${MBEDTLS_DIR}/pkcs5.c
    ${MBEDTLS_DIR}/pkcs7.c
    ${MBEDTLS_DIR}/pkcs12.c
    ${MBEDTLS_DIR}/lmots.c
    ${MBEDTLS_DIR}/lms.c

    # ─── ASN.1 / PEM encoding ───
    ${MBEDTLS_DIR}/asn1parse.c
    ${MBEDTLS_DIR}/asn1write.c
    ${MBEDTLS_DIR}/base64.c
    ${MBEDTLS_DIR}/pem.c
    ${MBEDTLS_DIR}/oid.c

    # ─── X.509 certificates ───
    ${MBEDTLS_DIR}/x509.c
    ${MBEDTLS_DIR}/x509_create.c
    ${MBEDTLS_DIR}/x509_crl.c
    ${MBEDTLS_DIR}/x509_crt.c
    ${MBEDTLS_DIR}/x509_csr.c
    ${MBEDTLS_DIR}/x509write.c
    ${MBEDTLS_DIR}/x509write_crt.c
    ${MBEDTLS_DIR}/x509write_csr.c

    # ─── SSL/TLS protocol ───
    ${MBEDTLS_DIR}/ssl_cache.c
    ${MBEDTLS_DIR}/ssl_ciphersuites.c
    ${MBEDTLS_DIR}/ssl_client.c
    ${MBEDTLS_DIR}/ssl_cookie.c
    ${MBEDTLS_DIR}/ssl_debug_helpers_generated.c
    ${MBEDTLS_DIR}/ssl_msg.c
    ${MBEDTLS_DIR}/ssl_ticket.c
    ${MBEDTLS_DIR}/ssl_tls.c
    ${MBEDTLS_DIR}/ssl_tls12_client.c
    ${MBEDTLS_DIR}/ssl_tls12_server.c
    ${MBEDTLS_DIR}/ssl_tls13_client.c
    ${MBEDTLS_DIR}/ssl_tls13_generic.c
    ${MBEDTLS_DIR}/ssl_tls13_keys.c
    ${MBEDTLS_DIR}/ssl_tls13_server.c

    # ─── PSA Crypto API ───
    ${MBEDTLS_DIR}/psa_crypto.c
    ${MBEDTLS_DIR}/psa_crypto_aead.c
    ${MBEDTLS_DIR}/psa_crypto_cipher.c
    ${MBEDTLS_DIR}/psa_crypto_client.c
    ${MBEDTLS_DIR}/psa_crypto_driver_wrappers_no_static.c
    ${MBEDTLS_DIR}/psa_crypto_ecp.c
    ${MBEDTLS_DIR}/psa_crypto_ffdh.c
    ${MBEDTLS_DIR}/psa_crypto_hash.c
    ${MBEDTLS_DIR}/psa_crypto_mac.c
    ${MBEDTLS_DIR}/psa_crypto_pake.c
    ${MBEDTLS_DIR}/psa_crypto_random.c
    ${MBEDTLS_DIR}/psa_crypto_rsa.c
    ${MBEDTLS_DIR}/psa_crypto_se.c
    ${MBEDTLS_DIR}/psa_crypto_slot_management.c
    ${MBEDTLS_DIR}/psa_crypto_storage.c
    ${MBEDTLS_DIR}/psa_its_file.c
    ${MBEDTLS_DIR}/psa_util.c

    # ─── Platform, RNG, and utilities ───
    ${MBEDTLS_DIR}/block_cipher.c
    ${MBEDTLS_DIR}/constant_time.c
    ${MBEDTLS_DIR}/ctr_drbg.c
    ${MBEDTLS_DIR}/debug.c
    ${MBEDTLS_DIR}/entropy.c
    ${MBEDTLS_DIR}/entropy_poll.c
    ${MBEDTLS_DIR}/error.c
    ${MBEDTLS_DIR}/hmac_drbg.c
    ${MBEDTLS_DIR}/memory_buffer_alloc.c
    ${MBEDTLS_DIR}/mps_reader.c
    ${MBEDTLS_DIR}/mps_trace.c
    ${MBEDTLS_DIR}/net_sockets.c
    ${MBEDTLS_DIR}/padlock.c
    ${MBEDTLS_DIR}/platform.c
    ${MBEDTLS_DIR}/platform_util.c
    ${MBEDTLS_DIR}/threading.c
    ${MBEDTLS_DIR}/timing.c
    ${MBEDTLS_DIR}/version.c
    ${MBEDTLS_DIR}/version_features.c
)
# Maintenance note: source files are listed explicitly (no file(GLOB)) to ensure
# reproducible builds. When upgrading Mbed TLS, diff the upstream library/
# directory against this list and add/remove entries as needed.

target_include_directories(mbedtls_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/external/mbedtls/include)
target_include_directories(mbedtls_lib PRIVATE
    ${PROJECT_SOURCE_DIR}/external/mbedtls/library)

# Vendored C: build to C99 and silence our strict warnings (see LumaTargetHelpers).
luma_configure_vendored_c_target(mbedtls_lib)

# Windows entropy/RNG backend used internally by Mbed TLS.
if(WIN32)
    target_link_libraries(mbedtls_lib PRIVATE bcrypt)
endif()
