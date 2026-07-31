// ─────────────────────────────────────────────────────────────────────────────
// Compiler Module
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Compile a type-checked AST into bytecode for the VM.
//
// Key Types:
//   - Compiler: Main class that traverses the AST and emits bytecode.
//   - Chunk: A sequence of bytecode instructions and constants.
//   - CompiledFunction: A chunk associated with a function name and arity.
//   - Opcode: A single bytecode instruction.
//
// Dependencies:
//   - analysis/ast: For traversing the AST.
//   - runtime/vm: For opcode and value definitions.
//
// ─── Method naming conventions ───────────────────────────────────────────────
//   compile_<node>()  — Takes an AST node; drives compilation of one language
//                       construct (may call emit_* internally).
//   emit_<semantic>() — Emits one or more bytecode instructions for a named
//                       semantic action (e.g. emit_variable_store, emit_loop).
//   emit_<opcode>()   — Emits exactly one specific opcode (e.g. emit_constant,
//                       emit_jump).  Low-level encoding helpers.
//   add_<table>()     — Adds an entry to a compile-time pool or table and
//                       returns its index (e.g. add_name).
//   resolve_<thing>() — Looks up a variable, upvalue, or qualified name
//                       without emitting bytecode.
//   declare_*()       — Registers a new local variable or scope entity.
//   begin_*/end_*()   — Paired scope lifecycle management (begin_scope /
//                       end_scope, begin_function / end_function, etc.).
//
// The compile_* / emit_* split is intentional: compile_* methods decide
// *what* to generate from AST semantics; emit_* methods handle *how* bytes
// are encoded into the current chunk.
//
// ─── Helper classes ─────────────────────────────────────────────────────────
//   LoopCompiler            For-loop and while-loop compilation
//   PatternCompiler         Match expression pattern compilation
//   BinaryOperatorCompiler  Binary operator opcode selection
//   CollectionCompiler      Array/dict/tuple literal compilation
//   DeclarationCompiler     Function/record/choice declarations
//   InterpolationHandler    String interpolation compilation
//   VariableResolver        Variable name resolution
//   ConstantFolder          Compile-time constant folding
//
// ─── Control flow abstractions ──────────────────────────────────────────────
// Jump emission and patching are already centralised — no ControlFlowManager
// is needed.  The existing layers cover the need completely and consistently:
//
//   emit_jump / patch_jump / emit_loop (raw encoding):
//     Chunk (chunk.hpp)
//       └─ BytecodeEmitter (bytecode_emitter.hpp) — abstract virtual interface
//            └─ Emitter (emitter.hpp) — concrete impl wrapping a Chunk
//                 └─ Compiler::emit_jump/patch_jump/emit_loop (this file,
//                    inline methods) — used by all code inside compiler_*.cpp
//                      └─ CompilerAccess::emit_jump/patch_jump/emit_loop
//                         (compiler_access.hpp) — used by ALL helper classes
//
//   All helper classes call api_.emit_jump / api_.patch_jump / api_.emit_loop
//   through CompilerAccess — there is no scattered re-implementation.
//
//   break / continue tracking:
//     LoopContext (loop_context.hpp) — centralises per-scope loop state.
//       Embedded in CompilerScope::loop_context (this file).
//       Managed via Compiler::begin_loop / end_loop (compiler.cpp).
//       break  → statement_compiler.cpp calls loop_context.add_break(jump)
//       continue → statement_compiler.cpp calls loop_context.current_start()
//       Helper classes enter/exit loops via api_.begin_loop / api_.end_loop.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_COMPILER_HPP
#define LUMA_COMPILER_COMPILER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/ast/ast_dispatcher.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compile_result.hpp"
// Helper class headers — full includes required.
//
// Forward declarations are NOT possible here because:
//   - CompilerAccess, VariableResolver, ConstantFolder, PatternCompiler,
//     LoopCompiler, BinaryOperatorCompiler, CollectionCompiler,
//     InterpolationHandler, and DeclarationCompiler are all held as direct
//     by-value members of Compiler (sizeof must be known).
//   - CompilerLimits provides static constexpr values used in this header.
//   - Emitter is returned by value from an inline method.
//   - ExceptionContext is held by value inside CompilerScope.
//   - Op (opcode enum) appears in method signatures throughout the class.
//
// Switching to std::unique_ptr would break the current zero-allocation
// design and require heap allocation for each helper on every compile()
// call, which is not worthwhile for this internal header.
#include "runtime/compiler/binary_operator_compiler.hpp"
#include "runtime/compiler/collection_compiler.hpp"
#include "runtime/compiler/compilation_context.hpp"
#include "runtime/compiler/compiler_access.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/const_folder.hpp"
#include "runtime/compiler/declaration_compiler.hpp"
#include "runtime/compiler/emitter.hpp"
#include "runtime/compiler/exception_context.hpp"
#include "runtime/compiler/interpolation_handler.hpp"
#include "runtime/compiler/loop_compiler.hpp"
#include "runtime/compiler/loop_context.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/pattern_compiler.hpp"
#include "runtime/compiler/scratch_slot_guard.hpp"
#include "runtime/compiler/variable_resolver.hpp"

