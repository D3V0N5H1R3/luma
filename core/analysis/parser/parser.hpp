// ─────────────────────────────────────────────────────────────────────────────
// Parser Module
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Construct an Abstract Syntax Tree (AST) from a token stream.
//
// Key Types:
//   - Parser: Main parser class that produces an AST from tokens.
//   - Declaration: Base class for all top-level declarations (functions, etc.).
//   - Statement: Base class for all statements (if, for, return, etc.).
//   - Expression: Base class for all expressions (literals, operators, etc.).
//
// Dependencies:
//   - analysis/ast: For AST node definitions.
//   - analysis/lexer: For the token stream input.
//   - analysis/diagnostics: For reporting syntax errors.
//
// Include audit (R-A19):
//   All project includes are required — AST types are used by value in
//   return types and member variables; resource_limits.hpp and
//   scope_guard.hpp are used in the inline make_recursion_guard() method.
//   No types are used only by pointer/reference, so forward declarations
//   cannot reduce the include set without moving inline code out of the
//   header.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_PARSER_PARSER_HPP
#define LUMA_PARSER_PARSER_HPP

#include <cassert>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_emitter.hpp"
#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/parser/error_recovery.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "common/scope_guard.hpp"

namespace luma {

// Structured constants governing parser resource limits.
namespace parser_limits {
inline constexpr std::size_t k_max_errors = 100;
} // namespace parser_limits

// Forward declaration — full definition in type_annotation_parser.hpp.
class TypeAnnotationParser;

// Lightweight exception used exclusively for parser error-recovery control flow.
//
// ParseFailure is NOT an error-reporting mechanism.  It carries no diagnostic
// data — the structured Diagnostic (with error code, source spans, hints, and
// fix suggestions) is emitted via DiagnosticEmitter *before* the throw.  The
// exception exists only to unwind the call stack to the top-level parse()
// loop, which catches it and calls synchronize() to skip past the error site.
//
// Control-flow contract:
//   1. Parser detects an unrecoverable syntax error.
//   2. Parser emits a Diagnostic via emit_error() / syntax_error().
//   3. Parser throws ParseFailure to unwind to parse().
//   4. parse() catches ParseFailure and calls synchronize().
//   5. Parsing resumes from the next statement/declaration boundary.
//
// This replaces the legacy SyntaxError exception, which bundled message,
// location, and hint into the exception object.
class ParseFailure : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override {
        return "parse failure (diagnostic already emitted)";
    }
};

// ─── Design Note ───
// The Parser class is intentionally a single type: all parsing methods share
// the token stream position, lookahead, and error recovery state that live
// in the private member variables below.
//
// The *implementation* is decomposed across multiple translation units to
// keep individual files navigable and to improve incremental build times:
//
//   File                           Responsibility
//   ─────────────────────────────  ─────────────────────────────────────────
//   parser.cpp                    Core infrastructure: token consumption,
//                                 error recovery, synchronise, entry point.
//   parser_expr.cpp               Expression parsing (precedence climbing).
//   parser_stmt.cpp               Statement parsing (control flow, blocks).
//   parser_decl.cpp               Declaration parsing (functions, records,
//                                 interfaces, choice types, namespaces).
//   type_annotation_parser.cpp    Type annotation parsing (extracted into
//                                 a friend helper class; see
//                                 type_annotation_parser.hpp).
//
// The header below groups method declarations into logical sections that
// mirror this implementation split.  When adding new syntax, place the
// declaration in the appropriate section and implement it in the
// corresponding .cpp file.

class Parser : public DiagnosticEmitter {
public:
    // Precondition: tokens must contain at least one token (the trailing EOF
    // appended by the lexer). This invariant lets current()/peek_at() return
    // tokens_.back() instead of maintaining a fragile static sentinel.
    explicit Parser(std::vector<Token> tokens)
        : DiagnosticEmitter(DiagnosticCategory::Syntax, DiagnosticSource::Syntax),
          tokens_{std::move(tokens)},
          token_count_{static_cast<int>(tokens_.size())} {
        assert(!tokens_.empty() && "token stream must contain at least the EOF token");
    }

