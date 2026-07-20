// ─────────────────────────────────────────────────────────────────────────────
// DeclarationCompiler
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Compile all top-level and namespace-level declarations
//   (functions, choice types, records, namespaces, use declarations), extracted
//   from the Compiler class.
//
// Design: Holds a non-owning reference to an ICompilationBackend interface, which
//   provides controlled access to Compiler's emit, scope, variable management,
//   and compilation methods.
//
// ICompilationBackend methods used (broadest subset — touches all major API groups):
//   - emit()               — single-byte opcodes (None, Pop, etc.)
//   - emit_u8()            — MakeChoiceConstructor with field count
//   - emit_u16()           — GetLocal / SetLocal / SetGlobal / GetGlobal / MakeClosure
//   - emit_raw_byte()      — upvalue count and field count operands
//   - emit_constant()      — type-name and variant-name string constants
//   - emit_jump()          — default-parameter skip jumps
//   - patch_jump()         — back-patch default-parameter jumps
//   - begin_function() / end_function() — function scope lifecycle
//   - declare_local()      — function parameter slots
//   - current_scope()      — set required_arity and param_names on the scope
//   - add_name()           — intern global names for SetGlobal
//   - compile_statement()  — function body statements
//   - error()              — function compilation error reporting
//   - error_limit_exceeded() — variant field count limit
//   - ctx()                — access program, record_declarations
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_DECLARATION_COMPILER_HPP
#define LUMA_COMPILER_DECLARATION_COMPILER_HPP

#include <cstddef>
#include <memory>
#include <string>

#include "analysis/ast/declaration.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler_helper.hpp"

namespace luma {

// Compiles top-level and namespace-level declarations: functions, choice
// types, records, namespaces, and use (import) declarations.
// Lifetime is bounded by the Compiler that owns it by value.
class DeclarationCompiler : public CompilerHelper {
public:
    using CompilerHelper::CompilerHelper;

    void compile_declaration(const Declaration& decl);
    void compile_function_decl(const FunctionDeclaration& decl);
    void compile_choice_declaration(const ChoiceDeclaration& decl);
    void compile_record_declaration(const RecordDeclaration& decl);
    void compile_namespace_declaration(const NamespaceDeclaration& decl);
    void compile_use_declaration(const UseDeclaration& decl);

private:
    void emit_choice_variant(const std::string& type_name, const std::string& variant_name,
                             std::size_t field_count, SourceLocation loc);
    void emit_qualified_choice_variants(const std::string& type_name,
                                        const ChoiceDeclaration& choice, SourceLocation loc);
    void emit_function_definition(const std::string& name, const FunctionDeclaration& func);
    void emit_default_parameter_values(const FunctionDeclaration& func);
    void emit_closure_and_global(const std::string& name, CompiledFunction compiled,
                                 const FunctionDeclaration& func);

    // Use declaration helpers.
    void compile_wildcard_import(const std::string& ns_name, SourceLocation loc);
    void compile_specific_import(const std::string& path, const std::string& ns_name,
                                 const std::string& member_name, SourceLocation loc);
    [[nodiscard]] const NamespaceDeclaration* find_namespace(const std::string& name) const;
    void emit_global_alias(const std::string& qualified, const std::string& bare,
                           SourceLocation loc);
    void emit_choice_aliases(const std::string& ns_name, const ChoiceDeclaration& choice,
                             SourceLocation loc);
};

} // namespace luma

#endif // LUMA_COMPILER_DECLARATION_COMPILER_HPP
