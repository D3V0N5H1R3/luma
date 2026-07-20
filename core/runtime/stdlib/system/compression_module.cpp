#include "runtime/stdlib/system/compression_module.hpp"

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>

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

// === Registration ===

void register_compression_ns(const EnvPtr& env, bool sandbox) {
    ModuleBuilder{"Compression", env}
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
