#include "runtime/compiler/declaration_compiler.hpp"

#include <algorithm>
#include <format>

#include "analysis/ast/ast_dispatch.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_errors.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/i_compilation_backend.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/scratch_slot_guard.hpp"
#include "runtime/interpreter/value.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

// ─────────── Declaration dispatch ───────────

void DeclarationCompiler::compile_declaration(const Declaration& decl) {
    dispatch_declaration(decl, [this](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, FunctionDeclaration>) {
            compile_function_decl(node);
        } else if constexpr (std::is_same_v<T, ChoiceDeclaration>) {
            compile_choice_declaration(node);
        } else if constexpr (std::is_same_v<T, RecordDeclaration>) {
            compile_record_declaration(node);
        } else if constexpr (std::is_same_v<T, NamespaceDeclaration>) {
            compile_namespace_declaration(node);
        } else if constexpr (std::is_same_v<T, UseDeclaration>) {
            compile_use_declaration(node);
        }
        // Include, Interface, TypeAlias — compile-time only, no runtime representation.
    });
}

// ─────────── Function declarations ───────────

void DeclarationCompiler::compile_function_decl(const FunctionDeclaration& decl) {
    emit_function_definition(decl.name, decl);
}

void DeclarationCompiler::emit_default_parameter_values(const FunctionDeclaration& func) {
    // Emit default value initialisation for optional parameters.
    // At call time, missing arguments are padded with None values.
    // For each optional parameter, check if the slot is None and replace
    // it with the compiled default expression.
    for (std::size_t i = 0; i < func.parameters.size(); ++i) {
        const auto& param = func.parameters[i];

        if (param.default_value) {
            // slot index = i + k_first_parameter_slot (slot 0 is the function itself)
            auto slot = static_cast<std::uint16_t>(i + CompilerLimits::k_first_parameter_slot);

            // GetLocal slot — push the param value
            api_.emit_u16(Op::GetLocal, slot, func.location);
            // Push None for comparison
            api_.emit(Op::None, func.location);
            // Compare: param == None?
            api_.emit(Op::Equal, func.location);
            // JumpIfFalse — skip default assignment if param was provided
            auto skip_jump = api_.emit_jump(Op::JumpIfFalse, func.location);
            // Compile the default expression. JumpIfFalse peeks (does not pop)
            // its boolean condition, so that temporary stays live on the operand
            // stack while the default value is compiled. Reserve one scratch slot
            // so a default that itself declares locals (e.g. a match/if value
            // block) computes slot indices matching its true runtime stack
            // position, mirroring compile_range/compile_call/compile_spawn.
            {
                const ScratchSlotGuard scratch{api_, 1, func.location};
                api_.compile_expression(*param.default_value);
            }
            // SetLocal slot — store default value
            api_.emit_u16(Op::SetLocal, slot, func.location);
            api_.emit(Op::Pop, func.location);
            // Patch the jump
            api_.patch_jump(skip_jump);
            // Pop the boolean from the Equal comparison (on both paths).
            api_.emit(Op::Pop, func.location);
        }
    }
}

void DeclarationCompiler::emit_closure_and_global(const std::string& name,
                                                  CompiledFunction compiled,
                                                  const FunctionDeclaration& func) {
    if (compiled.upvalue_count > CompilerLimits::k_max_upvalues) {
        auto e = compiler_errors::too_many_upvalues(CompilerLimits::k_max_upvalues);
        api_.error(e.message, func.location, e.hint);
        return;
    }

    auto& ctx = api_.ctx();

    // The function index is emitted as a u16 MakeClosure operand, so the
    // function table must not exceed the 16-bit index space; beyond it the cast
    // below would wrap and MakeClosure would reference the wrong function.
    if (ctx.compiled_functions.functions.size() >= CompilerLimits::k_max_functions) {
        auto e = compiler_errors::too_many_functions(CompilerLimits::k_max_functions);
        api_.error(e.message, func.location, e.hint);
        return;
    }

    auto func_idx = static_cast<std::uint16_t>(ctx.compiled_functions.functions.size());
    ctx.compiled_functions.functions.push_back(std::move(compiled));

    api_.emit_u16(Op::MakeClosure, func_idx, func.location);

    auto upvalue_count = ctx.compiled_functions.functions.back().upvalue_count;

    api_.emit_raw_byte(static_cast<std::uint8_t>(upvalue_count));

    auto name_idx = api_.add_name(name);
    api_.emit_u16(Op::SetGlobal, name_idx, func.location);
    api_.emit(Op::Pop, func.location);
}

