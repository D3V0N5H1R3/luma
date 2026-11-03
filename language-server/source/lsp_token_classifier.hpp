#ifndef LUMA_LSP_TOKEN_CLASSIFIER_HPP
#define LUMA_LSP_TOKEN_CLASSIFIER_HPP

// Consolidated token and symbol classification for semantic tokens.
//
// Provides three layers:
//
//   1. token_class   — lightweight predicates that categorise a TokenType
//                      (keyword, builtin type, numeric literal, operator).
//
//   2. SymbolClassifier — maps identifier tokens to semantic token types
//                         (Function, Variable, Parameter, etc.) using
//                         analysis results and the stdlib registry.
//
//   3. classify_token — top-level classifier that maps any Token to a
//                      (SemanticTokenType index, modifier flags) pair.
//                      Delegates to token_class predicates for non-identifiers
//                      and to SymbolClassifier for identifiers.
//
// Previously the symbol classification lived in a separate
// lsp_symbol_classifier.hpp; it has been inlined here since
// token_classifier is its only consumer.

#include <array>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_constants.hpp"
#include "lsp_stdlib_registry.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ─── Token-type predicates ──────────────────────────────────
// Stateless helpers that classify a TokenType by syntactic category.

namespace token_class {

[[nodiscard]] inline constexpr bool is_keyword(TokenType type) {
    return is_keyword_token_type(type) && !is_builtin_type_token_type(type);
}

[[nodiscard]] inline constexpr bool is_builtin_type(TokenType type) {
    return is_builtin_type_token_type(type);
}

[[nodiscard]] inline constexpr bool is_numeric_literal(TokenType type) {
    return type == TokenType::IntegerLiteral || type == TokenType::NumberLiteral;
}

[[nodiscard]] inline bool is_operator(TokenType type) {
    // Every operator TokenType the lexer can produce (token_type.hpp
    // "Operators" block) maps to the Operator semantic-token category, so
    // compound assignments, ranges, optional-chaining, and integer division
    // are classified consistently with their single-character siblings rather
    // than left unclassified.
    static constexpr auto operator_types = std::array{
        TokenType::AmpersandAmpersand,
        TokenType::Arrow,
        TokenType::Bang,
        TokenType::BangEquals,
        TokenType::BangGreater,
        TokenType::DotDot,
        TokenType::DotDotEquals,
        TokenType::Equals,
        TokenType::EqualsEquals,
        TokenType::Greater,
        TokenType::GreaterEquals,
        TokenType::GreaterGreater,
        TokenType::Less,
        TokenType::LessEquals,
        TokenType::Minus,
        TokenType::MinusEquals,
        TokenType::MinusMinus,
        TokenType::Percent,
        TokenType::PercentEquals,
        TokenType::Pipe,
        TokenType::PipeGreater,
        TokenType::PipePipe,
        TokenType::Plus,
        TokenType::PlusEquals,
        TokenType::PlusPlus,
        TokenType::QuestionBracket,
        TokenType::QuestionDot,
        TokenType::QuestionMark,
        TokenType::QuestionQuestion,
        TokenType::Slash,
        TokenType::SlashEquals,
        TokenType::SlashSlash,
        TokenType::SlashSlashEquals,
        TokenType::Star,
        TokenType::StarEquals,
    };
    return std::ranges::find(operator_types, type) != operator_types.end();
}

// Built-in type names the lexer emits as Identifier tokens: `void` and the
// demoted container/handle types (`set`, `queue`, `task`, …).
[[nodiscard]] inline bool is_builtin_type_name(std::string_view name) {
    return name == "void" || is_builtin_type_identifier(name);
}

} // namespace token_class

// The tokens around the one being classified, when known. Identifiers whose
// meaning depends on their position (built-in type names that are also valid
// field and variable names) are classified from them.
struct TokenNeighbours {
    const Token* previous{nullptr};
    const Token* next{nullptr};
};

// Whether an identifier between `n.previous` and `n.next` sits where a type is
// written: not after `.` (a field or member), and followed on the same line by
// `<`, a name, `>`, or `?` (`set<T>`, `socket s`, `function void main`,
// `array<socket>`), or right after `<` or `->` (`optional<xml>`, `-> void`).
[[nodiscard]] inline bool in_type_position(const TokenNeighbours& n) {
    if (n.previous != nullptr && n.previous->type == TokenType::Dot) {
        return false;
    }
    if (n.next != nullptr && !n.next->preceded_by_newline) {
        const auto next = n.next->type;
        if (next == TokenType::Less || next == TokenType::Identifier ||
            next == TokenType::Greater || next == TokenType::GreaterGreater ||
            next == TokenType::QuestionMark) {
            return true;
        }
    }
    return n.previous != nullptr &&
           (n.previous->type == TokenType::Less || n.previous->type == TokenType::Arrow);
}

// ─── Symbol classifier ─────────────────────────────────────
// Maps identifier tokens to semantic token types using analysis
// results and the stdlib registry.

