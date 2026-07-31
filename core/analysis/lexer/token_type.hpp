#ifndef LUMA_LEXER_TOKEN_TYPE_HPP
#define LUMA_LEXER_TOKEN_TYPE_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <string_view>

namespace luma {

enum class TokenType {
    // Literals
    BooleanLiteral,
    IntegerLiteral,
    NoneLiteral,
    NumberLiteral,
    StringLiteral,

    // String interpolation
    InterpolationEnd,
    InterpolationStart,
    StringEnd,
    StringMiddle,
    StringStart,

    // Identifier
    Identifier,

    // Keywords
    Await,
    Borrow,
    Break,
    Case,
    Catch,
    Choice,
    Continue,
    Downcast,
    Else,
    Failure,
    Finally,
    For,
    Function,
    If,
    In,
    Include,
    Interface,
    Internal,
    Is,
    Match,
    Mutable,
    Namespace,
    Record,
    Return,
    Some,
    Spawn,
    Success,
    TaskScope,
    TrustedDowncast,
    Try,
    Type,
    Unique,
    Use,
    While,
    With,

    // Type keywords.  Only the primitives and the types with literal/inference
    // sugar remain reserved words; the container/handle types (queue, stack,
    // set, task, channel, socket, xml, …) are ordinary identifiers that the
    // parser recognises as type names via is_builtin_type_identifier().
    ArrayType,
    BooleanType,
    DecimalType,
    DictionaryType,
    IntegerType,
    NumberType,
    OptionalType,
    ResultType,
    StringType,

    // Operators
    AmpersandAmpersand,
    Arrow,
    Bang,
    BangEquals,
    BangGreater,
    DotDot,
    DotDotEquals,
    Equals,
    EqualsEquals,
    Greater,
    GreaterEquals,
    GreaterGreater,
    Less,
    LessEquals,
    Minus,
    MinusEquals,
    MinusMinus,
    Percent,
    PercentEquals,
    Pipe,
    PipeGreater,
    PipePipe,
    Plus,
    PlusEquals,
    PlusPlus,
    QuestionBracket,
    QuestionDot,
    QuestionMark,
    QuestionQuestion,
    Slash,
    SlashEquals,
    SlashSlash,
    SlashSlashEquals,
    Star,
    StarEquals,

    // Punctuation
    Colon,
    ColonColon,
    Comma,
    Dot,
    LeftBrace,
    LeftBracket,
    LeftParen,
    RightBrace,
    RightBracket,
    RightParen,

    // Annotations
    Annotation,

    // Special
    Error, // synthetic recovery token emitted when the lexer encounters an unrecognised input
    EndOfFile,

