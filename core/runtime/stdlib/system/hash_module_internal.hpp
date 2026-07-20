#ifndef LUMA_STDLIB_HASH_MODULE_INTERNAL_HPP
#define LUMA_STDLIB_HASH_MODULE_INTERNAL_HPP

#include <cstddef>
#include <string>
#include <string_view>

#include <mbedtls/md.h>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/hex_codec.hpp"
#include "common/unreachable.hpp"

namespace luma {

// Algorithm lookup-table row shared by hash_digest.cpp and hash_file.cpp.
// Registering a digest becomes a single {name, type} entry rather than a
// copy-pasted lambda block.
struct HashAlgorithmSpec {
    std::string_view name;
    mbedtls_md_type_t type;
};

// Shared hash digest context used by hash_digest.cpp and hash_file.cpp.
struct DigestContext {
    const mbedtls_md_info_t* info{nullptr};
    std::size_t digest_size{0};
    unsigned char digest[MBEDTLS_MD_MAX_SIZE]{};

    [[nodiscard]] bool init(mbedtls_md_type_t type) {
        info = mbedtls_md_info_from_type(type);
        if (!info) {
            return false;
        }
        digest_size = mbedtls_md_get_size(info);
        return true;
    }
};

// Compute the lowercase hex-encoded digest of `input` under algorithm `type`.
//
// Callers always pass a hardcoded MBEDTLS_MD_* constant validated at
// registration time, so an init failure is an internal invariant violation
// rather than a user error.  A digest-computation failure is reported as a
// RuntimeError at `loc`.
[[nodiscard]] inline std::string compute_digest_hex(mbedtls_md_type_t type, std::string_view input,
                                                    const SourceLocation& loc) {
    DigestContext ctx;
    if (!ctx.init(type)) {
        LUMA_UNREACHABLE();
    }

    if (mbedtls_md(ctx.info, reinterpret_cast<const unsigned char*>(input.data()), input.size(),
                   ctx.digest) != 0) {
        throw RuntimeError{"Hash: digest computation failed", loc};
    }

    return to_hex(ctx.digest, ctx.digest_size);
}

} // namespace luma

#endif // LUMA_STDLIB_HASH_MODULE_INTERNAL_HPP
