#include <cstdint>
#include <format>
#include <limits>
#include <unordered_set>

#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "common/scope_guard.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_errors.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/scratch_slot_guard.hpp"

namespace luma {

// ─────────── Expression compilation ───────────
//
// compile_expression() dispatches each expression kind to a compile_X()
// handler via the ExpressionDispatcher<Compiler, void> CRTP base
// (ast_dispatcher.hpp).  The inline visit_X() forwarders in compiler.hpp
// bridge the CRTP visit_X() names to the compile_X() method names used
// throughout this file.
//
// Exhaustiveness: the switch in ExpressionDispatcher::dispatch_expr() has
// no default case, so the compiler warns with -Wswitch when a new
// ExpressionKind is added to the enum but not to the dispatcher switch.
//
// Groups:
//   Literals:     LiteralExpression, StringInterpolationExpression
//   Variables:    VariableExpression, FieldAccessExpression, IndexAccessExpression
//   Operators:    UnaryExpression, BinaryExpression
//   Control:      IfExpression, MatchExpression
//   Functions:    CallExpression, LambdaExpression, PipeExpression, ErrorPipeExpression
//   Collections:  ArrayLiteralExpression, DictionaryLiteralExpression,
//                 TupleLiteralExpression, RecordCreationExpression, RecordWithExpression
//   Types:        DowncastExpression, IsExpression, ChoiceValue constructors
//                 (SuccessExpression, FailureExpression, SomeExpression)
//   Concurrency:  SpawnExpression, AwaitExpression, TaskScopeExpression
//   Other:        RangeExpression

void Compiler::compile_expression(const Expression& expr) {
    // Defence-in-depth guard against native stack overflow on pathologically
    // deep expression ASTs.  In the normal pipeline the type checker rejects
    // such input first (so this rarely fires), but a profile that compiles
    // without type checking would otherwise recurse until the stack is
    // exhausted.  Emit a diagnostic and stop descending; the resulting
    // bytecode is discarded because compilation is now in an error state.
    if (++expression_depth_ > ResourceLimits::max_expression_depth) {
        --expression_depth_;
        error("maximum expression nesting depth exceeded", expr.location,
              "simplify the expression or split it into smaller parts");
        return;
    }

    const ScopeGuard guard{[this] { --expression_depth_; }};

    dispatch_expr(expr);
}

void Compiler::compile_literal(const LiteralExpression& expr) {
    switch (expr.literal_type()) {
        case LiteralExpression::LiteralType::None:
            emit(Op::None, expr.location);
            break;
        case LiteralExpression::LiteralType::Boolean:
            emit(expr.boolean_value() ? Op::True : Op::False, expr.location);
            break;
        case LiteralExpression::LiteralType::Integer:
            if (expr.integer_value() == CompilerLimits::k_integer_zero) {
                emit(Op::Zero, expr.location);
            } else if (expr.integer_value() == CompilerLimits::k_integer_one) {
                emit(Op::One, expr.location);
            } else {
                (void)emit_constant(Value{expr.integer_value()}, expr.location);
            }

            break;
        case LiteralExpression::LiteralType::Number:
            (void)emit_constant(Value{expr.number_value()}, expr.location);
            break;
        case LiteralExpression::LiteralType::String:
            (void)emit_constant(Value{expr.string_value()}, expr.location);
            break;
    }
}

void Compiler::compile_variable(const VariableExpression& expr) {
    const auto resolved = resolve_variable(expr.name, expr.location);

    switch (resolved.location) {
        case VarLocation::Local:
            emit_u16(Op::GetLocal, resolved.slot, expr.location);
            break;
        case VarLocation::Upvalue:
            emit_u16(Op::GetUpvalue, resolved.slot, expr.location);
            break;
        case VarLocation::Global:
            emit_u16(Op::GetGlobal, resolved.slot, expr.location);
            break;
    }
}

void Compiler::compile_unary(const UnaryExpression& expr) {
    // ─── Unary constant folding ───
    if (expr.operand->kind == ExpressionKind::Literal) {
        const auto& lit = static_cast<const LiteralExpression&>(*expr.operand);

        if (expr.op == TokenType::Minus) {
            if (lit.literal_type() == LiteralExpression::LiteralType::Integer) {
                // Guard against signed overflow: -INT64_MIN is UB.
                if (lit.integer_value() == std::numeric_limits<std::int64_t>::min()) {
                    (void)emit_constant(Value{-static_cast<double>(lit.integer_value())},
                                        expr.location);
                } else {
                    (void)emit_constant(Value{-lit.integer_value()}, expr.location);
                }
                return;
            }

            if (lit.literal_type() == LiteralExpression::LiteralType::Number) {
                (void)emit_constant(Value{-lit.number_value()}, expr.location);
                return;
            }
        }

        if (expr.op == TokenType::Bang &&
            lit.literal_type() == LiteralExpression::LiteralType::Boolean) {
            emit(lit.boolean_value() ? Op::False : Op::True, expr.location);
            return;
        }
    }

    compile_expression(*expr.operand);

    switch (expr.op) {
        case TokenType::Minus:
            emit(Op::Negate, expr.location);
            break;
        case TokenType::Bang:
            emit(Op::Not, expr.location);
            break;
        case TokenType::QuestionMark: {
            // The ? operator: unwrap success/some, or return early with failure/none.
            // Stack: [value]
            emit(Op::Dup, expr.location);       // [value, value]
            emit(Op::IsSuccess, expr.location); // [value, bool]

            auto skip = emit_jump(Op::JumpIfTrue, expr.location);

            // Failure/none path: pop bool, return the value as-is.
            emit(Op::Pop, expr.location); // [value]

            // Emit pending finally blocks before returning, just like
            // compile_return does, so that try/finally cleanup runs.
            emit_try_unwind(current_scope().exception_context.depth(), expr.location);

            emit(Op::Return, expr.location);

            // Success/some path: pop bool, unwrap the value.
            patch_jump(skip);
            emit(Op::Pop, expr.location);    // [value]
            emit(Op::Unwrap, expr.location); // [inner]
            break;
        }
        default: {
            auto e = compiler_errors::unknown_unary_operator();
            error(e.message, expr.location, e.hint);
            break;
        }
    }
}

void Compiler::compile_call(const CallExpression& expr) {
    compile_expression(*expr.callee);

    if (expr.arguments.size() > CompilerLimits::k_max_arguments) {
        auto e = compiler_errors::too_many_positional_arguments(CompilerLimits::k_max_arguments);
        error(e.message, expr.location, e.hint);
        return;
    }

    // The callee and each already-compiled argument remain on the operand stack
    // while later arguments are compiled. Reserve a placeholder local for every
    // such temporary so a value-producing block (match/if used as an expression)
    // argument computes local slot indices matching its true runtime position.
    std::size_t scratch = 0;

    // Compile positional arguments.
    for (const auto& arg : expr.arguments) {
        reserve_scratch_slots(1, expr.location);
        ++scratch;
        compile_expression(*arg);
    }

    if (!expr.named_arguments.empty()) {
        if (expr.named_arguments.size() > CompilerLimits::k_max_arguments) {
            release_scratch_slots(scratch);
            auto e = compiler_errors::too_many_named_arguments(CompilerLimits::k_max_arguments);
            error(e.message, expr.location, e.hint);
            return;
        }

        // Compile named arguments — push name-value pairs.
        for (const auto& named : expr.named_arguments) {
            reserve_scratch_slots(1, expr.location);
            ++scratch;
            (void)emit_constant(Value{named.name}, expr.location);
            reserve_scratch_slots(1, expr.location);
            ++scratch;
            compile_expression(*named.value);
        }

        release_scratch_slots(scratch);

        // Safe: size validated against CompilerLimits::k_max_arguments above
        emit_u8(Op::CallNamed, static_cast<std::uint8_t>(expr.arguments.size()), expr.location);
        emit_raw_byte(static_cast<std::uint8_t>(expr.named_arguments.size()));
    } else {
        release_scratch_slots(scratch);

        // Safe: size validated against CompilerLimits::k_max_arguments above
        emit_u8(Op::Call, static_cast<std::uint8_t>(expr.arguments.size()), expr.location);
    }
}

bool Compiler::compile_qualified_module_access(const FieldAccessExpression& expr) {
    // Walk the field-access chain to build a dotted path and emit GetGlobal.
    std::vector<std::string_view> parts;
    parts.push_back(expr.field_name);

    const Expression* obj = expr.object.get();

    while (obj->kind == ExpressionKind::FieldAccess) {
        const auto& fa = static_cast<const FieldAccessExpression&>(*obj);
        parts.push_back(fa.field_name);
        obj = fa.object.get();
    }

    if (obj->kind != ExpressionKind::Variable) {
        return false;
    }

    const auto& var = static_cast<const VariableExpression&>(*obj);

    // Only use qualified lookup if the root is not a local variable.
    if (resolve_local(var.name) || resolve_upvalue(var.name)) {
        return false;
    }

    parts.push_back(var.name);

    std::string qualified;
    qualified.reserve(64);

    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (it != parts.rbegin()) {
            qualified += '.';
        }
        qualified.append(*it);
    }

