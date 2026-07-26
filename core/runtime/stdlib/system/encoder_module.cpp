#include "runtime/stdlib/system/encoder_module.hpp"

#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/base64_codec.hpp"
#include "common/url_codec.hpp"
#include "common/utf8.hpp"
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

// ─── Text-encoding selector (Encoder.Encoding) ───

enum class TextEncoding {
    Utf8,
    Ascii,
    Latin1
};

// Map an Encoder.Encoding choice variant name to the enum.  Variant names must
// match the Encoder.Encoding choice in stdlib_type_arities.cpp exactly.
[[nodiscard]] std::optional<TextEncoding> encoding_from_variant(std::string_view variant) {
    if (variant == "Utf8") {
        return TextEncoding::Utf8;
    }
    if (variant == "Ascii") {
        return TextEncoding::Ascii;
    }
    if (variant == "Latin1") {
        return TextEncoding::Latin1;
    }

    return std::nullopt;
}

// Extract the TextEncoding from an Encoder.Encoding choice argument, throwing a
// RuntimeError when it is not a genuine Encoding variant.
[[nodiscard]] TextEncoding expect_encoding(const Value& arg, std::string_view func,
                                           const SourceLocation& loc) {
    if (!arg.is_choice()) {
        throw RuntimeError{std::string{func} + ": expected an Encoder.Encoding choice", loc,
                           "pass Encoder.Encoding.Utf8, .Ascii, or .Latin1"};
    }

    const auto encoding = encoding_from_variant(arg.as_choice()->variant);

    if (!encoding.has_value()) {
        throw RuntimeError{std::string{func} + ": unknown encoding 'Encoder.Encoding." +
                               arg.as_choice()->variant + "'",
                           loc, "use Encoder.Encoding.Utf8, .Ascii, or .Latin1"};
    }

    return *encoding;
}

// Strictly validate that `bytes` is well-formed UTF-8: correct sequence lengths,
// continuation bytes, no overlong forms, no surrogates, and codepoints within
// U+10FFFF.
[[nodiscard]] bool is_valid_utf8(std::string_view bytes) {
    std::size_t pos = 0;

    while (pos < bytes.size()) {
        const auto lead = static_cast<std::uint8_t>(bytes[pos]);

        std::size_t len = 0;
        std::uint32_t cp = 0;
        std::uint32_t min_cp = 0;

        if (lead < 0x80) {
            ++pos;
            continue;
        }
        if ((lead & 0xE0) == 0xC0) {
            len = 2;
            cp = lead & 0x1FU;
            min_cp = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            len = 3;
            cp = lead & 0x0FU;
            min_cp = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            len = 4;
            cp = lead & 0x07U;
            min_cp = 0x10000;
        } else {
            return false; // invalid lead byte (continuation or > 4-byte)
        }

        if (pos + len > bytes.size()) {
            return false;
        }

        for (std::size_t k = 1; k < len; ++k) {
            const auto cont = static_cast<std::uint8_t>(bytes[pos + k]);
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (cont & 0x3FU);
        }

        if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false; // overlong, out of range, or surrogate
        }

        pos += len;
    }

    return true;
}

// Read an array<integer> argument as raw bytes, failing (via the returned
// optional error message) when an element is not an integer in 0–255.
[[nodiscard]] std::optional<std::string> read_bytes(std::span<const Value> elements,
                                                    std::string_view func, std::string& out) {
    out.clear();
    out.reserve(elements.size());

    for (const auto& element : elements) {
        if (!element.is_integer()) {
            return std::format("{}: byte array must contain integers", func);
        }

        const auto byte = element.as_integer();

        if (byte < 0 || byte > 255) {
            return std::format("{}: byte {} is out of range (0–255)", func, byte);
        }

        out += static_cast<char>(static_cast<std::uint8_t>(byte));
    }

    return std::nullopt;
}

// Build a Luma array<integer> Value from raw bytes.
[[nodiscard]] Value bytes_to_array(std::string_view bytes) {
    auto arr = std::make_shared<ArrayValue>();
    arr->elements->reserve(bytes.size());

    for (const char c : bytes) {
        arr->elements->push_back(Value{static_cast<std::int64_t>(static_cast<std::uint8_t>(c))});
    }

    return Value{std::move(arr)};
}

// ─── Typed decode errors (Encoder.Error) ───

// Wrap an Encoder.Error variant name in a ChoiceValue.  Runtime short name
// "Error" mirrors make_io_error_choice (the type checker resolves the qualified
// "Encoder.Error" separately).  The four variant names must match the
// ChoiceDeclaration in core/analysis/types/stdlib_type_arities.cpp exactly.
[[nodiscard]] Value make_encoder_error_choice(std::string_view variant) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "Error";
    cv->variant = std::string{variant};

    return Value{std::move(cv)};
}