namespace luma {

// Forward declarations
struct Diagnostic;
class Value;

// NOTE(refactor/C1): Compiler class decomposition — current status.
//
// The Compiler class was originally identified as a "god class" candidate.
// After the refactoring effort, it has been significantly decomposed:
//
//   Extraction status:
//     ✓ VariableResolver        — scope lifecycle and local/upvalue/global resolution
//     ✓ ConstantFolder          — compile-time constant folding (depends on IConstantEmitter)
//     ✓ PatternCompiler         — match expressions, match statements, destructuring
//     ✓ LoopCompiler            — for and while loop compilation
//     ✓ BinaryOperatorCompiler  — binary and compound-assignment operators
//     ✓ CollectionCompiler      — array/dict/tuple/record literals and index access
//     ✓ InterpolationHandler    — string interpolation compilation
//     ✓ DeclarationCompiler     — function/record/choice/namespace/use declarations
//     ✓ ICompilationBackend     — pure virtual interface decoupling helpers from Compiler
//     ✓ IConstantEmitter        — narrow interface for ConstantFolder
//     ✓ CompilationContext      — mutable state extracted to compilation_context.hpp
//     ✓ CompiledFunction        — function metadata extracted to compiled_function.hpp
//
//   What remains in Compiler:
//     - CRTP visitor forwarders (~40 trivial one-liners mandated by the dispatch pattern)
//     - Inline emit_* helpers (~20 lines) delegating to Emitter
//     - compile_expression/statement dispatch entry points
//     - Expression-specific compilation (expression_compiler.cpp, 354 lines)
//     - Statement-specific compilation (statement_compiler.cpp, 293 lines)
//     - Assignment compilation (assignment_compiler.cpp, 181 lines)
//     - Pipe compilation (pipe_compiler.cpp, 87 lines)
//
//   Why further decomposition (ExpressionCompiler, StatementCompiler classes)
//   is not pursued:
//     1. The CRTP dispatcher pattern requires visit_* on the same class.
//     2. Expression and statement compilation cross-call each other frequently
//        (compile_expression from statements, compile_statement from expressions).
//     3. All sub-compilers would need shared CompilationContext + mutual references,
//        creating circular dependencies without reducing coupling.
//     4. The existing file-based decomposition (.cpp files) already provides
//        navigability and compilation-unit isolation.
//
//   The Compiler class now functions as a thin facade/dispatcher coordinating
//   8 focused helper classes through the ICompilationBackend interface.
//
// Compiles a type-checked AST into bytecode.
//
// Inherits from the CRTP dispatcher bases (ast_dispatcher.hpp) so that
// compile_expression() and compile_statement() reduce to a single
// dispatch_expr() / dispatch_stmt() call.  Each visit_X() method below
// is an inline forwarder to the corresponding compile_X() method.
//
// The CRTP dispatchers use a switch on the node kind enum with no default
// case, so the compiler emits a -Wswitch warning when a new kind is added
// to the enum without updating the dispatcher — preserving exhaustiveness.
//
// Bytecode emission abstraction: all bytecode is emitted through the
// emit_*(), add_name(), emit_raw_byte(), emit_raw_u16(), current_offset()
// helper methods rather than accessing the Chunk directly.  This decouples
// AST traversal from bytecode encoding details.
//
// Scope and variable management is delegated to VariableResolver, which
// owns scope lifecycle (begin/end scope) and variable resolution
// (local → upvalue → global).  The Compiler retains thin inline wrappers
// for call-site convenience.
class Compiler : public ExpressionDispatcher<Compiler, void>, public StatementDispatcher<Compiler> {
public:
    Compiler() = default;