    [[nodiscard]] Program parse();

    // Returns syntax errors collected during parsing.
    // Non-empty when the parser recovered from one or more errors.
    [[nodiscard]] const std::vector<Diagnostic>& get_errors() const {
        return diagnostics();
    }

private:
    friend class TypeAnnotationParser;

    // ====================================================================
    // Diagnostic Helpers
    // ====================================================================

    // Shadows DiagnosticEmitter::emit_error() intentionally: supports an
    // optional end location for multi-character span highlighting, which
    // the base class single-location API does not provide.
    void emit_error(std::string_view message, const SourceLocation& start,
                    std::string_view hint = "", std::optional<SourceLocation> end = std::nullopt);

    // Emit a structured syntax error diagnostic and throw ParseFailure
    // to unwind to the parse() error-recovery loop.  All error context
    // is captured in the Diagnostic; ParseFailure carries no data.
    [[noreturn]] void syntax_error(std::string_view message, const SourceLocation& loc,
                                   std::string_view hint = "");

    // Guards the iterative chain builders (binary operators, pipes, postfix).
    // These build left-associative spines *iteratively*, so make_recursion_guard
    // never trips on a flat `a + b + c + ...` chain — yet the resulting AST is
    // walked (and destroyed) *recursively*.  A chain longer than
    // max_expression_depth would therefore overflow the native stack during
    // analysis or teardown, so emit a clean syntax error once it grows past the
    // same limit the type checker and compiler already enforce.
    void ensure_chain_within_limit(std::size_t chain_length, const SourceLocation& loc);

    // ====================================================================
    // Token Consumption and Lookahead              (impl: parser.cpp)
    // ====================================================================

    [[nodiscard]] bool is_at_end() const;
    [[nodiscard]] const Token& current() const;
    [[nodiscard]] const Token& peek_at(int offset) const;
    [[nodiscard]] bool check(TokenType type) const;
    [[nodiscard]] bool check_at(int offset, TokenType type) const;
    const Token& advance();
    const Token& expect(TokenType type);
    [[nodiscard]] std::string build_expect_message(TokenType expected) const;
    [[nodiscard]] bool consume_if(TokenType type);

    // Recoverable expect — reports the error as a diagnostic and
    // attempts to continue parsing instead of throwing.  Returns
    // true if recovery succeeded (the token was consumed or
    // synthesised), false if recovery failed and the caller should
    // bail out.  Use for non-critical structural tokens like closing
    // delimiters where the parser can make forward progress.
    bool recover_expect(TokenType type);

    // Skip tokens until a likely statement or declaration boundary.
    void synchronize();

    // Returns true if the current token looks like it could start a
    // new statement or declaration (used for error recovery).
    // ─── Error Recovery Strategy ───
    //
    // Error recovery uses three cooperating mechanisms:
    //
    //   recover_expect(TokenType)
    //     Recoverable alternative to expect().  On mismatch, records a
    //     diagnostic and tries three strategies in order:
    //       1. Skip-one: if the NEXT token matches, skip the current
    //          (extraneous) token and consume the expected one.
    //       2. Insert: for closing delimiters ()/}/]), assume the token
    //          was omitted and continue without consuming.
    //       3. Statement-start: if the current token could start a new
    //          construct, let the caller continue (the missing token is
    //          implicitly inserted).
    //     Falls back to returning false when none apply.
    //
    //   synchronize()
    //     Called after catching ParseFailure.  Advances past the error
    //     site, respecting delimiter nesting (braces/parens/brackets),
    //     and stops at the next statement or declaration boundary.
    //
    //   looks_like_statement_start()
    //     Heuristic: returns true if the current token is a type keyword,
    //     control-flow keyword, or identifier — tokens that typically
    //     begin a new statement or declaration.  Used by both
    //     recover_expect and synchronize as the "safe landing" test.
    // The error_recovery namespace centralises recovery heuristics
    // (choose_strategy, is_statement_start, is_closing_delimiter) so
    // they can be tested and tuned independently of the grammar rules.
    // See error_recovery_test.cpp for dedicated unit tests.
    [[nodiscard]] bool looks_like_statement_start() const;

