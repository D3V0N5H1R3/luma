#ifndef LUMA_FUZZ_FUZZ_GEN_VOCAB_HPP
#define LUMA_FUZZ_FUZZ_GEN_VOCAB_HPP

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <tuple>
#include <type_traits>

// Token vocabulary for the structured Luma-source generator.
//
// Fixed arrays of type names, identifiers, operators and stdlib call fragments,
// plus the `pick` / `pick_kind` helpers that select a uniformly random element
// and the small generate_* wrappers built on top of them.  These are the shared
// building blocks that the chaos grammar (fuzz_gen_grammar.hpp), the well-typed
// grammar (fuzz_gen_typed.hpp), the feature and stdlib emitters
// (fuzz_gen_features.hpp, fuzz_gen_stdlib.hpp) and the program builders
// (fuzz_gen_program.hpp) all draw from.  See fuzz_source_generator.hpp for the
// Provider concept these templates require.

namespace luma::fuzz::gen {

inline constexpr std::array k_types = {"integer",
                                       "number",
                                       "string",
                                       "boolean",
                                       "array<integer>",
                                       "dictionary<string>",
                                       "none",
                                       "optional<integer>",
                                       "result<integer>",
                                       "(integer, string)"};

// Shared identifier pool for the chaos grammar.  Every entry must be a legal
// identifier: a reserved word here (`result` once was) turns most generated
// programs into syntax errors before they reach the type checker.
inline constexpr std::array k_identifiers = {"x",    "y",    "z",     "n",     "acc",
                                             "item", "res",  "value", "count", "total",
                                             "flag", "temp", "data",  "list",  "idx"};

// Binary operators the language accepts. The bitwise operators (`& | ^ << >>`)
// were removed (R06) — bit manipulation moved to the Bits module — so they are
// intentionally absent here to keep generated programs valid past the parser.
inline constexpr std::array k_binops = {
    "+", "-", "*", "/", "//", "%", "==", "!=", "<", ">", "<=", ">=", "&&", "||"};

// Prefix unary operators. `~` (bitwise not) was removed with the other bitwise
// operators (R06).
inline constexpr std::array k_unary_ops = {"-", "!"};

inline constexpr std::array k_variant_names = {"Alpha", "Beta", "Gamma", "Delta"};

// Method-call fragments valid on the right-hand side of a pipe (|>) or an
// error pipe (!>).  The piped value becomes the call's implicit first argument.
// Type mismatches are tolerated: the goal is to exercise the pipe operators
// across the parser, type checker, and compiler, not to produce well-typed
// programs.  The String stages are deliberately varied so fuzzer-generated
// string literals reach the UTF-8 transform, decomposition, and validation
// paths in the VM.
inline constexpr std::array k_pipe_stages = {
    "String.uppercase()", "String.lowercase()",  "String.length()",        "String.byte_length()",
    "String.reverse()",   "String.trim()",       "String.title_case()",    "String.to_snake_case()",
    "String.slug()",      "String.characters()", "String.to_codepoints()", "String.is_palindrome()",
    "Array.length()",     "Array.reverse()",     "Math.absolute()"};

// Standard-library data-format decoders that stay available in sandbox mode,
// which is how every fuzz target executes.  Driven with a generated string
// literal so the hand-written parser behind them is reached through the VM.
// (Csv and Xml are OS-only modules — absent under the sandbox — and have their
// own direct targets, fuzz_csv and fuzz_xml.)
inline constexpr std::array k_decoders = {"Json.deserialize", "Json.is_valid"};

// Converter string parsers.  Each takes a single string and runs a hand-written
// scanner over it — the roman/base/numeric parsers and, for
// character_to_codepoint, a UTF-8 decoder with manual offset and bit
// arithmetic.  Driving them with a generated string literal reaches those
// parsers through the VM on adversarial input, the same trust-boundary
// rationale as the data-format decoders above.
inline constexpr std::array k_converter_string_parsers = {"Converter.to_boolean",
                                                          "Converter.to_integer",
                                                          "Converter.to_number",
                                                          "Converter.from_hexadecimal",
                                                          "Converter.from_binary",
                                                          "Converter.from_roman",
                                                          "Converter.character_to_codepoint"};

// Converter integer formatters.  Each takes a single integer and exercises the
// base/roman/word/ordinal encoders and the UTF-8 codepoint encoder, including
// their range guards and the signed-minimum special cases.
inline constexpr std::array k_converter_int_formatters = {
    "Converter.to_hexadecimal", "Converter.to_binary",       "Converter.ordinal",
    "Converter.to_roman",       "Converter.number_to_words", "Converter.codepoint_to_character"};

// Pick a uniformly random element of a fixed, non-empty array.
template <typename Provider, typename Array>
[[nodiscard]] auto pick(Provider& fdp, const Array& options) {
    static_assert(std::tuple_size_v<Array> > 0, "pick needs at least one option");
    return options[fdp.template ConsumeIntegralInRange<std::size_t>(0, options.size() - 1)];
}

// Pick a uniformly random enumerator of an `enum class` whose final enumerator
// is the sentinel `Count`.  Dispatching on the result with a `switch` that has
// no `default:` lets -Wswitch flag any enumerator the switch forgets, so the
// number of choices can never drift from the number of handled cases.
template <typename Kind, typename Provider> [[nodiscard]] Kind pick_kind(Provider& fdp) {
    using Underlying = std::underlying_type_t<Kind>;
    constexpr auto count = static_cast<Underlying>(Kind::Count);
    static_assert(count > 0, "an enum dispatched by pick_kind needs at least one kind");
    return static_cast<Kind>(fdp.template ConsumeIntegralInRange<Underlying>(0, count - 1));
}

// pick_kind never yields the `Count` sentinel; reaching the code after a kind
// switch therefore means the switch and the enum disagree.
[[noreturn]] inline void unreachable_kind() {
    std::abort();
}

template <typename Provider> [[nodiscard]] std::string generate_type(Provider& fdp) {
    return pick(fdp, k_types);
}

template <typename Provider> [[nodiscard]] std::string generate_identifier(Provider& fdp) {
    return pick(fdp, k_identifiers);
}

template <typename Provider> [[nodiscard]] std::string generate_binop(Provider& fdp) {
    return pick(fdp, k_binops);
}

template <typename Provider> [[nodiscard]] std::string generate_unaryop(Provider& fdp) {
    return pick(fdp, k_unary_ops);
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

// Build a guaranteed-valid Luma string literal from fuzzer bytes.  Luma only
// recognises the escapes \n \t \r \0 \\ \" and \$ and rejects raw control or
// non-ASCII bytes, so each character is either emitted as a safe escape, kept
// as printable ASCII, or dropped.  The decoded runtime string still carries the
// structural characters ({ } [ ] : , " \) that drive the data-format parsers.
template <typename Provider> [[nodiscard]] std::string generate_string_literal(Provider& fdp) {
    const auto raw = fdp.ConsumeRandomLengthString(32);
    std::string literal = "\"";
    for (const char ch : raw) {
        switch (ch) {
            case '"':
                literal += "\\\"";
                break;
            case '\\':
                literal += "\\\\";
                break;
            case '\n':
                literal += "\\n";
                break;
            case '\t':
                literal += "\\t";
                break;
            case '\r':
                literal += "\\r";
                break;
            case '$':
                literal += "\\$";
                break;
            default: {
                const auto byte = static_cast<unsigned char>(ch);
                if (byte >= 0x20U && byte < 0x7FU) {
                    literal += ch;
                }
                break;
            }
        }
    }
    literal += "\"";
    return literal;
}

} // namespace luma::fuzz::gen

#endif // LUMA_FUZZ_FUZZ_GEN_VOCAB_HPP
