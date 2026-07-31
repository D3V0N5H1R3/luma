// ─────────────────────────────────────────────────────────────────────────────
// TypeChecker Core                        (TypeChecker partial implementation)
// ─────────────────────────────────────────────────────────────────────────────
// This file implements the core orchestration and cross-cutting concerns:
//
//   check()                 — Entry point: reset state, register declarations,
//                             check declarations, emit unused-function warnings.
//   Scope management        — push_scope(), pop_scope() (with unused-variable
//                             warnings on pop).
//   Diagnostics             — error(), warn(), type_mismatch_hint().
//   Name suggestions        — suggest_type_name(), suggest_variable_name()
//                             (gather candidates, delegate to suggest_name()
//                             in common/string_utils.hpp for Levenshtein matching).
//   Match exhaustiveness    — check_match_exhaustiveness()
//                             (delegates to MatchExhaustivenessChecker).
// ─────────────────────────────────────────────────────────────────────────────

#include "analysis/types/type_checker.hpp"

#include <array>
#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/expression_type_checker.hpp"
#include "analysis/types/match_exhaustiveness.hpp"
#include "analysis/types/statement_type_checker.hpp"
#include "analysis/types/stdlib_types.hpp"
#include "common/string_utils.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// TypeChecker — constructor and destructor
// ═══════════════════════════════════════════════════════════

TypeChecker::TypeChecker()
    : DiagnosticEmitter(DiagnosticCategory::Type, DiagnosticSource::Type),
      expr_checker_{std::make_unique<ExpressionTypeChecker>(*this)},
      stmt_checker_{std::make_unique<StatementTypeChecker>(*this)},
      exhaustiveness_checker_{std::make_unique<MatchExhaustivenessChecker>(*this)} {}

TypeChecker::~TypeChecker() = default;

// ═══════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════

void TypeChecker::error(std::string_view message, const SourceLocation& loc, std::string_view hint,
                        DiagnosticCode code, std::optional<Fix> fix) {
    emit_error(loc, std::string{message}, hint, code, std::move(fix));
}

void TypeChecker::warn(std::string_view message, const SourceLocation& loc, std::string_view hint,
                       DiagnosticCode code, std::optional<Fix> fix) {
    // Warnings are stored in a separate warnings_ vector (not in
    // DiagnosticEmitter::diagnostics_) so the type checker can report
    // them independently from errors.  This prevents using the base
    // class emit_warning(), which routes to diagnostics_.
    warnings_.push_back(build_diagnostic(Severity::Warning, DiagnosticCategory::Warning,
                                         DiagnosticSource::Type, loc, std::string{message}, hint,
                                         code, std::move(fix)));
}

const std::vector<Diagnostic>& TypeChecker::get_warnings() const {
    return warnings_;
}

const StringMap<TypeInfo>& TypeChecker::stdlib_signatures() const {
    return stdlib_signatures_cache_;
}

SymbolTable TypeChecker::export_symbols() {
    return symbol_exporter_.build(*this);
}

bool TypeChecker::is_stdlib_namespace(std::string_view name) const {
    return stdlib_handler_.is_stdlib_namespace(name);
}

std::string TypeChecker::suggest_type_name(std::string_view unknown) const {
    // Collect all known type names.
    static constexpr auto builtins = std::to_array<std::string_view>({
        "boolean", "integer",         "number",     "decimal", "string",   "none",
        "void",    "array",           "dictionary", "result",  "optional", "task",
        "channel", "reference",       "socket",     "widget",  "xml",      "binary_tree",
        "set",     "key_value_store", "queue",      "stack",
    });

    std::vector<std::string_view> candidates(builtins.begin(), builtins.end());

    for (const auto& name : registry_.all_symbol_names()) {
        candidates.emplace_back(name);
    }

    // Generic type parameters are transient bindings not stored in the registry.
    for (const auto& [name, _] : generics_.bindings()) {
        candidates.emplace_back(name);
    }

    return suggest_name(candidates, unknown);
}