    // Returns true if the current token can begin a top-level declaration.
    // Used by parse() to decide between parse_declaration() and parse_statement().
    [[nodiscard]] bool is_declaration_start() const;

    // Returns a hint when the current identifier matches a keyword from
    // another programming language (e.g. "var", "let", "class").
    [[nodiscard]] static std::optional<std::string> foreign_keyword_hint(std::string_view lexeme);

    // Returns the token type at the given lookahead offset relative to token_pos_.
    // Out-of-bounds offsets return EndOfFile.
    [[nodiscard]] TokenType token_type_at(int offset) const;

    // Returns true if the given token type can begin an expression (literal,
    // identifier, prefix operator, grouping, keyword expression start).
    // Used for disambiguation (e.g. postfix `?` vs. next expression).
    [[nodiscard]] static bool can_start_expression(TokenType type);

    // Scans forward from `start_offset` counting parenthesis depth until the
    // matching ')' is found. Returns the offset just past the ')'.
    [[nodiscard]] int find_matching_paren(int start_offset) const;

    // ====================================================================
    // Type Annotation Parsing          (impl: type_annotation_parser.cpp)
    // ====================================================================

    [[nodiscard]] TypeAnnotation parse_type_annotation();
    [[nodiscard]] std::vector<TypeParam> parse_type_param_list();
    [[nodiscard]] std::vector<TypeParam> parse_optional_type_params();

    // ====================================================================
    // Declaration Parsing                          (impl: parser_decl.cpp)
    // ====================================================================

    [[nodiscard]] DeclarationPtr parse_declaration();
    [[nodiscard]] DeclarationPtr parse_annotated_function();
    [[nodiscard]] DeclarationPtr parse_function_decl();
    [[nodiscard]] std::vector<Parameter> parse_parameter_list();
    [[nodiscard]] Parameter parse_parameter();
    [[nodiscard]] DeclarationPtr parse_record_decl();
    [[nodiscard]] DeclarationPtr parse_choice_decl();
    [[nodiscard]] DeclarationPtr parse_interface_decl();
    [[nodiscard]] DeclarationPtr parse_namespace_decl();
    [[nodiscard]] DeclarationPtr parse_type_alias_decl();
    [[nodiscard]] DeclarationPtr parse_include_decl();
    [[nodiscard]] DeclarationPtr parse_use_decl();

    // Parses a brace-delimited, comma-separated list of typed fields.
    // Used by both record and interface declarations. When `allow_defaults`
    // is true, fields may have `= expr` default values (records); when
    // false, defaults are not permitted (interfaces).
    [[nodiscard]] std::vector<RecordField> parse_field_list(bool allow_defaults);

    // Parses a [Namespace.]Type.Variant qualified name used in match patterns.
    // Returns (type_name, variant_name).
    struct QualifiedVariant {
        std::string type_name;
        std::string variant_name;
    };

    [[nodiscard]] QualifiedVariant parse_qualified_variant_name();

    // ====================================================================
    // Statement Parsing                            (impl: parser_stmt.cpp)
    // ====================================================================

    [[nodiscard]] StatementPtr parse_statement();
    [[nodiscard]] StatementPtr parse_mutable_decl();
    [[nodiscard]] StatementPtr parse_variable_decl(bool is_mutable);
    [[nodiscard]] bool looks_like_variable_decl() const;
    [[nodiscard]] int skip_type_at(int offset, int depth = 0) const;