    auto name_idx = add_name(std::move(qualified));
    emit_u16(Op::GetGlobal, name_idx, expr.location);
    return true;
}

void Compiler::compile_field_access(const FieldAccessExpression& expr) {
    // Try qualified name resolution for stdlib module access (e.g. Array.filter).
    if (compile_qualified_module_access(expr)) {
        return;
    }

    compile_expression(*expr.object);

    auto name_idx = add_name(expr.field_name);

    if (expr.is_optional) {
        emit_u16(Op::GetFieldOpt, name_idx, expr.location);
    } else {
        emit_u16(Op::GetField, name_idx, expr.location);
    }
}

void Compiler::compile_lambda(const LambdaExpression& expr) {
    begin_function("<lambda>", static_cast<int>(expr.parameters.size()));

    {
        const ScopeDepthGuard scope(*this);

        for (const auto& param : expr.parameters) {
            // Slot index not needed; only registering the parameter name
            (void)declare_local(param.name, param.is_mutable, expr.location);
        }

        if (expr.is_expression_body() && (expr.expression_body() != nullptr)) {
            compile_expression(*expr.expression_body());
            emit(Op::Return, expr.location);
        } else {
            for (const auto& stmt : expr.statements()) {
                compile_statement(*stmt);
            }

            emit(Op::None, expr.location);
            emit(Op::Return, expr.location);
        }
    }

    auto compiled = end_function();

    // Check the upvalue count before emitting anything so that the error path
    // never leaves a truncated MakeClosure (missing its trailing upvalue-count
    // byte) in the enclosing chunk. This mirrors the ordering in
    // DeclarationCompiler::emit_closure_and_global.
    if (compiled.upvalue_count > CompilerLimits::k_max_upvalues) {
        auto e = compiler_errors::too_many_upvalues(CompilerLimits::k_max_upvalues);
        error(e.message, expr.location, e.hint);
        return;
    }

    // The function index is emitted as a u16 MakeClosure operand, so the
    // function table must not exceed the 16-bit index space; beyond it the cast
    // below would wrap and MakeClosure would reference the wrong function.
    if (ctx_.compiled_functions.functions.size() >= CompilerLimits::k_max_functions) {
        auto e = compiler_errors::too_many_functions(CompilerLimits::k_max_functions);
        error(e.message, expr.location, e.hint);
        return;
    }

    auto func_idx = static_cast<std::uint16_t>(ctx_.compiled_functions.functions.size());
    ctx_.compiled_functions.functions.push_back(std::move(compiled));

    emit_u16(Op::MakeClosure, func_idx, expr.location);

    const auto upvalue_count = ctx_.compiled_functions.functions.back().upvalue_count;
    emit_raw_byte(static_cast<std::uint8_t>(upvalue_count));
}

