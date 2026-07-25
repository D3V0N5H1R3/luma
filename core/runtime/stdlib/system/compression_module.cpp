#include "runtime/stdlib/system/compression_module.hpp"

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/file_helpers.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "runtime/stdlib/system/compression_codec.hpp"

// The pure codec layer (RLE, deflate/inflate, gzip) lives in
// compression_codec.cpp behind compression_codec.hpp; this translation unit
// only wires those functions into the Luma Compression module.

namespace luma {

namespace {

// Requires arg to be a Compression.Format choice and returns its variant name
// (Deflate/Gzip/Rle).  Compression.compress/decompress dispatch on this to
// select the underlying per-algorithm codec — mirroring how Hash.digest /
// Hash.verify dispatch on a Hash.Algorithm choice (resolve_algorithm_name in
// hash_digest.cpp), except Compression.Format has no string dual-form: it is
// the sole runtime-dispatch entry point, while deflate/inflate, gzip/gunzip,
// and encode_rle/decode_rle stay the primary, directly-named functions.
// Variant names must match the Compression.Format choice in
// core/analysis/types/stdlib_type_arities.cpp exactly (PascalCase).
[[nodiscard]] const std::string& require_format_variant(const Value& arg, std::string_view fn,
                                                        SourceLocation loc) {
    if (!arg.is_choice()) {
        throw RuntimeError{std::format("{}: format must be a Compression.Format", fn), loc,
                           "pass a Compression.Format variant, e.g. Compression.Format.Gzip"};
    }

    return arg.as_choice()->variant;
}

} // namespace

// === Registration ===

void register_compression_ns(const EnvPtr& env, bool sandbox) {
    ModuleBuilder{"Compression", env}
        .func("compress", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& data = expect_string(args[0], "Compression.compress", loc);
            const auto& variant = require_format_variant(args[1], "Compression.compress", loc);

            if (variant == "Deflate") {
                return Value{compression::deflate_compress(data)};
            }
            if (variant == "Gzip") {
                return Value{compression::gzip_compress(data)};
            }
            if (variant == "Rle") {
                return Value{compression::rle_encode(data)};
            }

            throw RuntimeError{
                std::format("Compression.compress: unknown Compression.Format '{}'", variant), loc};
        })
        .func("decompress", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& data = expect_string(args[0], "Compression.decompress", loc);
            const auto& variant = require_format_variant(args[1], "Compression.decompress", loc);

            std::optional<std::string> decompressed;
            std::string_view malformed_msg;

            if (variant == "Deflate") {
                decompressed = compression::deflate_decompress(data);
                malformed_msg = "Compression.decompress: malformed deflate data";
            } else if (variant == "Gzip") {
                decompressed = compression::gzip_decompress(data);
                malformed_msg = "Compression.decompress: malformed gzip data";
            } else if (variant == "Rle") {
                decompressed = compression::rle_decode(data);
                malformed_msg = "Compression.decompress: malformed RLE data";
            } else {
                throw RuntimeError{
                    std::format("Compression.decompress: unknown Compression.Format '{}'", variant),
                    loc};
            }

            if (!decompressed) {
                return make_failure_value(std::string{malformed_msg});
            }
            return make_success_value(Value{*decompressed});
        })
        .func("deflate", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Compression.deflate", loc);
            return Value{compression::deflate_compress(args[0].as_string())};
        })
        .func("inflate", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Compression.inflate", loc);
            const auto decompressed = compression::deflate_decompress(args[0].as_string());
            if (!decompressed) {
                return make_failure_value(
                    std::string{"Compression.inflate: malformed deflate data"});
            }
            return make_success_value(Value{*decompressed});
        })
        .func("gzip", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Compression.gzip", loc);
            return Value{compression::gzip_compress(args[0].as_string())};
        })
        .func("gunzip", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Compression.gunzip", loc);
            const auto decompressed = compression::gzip_decompress(args[0].as_string());
            if (!decompressed) {
                return make_failure_value(std::string{"Compression.gunzip: malformed gzip data"});
            }
            return make_success_value(Value{*decompressed});
        })
        .func("encode_rle", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Compression.encode_rle", loc);
            return Value{compression::rle_encode(args[0].as_string())};
        })
        .func("decode_rle", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Compression.decode_rle", loc);
            const auto decoded = compression::rle_decode(args[0].as_string());
            if (!decoded) {
                return make_failure_value(
                    std::string{"Compression.decode_rle: malformed RLE data"});
            }
            return make_success_value(Value{*decoded});
        })
        .func("deflate_with", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& data = expect_string(args[0], "Compression.deflate_with", loc);
            const auto level = expect_integer(args[1], "Compression.deflate_with", loc);
            if (level < compression::k_min_deflate_level ||
                level > compression::k_max_deflate_level) {
                return make_failure_value(
                    std::string{"Compression.deflate_with: level must be between 1 and 9"});
            }
            return make_success_value(
                Value{compression::deflate_compress(data, static_cast<int>(level))});
        })
        .func("gzip_with", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& data = expect_string(args[0], "Compression.gzip_with", loc);
            const auto level = expect_integer(args[1], "Compression.gzip_with", loc);
            if (level < compression::k_min_deflate_level ||
                level > compression::k_max_deflate_level) {
                return make_failure_value(
                    std::string{"Compression.gzip_with: level must be between 1 and 9"});
            }
            return make_success_value(
                Value{compression::gzip_compress(data, static_cast<int>(level))});
        })
        .func("compressed_size", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Compression.compressed_size", loc);
            const auto compressed = compression::deflate_compress(args[0].as_string());
            return Value{static_cast<std::int64_t>(compressed.size())};
        });

    if (!sandbox) {
        ModuleBuilder{"Compression", env}
            .func("gzip_file", 2)
            .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
                (void)expect_string(args[0], "Compression.gzip_file", loc);
                (void)expect_string(args[1], "Compression.gzip_file", loc);
                const auto in_path = validate_path(args[0].as_string(), loc);
                const auto out_path = validate_path(args[1].as_string(), loc);
                const auto content = file_helpers::read_file_contents(in_path, std::ios::binary);
                if (!content) {
                    return make_failure_value(
                        std::format("Compression.gzip_file: cannot open '{}'", in_path.string()));
                }
                const auto compressed = compression::gzip_compress(*content);
                if (!file_helpers::write_file_contents(out_path, compressed, std::ios::binary)) {
                    return make_failure_value(
                        std::format("Compression.gzip_file: cannot write '{}'", out_path.string()));
                }
                return make_success_value(Value{out_path.string()});
            })
            .func("gunzip_file", 1)
            .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
                (void)expect_string(args[0], "Compression.gunzip_file", loc);
                const auto safe = validate_path(args[0].as_string(), loc);
                const auto content_opt = file_helpers::read_file_contents(safe, std::ios::binary);
                if (!content_opt) {
                    return make_failure_value(
                        std::format("Compression.gunzip_file: cannot open '{}'", safe.string()));
                }
                const auto& content = *content_opt;
                const auto decompressed = compression::gzip_decompress(content);
                if (!decompressed) {
                    return make_failure_value(
                        std::string{"Compression.gunzip_file: malformed gzip data"});
                }
                return make_success_value(Value{*decompressed});
            })
            .func("gzip_file_with", 3)
            .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
                (void)expect_string(args[0], "Compression.gzip_file_with", loc);
                (void)expect_string(args[1], "Compression.gzip_file_with", loc);
                const auto safe_in = validate_path(args[0].as_string(), loc);
                const auto safe_out = validate_path(args[1].as_string(), loc);
                const auto level = expect_integer(args[2], "Compression.gzip_file_with", loc);
                if (level < compression::k_min_deflate_level ||
                    level > compression::k_max_deflate_level) {
                    return make_failure_value(
                        "Compression.gzip_file_with: level must be between 1 and 9");
                }
                const auto content = file_helpers::read_file_contents(safe_in, std::ios::binary);
                if (!content) {
                    return make_failure_value(std::format(
                        "Compression.gzip_file_with: cannot read '{}'", safe_in.string()));
                }
                const auto compressed =
                    compression::gzip_compress(*content, static_cast<int>(level));
                if (!file_helpers::write_file_contents(safe_out, compressed, std::ios::binary)) {
                    return make_failure_value(std::format(
                        "Compression.gzip_file_with: cannot write '{}'", safe_out.string()));
                }
                return make_success_value(Value{safe_out.string()});
            });
    }
}

} // namespace luma