    // Compile a complete program.
    // Returns the compiled functions and any diagnostics.
    using CompileResult = luma::CompileResult;

    [[nodiscard]] CompileResult compile(const Program& program, bool repl_mode = false);

    // Hidden local variable names — see HiddenVar enum in hidden_var.hpp.

private:
    // CompilerAccess is the sole friend — it exposes a controlled subset of
    // Compiler's private API to helper classes, replacing the previous 7
    // unrestricted friend declarations.
    friend class CompilerAccess;

    // Allow the CRTP dispatcher bases to call the private visit_X() handlers.
    friend class ExpressionDispatcher<Compiler, void>;
    friend class StatementDispatcher<Compiler>;

    // ─── Scope management (delegated to VariableResolver) ───
    void begin_scope() {
        variable_resolver_.begin_scope();
    }

    void end_scope() {
        variable_resolver_.end_scope();
    }

    // RAII guard that calls begin_scope() on construction and end_scope()
    // on destruction, making scope management exception-safe.
    //
    // Not a duplicate of luma::ScopeGuard<Func> (core/common/scope_guard.hpp)
    // or ICompilationBackend::ScopeGuard (i_compilation_backend.hpp):
    //   - luma::ScopeGuard is a generic cleanup-only guard (no setup on construction).
    //   - ScopeDepthGuard performs both setup (begin_scope) and teardown (end_scope).
    //   - ICompilationBackend::ScopeGuard is the same pattern but routed through
    //     the virtual interface for use by helper classes.
    class ScopeDepthGuard {
    public:
        explicit ScopeDepthGuard(Compiler& c) : compiler_(c) {
            c.begin_scope();
        }

        ~ScopeDepthGuard() {
            compiler_.end_scope();
        }

        ScopeDepthGuard(const ScopeDepthGuard&) = delete;
        ScopeDepthGuard& operator=(const ScopeDepthGuard&) = delete;

    private:
        Compiler& compiler_;
    };

    void begin_loop(std::size_t loop_start);
    void end_loop();
    void begin_function(const std::string& name, int arity);
    [[nodiscard]] CompiledFunction end_function();

    [[nodiscard]] std::uint16_t declare_local(std::string_view name, bool is_mutable,
                                              SourceLocation loc = {}) {
        return variable_resolver_.declare_local(name, is_mutable, loc);
    }

    [[nodiscard]] std::optional<std::uint16_t> resolve_local(std::string_view name) const {
        return variable_resolver_.resolve_local(name);
    }

    [[nodiscard]] std::optional<std::uint16_t> resolve_upvalue(std::string_view name) {
        return variable_resolver_.resolve_upvalue(name);
    }

    // Unified 3-level resolution: local → upvalue → global.
    [[nodiscard]] VarSlot resolve_variable(std::string_view name, const SourceLocation& loc) {
        return variable_resolver_.resolve_variable(name, loc);
    }

    // ─── Declaration compilation (delegated to DeclarationCompiler) ───
    void compile_declaration(const Declaration& decl) {
        declaration_compiler_.compile_declaration(decl);
    }

    // ─── Statement compilation ───
    void compile_statement(const Statement& stmt);
    void emit_try_unwind(std::size_t count, SourceLocation loc);
    void emit_loop_scope_unwind(SourceLocation loc);
    void compile_variable_decl(const VariableDeclStatement& stmt);
    void compile_assignment(const AssignmentStatement& stmt);
    void compile_compound_assignment(const CompoundAssignmentStatement& stmt);

    // Assignment target helpers — one per target kind (Variable, FieldAccess, IndexAccess).
    void compile_assign_to_variable(const VariableExpression& var, const Expression& value,
                                    SourceLocation loc);
    void compile_assign_to_field(const FieldAccessExpression& field, const Expression& value,
                                 SourceLocation loc);
    void compile_assign_to_index(const IndexAccessExpression& index, const Expression& value,
                                 SourceLocation loc);
    void compile_compound_assign_to_variable(const VariableExpression& var, const Expression& value,
                                             TokenType op, SourceLocation loc);
    void compile_compound_assign_to_field(const FieldAccessExpression& field,
                                          const Expression& value, TokenType op,
                                          SourceLocation loc);
    void compile_compound_assign_to_index(const IndexAccessExpression& index,
                                          const Expression& value, TokenType op,
                                          SourceLocation loc);