    [[nodiscard]] bool looks_like_tuple_destructuring() const;
    [[nodiscard]] StatementPtr parse_tuple_destructuring(bool is_mutable);
    [[nodiscard]] StatementPtr parse_return_statement();
    [[nodiscard]] StatementPtr parse_for_statement();
    [[nodiscard]] StatementPtr parse_while_statement();
    [[nodiscard]] StatementPtr parse_if_statement();
    [[nodiscard]] StatementPtr parse_match_statement();
    [[nodiscard]] StatementPtr parse_try_statement();
    [[nodiscard]] std::vector<MatchArm> parse_match_arms();
    [[nodiscard]] MatchArm parse_match_arm();
    void parse_case_pattern(MatchArm& arm);
    [[nodiscard]] MatchArm::AlternativePattern parse_alternative_pattern();

    // ─── Shared match pattern helpers ───────────────────────────────────
    // parse_pattern_literal / parse_pattern_comparison write into a pattern
    // payload so both full arms (parse_case_pattern) and '|' alternatives
    // (parse_alternative_pattern) can reuse them.
    void parse_pattern_literal(MatchPattern::PatternData& pattern);
    void parse_pattern_comparison(MatchPattern::PatternData& pattern);

    // Parses a `keyword ( identifier )` binding pattern (success / failure /
    // some) and stores it as PatternData, which must be constructible from a
    // single binding-name string.
    template <typename PatternData> void parse_single_binding_pattern(MatchArm& arm);
    void parse_pattern_choice_destructure(MatchArm& arm, std::string_view type_name,
                                          std::string_view variant_name);
    void parse_pattern_guard(MatchArm& arm);
    void parse_pattern_variant_into(MatchArm::AlternativePattern& alt);

    [[nodiscard]] StatementPtr parse_expression_statement();
    [[nodiscard]] std::vector<StatementPtr> parse_block_body();

    // Parses a brace-delimited statement-list body — `{` statements... `}` with
    // error recovery on the closing brace.  This is the block-bodied form shared
    // by loops, conditionals, try/catch/finally, functions, lambdas, and match
    // arms, so the open-brace/recover-close intent is expressed once here.
    [[nodiscard]] std::vector<StatementPtr> parse_statement_block();

    // Parses open-delimited, close-delimited content using fn.
    // Expects `open` token, calls fn(), then recover_expects `close` token.
    // Works for both value-returning and void lambdas.
    template <typename Fn>
    auto parse_bracketed(TokenType open, TokenType close,
                         Fn&& fn) -> decltype(std::forward<Fn>(fn)());

    // Parses a non-empty comma-separated sequence, appending elements to `out`.
    // Precondition: at least one element is expected (caller must verify the
    // closing delimiter is not the next token before calling this helper).
    //
    //   if (!check(TokenType::RightParen)) {
    //       parse_comma_list(params, [this] { return parse_parameter(); });
    //   }
    template <typename Container, typename Fn> void parse_comma_list(Container& out, Fn&& elem_fn);

    // Parses a possibly-empty, comma-driven list terminated by `close`, calling
    // elem_fn() for each element. Tolerates a single trailing comma before
    // `close`. Iteration is driven by the separator: once an element is not
    // followed by a comma the list ends, leaving the caller to consume/report
    // `close` (so a missing comma surfaces as "expected <close>"). Used where a
    // stray token should be diagnosed by the surrounding close-delimiter check
    // (array literals, record creation).
    template <typename Fn> void parse_trailing_comma_list(TokenType close, Fn&& elem_fn);

    // Parses a possibly-empty list bounded by `close`, calling elem_fn() for
    // each element. Iteration is driven by the closer: it requires a comma
    // between adjacent elements (a missing one is reported as "expected ','")
    // and tolerates a single trailing comma before `close`. Used where the
    // missing separator itself should be diagnosed (record-with overrides,
    // record/interface field lists).
    template <typename Fn> void parse_delimited_comma_list(TokenType close, Fn&& elem_fn);

    // ====================================================================
    // Expression Parsing                           (impl: parser_expr.cpp)
    // ====================================================================

