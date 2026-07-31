#pragma once

#include <array>
#include <cstddef>
#include <string>

// Token vocabulary for the structured Luma-source generator.
//
// Fixed arrays of type names, identifiers, operators and stdlib call fragments,
// plus the `pick` helper that selects a uniformly random element and the small
// generate_* wrappers built on top of it.  These are the shared building blocks
// that the grammar (fuzz_gen_grammar.hpp), the type-correct stdlib emitters
// (fuzz_gen_stdlib.hpp) and the declaration/program builders (fuzz_gen_program.hpp)
// all draw from.  See fuzz_source_generator.hpp for the Provider concept these
// templates require.

namespace luma::fuzz::gen {

inline constexpr std::array types = {"integer",
                                     "number",
                                     "string",
                                     "boolean",
                                     "array<integer>",
                                     "dictionary<string>",
                                     "none",
                                     "optional<integer>",
                                     "result<integer>",
                                     "(integer, string)"};

inline constexpr std::array identifiers = {"x",    "y",      "z",     "n",     "acc",
                                           "item", "result", "value", "count", "total",
                                           "flag", "temp",   "data",  "list",  "idx"};

// Binary operators the language accepts. The bitwise operators (`& | ^ << >>`)
// were removed (R06) — bit manipulation moved to the Bits module — so they are
// intentionally absent here to keep generated programs valid past the parser.
inline constexpr std::array binops = {
    "+", "-", "*", "/", "//", "%", "==", "!=", "<", ">", "<=", ">=", "&&", "||"};

// Prefix unary operators. `~` (bitwise not) was removed with the other bitwise
// operators (R06).
inline constexpr std::array unary_ops = {"-", "!"};

inline constexpr std::array variant_names = {"Alpha", "Beta", "Gamma", "Delta"};

// Method-call fragments valid on the right-hand side of a pipe (|>) or an
// error pipe (!>).  The piped value becomes the call's implicit first argument.
// Type mismatches are tolerated: the goal is to exercise the pipe operators
// across the parser, type checker, and compiler, not to produce well-typed
// programs.  The String stages are deliberately varied so fuzzer-generated
// string literals reach the UTF-8 transform, decomposition, and validation
// paths in the VM.
inline constexpr std::array pipe_stages = {
    "String.uppercase()", "String.lowercase()",  "String.length()",        "String.byte_length()",
    "String.reverse()",   "String.trim()",       "String.title_case()",    "String.to_snake_case()",
    "String.slug()",      "String.characters()", "String.to_codepoints()", "String.is_palindrome()",
    "Array.length()",     "Array.reverse()",     "Math.absolute()"};

// Standard-library data-format decoders.  Driven with a generated string
// literal so the underlying hand-written parsers (Json/Csv/Xml) are reached and
// stress-tested through the VM.
inline constexpr std::array decoders = {"Json.deserialize", "Csv.deserialize", "Xml.deserialize"};

// Converter string parsers.  Each takes a single string and runs a hand-written
// scanner over it — the roman/base/numeric parsers and, for
// character_to_codepoint, a UTF-8 decoder with manual offset and bit
// arithmetic.  Driving them with a generated string literal reaches those
// parsers through the VM on adversarial input, the same trust-boundary
// rationale as the data-format decoders above.
inline constexpr std::array converter_string_parsers = {"Converter.to_boolean",
                                                        "Converter.to_integer",
                                                        "Converter.to_number",
                                                        "Converter.from_hexadecimal",
                                                        "Converter.from_binary",
                                                        "Converter.from_roman",
                                                        "Converter.character_to_codepoint"};

// Converter integer formatters.  Each takes a single integer and exercises the
// base/roman/word/ordinal encoders and the UTF-8 codepoint encoder, including
// their range guards and the signed-minimum special cases.
inline constexpr std::array converter_int_formatters = {
    "Converter.to_hexadecimal", "Converter.to_binary",       "Converter.ordinal",
    "Converter.to_roman",       "Converter.number_to_words", "Converter.codepoint_to_character"};

// Pick a uniformly random element of a fixed array.
template <typename Provider, typename Array>
[[nodiscard]] auto pick(Provider& fdp, const Array& options) {
    return options[fdp.template ConsumeIntegralInRange<std::size_t>(0, options.size() - 1)];
}

template <typename Provider> [[nodiscard]] std::string generate_type(Provider& fdp) {
    return pick(fdp, types);
}

template <typename Provider> [[nodiscard]] std::string generate_identifier(Provider& fdp) {
    return pick(fdp, identifiers);
}

template <typename Provider> [[nodiscard]] std::string generate_binop(Provider& fdp) {
    return pick(fdp, binops);
}

template <typename Provider> [[nodiscard]] std::string generate_unaryop(Provider& fdp) {
    return pick(fdp, unary_ops);
}

// Build a fresh "N.0" number literal from an fdp-chosen integer in [-100, 100].
// Threaded into generated source wherever a well-typed number literal is needed
// (the LinearAlgebra and Math emitters build their operands from it).
template <typename Provider> [[nodiscard]] std::string generate_number_literal(Provider& fdp) {
    return std::to_string(fdp.template ConsumeIntegralInRange<int>(-100, 100)) + ".0";
}

// Build a fresh integer literal drawn from the closed range [lo, hi].
template <typename Provider>
[[nodiscard]] std::string generate_int_literal(Provider& fdp, int lo, int hi) {
    return std::to_string(fdp.template ConsumeIntegralInRange<int>(lo, hi));
}

// Draw a collision-avoiding numeric suffix for generated binding names, keeping
// them distinct from the shared identifier pool.
template <typename Provider> [[nodiscard]] std::string fresh_suffix(Provider& fdp) {
    return std::to_string(fdp.template ConsumeIntegralInRange<int>(0, 100000));
}

} // namespace luma::fuzz::gen
