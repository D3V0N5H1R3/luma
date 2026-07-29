// hash_file.cpp — File-based hash operations for the Hash module.

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "mbedtls/md.h"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "runtime/stdlib/system/hash_module.hpp"
#include "runtime/stdlib/system/hash_module_internal.hpp"

namespace luma {

namespace {

// ═══════════════════════════════════════════════════════════════════════════════
// File hashing helpers
// ═══════════════════════════════════════════════════════════════════════════════

// File-hash functions differ only by algorithm, so register them from a
// {name, type} table (Hash.md5_file, Hash.sha1_file, Hash.sha256_file,
// Hash.sha512_file).  HashAlgorithmSpec is shared with the digest table in
// hash_digest.cpp.
constexpr std::array<HashAlgorithmSpec, 4> k_file_hash_algorithms{{
    {"md5_file", MBEDTLS_MD_MD5},
    {"sha1_file", MBEDTLS_MD_SHA1},
    {"sha256_file", MBEDTLS_MD_SHA256},
    {"sha512_file", MBEDTLS_MD_SHA512},
}};

// Resolves a Hash.digest_file algorithm argument (a Hash.Algorithm choice or an
// algorithm-name string) to the mbedtls digest type, or MBEDTLS_MD_NONE when the
// name is not one of the four file-hashable digests.  Mirrors resolve_algorithm_
// name in hash_digest.cpp but limited to the mbedtls digests (crc32 has no
// mbedtls streaming form).
[[nodiscard]] mbedtls_md_type_t file_md_type_from_arg(const Value& arg, std::string_view fn,
                                                      const SourceLocation& loc) {
    std::string_view name;

    if (arg.is_choice()) {
        const auto& variant = arg.as_choice()->variant;

        if (variant == "Md5") {
            name = "md5";
        } else if (variant == "Sha1") {
            name = "sha1";
        } else if (variant == "Sha256") {
            name = "sha256";
        } else if (variant == "Sha512") {
            name = "sha512";
        } else {
            throw RuntimeError{
                std::format("{}: algorithm 'Hash.Algorithm.{}' cannot hash a file", fn, variant),
                loc, "use Md5, Sha1, Sha256, or Sha512"};
        }
    } else if (arg.is_string()) {
        name = arg.as_string();
    } else {
        throw RuntimeError{
            std::format("{}: algorithm must be a Hash.Algorithm or an algorithm-name string", fn),
            loc, "pass a Hash.Algorithm variant (e.g. Hash.Algorithm.Sha256) or a string"};
    }

    if (name == "md5") {
        return MBEDTLS_MD_MD5;
    }
    if (name == "sha1") {
        return MBEDTLS_MD_SHA1;
    }
    if (name == "sha256") {
        return MBEDTLS_MD_SHA256;
    }
    if (name == "sha512") {
        return MBEDTLS_MD_SHA512;
    }

    return MBEDTLS_MD_NONE;
}

[[nodiscard]] std::string hash_file(const std::filesystem::path& path, mbedtls_md_type_t type,
                                    const SourceLocation& loc) {
    std::ifstream file{path, std::ios::binary};

    if (!file.is_open()) {
        return {}; // Caller wraps in result<Value> failure.
    }

    // Read directly into a string, avoiding the ostringstream double-buffer.
    std::string contents{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

    return compute_digest_hex(type, contents, loc);
}

// Validates `user_path`, guards against oversized files, and returns the hex
// digest under `type` as a result<string>.  Shared by the per-algorithm file
// functions and Hash.digest_file so the size guard and path validation live in
// one place.
[[nodiscard]] Value hash_file_at(const std::string& user_path, mbedtls_md_type_t type,
                                 std::string_view qualified, const SourceLocation& loc) {
    const auto safe = validate_path(user_path, loc);

    // Hashing slurps the whole file into memory, so guard against memory
    // exhaustion the same way FileSystem.read_file does: reject anything larger
    // than the interpreter's maximum string size before reading a single byte.
    // If the size cannot be determined (e.g. a FIFO or device node), fail closed
    // rather than risk an unbounded read.
    std::error_code size_ec;
    const auto file_bytes = std::filesystem::file_size(safe, size_ec);
    if (size_ec) {
        return make_failure_value(std::format("{}: cannot determine the size of '{}': {}",
                                              qualified, safe.string(), size_ec.message()));
    }
    if (file_bytes > ResourceLimits::max_string_size) {
        return make_failure_value(std::format("{}: file '{}' exceeds the maximum size of {} bytes",
                                              qualified, safe.string(),
                                              ResourceLimits::max_string_size));
    }

    const auto hex = hash_file(safe, type, loc);

    if (hex.empty()) {
        return make_failure_value(std::format("{}: cannot open '{}'", qualified, safe.string()));
    }

    return make_success_value(Value{hex});
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// File hashing registration
// ═══════════════════════════════════════════════════════════════════════════════

void register_hash_file(const EnvPtr& env) {
    ModuleBuilder builder{"Hash", env};

    for (const auto& spec : k_file_hash_algorithms) {
        builder.func(spec.name, 1)
            .raw_body([type = spec.type, qualified = "Hash." + std::string{spec.name}](
                          std::span<const Value> args, SourceLocation loc) -> Value {
                const auto& user_path = expect_string(args[0], qualified, loc);

                return hash_file_at(user_path, type, qualified, loc);
            });
    }

    // Hash.digest_file(algo, path) — generic streaming file digest under a
    // Hash.Algorithm choice or an algorithm-name string, mirroring Hash.digest.
    builder.func("digest_file", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto type = file_md_type_from_arg(args[0], "Hash.digest_file", loc);

            const auto& user_path = expect_string(args[1], "Hash.digest_file", loc);

            if (type == MBEDTLS_MD_NONE) {
                return make_failure_value(error_msg(
                    "Hash", "digest_file", "unknown algorithm (use md5, sha1, sha256, or sha512)"));
            }

            return hash_file_at(user_path, type, "Hash.digest_file", loc);
        });
}

} // namespace luma