    // Helper: parse a left-associative binary expression level.
    // `next` is a pointer-to-member for the next precedence level.
    // `ops` are the token types at this level.
    template <typename... Ops>
    [[nodiscard]] ExpressionPtr parse_binary_level(ExpressionPtr (Parser::*next)(), Ops... ops);

    [[nodiscard]] ExpressionPtr parse_expression();
    [[nodiscard]] ExpressionPtr parse_pipe();
    [[nodiscard]] ExpressionPtr parse_or();
    [[nodiscard]] ExpressionPtr parse_null_coalescing();
    [[nodiscard]] ExpressionPtr parse_and();
    [[nodiscard]] ExpressionPtr parse_bitwise_or();
    [[nodiscard]] ExpressionPtr parse_bitwise_xor();
    [[nodiscard]] ExpressionPtr parse_bitwise_and();
    [[nodiscard]] ExpressionPtr parse_equality();
    [[nodiscard]] ExpressionPtr parse_comparison();
    [[nodiscard]] ExpressionPtr parse_shift();
    [[nodiscard]] ExpressionPtr parse_addition();
    [[nodiscard]] ExpressionPtr parse_multiplication();
    [[nodiscard]] ExpressionPtr parse_unary();
    [[nodiscard]] ExpressionPtr parse_postfix();

    // Parses a field access (`.field` / `?.field`) after the operator has been
    // consumed. `op_loc` is the operator token's location and `is_optional`
    // selects the '.' vs '?.' spelling used in diagnostics.
    [[nodiscard]] ExpressionPtr parse_field_access(ExpressionPtr receiver,
                                                   const SourceLocation& op_loc, bool is_optional);
    [[nodiscard]] ExpressionPtr parse_call(ExpressionPtr callee);
    [[nodiscard]] ExpressionPtr parse_turbofish_call(ExpressionPtr callee);
    void parse_argument_list(std::vector<ExpressionPtr>& arguments,
                             std::vector<NamedArgument>& named_arguments);
    [[nodiscard]] ExpressionPtr parse_primary();

    // Parses a plain literal (integer, number, boolean, none, string, string interpolation).
    // Returns nullptr if the current token is not a literal.
    [[nodiscard]] ExpressionPtr parse_primary_literal();

    // Parses an identifier that may be a variable reference, qualified/simple
    // record creation, or generic record creation.
    [[nodiscard]] ExpressionPtr parse_identifier_or_record_creation();

    // Parses keyword(expr) wrapper expressions (success, failure, some).
    template <typename NodeType> [[nodiscard]] ExpressionPtr parse_wrapper_expression();

    // Parses keyword<Type>(expr) typed expressions (downcast, is).
    template <typename NodeType, typename... ExtraArgs>
    [[nodiscard]] ExpressionPtr parse_typed_expression(ExtraArgs&&... extra_args);

    [[nodiscard]] ExpressionPtr parse_string_interpolation();
    [[nodiscard]] ExpressionPtr parse_if_expression();
    [[nodiscard]] IfExpression::Branch parse_if_branch();
    [[nodiscard]] ExpressionPtr parse_match_expression();
    [[nodiscard]] ExpressionPtr parse_array_literal();
    [[nodiscard]] ExpressionPtr parse_dict_or_block_expr();
    [[nodiscard]] ExpressionPtr parse_paren_or_lambda_or_tuple();
    [[nodiscard]] bool looks_like_lambda() const;
    [[nodiscard]] ExpressionPtr parse_inline_lambda();
    [[nodiscard]] ExpressionPtr parse_record_creation(const SourceLocation& location,
                                                      std::string_view type_name,
                                                      std::vector<TypeAnnotation> type_args);
    // Lookahead: returns true when the current '<' opens a generic type
    // argument list that is followed by '{' (record creation), rather
    // than being a comparison operator.
    [[nodiscard]] bool is_generic_record_creation() const;
    void close_generic();

