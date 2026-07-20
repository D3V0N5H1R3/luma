// ─────────────────────────────────────────────────────────────────────────────
// CompilerAccess — implementation
// ─────────────────────────────────────────────────────────────────────────────
// Every method delegates directly to the corresponding Compiler private
// method.  The friend relationship with Compiler is established in
// compiler.hpp, granting CompilerAccess (and only CompilerAccess) access
// to Compiler's private members.

#include "runtime/compiler/compiler_access.hpp"

#include "runtime/compiler/compiler.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// ─── Bytecode emission ──────────────────────────────────────────────────────

void CompilerAccess::emit(Op op, SourceLocation loc) {
    compiler_.emit(op, loc);
}

void CompilerAccess::emit_u8(Op op, std::uint8_t operand, SourceLocation loc) {
    compiler_.emit_u8(op, operand, loc);
}

void CompilerAccess::emit_u16(Op op, std::uint16_t operand, SourceLocation loc) {
    compiler_.emit_u16(op, operand, loc);
}

std::uint16_t CompilerAccess::emit_constant(Value value, SourceLocation loc) {
    return compiler_.emit_constant(std::move(value), loc);
}

std::size_t CompilerAccess::emit_jump(Op op, SourceLocation loc) {
    return compiler_.emit_jump(op, loc);
}

void CompilerAccess::emit_loop(std::size_t loop_start, SourceLocation loc) {
    compiler_.emit_loop(loop_start, loc);
}

void CompilerAccess::emit_raw_byte(std::uint8_t byte) {
    compiler_.emit_raw_byte(byte);
}

void CompilerAccess::emit_raw_u16(std::uint16_t value) {
    compiler_.emit_raw_u16(value);
}

void CompilerAccess::patch_jump(std::size_t offset) {
    compiler_.patch_jump(offset);
}

// ─── Scope lifecycle ────────────────────────────────────────────────────────

void CompilerAccess::begin_scope() {
    compiler_.begin_scope();
}

void CompilerAccess::end_scope() {
    compiler_.end_scope();
}

void CompilerAccess::begin_loop(std::size_t loop_start) {
    compiler_.begin_loop(loop_start);
}

void CompilerAccess::end_loop() {
    compiler_.end_loop();
}

void CompilerAccess::begin_function(const std::string& name, int arity) {
    compiler_.begin_function(name, arity);
}

CompiledFunction CompilerAccess::end_function() {
    return compiler_.end_function();
}

CompilerScope& CompilerAccess::current_scope() {
    return compiler_.current_scope();
}

const CompilerScope& CompilerAccess::current_scope() const {
    return compiler_.current_scope();
}

std::size_t CompilerAccess::current_offset() const {
    return compiler_.current_offset();
}

// ─── Variable management ────────────────────────────────────────────────────

std::uint16_t CompilerAccess::declare_local(std::string_view name, bool is_mutable,
                                            SourceLocation loc) {
    return compiler_.declare_local(name, is_mutable, loc);
}

std::optional<std::uint16_t> CompilerAccess::resolve_local(std::string_view name) const {
    return compiler_.resolve_local(name);
}

VarSlot CompilerAccess::resolve_variable(std::string_view name, const SourceLocation& loc) {
    return compiler_.resolve_variable(name, loc);
}

InternedString CompilerAccess::intern_name(std::string_view name) const {
    return compiler_.intern_name(name);
}

void CompilerAccess::reserve_scratch_slots(std::size_t count, SourceLocation loc) {
    compiler_.reserve_scratch_slots(count, loc);
}

void CompilerAccess::release_scratch_slots(std::size_t count) {
    compiler_.release_scratch_slots(count);
}

// ─── Sub-expression/statement compilation ───────────────────────────────────

void CompilerAccess::compile_expression(const Expression& expr) {
    compiler_.compile_expression(expr);
}

void CompilerAccess::compile_statement(const Statement& stmt) {
    compiler_.compile_statement(stmt);
}

void CompilerAccess::compile_body_as_expression(const std::vector<StatementPtr>& body,
                                                SourceLocation loc) {
    compiler_.compile_body_as_expression(body, loc);
}

bool CompilerAccess::try_fold_binary_at_compile_time(const LiteralExpression& lhs,
                                                     const LiteralExpression& rhs, TokenType op,
                                                     SourceLocation loc) {
    return compiler_.try_fold_binary_at_compile_time(lhs, rhs, op, loc);
}

// ─── Name table ─────────────────────────────────────────────────────────────

std::uint16_t CompilerAccess::add_name(std::string_view name) {
    return compiler_.add_name(name);
}

// ─── Error reporting ────────────────────────────────────────────────────────

void CompilerAccess::error(std::string_view message, SourceLocation loc, std::string_view hint) {
    compiler_.error(message, loc, hint);
}

void CompilerAccess::warning(std::string_view message, SourceLocation loc, std::string_view hint) {
    compiler_.warning(message, loc, hint);
}

void CompilerAccess::error_limit_exceeded(std::string_view description, std::size_t maximum,
                                          SourceLocation loc, std::string_view hint) {
    compiler_.error_limit_exceeded(description, maximum, loc, hint);
}

// ─── Compilation context ────────────────────────────────────────────────────

CompilationContext& CompilerAccess::ctx() {
    return compiler_.ctx_;
}

const CompilationContext& CompilerAccess::ctx() const {
    return compiler_.ctx_;
}

} // namespace luma