struct SymbolClassifier {
    // Classify an identifier token, returning (type_index, modifier_flags).
    [[nodiscard]] static std::pair<int, int>
    classify_identifier(const Token& tok, const AnalysisResult& result,
                        const StdlibRegistry& stdlib_registry, const TokenNeighbours& neighbours) {
        // Stdlib module name → Namespace + Readonly.
        if (stdlib_registry.has_module(tok.lexeme)) {
            return {static_cast<int>(SemanticTokenType::Namespace),
                    static_cast<int>(SemanticTokenModifier::Readonly)};
        }

        int modifiers = 0;

        // Look up user-defined function (including namespace-qualified).
        auto fn_it = result.semantic.symbols.user_functions.find(tok.lexeme);
        if (fn_it == result.semantic.symbols.user_functions.end()) {
            auto short_it = result.semantic.symbols.function_short_names.find(tok.lexeme);
            if (short_it != result.semantic.symbols.function_short_names.end()) {
                for (const auto& qname : short_it->second) {
                    auto candidate = result.semantic.symbols.user_functions.find(qname);
                    if (candidate != result.semantic.symbols.user_functions.end()) {
                        fn_it = candidate;
                        break;
                    }
                }
            }
        }
        if (fn_it != result.semantic.symbols.user_functions.end()) {
            // `location` is anchored at the `function` keyword, not the name, so
            // compare the token against the resolved NAME range — otherwise the
            // Definition modifier never matches. The range is precomputed once in
            // build_token_index (stored by value on UserFunctionInfo), so this
            // per-token classification stays O(1); rescanning the token stream
            // here would be quadratic over the document.
            const Range& name_range = fn_it->second.name_range;
            const Range tok_rng = token_range(tok);

            if (tok_rng.start.line == name_range.start.line &&
                tok_rng.start.character == name_range.start.character) {
                modifiers = static_cast<int>(SemanticTokenModifier::Definition);
            }
            return {static_cast<int>(SemanticTokenType::Function), modifiers};
        }

        // Type definition (record, choice, interface, type_alias).
        bool is_user_symbol = false;
        if (auto def = result.find_definition(tok.lexeme)) {
            if (constants::type_kind::is_type_definition(def->type_string)) {
                return {static_cast<int>(SemanticTokenType::Type), 0};
            }

            is_user_symbol = true;
            if (!def->is_mutable) {
                modifiers |= static_cast<int>(SemanticTokenModifier::Readonly);
            }
        } else if (result.semantic.locals.local_variable_types.contains(tok.lexeme)) {
            is_user_symbol = true;
            if (!is_mutable_symbol(result, tok.lexeme)) {
                modifiers |= static_cast<int>(SemanticTokenModifier::Readonly);
            }
        }

        // Built-in type names that lex as identifiers: the `void` return type
        // and the demoted container/handle types (`set`, `queue`, `task`, …).
        // Those are also valid variable, field, and argument names, so they are
        // types only when no user symbol claims the name and they sit where a
        // type is written.
        if (!is_user_symbol && token_class::is_builtin_type_name(tok.lexeme) &&
            in_type_position(neighbours)) {
            return {static_cast<int>(SemanticTokenType::Type), 0};
        }

        // Function parameter check.
        const auto enclosing = find_enclosing_function(result, tok.location.line);
        if (enclosing.has_value()) {
            auto fi = result.semantic.symbols.user_functions.find(*enclosing);
            if (fi != result.semantic.symbols.user_functions.end()) {
                for (const auto& p : fi->second.parameters) {
                    if (p.name == tok.lexeme) {
                        return {static_cast<int>(SemanticTokenType::Parameter), 0};
                    }
                }
            }
        }

        return {static_cast<int>(SemanticTokenType::Variable), modifiers};
    }
};

// ─── Top-level token classifier ─────────────────────────────
// Returns {semantic_token_type_index, modifier_flags}.
// A negative type index means the token should be skipped.

[[nodiscard]] inline std::pair<int, int> classify_token(const Token& tok,
                                                        const AnalysisResult& result,
                                                        const StdlibRegistry& stdlib_registry,
                                                        const TokenNeighbours& neighbours = {}) {
    // Word literals (`true`, `false`, `none`) and the value constructors
    // (`success`, `failure`, `some`) are lexer keywords, but the grammars give
    // them their own scopes (constant.language / support.function.builtin, see
    // Luma_Syntax_Highlighting.md). LSP has no standard "constant" token type,
    // and a `keyword` token would override those scopes, so they are left to
    // the grammar.
    switch (tok.type) {
        case TokenType::BooleanLiteral:
        case TokenType::NoneLiteral:
        case TokenType::Success:
        case TokenType::Failure:
        case TokenType::Some:
            return {-1, 0};
        default:
            break;
    }
    if (token_class::is_keyword(tok.type)) {
        return {static_cast<int>(SemanticTokenType::Keyword), 0};
    }
    if (token_class::is_builtin_type(tok.type)) {
        return {static_cast<int>(SemanticTokenType::Type), 0};
    }
    if (token_class::is_numeric_literal(tok.type)) {
        return {static_cast<int>(SemanticTokenType::Number), 0};
    }
    // A whole string literal, or one literal segment of an interpolated string
    // ("text ${…} more"). The ${ and } delimiters are left to the grammar.
    if (tok.type == TokenType::StringLiteral || tok.type == TokenType::StringStart ||
        tok.type == TokenType::StringMiddle || tok.type == TokenType::StringEnd) {
        return {static_cast<int>(SemanticTokenType::String), 0};
    }
    if (tok.type == TokenType::Annotation) {
        return {static_cast<int>(SemanticTokenType::Decorator), 0};
    }
    if (token_class::is_operator(tok.type)) {
        return {static_cast<int>(SemanticTokenType::Operator), 0};
    }
    if (tok.type == TokenType::Identifier) {
        return SymbolClassifier::classify_identifier(tok, result, stdlib_registry, neighbours);
    }
    return {-1, 0};
}

} // namespace luma::lsp

#endif // LUMA_LSP_TOKEN_CLASSIFIER_HPP