    // Common logic for compound assignment: compile RHS, apply compound operator.
    // Caller provides fetch (load current value onto stack) and store (write back)
    // as lambdas, reducing duplication across the three assignment target variants.
    // Templatised to avoid std::function vtable overhead — all call sites are in the
    // same translation unit (assignment_compiler.cpp) and pass small lambdas.
    // temps_beneath is the number of operand-stack temporaries left by emit_fetch
    // (and the target sub-expressions) that remain live while the right-hand side
    // is compiled; placeholder locals are reserved for them so a value-producing
    // block right-hand side computes correct local slots.
    template <typename EmitFetch, typename EmitStore>
    void compile_compound_op_common(EmitFetch&& emit_fetch, EmitStore&& emit_store,
                                    const Expression& rhs, TokenType op, SourceLocation loc,
                                    std::size_t temps_beneath);

    // Shared variable-store logic: widening check → set opcode → pop.
    // When use_set_local_pop is true, locals use SetLocalPop (which
    // already discards the value) instead of SetLocal + Pop.
    void emit_variable_store(const VarSlot& resolved, bool use_set_local_pop, SourceLocation loc);
    void emit_field_set_pop(std::uint16_t name_idx, SourceLocation loc);
    void emit_index_set_pop(SourceLocation loc);

    // Reserve `count` placeholder local slots to account for operand-stack
    // temporaries that are live while a value sub-expression is compiled.
    // Local slots are assigned as `locals.size() - 1` (see declare_local),
    // which assumes the value being bound is the first temporary above the
    // locals region. When temporaries are already on the operand stack (e.g.
    // the container and index of `container[index] = value`), a value
    // expression that declares locals mid-compilation — such as a match/if
    // value block — would otherwise compute slots that ignore those
    // temporaries and corrupt the stack. Reserving placeholders keeps slot
    // indices aligned with true runtime stack positions; release_scratch_slots
    // drops them afterwards without emitting runtime pops (the temporaries are
    // consumed by the trailing IndexSet/SetField instead).
    void reserve_scratch_slots(std::size_t count, SourceLocation loc);
    void release_scratch_slots(std::size_t count);

    void compile_unary_mutation(const Expression& target, Op local_op, Op op, SourceLocation loc);
    void compile_return(const ReturnStatement& stmt);

    void compile_for(const ForStatement& stmt) {
        loop_compiler_.compile_for(stmt);
    }

    void compile_for_iteration(const ForStatement& stmt, const ForIterationState& state);
    void compile_if_statement(const IfStatement& stmt);

    void compile_while(const WhileStatement& stmt) {
        loop_compiler_.compile_while(stmt);
    }

    void compile_match_statement(const MatchStatement& stmt) {
        pattern_compiler_.compile_match_statement(stmt);
    }

    void compile_match_statement_as_expression(const MatchStatement& stmt) {
        pattern_compiler_.compile_match_statement_as_expression(stmt);
    }

    void compile_try(const TryStatement& stmt);
    void compile_try_catch_finally(const TryStatement& stmt);
    void compile_finally_body(const TryStatement& stmt);
    void compile_expression_statement(const ExpressionStatement& stmt);
    void compile_body_as_expression(const std::vector<StatementPtr>& body, SourceLocation loc);

    void compile_tuple_destructuring(const TupleDestructuringStatement& stmt) {
        pattern_compiler_.compile_tuple_destructuring(stmt);
    }

    void compile_record_destructuring(const RecordDestructuringStatement& stmt) {
        pattern_compiler_.compile_record_destructuring(stmt);
    }

    void compile_block(const BlockStatement& stmt);

    // ─── Expression compilation ───
    void compile_expression(const Expression& expr);
    void compile_literal(const LiteralExpression& expr);
    void compile_variable(const VariableExpression& expr);

    void compile_binary(const BinaryExpression& expr) {
        binary_op_compiler_.compile_binary(expr);
    }

    template <typename FoldOp>
    void compile_short_circuit(const BinaryExpression& expr, Op jump_op, FoldOp fold);

    [[nodiscard]] bool try_fold_binary_at_compile_time(const LiteralExpression& lhs,
                                                       const LiteralExpression& rhs, TokenType op,
                                                       SourceLocation loc) {
        return const_folder_.try_fold_binary_at_compile_time(lhs, rhs, op, loc);
    }

    void compile_unary(const UnaryExpression& expr);
    void compile_call(const CallExpression& expr);
    void compile_field_access(const FieldAccessExpression& expr);
    [[nodiscard]] bool compile_qualified_module_access(const FieldAccessExpression& expr);

