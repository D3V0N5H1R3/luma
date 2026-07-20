// ─────────────────────────────────────────────────────────────────────────────
// Linter — Declaration handlers
// ─────────────────────────────────────────────────────────────────────────────
// Implements visit_function, visit_namespace, visit_include — the three
// DeclarationDispatcher CRTP handlers that route top-level declarations to
// their lint checks.  Extracted from linter_variables.cpp to match the
// DeclarationDispatcher naming and keep file responsibilities clear.
// ─────────────────────────────────────────────────────────────────────────────

#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/linter/linter.hpp"

namespace luma {

void Linter::visit_function(const FunctionDeclaration& func) {
    // Check for empty function body.
    if (func.body.empty() && !func.is_main && !func.is_test) {
        warn(std::format("Function '{}' has an empty body", func.name), func.location,
             "add an implementation or a 'return' statement to indicate "
             "the function is intentionally empty",
             DiagnosticCode::EmptyBody);
    }

    // Lint the function body in a new scope.
    auto guard = make_scope_guard();

    // Register parameters in scope and track them for unused detection.
    for (const auto& param : func.parameters) {
        tracker_.track_variable(param.name, func.location, true);
    }

    lint_block(func.body);
}

void Linter::visit_namespace(const NamespaceDeclaration& ns) {
    for (const auto& inner : ns.declarations) {
        lint_declaration(*inner);
    }
}

void Linter::visit_include(const IncludeDeclaration& inc) {
    tracker_.track_include(inc.path, inc.location);
}

} // namespace luma
