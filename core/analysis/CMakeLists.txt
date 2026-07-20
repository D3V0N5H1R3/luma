# core/analysis/CMakeLists.txt
# Shared analysis pipeline: lexer → parser → type checker → linter → resolver.
# Used by luma_lsp to avoid depending on the full luma_core library.

set(luma_analysis_sources
    lexer/lexer.cpp
    lexer/lexer_numbers.cpp
    lexer/lexer_operators.cpp
    lexer/lexer_string.cpp
    parser/parser.cpp
    parser/parser_decl.cpp
    parser/parser_stmt.cpp
    parser/parser_expr.cpp
    parser/type_annotation_parser.cpp
    types/type_checker.cpp
    types/type_info.cpp
    types/type_scope.cpp
    types/symbol_exporter.cpp
    types/type_checker_resolve.cpp
    types/generic_inference.cpp
    types/type_checker_decl.cpp
    types/expression_type_checker.cpp
    types/expression_type_checker_literals.cpp
    types/expression_type_checker_binary.cpp
    types/compile_time_arithmetic.cpp
    types/numeric_operator_checker.cpp
    types/expression_type_checker_calls.cpp
    types/expression_type_checker_access.cpp
    types/index_access_checker.cpp
    types/expression_type_checker_misc.cpp
    types/statement_type_checker.cpp
    types/statement_type_checker_declarations.cpp
    types/statement_type_checker_control.cpp
    types/match_exhaustiveness.cpp
    types/match_arm_binding.cpp
    types/statement_type_checker_misc.cpp
    types/stdlib_type_handler.cpp
    types/stdlib_type_signatures.cpp
    types/stdlib_type_arities.cpp
    types/generic_resolver.cpp
    source/source_manager.cpp
    resolver/resolver.cpp
    prelude/gui_prelude.cpp
    linter/lint_rule.cpp
    linter/lint_plugin.cpp
    linter/builtin_plugins.cpp
    linter/linter.cpp
    linter/linter_tracker.cpp
    linter/linter_declarations.cpp
    linter/linter_variables.cpp
    linter/linter_control_flow.cpp
    linter/linter_expressions.cpp
    diagnostics/diagnostic.cpp
    diagnostics/diagnostic_builders.cpp
    diagnostics/diagnostic_emitter.cpp
    diagnostics/renderer.cpp

    # Generic pass/pipeline framework — depends only on the AST and diagnostics.
    # The back-end pass wrappers (compiler/optimizer/verifier) live next to the
    # code they wrap under core/runtime/compiler.
    pipeline/pipeline.cpp
)

luma_add_library(luma_analysis STATIC ${luma_analysis_sources})

# Embed the shared common utilities and the stdlib catalog (compiled as separate
# OBJECT libraries) so their translation units appear exactly once.
target_sources(luma_analysis PRIVATE
    $<TARGET_OBJECTS:luma_common>
    $<TARGET_OBJECTS:luma_stdlib_catalog>
)