void Compiler::compile_if_expression(const IfExpression& expr) {
    compile_expression(*expr.condition);

    auto then_jump = emit_jump(Op::JumpIfFalse, expr.location);
    emit(Op::Pop, expr.location);

    // Then branch — expression or body.
    if (expr.then_expr() != nullptr) {
        compile_expression(*expr.then_expr());
    } else {
        const ScopeDepthGuard scope(*this);
        compile_body_as_expression(expr.then_body(), expr.location);
    }

    auto else_jump = emit_jump(Op::Jump, expr.location);
    patch_jump(then_jump);
    emit(Op::Pop, expr.location);

    // Else branch.
    if (expr.else_expr() != nullptr) {
        compile_expression(*expr.else_expr());
    } else if (!expr.else_body().empty()) {
        const ScopeDepthGuard scope(*this);
        compile_body_as_expression(expr.else_body(), expr.location);
    } else {
        emit(Op::None, expr.location);
    }

    patch_jump(else_jump);
}

void Compiler::compile_downcast(const DowncastExpression& expr) {
    compile_expression(*expr.operand);

    auto type_str = build_type_string(expr.target_type);
    auto name_idx = add_name(type_str);

    if (expr.is_trusted) {
        emit_u16(Op::TrustedDowncast, name_idx, expr.location);
    } else {
        emit_u16(Op::Downcast, name_idx, expr.location);
    }
}