    void compile_index_access(const IndexAccessExpression& expr) {
        collection_compiler_.compile_index_access(expr);
    }

    void compile_lambda(const LambdaExpression& expr);
    void compile_if_expression(const IfExpression& expr);

    void compile_match_expression(const MatchExpression& expr) {
        pattern_compiler_.compile_match_expression(expr);
    }

    void compile_pipe(const PipeExpression& expr);
    void compile_error_pipe(const ErrorPipeExpression& expr);

    void compile_record_creation(const RecordCreationExpression& expr) {
        collection_compiler_.compile_record_creation(expr);
    }

    void compile_record_with(const RecordWithExpression& expr) {
        collection_compiler_.compile_record_with(expr);
    }

    void compile_array_literal(const ArrayLiteralExpression& expr) {
        collection_compiler_.compile_array_literal(expr);
    }

    void compile_dict_literal(const DictionaryLiteralExpression& expr) {
        collection_compiler_.compile_dict_literal(expr);
    }

    void compile_tuple_literal(const TupleLiteralExpression& expr) {
        collection_compiler_.compile_tuple_literal(expr);
    }

    void compile_string_interpolation(const StringInterpolationExpression& expr) {
        interpolation_handler_.compile(expr);
    }

    void compile_downcast(const DowncastExpression& expr);
    void compile_is(const IsExpression& expr);
    [[nodiscard]] std::string build_type_string(const TypeAnnotation& type) const;
    void compile_success(const SuccessExpression& expr);
    void compile_failure(const FailureExpression& expr);
    void compile_some(const SomeExpression& expr);
    void compile_range(const RangeExpression& expr);
    void compile_spawn(const SpawnExpression& expr);
    void compile_await(const AwaitExpression& expr);
    void compile_task_scope(const TaskScopeExpression& expr);

    // ─── CRTP visitor forwarders (ExpressionDispatcher<Compiler, void>) ───
    // Each visit_X() method is an inline forwarder to the corresponding
    // compile_X() method.  The CRTP dispatch_expr() / dispatch_stmt() calls
    // route here instead of through the if-constexpr chains that were in
    // compile_expression() / compile_statement().

    // Literals
    void visit_literal(const LiteralExpression& e) {
        compile_literal(e);
    }

    void visit_string_interpolation(const StringInterpolationExpression& e) {
        compile_string_interpolation(e);
    }

    // Variables / access
    void visit_variable(const VariableExpression& e) {
        compile_variable(e);
    }

    void visit_field_access(const FieldAccessExpression& e) {
        compile_field_access(e);
    }

    void visit_index_access(const IndexAccessExpression& e) {
        compile_index_access(e);
    }

    // Operators
    void visit_binary(const BinaryExpression& e) {
        compile_binary(e);
    }

    void visit_unary(const UnaryExpression& e) {
        compile_unary(e);
    }

    // Functions / lambdas
    void visit_call(const CallExpression& e) {
        compile_call(e);
    }

    void visit_lambda(const LambdaExpression& e) {
        compile_lambda(e);
    }

    // Pipes
    void visit_pipe(const PipeExpression& e) {
        compile_pipe(e);
    }

    void visit_error_pipe(const ErrorPipeExpression& e) {
        compile_error_pipe(e);
    }

    // Control flow
    void visit_if(const IfExpression& e) {
        compile_if_expression(e);
    }

    void visit_match(const MatchExpression& e) {
        compile_match_expression(e);
    }

    // Collections
    void visit_array_literal(const ArrayLiteralExpression& e) {
        compile_array_literal(e);
    }

    void visit_dictionary_literal(const DictionaryLiteralExpression& e) {
        compile_dict_literal(e);
    }

    void visit_tuple_literal(const TupleLiteralExpression& e) {
        compile_tuple_literal(e);
    }

    void visit_record_creation(const RecordCreationExpression& e) {
        compile_record_creation(e);
    }

    void visit_record_with(const RecordWithExpression& e) {
        compile_record_with(e);
    }

    // Type operations
    void visit_downcast(const DowncastExpression& e) {
        compile_downcast(e);
    }

    void visit_is(const IsExpression& e) {
        compile_is(e);
    }

    // Result / optional constructors
    void visit_success(const SuccessExpression& e) {
        compile_success(e);
    }

    void visit_failure(const FailureExpression& e) {
        compile_failure(e);
    }

