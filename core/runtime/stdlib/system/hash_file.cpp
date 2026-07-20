// hash_file.cpp — File-based hash operations for the Hash module.

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>

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
// {name, type} table (Hash.sha256_file, Hash.sha512_file).  HashAlgorithmSpec
// is shared with the digest table in hash_digest.cpp.
constexpr std::array<HashAlgorithmSpec, 2> k_file_hash_algorithms{{
    {"sha256_file", MBEDTLS_MD_SHA256},
    {"sha512_file", MBEDTLS_MD_SHA512},
}};

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

                const auto safe = validate_path(user_path, loc);

                // Hashing slurps the whole file into memory, so guard against
                // memory exhaustion the same way FileSystem.read_file does:
                // reject anything larger than the interpreter's maximum string
                // size before reading a single byte. If the size cannot be
                // determined (e.g. a FIFO or device node), fail closed rather
                // than risk an unbounded read.
                std::error_code size_ec;
                const auto file_bytes = std::filesystem::file_size(safe, size_ec);
                if (size_ec) {
                    return make_failure_value(
                        std::format("{}: cannot determine the size of '{}': {}", qualified,
                                    safe.string(), size_ec.message()));
                }
                if (file_bytes > ResourceLimits::max_string_size) {
                    return make_failure_value(
                        std::format("{}: file '{}' exceeds the maximum size of {} bytes", qualified,
                                    safe.string(), ResourceLimits::max_string_size));
                }

                const auto hex = hash_file(safe, type, loc);

                if (hex.empty()) {
                    return make_failure_value(
                        std::format("{}: cannot open '{}'", qualified, safe.string()));
                }

                return make_success_value(Value{hex});
            });
    }
}

} // namespace luma
