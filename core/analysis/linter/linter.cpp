#include "analysis/linter/linter.hpp"

#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/linter/lint_plugin.hpp"
#include "analysis/linter/lint_rule.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"

namespace luma {

// ─────────── Public API ───────────

std::vector<Diagnostic> Linter::lint(const Program& program) {
    clear_diagnostics();
    tracker_.clear();
    // The top-level scope uses raw push/pop rather than an RAII make_scope_guard()
    // because pop_scope() reports this scope's unused variables, which must be
    // emitted before take_diagnostics() moves the diagnostics out at the end of
    // lint().  A scope guard would instead pop during the return's stack
    // unwinding — after the buffer was already emptied — silently dropping those
    // warnings.  All inner scopes (functions, blocks, loops) use
    // make_scope_guard() for exception safety.
    push_scope();

    for (const auto& decl : program.declarations) {
        lint_declaration(*decl);
    }

    lint_block(program.statements);

    pop_scope();
    report_unused_includes();

    return take_diagnostics();
}

// ─────────── AST traversal dispatch ───────────
//
// Each entry point delegates to the corresponding CRTP dispatcher
// (DeclarationDispatcher, StatementDispatcher, ExpressionDispatcher)
// which routes the node to the appropriate visit_X method.

void Linter::lint_declaration(const Declaration& decl) {
    run_declaration_plugins(decl);
    dispatch_decl(decl);
}

void Linter::lint_statement(const Statement& stmt, bool is_tail_position) {
    run_statement_plugins(stmt);

    // When at the tail of a value block, expression-statements produce the
    // block's result — skip the discarded-result check by handling them
    // directly instead of going through dispatch_stmt / visit_expression_statement.
    if (is_tail_position && stmt.kind == StatementKind::Expression) {
        const auto& expr_stmt = static_cast<const ExpressionStatement&>(stmt);
        lint_expression(*expr_stmt.expression);
        return;
    }

    dispatch_stmt(stmt);
}

void Linter::lint_expression(const Expression& expr) {
    // Guard against native stack overflow on pathologically deep expression
    // ASTs (e.g. a very long flat `a + b + c + ...` chain the parser builds
    // iteratively).  The parser and type checker already report over-nesting;
    // the linter is advisory, so it simply stops descending here.
    if (++expression_depth_ > ResourceLimits::max_expression_depth) {
        --expression_depth_;
        return;
    }

    const ScopeGuard guard{[this] { --expression_depth_; }};

    run_expression_plugins(expr);
    dispatch_expr(expr);
}

void Linter::lint_block(const std::vector<std::unique_ptr<Statement>>& stmts, bool last_is_value) {
    bool seen_terminator = false;

    for (std::size_t i = 0; i < stmts.size(); ++i) {
        const auto& stmt = *stmts[i];

        if (seen_terminator) {
            warn("Unreachable code after return/break/continue", stmt.location,
                 "Remove this code or restructure the control flow",
                 DiagnosticCode::UnreachableCode);
            break; // only warn once per block
        }

        // When linting the last statement of a value block, pass the tail
        // position flag so that lint_statement can suppress the W0010
        // discarded-result warning — the expression's value is the result
        // of the enclosing block.
        const bool is_last_statement = (i + 1 == stmts.size());

        const bool is_tail =
            last_is_value && is_last_statement && stmt.kind == StatementKind::Expression;

        lint_statement(stmt, is_tail);

        if (is_terminator_statement(stmt)) {
            seen_terminator = true;
        }
    }
}

void Linter::lint_scoped_block(const std::vector<std::unique_ptr<Statement>>& stmts,
                               bool last_is_value) {
    auto guard = make_scope_guard();
    lint_block(stmts, last_is_value);
}

// ─────────── Diagnostics ───────────

void Linter::warn(std::string_view message, SourceLocation loc, std::string_view hint,
                  DiagnosticCode code) {
    // If the rule has non-default severity, use it instead of plain warning.
    if (code != DiagnosticCode::None) {
        if (const auto* rule = registry_.find_by_code(code)) {
            emit(build_diagnostic(rule->severity, DiagnosticCategory::Warning,
                                  DiagnosticSource::Lint, loc, std::string(message), hint, code));
            return;
        }
    }

    emit_warning(loc, std::string(message), hint, code);
}

// ─────────── Scope lifecycle ───────────

void Linter::push_scope() {
    tracker_.push_scope();
}

void Linter::pop_scope() {
    report_unused_in_current_scope();
    tracker_.pop_scope();
}

// ─────────── Usage reporting ───────────

void Linter::report_unused_in_current_scope() {
    if (tracker_.empty()) {
        return;
    }

    for (const auto& [name, info] : tracker_.current_variables()) {
        if (info.used) {
            // Check for mutable-never-mutated (only if the variable IS used).
            if (info.is_mutable && !info.mutated && !info.is_parameter) {
                warn(std::format("Mutable variable '{}' is never mutated", name), info.location,
                     "remove the 'mutable' keyword if mutation is not needed",
                     DiagnosticCode::MutableNeverMutated);
            }

            continue;
        }

        if (info.is_parameter) {
            warn(std::format("Parameter '{}' is never used", name), info.location,
                 std::format("prefix with '_' if intentionally unused, e.g. '_{}'", name),
                 DiagnosticCode::UnusedParameter);
        } else {
            warn(std::format("Variable '{}' is never used", name), info.location,
                 std::format("prefix with '_' if intentionally unused, e.g. '_{}'", name),
                 DiagnosticCode::UnusedVariable);
        }
    }
}

void Linter::report_unused_includes() {
    for (const auto& info : tracker_.includes()) {
        if (!info.used) {
            warn(std::format("Included file '{}' appears unused", info.path), info.location,
                 "remove the include if it is not needed", DiagnosticCode::UnusedInclude);
        }
    }
}

// ─────────── Plugin dispatch ───────────

template <typename Node, typename CheckFn>
void Linter::run_plugins(const Node& node, CheckFn check_fn) {
    std::vector<LintFinding> findings;

    for (const auto* plugin : plugins_.enabled_plugins()) {
        check_fn(plugin, node, findings);
    }

    for (const auto& finding : findings) {
        warn(finding.message, finding.location, finding.hint, finding.code);
    }
}

void Linter::run_expression_plugins(const Expression& expr) {
    run_plugins(expr, [](const auto* p, const auto& e, auto& f) { p->check_expression(e, f); });
}

void Linter::run_statement_plugins(const Statement& stmt) {
    run_plugins(stmt, [](const auto* p, const auto& s, auto& f) { p->check_statement(s, f); });
}

void Linter::run_declaration_plugins(const Declaration& decl) {
    run_plugins(decl, [](const auto* p, const auto& d, auto& f) { p->check_declaration(d, f); });
}

} // namespace luma