void DeclarationCompiler::emit_function_definition(const std::string& name,
                                                   const FunctionDeclaration& func) {
    api_.begin_function(name, static_cast<int>(func.parameters.size()));

    {
        const ICompilationBackend::ScopeGuard scope(api_);

        // Count required parameters (those without default values).
        int required_count = 0;

        for (const auto& param : func.parameters) {
            if (!param.default_value) {
                ++required_count;
            }
        }

        api_.current_scope().function.required_arity = required_count;

        for (const auto& param : func.parameters) {
            // Slot index not needed; only registering the parameter name
            (void)api_.declare_local(param.name, param.is_mutable, func.location);
            api_.current_scope().function.param_names.push_back(param.name);
        }

        emit_default_parameter_values(func);

        for (const auto& stmt : func.body) {
            api_.compile_statement(*stmt);

            // Dead code elimination: stop emitting after an unconditional return.
            if (stmt->kind == StatementKind::Return) {
                break;
            }
        }
    }

    auto compiled = api_.end_function();
    compiled.is_main = func.is_main;
    compiled.is_test = func.is_test;

    emit_closure_and_global(name, std::move(compiled), func);
}

// ─────────── Choice declarations ───────────

void DeclarationCompiler::compile_choice_declaration(const ChoiceDeclaration& choice) {
    emit_qualified_choice_variants(choice.name, choice, choice.location);
}

void DeclarationCompiler::emit_choice_variant(const std::string& type_name,
                                              const std::string& variant_name,
                                              std::size_t field_count, SourceLocation loc) {
    if (field_count == 0) {
        // Unit variant — emit as a constant choice value.
        api_.emit_constant(Value{type_name}, loc);
        api_.emit_constant(Value{variant_name}, loc);
        api_.emit(Op::MakeChoice, loc);
    } else {
        // Data variant — emit a constructor factory.
        // MakeChoiceConstructor pops type_name and variant_name from the stack
        // and creates a NativeFunctionValue constructor at runtime.  This avoids
        // storing non-serialisable NativeFunctionValue objects in the constant
        // pool, which would become None when loaded from the bytecode cache.
        if (field_count > CompilerLimits::k_max_arguments) {
            auto e =
                compiler_errors::too_many_choice_variant_fields(CompilerLimits::k_max_arguments);
            api_.error(e.message, loc, e.hint);
            return;
        }
        api_.emit_constant(Value{type_name}, loc);
        api_.emit_constant(Value{variant_name}, loc);
        api_.emit_u8(Op::MakeChoiceConstructor, static_cast<std::uint8_t>(field_count), loc);
    }
}

void DeclarationCompiler::emit_qualified_choice_variants(const std::string& type_name,
                                                         const ChoiceDeclaration& choice,
                                                         SourceLocation loc) {
    for (const auto& variant : choice.variants) {
        auto qualified = make_qualified(type_name, variant.name);
        auto name_idx = api_.add_name(qualified);
        emit_choice_variant(type_name, variant.name, variant.fields.size(), loc);
        api_.emit_u16(Op::SetGlobal, name_idx, loc);
        api_.emit(Op::Pop, loc);
    }
}

// ─────────── Record declarations ───────────

void DeclarationCompiler::compile_record_declaration(const RecordDeclaration& record) {
    // Records are types — register the constructor as a global.
    auto name_idx = api_.add_name(record.name);

    api_.emit(Op::None, record.location); // Placeholder for record constructor.
    api_.emit_u16(Op::SetGlobal, name_idx, record.location);
    api_.emit(Op::Pop, record.location);

    // Store declaration for default-value lookup during record creation.
    api_.ctx().record_declarations.declarations.insert_or_assign(record.name, std::cref(record));
}

// ─────────── Namespace declarations ───────────

void DeclarationCompiler::compile_namespace_declaration(const NamespaceDeclaration& ns) {
    for (const auto& inner : ns.declarations) {
        if (inner->kind == DeclarationKind::Function) {
            const auto& func = static_cast<const FunctionDeclaration&>(*inner);
            auto qualified = make_qualified(ns.name, func.name);
            emit_function_definition(qualified, func);
        } else if (inner->kind == DeclarationKind::Record) {
            // Register the record under its qualified name.
            const auto& record = static_cast<const RecordDeclaration&>(*inner);
            auto qualified = make_qualified(ns.name, record.name);
            auto name_idx = api_.add_name(qualified);

            api_.emit(Op::None, ns.location);
            api_.emit_u16(Op::SetGlobal, name_idx, ns.location);
            api_.emit(Op::Pop, ns.location);

            api_.ctx().record_declarations.declarations.insert_or_assign(qualified,
                                                                         std::cref(record));

            // Also register the unqualified name for use within the namespace.
            api_.ctx().record_declarations.declarations.insert_or_assign(record.name,
                                                                         std::cref(record));
        } else if (inner->kind == DeclarationKind::Choice) {
            // Register choice variants under qualified names.
            const auto& choice = static_cast<const ChoiceDeclaration&>(*inner);
            auto type_name = make_qualified(ns.name, choice.name);
            emit_qualified_choice_variants(type_name, choice, ns.location);
        } else {
            // Recurse for nested declarations.
            compile_declaration(*inner);
        }
    }
}

