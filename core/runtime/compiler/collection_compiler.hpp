// ─────────────────────────────────────────────────────────────────────────────
// CollectionCompiler
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Compile collection literal and access expressions (array,
//   dictionary, tuple, record creation, index access), extracted from the
//   Compiler class.
//
// Design: Holds a non-owning reference to an ICompilationBackend interface, which
//   provides controlled access to Compiler's emit, compilation, and error
//   reporting methods.
//
// ICompilationBackend methods used:
//   - emit()               — single-byte opcode emission
//   - emit_u8()            — opcode + u8 operand (RecordWith)
//   - emit_u16()           — opcode + u16 operand (MakeRecord, MakeArray etc.)
//   - emit_raw_byte()      — raw byte for field counts
//   - emit_raw_u16()       — raw u16 for name indices in record field lists
//   - compile_expression() — recursive element/key/value compilation
//   - add_name()           — intern field and type names
//   - error_limit_exceeded() — report over-size collection errors
//   - warning()            — warn on unknown record fields
//   - ctx()                — access record_declarations for known field order
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_COLLECTION_COMPILER_HPP
#define LUMA_COMPILER_COLLECTION_COMPILER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler_helper.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

struct RecordFieldInit;

// Compiles collection literals (array, dictionary, tuple), record creation,
// record-with expressions, and index access expressions.
// Lifetime is bounded by the Compiler that owns it by value.
class CollectionCompiler : public CompilerHelper {
public:
    using CompilerHelper::CompilerHelper;

    void compile_array_literal(const ArrayLiteralExpression& expr);
    void compile_dict_literal(const DictionaryLiteralExpression& expr);
    void compile_tuple_literal(const TupleLiteralExpression& expr);
    void compile_record_creation(const RecordCreationExpression& expr);
    void compile_record_with(const RecordWithExpression& expr);
    void compile_index_access(const IndexAccessExpression& expr);

private:
    template <typename CompileElementFn>
    void compile_collection_impl(std::size_t count, std::size_t max_size, Op make_op,
                                 SourceLocation loc, const char* name, const char* hint,
                                 CompileElementFn compile_elements);

    void compile_record_fields(const std::vector<RecordFieldInit>& fields,
                               const RecordDeclaration* decl, SourceLocation loc);
    void emit_record_field_names(const std::vector<RecordFieldInit>& fields, SourceLocation loc);

    // Compile a sequence of stack-pushed element expressions, reserving a
    // placeholder local for every operand-stack temporary left by the earlier
    // elements so that a value-producing block (match/if used as an expression)
    // element computes local slot indices matching its true runtime position.
    void compile_stacked_elements(const std::vector<ExpressionPtr>& elements, SourceLocation loc);

    // Emit interned name indices for a range of named fields.
    template <typename Container> void emit_field_name_indices(const Container& fields) {
        for (const auto& field : fields) {
            api_.emit_raw_u16(api_.add_name(field.name));
        }
    }
};

} // namespace luma

#endif // LUMA_COMPILER_COLLECTION_COMPILER_HPP