void Compiler::compile_is(const IsExpression& expr) {
    compile_expression(*expr.operand);

    auto type_str = build_type_string(expr.target_type);
    auto name_idx = add_name(type_str);
    emit_u16(Op::IsType, name_idx, expr.location);
}

std::string Compiler::build_type_string(const TypeAnnotation& type) const {
    // Build a fully-qualified type string, recursing into tuple elements and
    // generic type arguments so that every nesting level is preserved (e.g.
    // "dictionary<array<integer>>", "result<integer,string>").  The VM relies on
    // this string to verify type arguments at runtime for is<T>/downcast<T>.
    if (type.kind() == TypeAnnotationKind::Tuple && !type.tuple_elements().empty()) {
        std::string result = "(";
        const auto& elems = type.tuple_elements();

        for (std::size_t i = 0; i < elems.size(); ++i) {
            if (i > 0) {
                result += ",";
            }

            result += build_type_string(elems[i]);
        }

        result += ")";
        return result;
    }

    if (!type.type_params().empty()) {
        std::string result = type.name();
        result += "<";

        const auto& params = type.type_params();

        for (std::size_t i = 0; i < params.size(); ++i) {
            if (i > 0) {
                result += ",";
            }

            result += build_type_string(params[i]);
        }

        result += ">";
        return result;
    }

    return type.name();
}

void Compiler::compile_success(const SuccessExpression& expr) {
    compile_expression(*expr.value);
    emit(Op::MakeSuccess, expr.location);
}

void Compiler::compile_failure(const FailureExpression& expr) {
    compile_expression(*expr.message);
    emit(Op::MakeFailure, expr.location);
}

void Compiler::compile_some(const SomeExpression& expr) {
    compile_expression(*expr.value);
    emit(Op::MakeSome, expr.location);
}

void Compiler::compile_range(const RangeExpression& expr) {
    compile_expression(*expr.start);
    // The start value remains on the stack while the end is compiled.
    {
        const ScratchSlotGuard scratch{access_, 1, expr.location};
        compile_expression(*expr.end);
    }

    emit(expr.inclusive ? Op::MakeRangeInc : Op::MakeRange, expr.location);
}

void Compiler::compile_spawn(const SpawnExpression& expr) {
    // Decompose the call expression: push callee and args individually,
    // then emit Spawn with the arg count so the VM can run it in a thread.
    const auto& call = static_cast<const CallExpression&>(*expr.call);

    compile_expression(*call.callee);

    if (call.arguments.size() > CompilerLimits::k_max_arguments) {
        auto e = compiler_errors::too_many_spawn_arguments(CompilerLimits::k_max_arguments);
        error(e.message, expr.location, e.hint);
        return;
    }

    // The callee and each compiled argument stay on the operand stack while the
    // remaining arguments are compiled; reserve placeholder locals so a
    // value-producing block argument computes correct local slots.
    std::size_t scratch = 0;

    for (const auto& arg : call.arguments) {
        reserve_scratch_slots(1, expr.location);
        ++scratch;
        compile_expression(*arg);
    }

    release_scratch_slots(scratch);

    // Safe: size validated against CompilerLimits::k_max_arguments above
    emit_u8(Op::Spawn, static_cast<std::uint8_t>(call.arguments.size()), expr.location);
}

void Compiler::compile_await(const AwaitExpression& expr) {
    compile_expression(*expr.operand);
    emit(Op::Await, expr.location);
}

void Compiler::compile_task_scope(const TaskScopeExpression& expr) {
    emit(Op::TaskScopeBegin, expr.location);

    {
        const ScopeDepthGuard scope(*this);

        for (const auto& stmt : expr.body) {
            compile_statement(*stmt);
        }
    }

    emit(Op::TaskScopeEnd, expr.location);
}

} // namespace luma