    void visit_some(const SomeExpression& e) {
        compile_some(e);
    }

    // Range
    void visit_range(const RangeExpression& e) {
        compile_range(e);
    }

    // Concurrency
    void visit_spawn(const SpawnExpression& e) {
        compile_spawn(e);
    }

    void visit_await(const AwaitExpression& e) {
        compile_await(e);
    }

    void visit_task_scope(const TaskScopeExpression& e) {
        compile_task_scope(e);
    }

    // ─── CRTP visitor forwarders (StatementDispatcher<Compiler>) ───

    // Declarations
    void visit_variable_declaration(const VariableDeclStatement& s) {
        compile_variable_decl(s);
    }

    // Assignment
    void visit_assignment(const AssignmentStatement& s) {
        compile_assignment(s);
    }

    void visit_compound_assignment(const CompoundAssignmentStatement& s) {
        compile_compound_assignment(s);
    }

    void visit_increment(const IncrementStatement& s) {
        compile_unary_mutation(*s.target, Op::IncrementLocal, Op::Increment, s.location);
    }

    void visit_decrement(const DecrementStatement& s) {
        compile_unary_mutation(*s.target, Op::DecrementLocal, Op::Decrement, s.location);
    }

    // Expressions
    void visit_expression_statement(const ExpressionStatement& s) {
        compile_expression_statement(s);
    }

    // Control flow
    void visit_return(const ReturnStatement& s) {
        compile_return(s);
    }

    void visit_for(const ForStatement& s) {
        compile_for(s);
    }

    void visit_if_statement(const IfStatement& s) {
        compile_if_statement(s);
    }

    void visit_while(const WhileStatement& s) {
        compile_while(s);
    }

    void visit_match_statement(const MatchStatement& s) {
        compile_match_statement(s);
    }

    // Error handling
    void visit_try(const TryStatement& s) {
        compile_try(s);
    }

    // Destructuring
    void visit_tuple_destructuring(const TupleDestructuringStatement& s) {
        compile_tuple_destructuring(s);
    }

    void visit_record_destructuring(const RecordDestructuringStatement& s) {
        compile_record_destructuring(s);
    }

    // Blocks
    void visit_block(const BlockStatement& s) {
        compile_block(s);
    }

    // Break / Continue
    void visit_break(const BreakStatement& s);
    void visit_continue(const ContinueStatement& s);

    // ─── Bytecode emission helpers ───
    // These methods form the bytecode emission abstraction layer.  All
    // compilation code emits bytecode through these helpers rather than
    // accessing the underlying Chunk directly.
    //
    // Trivial forwarders are defined inline here to avoid per-call overhead;
    // each simply delegates to the Emitter bound to the current chunk.

    void emit(Op op, SourceLocation loc) {
        auto em = emitter();
        em.emit_opcode(op, loc);
        track_return(op);
    }

    void emit_u8(Op op, std::uint8_t operand, SourceLocation loc) {
        auto em = emitter();
        em.emit_u8(op, operand, loc);
        track_return(op);
    }

    void emit_u16(Op op, std::uint16_t operand, SourceLocation loc) {
        auto em = emitter();
        em.emit_u16(op, operand, loc);
        track_return(op);
    }

    [[nodiscard]] std::uint16_t emit_constant(Value value, SourceLocation loc) {
        auto em = emitter();
        auto index = em.add_constant(std::move(value));
        emit_u16(Op::Constant, index, loc);
        return index;
    }

    [[nodiscard]] std::size_t emit_jump(Op op, SourceLocation loc) {
        ctx_.last_was_return = false;
        auto em = emitter();
        return em.emit_jump(op, loc);
    }

    void patch_jump(std::size_t offset) {
        auto em = emitter();
        em.patch_jump(offset);
    }

    void emit_loop(std::size_t loop_start, SourceLocation loc) {
        ctx_.last_was_return = false;
        auto em = emitter();
        em.emit_loop(loop_start, loc);
    }

    void emit_compound_op(TokenType op, SourceLocation loc) {
        binary_op_compiler_.emit_compound_op(op, loc);
    }

    void emit_implicit_return(SourceLocation loc);

    // Emit IntToNumber if the target type is "number", promoting an integer
    // on top of the stack to a double.  Used at variable declarations,
    // assignments, and compound assignments to number-typed locals.
    void emit_number_widening_if_needed(const TypeAnnotation& type, SourceLocation loc);

