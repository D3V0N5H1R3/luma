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
    static constexpr auto operator_types = std::array{
        TokenType::AmpersandAmpersand,
        TokenType::Arrow,
        TokenType::Bang,
        TokenType::BangEquals,
        TokenType::EqualsEquals,
        TokenType::Greater,
        TokenType::GreaterEquals,
        TokenType::Less,
        TokenType::LessEquals,
        TokenType::Minus,
        TokenType::Percent,
        TokenType::Pipe,
        TokenType::PipeGreater,
        TokenType::PipePipe,
        TokenType::Plus,
        TokenType::Slash,
        TokenType::Star,
    };
    return std::ranges::find(operator_types, type) != operator_types.end();
}

} // namespace token_class

// ─── Symbol classifier ─────────────────────────────────────
// Maps identifier tokens to semantic token types using analysis
// results and the stdlib registry.

struct SymbolClassifier {
    // Classify an identifier token, returning (type_index, modifier_flags).
    [[nodiscard]] static std::pair<int, int>
    classify_identifier(const Token& tok, const AnalysisResult& result,
                        const StdlibRegistry& stdlib_registry) {
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
        if (auto def = result.find_definition(tok.lexeme)) {
            if (constants::type_kind::is_type_definition(def->type_string)) {
                return {static_cast<int>(SemanticTokenType::Type), 0};
            }

            if (!def->is_mutable) {
                modifiers |= static_cast<int>(SemanticTokenModifier::Readonly);
            }
        } else if (result.semantic.locals.local_variable_types.contains(tok.lexeme)) {
            if (!is_mutable_symbol(result, tok.lexeme)) {
                modifiers |= static_cast<int>(SemanticTokenModifier::Readonly);
            }
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
                                                        const StdlibRegistry& stdlib_registry) {
    if (token_class::is_keyword(tok.type)) {
        return {static_cast<int>(SemanticTokenType::Keyword), 0};
    }
    if (token_class::is_builtin_type(tok.type)) {
        return {static_cast<int>(SemanticTokenType::Type), 0};
    }
    if (token_class::is_numeric_literal(tok.type)) {
        return {static_cast<int>(SemanticTokenType::Number), 0};
    }
    if (tok.type == TokenType::StringLiteral) {
        return {static_cast<int>(SemanticTokenType::String), 0};
    }
    if (tok.type == TokenType::Annotation) {
        return {static_cast<int>(SemanticTokenType::Decorator), 0};
    }
    if (token_class::is_operator(tok.type)) {
        return {static_cast<int>(SemanticTokenType::Operator), 0};
    }
    if (tok.type == TokenType::Identifier) {
        return SymbolClassifier::classify_identifier(tok, result, stdlib_registry);
    }
    return {-1, 0};
}

} // namespace luma::lsp

#endif // LUMA_LSP_TOKEN_CLASSIFIER_HPP
