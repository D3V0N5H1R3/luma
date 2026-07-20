#include "runtime/stdlib/system/encoder_module.hpp"

#include <format>
#include <optional>
#include <string>

#include "analysis/source/source_location.hpp"
#include "common/base64_codec.hpp"
#include "common/url_codec.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

// Function-pointer types for the codec's public base64 encode/decode API.
using Base64Encoder = std::string (*)(const std::string&);
using Base64Decoder = std::optional<std::string> (*)(const std::string&);

// Create a native function wrapping a base64 codec encoder.  The codec enforces
// the output-size limit; safe_call converts a thrown std::length_error into a
// failure result.
[[nodiscard]] NativeFunction make_base64_encoder(std::string func_name, Base64Encoder encode) {
    return [name = std::move(func_name), encode](std::span<const Value> args,
                                                 SourceLocation loc) -> Value {
        (void)expect_string(args[0], name, loc);

        return safe_call("Encoder", name,
                         [&]() -> Value { return Value{encode(args[0].as_string())}; });
    };
}

// Create a native function wrapping a base64 codec decoder.
[[nodiscard]] NativeFunction make_base64_decoder(std::string func_name, Base64Decoder decode) {
    return [name = std::move(func_name), decode](std::span<const Value> args,
                                                 SourceLocation loc) -> Value {
        (void)expect_string(args[0], name, loc);

        auto decoded = decode(args[0].as_string());

        if (!decoded) {
            return make_failure_value(std::format("invalid {} input", name));
        }

        return make_success_value(Value{std::move(*decoded)});
    };
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Registration
// ══════════════════════════════════════════════════════════════════════════════

void register_encoder_ns(const EnvPtr& env) {
    ModuleBuilder{"Encoder", env}
        .func("encode_base64", 1)
        .raw_body(make_base64_encoder("Encoder.encode_base64", &base64_encode))
        .func("decode_base64", 1)
        .raw_body(make_base64_decoder("Encoder.decode_base64", &base64_decode))
        .func("encode_base64url", 1)
        .raw_body(make_base64_encoder("Encoder.encode_base64url", &base64url_encode))
        .func("decode_base64url", 1)
        .raw_body(make_base64_decoder("Encoder.decode_base64url", &base64url_decode))
        .func("encode_url", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Encoder.encode_url", loc);

            return safe_call("Encoder", "encode_url",
                             [&]() -> Value { return Value{url_encode(args[0].as_string())}; });
        })
        .func("decode_url", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Encoder.decode_url", loc);

            auto decoded = url_decode(args[0].as_string());

            if (!decoded) {
                return make_failure_value("invalid URL-encoded input");
            }

            return make_success_value(Value{std::move(*decoded)});
        });
}

} // namespace luma
