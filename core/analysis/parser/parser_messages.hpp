#ifndef LUMA_PARSER_PARSER_MESSAGES_HPP
#define LUMA_PARSER_PARSER_MESSAGES_HPP

#include <string_view>

// Centralised string constants for all parser diagnostic messages.
// Every user-visible string emitted by the parser lives here, grouped by
// the part of the grammar or the call-site that produces it.

namespace luma::parser_msg {

// ── Token expectation hints (used in expect_hint()) ──────────────────────────
// Returned when a specific delimiter or punctuation token is missing.

inline constexpr std::string_view hint_missing_right_paren = "did you forget a closing ')'?";

inline constexpr std::string_view hint_missing_right_brace = "did you forget a closing '}'?";

inline constexpr std::string_view hint_missing_right_bracket = "did you forget a closing ']'?";

inline constexpr std::string_view hint_expect_left_paren =
    "a '(' is required here to start the parameter list";

inline constexpr std::string_view hint_expect_left_brace =
    "a '{' is required here to start the block body";

inline constexpr std::string_view hint_expect_equals = "did you forget '=' to assign a value?";

inline constexpr std::string_view hint_expect_colon =
    "a ':' is expected here to separate the name from the type";

inline constexpr std::string_view hint_expect_arrow = "use '->' to specify the return type";

inline constexpr std::string_view hint_expect_identifier =
    "expected a name here (variable, function, or type name)";

// ── Foreign-keyword hints (used in Parser::foreign_keyword_hint) ──────────
// Shown when the user types a keyword from another language.
// Entries containing '{}' have the matched keyword substituted in at runtime.

// const
inline constexpr std::string_view hint_immutable_by_default =
    "Luma variables are immutable by default; "
    "use 'type name = value', e.g. 'integer x = 10'";

// enum
inline constexpr std::string_view hint_did_you_mean_choice =
    "did you mean 'choice'? Luma uses 'choice' for algebraic data types";

// fn / func / def / sub
inline constexpr std::string_view hint_did_you_mean_function =
    "did you mean 'function'? Luma uses 'function' for function declarations (not '{}')";

// class / struct
inline constexpr std::string_view hint_did_you_mean_record =
    "did you mean 'record'? Luma uses 'record' for data types (not '{}')";

// trait / protocol / abstract
inline constexpr std::string_view hint_did_you_mean_interface =
    "did you mean 'interface'? Luma uses 'interface' for type constraints (not '{}')";

// import / require
inline constexpr std::string_view hint_did_you_mean_include =
    "did you mean 'include'? Luma uses 'include \"path\"' to include files (not '{}')";

// var / let / val
inline constexpr std::string_view hint_var_decl =
    "Luma does not use '{}' for variable declarations; "
    "use 'type name = value', e.g. 'integer x = 10', "
    "or 'mutable integer x = 10' for mutable variables";

// elif / elsif
inline constexpr std::string_view hint_use_else_if =
    "Luma uses 'else if' (two words) for else-if branches";

// ── Match-arm hints ───────────────────────────────────────────────────────

inline constexpr std::string_view hint_split_match_arms = "split into separate match arms instead";

} // namespace luma::parser_msg

#endif // LUMA_PARSER_PARSER_MESSAGES_HPP
