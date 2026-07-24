// hash_digest.cpp — Digest computation and HMAC helpers for the Hash module.

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "common/crc32.hpp"
#include "common/hex_codec.hpp"
#include "common/unreachable.hpp"
#include "mbedtls/md.h"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/system/hash_module.hpp"
#include "runtime/stdlib/system/hash_module_internal.hpp"

namespace luma {

namespace {

// ═══════════════════════════════════════════════════════════════════════════════
// §1  Digest helpers
// ═══════════════════════════════════════════════════════════════════════════════

// Digest and HMAC functions are registered from small {name, type} tables so
// that adding an algorithm is a single row rather than a copy-pasted block.
// The HashAlgorithmSpec row type lives in hash_module_internal.hpp so the
// file-hash table in hash_file.cpp can reuse it.
constexpr std::array<HashAlgorithmSpec, 4> k_digest_algorithms{{
    {"md5", MBEDTLS_MD_MD5},
    {"sha1", MBEDTLS_MD_SHA1},
    {"sha256", MBEDTLS_MD_SHA256},
    {"sha512", MBEDTLS_MD_SHA512},
}};

constexpr std::array<HashAlgorithmSpec, 2> k_hmac_algorithms{{
    {"hmac_sha256", MBEDTLS_MD_SHA256},
    {"hmac_sha512", MBEDTLS_MD_SHA512},
}};

[[nodiscard]] mbedtls_md_type_t md_type_from_name(std::string_view name) {
    for (const auto& algo : k_digest_algorithms) {
        if (algo.name == name) {
            return algo.type;
        }
    }

    return MBEDTLS_MD_NONE;
}

// Maps a Hash.Algorithm choice variant to its canonical lowercase algorithm name.
// The variant names must match the Hash.Algorithm choice in
// core/analysis/types/stdlib_type_arities.cpp; Crc32 is included here even though
// it is not an mbedtls digest, because compute_named_digest handles it separately.
[[nodiscard]] std::optional<std::string_view>
algorithm_name_from_variant(std::string_view variant) {
    if (variant == "Md5") {
        return "md5";
    }
    if (variant == "Sha1") {
        return "sha1";
    }
    if (variant == "Sha256") {
        return "sha256";
    }
    if (variant == "Sha512") {
        return "sha512";
    }
    if (variant == "Crc32") {
        return "crc32";
    }

    return std::nullopt;
}

// Resolves an algorithm argument that may be either a Hash.Algorithm choice or a
// lowercase algorithm-name string to the canonical name, mirroring the dual
// choice/string acceptance of Terminal.color and Decimal.round.  A choice maps
// its variant (Sha256 → "sha256"); a string passes through unchanged so an
// unknown name still yields the existing "unknown algorithm" failure.  Any other
// type is a programmer error and throws — the choice path is total.
[[nodiscard]] std::string resolve_algorithm_name(const Value& arg, std::string_view fn,
                                                 const SourceLocation& loc) {
    if (arg.is_choice()) {
        const auto& variant = arg.as_choice()->variant;

        if (auto name = algorithm_name_from_variant(variant)) {
            return std::string{*name};
        }

        throw RuntimeError{std::format("{}: unknown algorithm 'Hash.Algorithm.{}'", fn, variant),
                           loc, "use a Hash.Algorithm variant, e.g. Hash.Algorithm.Sha256"};
    }

    if (arg.is_string()) {
        return arg.as_string();
    }

    throw RuntimeError{
        std::format("{}: algorithm must be a Hash.Algorithm or an algorithm-name string", fn), loc,
        "pass a Hash.Algorithm variant (e.g. Hash.Algorithm.Sha256) or a string (e.g. \"sha256\")"};
}

// Computes the hex digest of input under the named algorithm, or std::nullopt for
// an unknown name.  Handles both the mbedtls digests (md5/sha1/sha256/sha512) and
// crc32, which is not an mbedtls MD — its 32-bit checksum is rendered as a fixed
// 8-hex-digit string so Hash.digest and Hash.verify return a uniform string form.
[[nodiscard]] std::optional<std::string>
compute_named_digest(std::string_view algo, const std::string& input, const SourceLocation& loc) {
    if (algo == "crc32") {
        return std::format("{:08x}", crc32_hash(input));
    }

    const auto type = md_type_from_name(algo);

    if (type == MBEDTLS_MD_NONE) {
        return std::nullopt;
    }

    return compute_digest_hex(type, input, loc);
}

// Comma-separated list of the algorithms accepted by Hash.verify and Hash.digest,
// built from k_digest_algorithms (+ crc32) so the hint cannot drift from
// compute_named_digest.
[[nodiscard]] std::string supported_digest_algorithms() {
    std::string list;
    for (const auto& algo : k_digest_algorithms) {
        if (!list.empty()) {
            list += ", ";
        }
        list += algo.name;
    }
    list += ", crc32";

    return list;
}

// ═══════════════════════════════════════════════════════════════════════════════
// §2  HMAC
// ═══════════════════════════════════════════════════════════════════════════════

[[nodiscard]] std::string compute_hmac(mbedtls_md_type_t type, const std::string& key,
                                       const std::string& message, const SourceLocation& loc) {
    DigestContext ctx;
    if (!ctx.init(type)) {
        // See compute_digest_hex: callers always pass a valid MBEDTLS_MD_* constant.
        LUMA_UNREACHABLE();
    }

    if (mbedtls_md_hmac(ctx.info, reinterpret_cast<const unsigned char*>(key.data()), key.size(),
                        reinterpret_cast<const unsigned char*>(message.data()), message.size(),
                        ctx.digest) != 0) {
        throw RuntimeError{"Hash: HMAC computation failed", loc};
    }

    return to_hex(ctx.digest, ctx.digest_size);
}

// ═══════════════════════════════════════════════════════════════════════════════
// §3  Timing-safe compare
// ═══════════════════════════════════════════════════════════════════════════════

// Constant-time comparison of two hex-encoded hash strings to prevent
// timing attacks.  Always examines every byte regardless of where a
// mismatch occurs, so the execution time does not leak information
// about which characters match.
[[nodiscard]] bool constant_time_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }

