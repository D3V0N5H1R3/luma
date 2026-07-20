#include "runtime/compiler/compiler.hpp"

#include <format>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/compiler_errors.hpp"
#include "runtime/compiler/hidden_var.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// ─────────── Public API ───────────

Compiler::CompileResult Compiler::compile(const Program& program, bool repl_mode) {
    ctx_.diagnostics.clear();
    ctx_.compiled_functions.clear();
    ctx_.record_declarations.clear();
    ctx_.scope_stack.clear();
    ctx_.last_was_return = false;
    ctx_.program = &program;
    ctx_.interner = StringInterner{}; // Reset for each compilation unit.

    // Create the top-level scope (acts as "script" function).
    begin_function(CompilerLimits::k_top_level_name, 0);

    // Register all declarations first (forward declaration pass).
    for (const auto& decl : program.declarations) {
        compile_declaration(*decl);
    }

    // Compile top-level statements.
    for (std::size_t i = 0; i < program.statements.size(); ++i) {
        const auto& stmt = *program.statements[i];
        const bool is_last = (i == program.statements.size() - 1);

        // In REPL mode, if the last statement is an expression statement,
        // keep the value on the stack instead of popping it so it can be
        // returned as the REPL result.
        if (repl_mode && is_last && stmt.kind == StatementKind::Expression) {
            const auto& expr_stmt = static_cast<const ExpressionStatement&>(stmt);
            compile_expression(*expr_stmt.expression);
            emit(Op::Return, stmt.location);
        } else {
            compile_statement(stmt);
        }
    }

    emit_implicit_return({});

    auto top_level = end_function();

    CompileResult result;
    result.top_level = std::move(top_level);
    result.functions = std::move(ctx_.compiled_functions.functions);
    result.diagnostics = std::move(ctx_.diagnostics.diagnostics);
    result.success = !ctx_.diagnostics.has_error;

    return result;
}

// ─────────── Scope management ───────────

void Compiler::begin_loop(std::size_t loop_start) {
    current_scope().loop_context.push(loop_start, current_scope().scope_depth,
                                      current_scope().exception_context.depth());
}

void Compiler::end_loop() {
    current_scope().loop_context.end_loop([this](std::size_t offset) { patch_jump(offset); });
}

void Compiler::begin_function(const std::string& name, int arity) {
    ctx_.scope_stack.emplace_back();
    auto& scope = current_scope();
    scope.function.name = name;
    scope.function.arity = arity;

    // Reserve slot 0 for the function itself.
    (void)declare_local(name, false);
}

[[nodiscard]] CompiledFunction Compiler::end_function() {
    emit_implicit_return({});
    auto function = std::move(current_scope().function);
    function.build_param_name_index();
    ctx_.scope_stack.pop_back();
    return function;
}

// ─────────── Bytecode emission ───────────

Chunk& Compiler::current_chunk() {
    return current_scope().function.mutable_chunk();
}

// ─────────── Error reporting ───────────

void Compiler::error(std::string_view message, SourceLocation loc, std::string_view hint) {
    auto builder = diag::error(std::string{message})
                       .category(DiagnosticCategory::Compile)
                       .source(DiagnosticSource::Compile)
                       .primary(loc);
    if (!hint.empty()) {
        builder.hint(std::string{hint});
    }
    ctx_.diagnostics.add_error(builder.build());
}

void Compiler::warning(std::string_view message, SourceLocation loc, std::string_view hint) {
    auto builder =
        diag::warning(std::string{message}).source(DiagnosticSource::Compile).primary(loc);
    if (!hint.empty()) {
        builder.hint(std::string{hint});
    }
    ctx_.diagnostics.add_warning(builder.build());
}

void Compiler::error_limit_exceeded(std::string_view description, std::size_t maximum,
                                    SourceLocation loc, std::string_view hint) {
    error(std::format("too many {} (maximum {})", description, maximum), loc, hint);
}

// ─────────── Implicit return ───────────

void Compiler::emit_implicit_return(SourceLocation loc) {
    // Dead code elimination: skip if the last emitted opcode was already
    // a Return (the function has an explicit return on all paths).
    if (ctx_.last_was_return) {
        return;
    }

    emit(Op::None, loc);
    emit(Op::Return, loc);
}

void Compiler::emit_number_widening_if_needed(const TypeAnnotation& type, SourceLocation loc) {
    if (type.is_number_type()) {
        emit(Op::IntToNumber, loc);
    }
}

void Compiler::emit_number_widening_if_needed(bool is_number_type, SourceLocation loc) {
    if (is_number_type) {
        emit(Op::IntToNumber, loc);
    }
}

} // namespace luma