std::string TypeChecker::suggest_variable_name(std::string_view unknown) const {
    std::vector<std::string_view> candidates;

    // Search all visible scopes for variable names.
    auto scope = ctx_.current_scope;

    while (scope) {
        for (const auto& [name, _] : scope->locals()) {
            candidates.emplace_back(name);
        }

        scope = scope->parent();
    }

    // Also search the symbol registry for declared function and type names.
    for (const auto& name : registry_.all_symbol_names()) {
        candidates.emplace_back(name);
    }

    return suggest_name(candidates, unknown);
}

void TypeChecker::push_scope() {
    ctx_.current_scope = std::make_shared<TypeScope>(ctx_.current_scope);
}

void TypeChecker::pop_scope() {
    if (ctx_.current_scope) {
        // Warn about unused variables.
        for (const auto& [name, info] : ctx_.current_scope->locals()) {
            if (name.starts_with('_')) {
                continue;
            }

            if (!info.is_read && !info.is_parameter) {
                warn(std::format("unused variable '{}'", name), info.location,
                     "prefix with '_' to suppress this warning", DiagnosticCode::UnusedVariable);
            }

            // Warn about mutable variables/parameters that are never mutated.
            if (info.is_mutable && !info.is_written) {
                const auto* const label = info.is_parameter ? "parameter" : "variable";
                warn(std::format("{} '{}' is declared mutable but never mutated", label, name),
                     info.location, "remove the 'mutable' keyword",
                     DiagnosticCode::MutableNeverMutated);
            }
        }

        // Warn about unique variables that leave scope without being consumed.
        for (const auto& [name, info] : ctx_.current_scope->unconsumed_unique_locals()) {
            if (name.starts_with('_')) {
                continue;
            }

            warn(std::format("unique variable '{}' is never consumed", name), info.location,
                 "use the variable or prefix with '_' to suppress this warning");
        }

        ctx_.current_scope = ctx_.current_scope->parent();
    }
}

void TypeChecker::check_statement_list(const std::vector<StatementPtr>& stmts) {
    stmt_checker_->check_statement_list(stmts);
}

void TypeChecker::check_statement(const Statement& stmt) {
    stmt_checker_->check_statement(stmt);
}

void TypeChecker::check_match_exhaustiveness(const std::vector<MatchArm>& arms,
                                             const TypeInfo& subject_type,
                                             const SourceLocation& loc) {
    exhaustiveness_checker_->check(arms, subject_type, loc);
}

bool TypeChecker::definitely_returns(const std::vector<StatementPtr>& stmts) const {
    return stmt_checker_->definitely_returns(stmts);
}

TypeInfo TypeChecker::infer_expression_type(const Expression& expr) {
    return expr_checker_->infer_expression_type(expr);
}

TypeInfo TypeChecker::infer_assignment_target(const Expression& expr) {
    return expr_checker_->infer_assignment_target(expr);
}

TypeInfo TypeChecker::infer_block_result(const std::vector<std::unique_ptr<Statement>>& body) {
    return expr_checker_->infer_block_result(body);
}

std::string TypeChecker::type_mismatch_hint(const TypeInfo& expected, const TypeInfo& actual) {
    return ExpressionTypeChecker::type_mismatch_hint(expected, actual);
}

// ====================================================================
// Flow-Sensitive Type Refinements     (forwarded to ExpressionTypeChecker)
// ====================================================================

void TypeChecker::push_refinement(const std::string& var, TypeInfo narrowed) {
    expr_checker_->push_refinement(var, std::move(narrowed));
}

void TypeChecker::pop_refinements(std::size_t mark) {
    expr_checker_->pop_refinements(mark);
}

std::size_t TypeChecker::refinement_mark() const {
    return expr_checker_->refinement_mark();
}