    // Overload for locals already known to be number-typed (via Local::is_number_type).
    void emit_number_widening_if_needed(bool is_number_type, SourceLocation loc);

    // Add a name to the current chunk's name table and return its index.
    [[nodiscard]] std::uint16_t add_name(std::string_view name) {
        auto em = emitter();
        return em.add_name(name);
    }

    // Emit a raw byte into the bytecode stream (no opcode, no source map entry).
    void emit_raw_byte(std::uint8_t byte) {
        auto em = emitter();
        em.emit_raw_byte(byte);
    }

    // Emit a raw u16 value (big-endian) into the bytecode stream.
    void emit_raw_u16(std::uint16_t value) {
        auto em = emitter();
        em.emit_raw_u16(value);
    }

    // Current bytecode offset in the active chunk.
    [[nodiscard]] std::size_t current_offset() const {
        return current_scope().function.chunk().current_offset();
    }

    // ─── Helpers ───
    [[nodiscard]] Chunk& current_chunk();

    // Emit call arguments (positional and named) for a CallExpression.
    // Assumes callee is already on the stack.
    void emit_call_with_args(const CallExpression& call, std::uint8_t pos_count,
                             SourceLocation loc);

    void error(std::string_view message, SourceLocation loc, std::string_view hint = "");

    // Emit a warning-level diagnostic with optional hint.
    void warning(std::string_view message, SourceLocation loc, std::string_view hint = "");

    // Emit a "too many X (maximum Y)" compile error with a hint.
    void error_limit_exceeded(std::string_view description, std::size_t maximum, SourceLocation loc,
                              std::string_view hint);

    // Intern an identifier name and return a stable handle.
    // Enables O(1) comparisons in resolve_local and resolve_upvalue_in.
    // const: interner is mutable (caching semantics — see CompilationContext).
    [[nodiscard]] InternedString intern_name(std::string_view name) const {
        return ctx_.interner.intern(name);
    }

    // Update the last_was_return flag after emitting an opcode.
    // Centralises the tracking that emit(), emit_u8(), and emit_u16()
    // all need so the logic lives in exactly one place.
    void track_return(Op op) {
        ctx_.last_was_return = (op == Op::Return);
    }

    // ─── Emitter access ───
    // Returns an Emitter bound to the current function's chunk.
    // Used by the emit helper methods to delegate bytecode emission.
    [[nodiscard]] Emitter emitter() {
        return Emitter{current_chunk()};
    }

    // ─── Scope access helpers ───
    [[nodiscard]] CompilerScope& current_scope() {
        return ctx_.scope_stack.back();
    }

    [[nodiscard]] const CompilerScope& current_scope() const {
        return ctx_.scope_stack.back();
    }

    // ─── State ───
    // All mutable compilation state is grouped in CompilationContext.
    // Helper classes access ctx_ through CompilerAccess::ctx(), which
    // provides a controlled interface instead of direct private member access.
    CompilationContext ctx_;

    // Recursion-depth counter for compile_expression().  Defence-in-depth
    // guard against native stack overflow on pathologically deep expression
    // ASTs (e.g. a very long flat `a + b + c + ...` chain the parser builds
    // iteratively).  See ResourceLimits::max_expression_depth.
    int expression_depth_{0};

    // Controlled API surface for helper classes — the sole friend of Compiler.
    CompilerAccess access_{*this};

    VariableResolver variable_resolver_{access_};
    ConstantFolder const_folder_{access_};
    PatternCompiler pattern_compiler_{access_};
    LoopCompiler loop_compiler_{access_};
    BinaryOperatorCompiler binary_op_compiler_{access_};
    CollectionCompiler collection_compiler_{access_};
    InterpolationHandler interpolation_handler_{access_};
    DeclarationCompiler declaration_compiler_{access_};
};

// ─── Template definitions ───────────────────────────────────────────────────

template <typename EmitFetch, typename EmitStore>
void Compiler::compile_compound_op_common(EmitFetch&& emit_fetch, EmitStore&& emit_store,
                                          const Expression& rhs, TokenType op, SourceLocation loc,
                                          std::size_t temps_beneath) {
    emit_fetch();
    {
        const ScratchSlotGuard scratch{access_, temps_beneath, loc};
        compile_expression(rhs);
    }
    emit_compound_op(op, loc);
    emit_store();
}

} // namespace luma

#endif // LUMA_COMPILER_COMPILER_HPP