    volatile unsigned char result{0};

    for (std::size_t i{0}; i < a.size(); ++i) {
        result |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }

    return result == 0;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Digest and HMAC registration
// ═══════════════════════════════════════════════════════════════════════════════

void register_hash_digest(const EnvPtr& env) {
    ModuleBuilder builder{"Hash", env};

    // One-argument message digests (Hash.md5, Hash.sha1, Hash.sha256, Hash.sha512).
    for (const auto& algo : k_digest_algorithms) {
        builder.func(algo.name, 1)
            .raw_body([type = algo.type, qualified = "Hash." + std::string{algo.name}](
                          std::span<const Value> args, SourceLocation loc) -> Value {
                const auto& input = expect_string(args[0], qualified, loc);

                return Value{compute_digest_hex(type, input, loc)};
            });
    }

    // Keyed-hash message authentication codes (Hash.hmac_sha256, Hash.hmac_sha512).
    for (const auto& algo : k_hmac_algorithms) {
        builder.func(algo.name, 2)
            .raw_body([type = algo.type, qualified = "Hash." + std::string{algo.name}](
                          std::span<const Value> args, SourceLocation loc) -> Value {
                expect_all_strings(args, qualified, loc, "pass the key and message as strings");

                return Value{compute_hmac(type, args[0].as_string(), args[1].as_string(), loc)};
            });
    }

    builder.func("crc32", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto crc = crc32_hash(expect_string(args[0], "Hash.crc32", loc));

            return Value{static_cast<std::int64_t>(crc)};
        })
        .func("verify", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            // Accepts the algorithm as a Hash.Algorithm choice or a name string
            // (dual-form); input and expected are always strings.
            const auto algo = resolve_algorithm_name(args[0], "Hash.verify", loc);
            const auto& input = expect_string(args[1], "Hash.verify", loc);
            const auto& expected = expect_string(args[2], "Hash.verify", loc);

            const auto computed = compute_named_digest(algo, input, loc);

            if (!computed) {
                throw RuntimeError{
                    error_msg("Hash", "verify", std::format("unknown algorithm '{}'", algo)), loc,
                    std::format("supported algorithms are: {}", supported_digest_algorithms())};
            }

            return Value{constant_time_equal(*computed, expected)};
        })
        .func("digest", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            // Generic hex digest under a Hash.Algorithm choice or a name string,
            // so a program can select the algorithm type-safely rather than
            // calling md5/sha256/… by name.  crc32 renders as an 8-hex-digit string.
            const auto algo = resolve_algorithm_name(args[0], "Hash.digest", loc);
            const auto& input = expect_string(args[1], "Hash.digest", loc);

            const auto computed = compute_named_digest(algo, input, loc);

            if (!computed) {
                throw RuntimeError{
                    error_msg("Hash", "digest", std::format("unknown algorithm '{}'", algo)), loc,
                    std::format("supported algorithms are: {}", supported_digest_algorithms())};
            }

            return Value{*computed};
        })
        .func("algorithms", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            // Derived from k_digest_algorithms (+ crc32) so the reported set
            // cannot drift from the digest functions actually registered above.
            auto arr = std::make_shared<ArrayValue>();
            arr->elements->reserve(k_digest_algorithms.size() + 1);

            for (const auto& algo : k_digest_algorithms) {
                arr->elements->push_back(Value{std::string{algo.name}});
            }
            arr->elements->push_back(Value{std::string{"crc32"}});

            return Value{std::move(arr)};
        });
}

} // namespace luma
