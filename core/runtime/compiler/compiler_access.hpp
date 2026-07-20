// ─────────────────────────────────────────────────────────────────────────────
// CompilerAccess — Concrete implementation of ICompilationBackend
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Implement the ICompilationBackend interface by delegating
//   every method to the parent Compiler instance.  CompilerAccess is the
//   sole friend of Compiler, bridging the abstract interface that helper
//   classes depend on and the concrete Compiler that owns the state.
//
// Design: Holds a non-owning reference to the parent Compiler.  All methods
//   delegate directly to Compiler private methods.  Defined as a standalone
//   class (not nested) so that helper headers can use ICompilationBackend&
//   via forward declaration without including the full Compiler definition.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_COMPILER_ACCESS_HPP
#define LUMA_COMPILER_COMPILER_ACCESS_HPP

#include "runtime/compiler/i_compilation_backend.hpp"

namespace luma {

class Compiler;

class CompilerAccess final : public ICompilationBackend {
public:
    explicit CompilerAccess(Compiler& compiler) noexcept : compiler_(compiler) {}

    // ─── Bytecode emission ───
    void emit(Op op, SourceLocation loc) override;
    void emit_u8(Op op, std::uint8_t operand, SourceLocation loc) override;
    void emit_u16(Op op, std::uint16_t operand, SourceLocation loc) override;
    std::uint16_t emit_constant(Value value, SourceLocation loc) override;

    // ─── Jump emission and patching ───
    [[nodiscard]] std::size_t emit_jump(Op op, SourceLocation loc) override;
    void emit_loop(std::size_t loop_start, SourceLocation loc) override;
    void emit_raw_byte(std::uint8_t byte) override;
    void emit_raw_u16(std::uint16_t value) override;
    void patch_jump(std::size_t offset) override;

    // ─── Scope lifecycle ───
    void begin_scope() override;
    void end_scope() override;
    void begin_loop(std::size_t loop_start) override;
    void end_loop() override;
    void begin_function(const std::string& name, int arity) override;
    [[nodiscard]] CompiledFunction end_function() override;
    [[nodiscard]] CompilerScope& current_scope() override;
    [[nodiscard]] const CompilerScope& current_scope() const override;
    [[nodiscard]] std::size_t current_offset() const override;

    // ─── Variable management ───
    [[nodiscard]] std::uint16_t declare_local(std::string_view name, bool is_mutable,
                                              SourceLocation loc = {}) override;
    [[nodiscard]] std::optional<std::uint16_t> resolve_local(std::string_view name) const override;
    [[nodiscard]] VarSlot resolve_variable(std::string_view name,
                                           const SourceLocation& loc) override;
    [[nodiscard]] InternedString intern_name(std::string_view name) const override;

    void reserve_scratch_slots(std::size_t count, SourceLocation loc = {}) override;
    void release_scratch_slots(std::size_t count) override;

    // ─── Sub-expression/statement compilation ───
    void compile_expression(const Expression& expr) override;
    void compile_statement(const Statement& stmt) override;
    void compile_body_as_expression(const std::vector<StatementPtr>& body,
                                    SourceLocation loc) override;
    [[nodiscard]] bool try_fold_binary_at_compile_time(const LiteralExpression& lhs,
                                                       const LiteralExpression& rhs, TokenType op,
                                                       SourceLocation loc) override;

    // ─── Name table ───
    [[nodiscard]] std::uint16_t add_name(std::string_view name) override;

    // ─── Error reporting ───
    void error(std::string_view message, SourceLocation loc, std::string_view hint = "") override;
    void warning(std::string_view message, SourceLocation loc, std::string_view hint = "") override;
    void error_limit_exceeded(std::string_view description, std::size_t maximum, SourceLocation loc,
                              std::string_view hint) override;

    // ─── Compilation context ───
    [[nodiscard]] CompilationContext& ctx() override;
    [[nodiscard]] const CompilationContext& ctx() const override;

private:
    Compiler& compiler_;
};

} // namespace luma

#endif // LUMA_COMPILER_COMPILER_ACCESS_HPP