    // RAII guard that increments the nesting depth counter on construction
    // and decrements it on destruction.  Emits a diagnostic and throws
    // ParseFailure when the parser exceeds the maximum nesting depth.
    [[nodiscard]] auto make_recursion_guard(const SourceLocation& loc) {
        ++depth_;

        if (depth_ > ResourceLimits::max_parse_depth) {
            --depth_;
            syntax_error("maximum nesting depth exceeded", loc,
                         "simplify the expression or split it into smaller parts");
        }

        return ScopeGuard{[this] {
            --depth_;
        }};
    }

    // ====================================================================
    // Internal State
    // ====================================================================

    std::vector<Token> tokens_;
    const int token_count_;
    int token_pos_{0};
    int depth_{0};

    // Maximum number of syntax errors before the parser gives up.
    // Prevents diagnostic flooding on badly broken input.
    static constexpr std::size_t k_max_parse_errors{parser_limits::k_max_errors};
};

// ──────────── Template implementation ────────────

// Parses open-delimited, close-delimited content using fn.
// Expects `open` token, calls fn(), then recover_expects `close` token.
// Works for both value-returning and void lambdas.
template <typename Fn>
auto Parser::parse_bracketed(TokenType open, TokenType close,
                             Fn&& fn) -> decltype(std::forward<Fn>(fn)()) {
    expect(open);
    if constexpr (std::is_void_v<decltype(std::forward<Fn>(fn)())>) {
        std::forward<Fn>(fn)();
        recover_expect(close);
    } else {
        auto result = std::forward<Fn>(fn)();
        recover_expect(close);
        return result;
    }
}

template <typename... Ops>
ExpressionPtr Parser::parse_binary_level(ExpressionPtr (Parser::*next)(), Ops... ops) {
    auto left = (this->*next)();

    std::size_t chain_length = 0;
    while ((check(ops) || ...)) {
        const auto location = current().location;
        const auto operator_type = advance().type;

        auto right = (this->*next)();

        left = std::make_unique<BinaryExpression>(location, std::move(left), operator_type,
                                                  std::move(right));

        ensure_chain_within_limit(++chain_length, location);
    }

    return left;
}

template <typename Container, typename Fn>
void Parser::parse_comma_list(Container& out, Fn&& elem_fn) {
    out.push_back(elem_fn());
    while (consume_if(TokenType::Comma)) {
        out.push_back(elem_fn());
    }
}

template <typename Fn> void Parser::parse_trailing_comma_list(TokenType close, Fn&& elem_fn) {
    if (check(close)) {
        return;
    }

    elem_fn();

    while (consume_if(TokenType::Comma)) {
        if (check(close)) {
            break;
        }

        elem_fn();
    }
}

template <typename Fn> void Parser::parse_delimited_comma_list(TokenType close, Fn&& elem_fn) {
    while (!check(close) && !is_at_end()) {
        elem_fn();

        if (check(TokenType::Comma) && check_at(1, close)) {
            advance(); // consume trailing comma before the closer
        } else if (!check(close)) {
            expect(TokenType::Comma);
        }
    }
}

template <typename NodeType> ExpressionPtr Parser::parse_wrapper_expression() {
    const auto location = current().location;

    advance();
    expect(TokenType::LeftParen);

    auto value = parse_expression();

    recover_expect(TokenType::RightParen);

    return std::make_unique<NodeType>(location, std::move(value));
}

template <typename NodeType, typename... ExtraArgs>
ExpressionPtr Parser::parse_typed_expression(ExtraArgs&&... extra_args) {
    const auto location = current().location;

    advance();
    expect(TokenType::Less);

    auto target = parse_type_annotation();

    expect(TokenType::Greater);
    expect(TokenType::LeftParen);

    auto operand = parse_expression();

    recover_expect(TokenType::RightParen);

    return std::make_unique<NodeType>(location, std::move(target), std::move(operand),
                                      std::forward<ExtraArgs>(extra_args)...);
}

} // namespace luma

#endif // LUMA_PARSER_PARSER_HPP