    // Sentinel (must remain last)
    Count_
};

// Indexed by the underlying value of TokenType — order must match the enum exactly.
inline constexpr auto k_token_type_names = std::to_array<std::string_view>({
    // Literals
    "boolean literal", // BooleanLiteral
    "integer literal", // IntegerLiteral
    "none",            // NoneLiteral
    "number literal",  // NumberLiteral
    "string literal",  // StringLiteral

    // String interpolation
    "}",             // InterpolationEnd
    "${",            // InterpolationStart
    "string end",    // StringEnd
    "string middle", // StringMiddle
    "string start",  // StringStart

    // Identifier
    "identifier", // Identifier

    // Keywords
    "await",            // Await
    "borrow",           // Borrow
    "break",            // Break
    "case",             // Case
    "catch",            // Catch
    "choice",           // Choice
    "continue",         // Continue
    "downcast",         // Downcast
    "else",             // Else
    "failure",          // Failure
    "finally",          // Finally
    "for",              // For
    "function",         // Function
    "if",               // If
    "in",               // In
    "include",          // Include
    "interface",        // Interface
    "internal",         // Internal
    "is",               // Is
    "match",            // Match
    "mutable",          // Mutable
    "namespace",        // Namespace
    "record",           // Record
    "return",           // Return
    "some",             // Some
    "spawn",            // Spawn
    "success",          // Success
    "task_scope",       // TaskScope
    "trusted_downcast", // TrustedDowncast
    "try",              // Try
    "type",             // Type
    "unique",           // Unique
    "use",              // Use
    "while",            // While
    "with",             // With

    // Type keywords
    "array",      // ArrayType
    "boolean",    // BooleanType
    "decimal",    // DecimalType
    "dictionary", // DictionaryType
    "integer",    // IntegerType
    "number",     // NumberType
    "optional",   // OptionalType
    "result",     // ResultType
    "string",     // StringType

    // Operators
    "&&",  // AmpersandAmpersand
    "->",  // Arrow
    "!",   // Bang
    "!=",  // BangEquals
    "!>",  // BangGreater
    "..",  // DotDot
    "..=", // DotDotEquals
    "=",   // Equals
    "==",  // EqualsEquals
    ">",   // Greater
    ">=",  // GreaterEquals
    ">>",  // GreaterGreater
    "<",   // Less
    "<=",  // LessEquals
    "-",   // Minus
    "-=",  // MinusEquals
    "--",  // MinusMinus
    "%",   // Percent
    "%=",  // PercentEquals
    "|",   // Pipe
    "|>",  // PipeGreater
    "||",  // PipePipe
    "+",   // Plus
    "+=",  // PlusEquals
    "++",  // PlusPlus
    "?[",  // QuestionBracket
    "?.",  // QuestionDot
    "?",   // QuestionMark
    "??",  // QuestionQuestion
    "/",   // Slash
    "/=",  // SlashEquals
    "//",  // SlashSlash
    "//=", // SlashSlashEquals
    "*",   // Star
    "*=",  // StarEquals

    // Punctuation
    ":",  // Colon
    "::", // ColonColon
    ",",  // Comma
    ".",  // Dot
    "{",  // LeftBrace
    "[",  // LeftBracket
    "(",  // LeftParen
    "}",  // RightBrace
    "]",  // RightBracket
    ")",  // RightParen

    // Annotations
    "annotation", // Annotation

    // Special
    "error",       // Error
    "end of file", // EndOfFile
});

static_assert(k_token_type_names.size() == static_cast<std::size_t>(TokenType::Count_),
              "k_token_type_names must have one entry per TokenType");

[[nodiscard]] constexpr std::string_view token_type_to_string(TokenType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    if (index >= k_token_type_names.size()) {
        return "unknown";
    }
    return k_token_type_names[index];
}

// ─── Canonical keyword table ───
//
// Single source of truth for the language's word-spelled tokens: every keyword,
// type keyword, and word-literal (true/false/none).  Each row pairs a source
// spelling with its TokenType and a single classification kind.  The lexer's
// keyword_type() binary-searches this table (hence the sorted-by-spelling
// invariant checked below), and the three keyword predicates are derived from it
// via k_keyword_classifications — so adding a keyword is a single new row here
// (alongside its enumerator and k_token_type_names entry).
//
// `true` and `false` share the BooleanLiteral type, so the table has one more
// row than the number of distinct keyword TokenTypes.  This table carries source
// spellings; k_token_type_names remains the separate diagnostic-description table
// ("boolean literal", not "true"/"false").

// How a keyword participates in the classification predicates.  Every kind is a
// keyword; the distinction is whether it can also open a type expression.
enum class KeywordKind {
    Word,        // keyword or word-literal only (if, mutable, true, …)
    BuiltinType, // built-in type keyword; also a type keyword (array, string, …)
    TypeWord,    // type-position keyword, not a built-in type (function, none)
};

struct KeywordSpelling {
    std::string_view spelling;
    TokenType type;
    KeywordKind kind;
};

inline constexpr auto k_keywords = std::to_array<KeywordSpelling>({
    {"array", TokenType::ArrayType, KeywordKind::BuiltinType},
    {"await", TokenType::Await, KeywordKind::Word},
    {"boolean", TokenType::BooleanType, KeywordKind::BuiltinType},
    {"borrow", TokenType::Borrow, KeywordKind::Word},
    {"break", TokenType::Break, KeywordKind::Word},
    {"case", TokenType::Case, KeywordKind::Word},
    {"catch", TokenType::Catch, KeywordKind::Word},
    {"choice", TokenType::Choice, KeywordKind::Word},
    {"continue", TokenType::Continue, KeywordKind::Word},
    {"decimal", TokenType::DecimalType, KeywordKind::BuiltinType},
    {"dictionary", TokenType::DictionaryType, KeywordKind::BuiltinType},
    {"downcast", TokenType::Downcast, KeywordKind::Word},
    {"else", TokenType::Else, KeywordKind::Word},
    {"failure", TokenType::Failure, KeywordKind::Word},
    {"false", TokenType::BooleanLiteral, KeywordKind::Word},
    {"finally", TokenType::Finally, KeywordKind::Word},
    {"for", TokenType::For, KeywordKind::Word},
    {"function", TokenType::Function, KeywordKind::TypeWord},
    {"if", TokenType::If, KeywordKind::Word},
    {"in", TokenType::In, KeywordKind::Word},
    {"include", TokenType::Include, KeywordKind::Word},
    {"integer", TokenType::IntegerType, KeywordKind::BuiltinType},
    {"interface", TokenType::Interface, KeywordKind::Word},
    {"internal", TokenType::Internal, KeywordKind::Word},
    {"is", TokenType::Is, KeywordKind::Word},
    {"match", TokenType::Match, KeywordKind::Word},
    {"mutable", TokenType::Mutable, KeywordKind::Word},
    {"namespace", TokenType::Namespace, KeywordKind::Word},
    {"none", TokenType::NoneLiteral, KeywordKind::TypeWord},
    {"number", TokenType::NumberType, KeywordKind::BuiltinType},
    {"optional", TokenType::OptionalType, KeywordKind::BuiltinType},
    {"record", TokenType::Record, KeywordKind::Word},
    {"result", TokenType::ResultType, KeywordKind::BuiltinType},
    {"return", TokenType::Return, KeywordKind::Word},
    {"some", TokenType::Some, KeywordKind::Word},
    {"spawn", TokenType::Spawn, KeywordKind::Word},
    {"string", TokenType::StringType, KeywordKind::BuiltinType},
    {"success", TokenType::Success, KeywordKind::Word},
    {"task_scope", TokenType::TaskScope, KeywordKind::Word},
    {"true", TokenType::BooleanLiteral, KeywordKind::Word},
    {"trusted_downcast", TokenType::TrustedDowncast, KeywordKind::Word},
    {"try", TokenType::Try, KeywordKind::Word},
    {"type", TokenType::Type, KeywordKind::Word},
    {"unique", TokenType::Unique, KeywordKind::Word},
    {"use", TokenType::Use, KeywordKind::Word},
    {"while", TokenType::While, KeywordKind::Word},
    {"with", TokenType::With, KeywordKind::Word},
});

// keyword_type() binary-searches k_keywords, so it must stay sorted by spelling
// (strictly ascending, which also rejects duplicate spellings).
constexpr bool keywords_are_sorted() noexcept {
    std::string_view previous{};
    bool first{true};

    for (const auto& entry : k_keywords) {
        if (!first && !(previous < entry.spelling)) {
            return false;
        }

        previous = entry.spelling;
        first = false;
    }

    return true;
}

static_assert(keywords_are_sorted(),
              "k_keywords must be sorted by spelling for keyword_type()'s binary search");

// Per-TokenType view of the classification flags, indexed by the underlying enum
// value.  Derived from k_keywords at compile time so the predicates below are
// O(1) and can never drift from the spellings the lexer actually accepts.
struct KeywordClassification {
    bool is_keyword{false};
    bool is_builtin_type{false};
    bool is_type_keyword{false};
};

inline constexpr std::array<KeywordClassification, static_cast<std::size_t>(TokenType::Count_)>
build_keyword_classifications() noexcept {
    std::array<KeywordClassification, static_cast<std::size_t>(TokenType::Count_)> table{};

    for (const auto& entry : k_keywords) {
        auto& slot = table[static_cast<std::size_t>(entry.type)];
        slot.is_keyword = true;
        slot.is_builtin_type = entry.kind == KeywordKind::BuiltinType;
        slot.is_type_keyword =
            entry.kind == KeywordKind::BuiltinType || entry.kind == KeywordKind::TypeWord;
    }

    return table;
}

inline constexpr auto k_keyword_classifications = build_keyword_classifications();

// Returns true if the token type is any word-spelled keyword: every keyword,
// built-in type keyword, and word-literal (true/false/none).  This is the
// superset of the two type predicates below.
[[nodiscard]] constexpr bool is_keyword_token_type(TokenType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < k_keyword_classifications.size() && k_keyword_classifications[index].is_keyword;
}

// Returns true if the token type is a built-in type keyword
// (array, boolean, integer, string, etc.).
[[nodiscard]] constexpr bool is_builtin_type_token_type(TokenType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < k_keyword_classifications.size() &&
           k_keyword_classifications[index].is_builtin_type;
}

// Returns true if the token type can begin a type expression.
// This includes all built-in type keywords, the `function` keyword (for
// function-type annotations), and `none` (the unit/absence type).
[[nodiscard]] constexpr bool is_type_keyword(TokenType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < k_keyword_classifications.size() &&
           k_keyword_classifications[index].is_type_keyword;
}

// The container/handle type names that were demoted from reserved keywords to
// ordinary identifiers.  They are no longer reserved (a program may use them as
// variable or field names), but the parser still recognises them as the head of
// a type annotation in declaration position via is_builtin_type_identifier().
// Kept sorted for the binary search below.
inline constexpr auto k_builtin_type_identifiers = std::to_array<std::string_view>({
    "binary_tree",
    "channel",
    "graph",
    "hash_set",
    "key_value_store",
    "linked_list",
    "queue",
    "reference",
    "set",
    "socket",
    "stack",
    "task",
    "widget",
    "xml",
});

// Returns true if `name` is one of the demoted built-in container/handle type
// identifiers (queue, stack, set, task, channel, socket, xml, …).  Unlike the
// reserved type keywords, these lex as ordinary identifiers, so the parser uses
// this predicate to treat `queue<T> x = …` as a variable declaration.
[[nodiscard]] constexpr bool is_builtin_type_identifier(std::string_view name) noexcept {
    return std::binary_search(k_builtin_type_identifiers.begin(), k_builtin_type_identifiers.end(),
                              name);
}

// Returns true if the token type is a comparison operator
// (==, !=, <, >, <=, >=).
[[nodiscard]] constexpr bool is_comparison_op(TokenType type) noexcept {
    switch (type) {
        case TokenType::EqualsEquals:
        case TokenType::BangEquals:
        case TokenType::Less:
        case TokenType::Greater:
        case TokenType::LessEquals:
        case TokenType::GreaterEquals:
            return true;
        default:
            return false;
    }
}

} // namespace luma

template <> struct std::formatter<luma::TokenType> : std::formatter<std::string_view> {
    auto format(luma::TokenType type, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(luma::token_type_to_string(type), ctx);
    }
};

#endif // LUMA_LEXER_TOKEN_TYPE_HPP
