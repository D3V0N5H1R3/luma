#ifndef LUMA_AST_STATEMENT_HPP
#define LUMA_AST_STATEMENT_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/ast/ast_fwd.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/match_pattern.hpp"
#include "analysis/ast/type_annotation.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"
#include "common/enum_name.hpp"

namespace luma {

// ─────────────────────── Statements ───────────────────────

enum class StatementKind {
    Assignment,
    Block,
    Break, // Uses base Statement directly — no extra data needed.
    CompoundAssignment,
    Continue, // Uses base Statement directly — no extra data needed.
    Decrement,
    Expression,
    For,
    If,
    Increment,
    Match,
    Return,
    Try,
    TupleDestructuring,
    VariableDeclaration,
    While
};

[[nodiscard]] constexpr std::string_view to_string(StatementKind kind) noexcept {
    constexpr std::string_view k_names[] = {"Assignment",
                                            "Block",
                                            "Break",
                                            "CompoundAssignment",
                                            "Continue",
                                            "Decrement",
                                            "Expression",
                                            "For",
                                            "If",
                                            "Increment",
                                            "Match",
                                            "Return",
                                            "Try",
                                            "TupleDestructuring",
                                            "VariableDeclaration",
                                            "While"};

    static_assert(std::size(k_names) == static_cast<std::size_t>(StatementKind::While) + 1,
                  "StatementKind name table is out of sync with the enum");
    return enum_name(kind, k_names);
}

struct Statement {
    explicit Statement(StatementKind kind, SourceLocation loc) : kind{kind}, location{loc} {}

    Statement(const Statement&) = delete;
    Statement(Statement&&) noexcept = default;

    Statement& operator=(const Statement&) = delete;
    Statement& operator=(Statement&&) noexcept = default;

    virtual ~Statement() noexcept = default;

    StatementKind kind;
    SourceLocation location;
};

struct VariableDeclStatement : Statement {
    explicit VariableDeclStatement(SourceLocation loc, TypeAnnotation type, std::string name,
                                   bool is_mutable, ExpressionPtr initializer)
        : Statement{StatementKind::VariableDeclaration, loc},
          type{std::move(type)},
          name{std::move(name)},
          is_mutable{is_mutable},
          initializer{std::move(initializer)} {}

    TypeAnnotation type;
    std::string name;
    bool is_mutable{false};
    ExpressionPtr initializer;
};

struct AssignmentStatement : Statement {
    explicit AssignmentStatement(SourceLocation loc, ExpressionPtr target, ExpressionPtr value)
        : Statement{StatementKind::Assignment, loc},
          target{std::move(target)},
          value{std::move(value)} {}

    ExpressionPtr target;
    ExpressionPtr value;
};

struct CompoundAssignmentStatement : Statement {
    explicit CompoundAssignmentStatement(SourceLocation loc, ExpressionPtr target, TokenType op,
                                         ExpressionPtr value)
        : Statement{StatementKind::CompoundAssignment, loc},
          target{std::move(target)},
          op{op},
          value{std::move(value)} {}

    ExpressionPtr target;
    TokenType op;
    ExpressionPtr value;
};

struct IncrementStatement : Statement {
    explicit IncrementStatement(SourceLocation loc, ExpressionPtr target)
        : Statement{StatementKind::Increment, loc}, target{std::move(target)} {}

    ExpressionPtr target;
};

struct DecrementStatement : Statement {
    explicit DecrementStatement(SourceLocation loc, ExpressionPtr target)
        : Statement{StatementKind::Decrement, loc}, target{std::move(target)} {}

    ExpressionPtr target;
};

struct ExpressionStatement : Statement {
    explicit ExpressionStatement(SourceLocation loc, ExpressionPtr expression)
        : Statement{StatementKind::Expression, loc}, expression{std::move(expression)} {}

    ExpressionPtr expression;
};

struct ReturnStatement : Statement {
    explicit ReturnStatement(SourceLocation loc, ExpressionPtr value)
        : Statement{StatementKind::Return, loc}, value{std::move(value)} {}

    ExpressionPtr value;
};

struct ForStatement : Statement {
    explicit ForStatement(SourceLocation loc, std::string loop_variable, std::string index_variable,
                          ExpressionPtr iterable)
        : Statement{StatementKind::For, loc},
          loop_variable{std::move(loop_variable)},
          index_variable{std::move(index_variable)},
          iterable{std::move(iterable)} {}

    std::string loop_variable;
    std::string index_variable; // empty if no index
    std::vector<std::string>
        destructure_variables; // for `for (a, b) in ...` — non-empty means destructuring
    ExpressionPtr iterable;
    std::vector<StatementPtr> body;
};

struct IfStatement : Statement {
    explicit IfStatement(SourceLocation loc, ExpressionPtr condition)
        : Statement{StatementKind::If, loc}, condition{std::move(condition)} {}

    ExpressionPtr condition;
    std::vector<StatementPtr> then_body;
    std::vector<StatementPtr> else_body;
};

struct MatchStatement : Statement {
    explicit MatchStatement(SourceLocation loc, ExpressionPtr subject)
        : Statement{StatementKind::Match, loc}, subject{std::move(subject)} {}

    ExpressionPtr subject;
    std::vector<MatchArm> arms;
};

struct TupleDestructuringStatement : Statement {
    explicit TupleDestructuringStatement(
        SourceLocation loc, std::vector<std::pair<TypeAnnotation, std::string>> bindings,
        bool is_mutable, ExpressionPtr initializer)
        : Statement{StatementKind::TupleDestructuring, loc},
          bindings{std::move(bindings)},
          is_mutable{is_mutable},
          initializer{std::move(initializer)} {}

    std::vector<std::pair<TypeAnnotation, std::string>> bindings;
    bool is_mutable{false};
    ExpressionPtr initializer;
};

struct BlockStatement : Statement {
    explicit BlockStatement(SourceLocation loc, std::vector<StatementPtr> statements)
        : Statement{StatementKind::Block, loc}, statements{std::move(statements)} {}

    std::vector<StatementPtr> statements;
};

struct BreakStatement : Statement {
    explicit BreakStatement(SourceLocation loc) : Statement{StatementKind::Break, loc} {}
};

struct ContinueStatement : Statement {
    explicit ContinueStatement(SourceLocation loc) : Statement{StatementKind::Continue, loc} {}
};

struct WhileStatement : Statement {
    explicit WhileStatement(SourceLocation loc, ExpressionPtr condition)
        : Statement{StatementKind::While, loc}, condition{std::move(condition)} {}

    ExpressionPtr condition;
    std::vector<StatementPtr> body;
};

struct TryStatement : Statement {
    explicit TryStatement(SourceLocation loc) : Statement{StatementKind::Try, loc} {}

    std::vector<StatementPtr> try_body;
    std::string catch_var; // bound name for the error message; empty if no catch
    std::vector<StatementPtr> catch_body;
    std::vector<StatementPtr> finally_body;
};

// Returns true if the statement is a control-flow terminator (return, break, continue).
// Used by the type checker and linter to detect unreachable code.
[[nodiscard]] inline bool is_terminator_statement(const Statement& stmt) noexcept {
    return stmt.kind == StatementKind::Return || stmt.kind == StatementKind::Break ||
           stmt.kind == StatementKind::Continue;
}

} // namespace luma

#endif // LUMA_AST_STATEMENT_HPP