bool TypeChecker::try_extract_is_refinement(const Expression& condition, std::string& var_name,
                                            TypeInfo& narrowed_type) {
    return expr_checker_->try_extract_is_refinement(condition, var_name, narrowed_type);
}

// ====================================================================
// Match Exhaustiveness Query        (forwarded to MatchExhaustivenessChecker)
// ====================================================================

bool TypeChecker::is_match_exhaustive(const MatchStatement& match_stmt) const {
    return exhaustiveness_checker_->is_exhaustive(match_stmt);
}

// ═══════════════════════════════════════════════════════════
// State reset
// ═══════════════════════════════════════════════════════════

void TypeChecker::reset_state() {
    clear_diagnostics();
    warnings_.clear();
    records_.clear();
    choices_.clear();
    interfaces_.clear();
    type_aliases_.clear();
    functions_.clear();
    namespace_functions_.clear();
    resolving_aliases_.clear();
    internal_members_.clear();
    called_functions_.clear();
    resolved_type_cache_.clear();
    ctx_.reset();
    generics_.reset();
    registry_.reset();
}

// ═══════════════════════════════════════════════════════════
// Main entry point
// ═══════════════════════════════════════════════════════════

std::vector<Diagnostic> TypeChecker::check(const Program& program, bool require_main) {
    reset_state();

    stdlib_handler_.initialize();
    stdlib_signatures_cache_ = stdlib_handler_.build_signature_map();

    // Register stdlib-provided record and choice types so they are
    // visible to user programs (e.g. Http.Response, Log.Level).
    for (const auto& [qualified, rec] : stdlib_record_types()) {
        records_[qualified] = rec;
        registry_.register_symbol(qualified, SuggestionCategory::Type);
    }

    for (const auto& [qualified, ch] : stdlib_choice_types()) {
        choices_[qualified] = ch;
        registry_.register_symbol(qualified, SuggestionCategory::Type);
    }

    // Create global scope with built-in functions.
    ctx_.current_scope = std::make_shared<TypeScope>();

    ctx_.current_scope->define("print", TypeInfo::make(TypeInfo::Kind::Func), {});
    ctx_.current_scope->define("assert", TypeInfo::make(TypeInfo::Kind::Func), {});

    {
        auto type_of_func = TypeInfo::make(TypeInfo::Kind::Func);
        type_of_func.return_type =
            std::make_shared<TypeInfo>(TypeInfo::make(TypeInfo::Kind::String));

        ctx_.current_scope->define("type_of", type_of_func, {});
    }

    for (const auto& ns : stdlib_handler_.namespaces()) {
        ctx_.current_scope->define(ns, TypeInfo::make(TypeInfo::Kind::Namespace), {});
    }

    register_declarations(program.declarations);

    for (const auto& decl : program.declarations) {
        check_declaration(*decl);
    }

    for (const auto& stmt : program.statements) {
        check_statement(*stmt);
    }

    // Validate @main count.
    if (ctx_.main_count > 1) {
        error("multiple @main functions found; exactly one is required", SourceLocation{},
              "remove extra @main annotations so only one entry point remains");
    }

    if (require_main && ctx_.main_count == 0) {
        error("no @main function found", SourceLocation{},
              "add '@main' before a function to mark it as the entry point");
    }

    // Linter: warn about unused user-defined functions.
    // Skip @main, @test, functions in namespaces (public API), and
    // functions whose name starts with '_'.
    // Only emit when require_main is set (real programs, not test/library files).
    if (require_main) {
        for (const auto& [name, func] : functions_) {
            if (func->is_main || func->is_test) {
                continue;
            }

            if (name.starts_with('_')) {
                continue;
            }

            if (!called_functions_.contains(name)) {
                warn(std::format("function '{}' is declared but never called", name),
                     func->location, "prefix the name with '_' to suppress this warning",
                     DiagnosticCode::UnusedFunction);
            }
        }
    }

    return take_diagnostics();
}

} // namespace luma