// ─────────── Use declarations ───────────

void DeclarationCompiler::compile_use_declaration(const UseDeclaration& use_decl) {
    // Use declarations create global aliases so that bare names
    // resolve to their namespace-qualified counterparts at runtime.
    const auto& path = use_decl.namespace_path;
    const auto split = split_module(path);

    if (!split) {
        compile_wildcard_import(path, use_decl.location);
    } else {
        compile_specific_import(path, std::string{split->first}, std::string{split->second},
                                use_decl.location);
    }
}

const NamespaceDeclaration* DeclarationCompiler::find_namespace(const std::string& name) const {
    const auto& declarations = api_.ctx().program->declarations;
    const auto it = std::ranges::find_if(declarations, [&name](const auto& decl) {
        return decl->kind == DeclarationKind::Namespace &&
               static_cast<const NamespaceDeclaration&>(*decl).name == name;
    });

    if (it != declarations.end()) {
        return &static_cast<const NamespaceDeclaration&>(**it);
    }

    return nullptr;
}

void DeclarationCompiler::compile_wildcard_import(const std::string& ns_name, SourceLocation loc) {
    const auto* ns = find_namespace(ns_name);

    if (ns == nullptr) {
        auto e = compiler_errors::namespace_not_found(ns_name);
        api_.error(e.message, loc, e.hint);
        return;
    }

    for (const auto& inner : ns->declarations) {
        if (inner->is_internal_to_namespace) {
            continue;
        }

        if (inner->kind == DeclarationKind::Function) {
            const auto& func = static_cast<const FunctionDeclaration&>(*inner);
            emit_global_alias(make_qualified(ns->name, func.name), func.name, loc);
        } else if (inner->kind == DeclarationKind::Record) {
            const auto& rec = static_cast<const RecordDeclaration&>(*inner);
            api_.ctx().record_declarations.declarations.insert_or_assign(rec.name, std::cref(rec));
        } else if (inner->kind == DeclarationKind::Choice) {
            const auto& choice = static_cast<const ChoiceDeclaration&>(*inner);
            emit_choice_aliases(ns->name, choice, loc);
        }
    }
}

void DeclarationCompiler::compile_specific_import(const std::string& path,
                                                  const std::string& ns_name,
                                                  const std::string& member_name,
                                                  SourceLocation loc) {
    const auto* ns = find_namespace(ns_name);

    if (ns == nullptr) {
        emit_global_alias(path, member_name, loc);
        return;
    }

    for (const auto& inner : ns->declarations) {
        if (inner->kind == DeclarationKind::Function) {
            const auto& func = static_cast<const FunctionDeclaration&>(*inner);

            if (func.name == member_name) {
                emit_global_alias(path, member_name, loc);
                return;
            }
        } else if (inner->kind == DeclarationKind::Record) {
            const auto& rec = static_cast<const RecordDeclaration&>(*inner);

            if (rec.name == member_name) {
                api_.ctx().record_declarations.declarations.insert_or_assign(rec.name,
                                                                             std::cref(rec));
                return;
            }
        } else if (inner->kind == DeclarationKind::Choice) {
            const auto& choice = static_cast<const ChoiceDeclaration&>(*inner);

            if (choice.name == member_name) {
                emit_choice_aliases(ns_name, choice, loc);
                return;
            }
        }
    }

    // Fallback: try as a global alias.
    emit_global_alias(path, member_name, loc);
}

void DeclarationCompiler::emit_global_alias(const std::string& qualified, const std::string& bare,
                                            SourceLocation loc) {
    auto qual_idx = api_.add_name(qualified);
    auto bare_idx = api_.add_name(bare);

    api_.emit_u16(Op::GetGlobal, qual_idx, loc);
    api_.emit_u16(Op::SetGlobal, bare_idx, loc);
    api_.emit(Op::Pop, loc);
}

void DeclarationCompiler::emit_choice_aliases(const std::string& ns_name,
                                              const ChoiceDeclaration& choice, SourceLocation loc) {
    for (const auto& variant : choice.variants) {
        auto qualified = make_qualified(make_qualified(ns_name, choice.name), variant.name);
        auto bare = make_qualified(choice.name, variant.name);
        emit_global_alias(qualified, bare, loc);
    }
}

} // namespace luma