// Build a result<string, Encoder.Error> failure carrying the typed error choice.
[[nodiscard]] Value make_encoder_error_failure(std::string_view variant) {
    return Value{ResultValue::failure(make_encoder_error_choice(variant))};
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
        })
        .func("encode_text", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& text = expect_string(args[0], "Encoder.encode_text", loc);
            const auto encoding = expect_encoding(args[1], "Encoder.encode_text", loc);

            // A Luma string is already UTF-8, so UTF-8 encoding is a direct
            // byte copy; ASCII and Latin-1 walk the codepoints and reject any
            // that fall outside their range.
            if (encoding == TextEncoding::Utf8) {
                return make_success_value(bytes_to_array(text));
            }

            const std::uint32_t max_cp = (encoding == TextEncoding::Ascii) ? 0x7F : 0xFF;
            const std::string_view label = (encoding == TextEncoding::Ascii) ? "ASCII" : "Latin-1";

            std::string bytes;
            std::size_t pos = 0;

            while (pos < text.size()) {
                const std::uint32_t cp = utf8_decode_at(text, pos);

                if (cp > max_cp) {
                    return make_failure_value(std::format(
                        "Encoder.encode_text: codepoint U+{:04X} is not representable in {}", cp,
                        label));
                }

                bytes += static_cast<char>(static_cast<std::uint8_t>(cp));
                pos += static_cast<std::size_t>(
                    utf8_codepoint_len(static_cast<std::uint8_t>(text[pos])));
            }

            return make_success_value(bytes_to_array(bytes));
        })
        .func("decode_text", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& array = expect_array(args[0], "Encoder.decode_text", loc);
            const auto encoding = expect_encoding(args[1], "Encoder.decode_text", loc);

            std::string bytes;
            if (auto err = read_bytes(*array->elements, "Encoder.decode_text", bytes)) {
                return make_failure_value(std::move(*err));
            }

            switch (encoding) {
                case TextEncoding::Utf8:
                    if (!is_valid_utf8(bytes)) {
                        return make_failure_value("Encoder.decode_text: bytes are not valid UTF-8");
                    }
                    return make_success_value(Value{std::move(bytes)});

                case TextEncoding::Ascii:
                    for (const char c : bytes) {
                        if (static_cast<std::uint8_t>(c) > 0x7F) {
                            return make_failure_value(
                                "Encoder.decode_text: byte is not valid ASCII (0–127)");
                        }
                    }
                    return make_success_value(Value{std::move(bytes)});

                case TextEncoding::Latin1: {
                    // Each Latin-1 byte is a codepoint U+0000–U+00FF; re-encode
                    // as UTF-8 so the result is a well-formed Luma string.
                    std::string utf8;
                    for (const char c : bytes) {
                        utf8 += utf8_encode(static_cast<std::uint8_t>(c));
                    }
                    return make_success_value(Value{std::move(utf8)});
                }
            }

            // Unreachable — expect_encoding rejects any other variant.
            return make_failure_value("Encoder.decode_text: unknown encoding");
        })
        // ── Opt-in typed-error decode slice (Encoder.Error) ───
        // decode_base64_typed / decode_url_typed / decode_text_typed mirror their
        // string-error counterparts but surface an Encoder.Error choice
        // (result<string, Encoder.Error>) so a caller can branch on *why* a decode
        // failed.  The string-error decoders are left untouched.
        .func("decode_base64_typed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Encoder.decode_base64_typed", loc);

            auto decoded = base64_decode(args[0].as_string());

            if (!decoded) {
                return make_encoder_error_failure("InvalidBase64");
            }

            return make_success_value(Value{std::move(*decoded)});
        })
        .func("decode_url_typed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Encoder.decode_url_typed", loc);

            auto decoded = url_decode(args[0].as_string());

            if (!decoded) {
                return make_encoder_error_failure("InvalidPercentEncoding");
            }

            return make_success_value(Value{std::move(*decoded)});
        })
        .func("decode_text_typed", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& array = expect_array(args[0], "Encoder.decode_text_typed", loc);
            const auto encoding = expect_encoding(args[1], "Encoder.decode_text_typed", loc);

            // Bytes outside 0–255 are not a valid byte stream for any encoding;
            // classify that as the byte-domain failure for the requested
            // encoding (ASCII → InvalidAscii, otherwise InvalidUtf8).
            std::string bytes;
            if (read_bytes(*array->elements, "Encoder.decode_text_typed", bytes)) {
                return make_encoder_error_failure(encoding == TextEncoding::Ascii ? "InvalidAscii"
                                                                                  : "InvalidUtf8");
            }

            switch (encoding) {
                case TextEncoding::Utf8:
                    if (!is_valid_utf8(bytes)) {
                        return make_encoder_error_failure("InvalidUtf8");
                    }
                    return make_success_value(Value{std::move(bytes)});

                case TextEncoding::Ascii:
                    for (const char c : bytes) {
                        if (static_cast<std::uint8_t>(c) > 0x7F) {
                            return make_encoder_error_failure("InvalidAscii");
                        }
                    }
                    return make_success_value(Value{std::move(bytes)});

                case TextEncoding::Latin1: {
                    // Every 0–255 byte is a valid Latin-1 codepoint U+0000–U+00FF;
                    // re-encode as UTF-8 so the result is a well-formed Luma string
                    // (this branch cannot fail).
                    std::string utf8;
                    for (const char c : bytes) {
                        utf8 += utf8_encode(static_cast<std::uint8_t>(c));
                    }
                    return make_success_value(Value{std::move(utf8)});
                }
            }

            // Unreachable — expect_encoding rejects any other variant.
            return make_encoder_error_failure("InvalidUtf8");
        });
}

} // namespace luma
