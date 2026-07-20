#include "runtime/compiler/collection_compiler.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <unordered_set>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_errors.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/i_compilation_backend.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/scratch_slot_guard.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

namespace {
constexpr const char* k_array_size_hint =
    "split the array into smaller arrays or load data from a file";
constexpr const char* k_dict_size_hint =
    "split the dictionary into smaller dictionaries or load data from a file";
constexpr const char* k_tuple_size_hint =
    "use an array or record instead of a tuple for this many values";
} // namespace

// ─────────── Collection compilation ───────────

template <typename CompileElementFn>
void CollectionCompiler::compile_collection_impl(std::size_t count, std::size_t max_size,
                                                 Op make_op, SourceLocation loc, const char* name,
                                                 const char* hint,
                                                 CompileElementFn compile_elements) {
    if (count > max_size) {
        api_.error_limit_exceeded(name, max_size, loc, hint);
        return;
    }

    compile_elements();

    api_.emit_u16(make_op, static_cast<std::uint16_t>(count), loc);
}

void CollectionCompiler::compile_array_literal(const ArrayLiteralExpression& expr) {
    compile_collection_impl(expr.elements.size(), CompilerLimits::k_max_array_elements,
                            Op::MakeArray, expr.location, "elements in array literal",
                            k_array_size_hint,
                            [&] { compile_stacked_elements(expr.elements, expr.location); });
}

void CollectionCompiler::compile_dict_literal(const DictionaryLiteralExpression& expr) {
    compile_collection_impl(expr.entries.size(), CompilerLimits::k_max_dict_entries, Op::MakeDict,
                            expr.location, "entries in dictionary literal", k_dict_size_hint, [&] {
                                // Each compiled key and value remains on the operand stack while
                                // later entries are compiled; reserve a placeholder local per
                                // temporary so value-producing block keys/values resolve correctly.
                                std::size_t scratch = 0;
                                bool first = true;
                                for (const auto& entry : expr.entries) {
                                    if (!first) {
                                        api_.reserve_scratch_slots(1, expr.location);
                                        ++scratch;
                                    }
                                    first = false;
                                    api_.compile_expression(*entry.key);
                                    api_.reserve_scratch_slots(1, expr.location);
                                    ++scratch;
                                    api_.compile_expression(*entry.value);
                                }
                                if (scratch > 0) {
                                    api_.release_scratch_slots(scratch);
                                }
                            });
}

void CollectionCompiler::compile_tuple_literal(const TupleLiteralExpression& expr) {
    compile_collection_impl(expr.elements.size(), CompilerLimits::k_max_arguments, Op::MakeTuple,
                            expr.location, "elements in tuple literal", k_tuple_size_hint,
                            [&] { compile_stacked_elements(expr.elements, expr.location); });
}

void CollectionCompiler::compile_record_creation(const RecordCreationExpression& expr) {
    auto type_name_idx = api_.add_name(expr.type_name);

    // Build a set of explicitly provided field names.
    std::unordered_set<std::string> provided;

    for (const auto& field : expr.fields) {
        provided.insert(field.name);
    }

    // Determine the full field list: use declaration order, fill in defaults.
    auto decl_it = api_.ctx().record_declarations.declarations.find(expr.type_name);

    if (decl_it != api_.ctx().record_declarations.declarations.end()) {
        const auto* decl = &decl_it->second.get();
        const auto& decl_fields = decl->fields;

        if (decl_fields.size() > CompilerLimits::k_max_arguments) {
            auto e = compiler_errors::too_many_record_fields(CompilerLimits::k_max_arguments);
            api_.error(e.message, expr.location, e.hint);
            return;
        }

        compile_record_fields(expr.fields, decl, expr.location);

        api_.emit_u16(Op::MakeRecord, type_name_idx, expr.location);
        api_.emit_raw_byte(static_cast<std::uint8_t>(decl_fields.size()));

        // Emit field names in declaration order.
        emit_field_name_indices(decl_fields);
    } else {
        // No declaration found — use only explicit fields.
        auto e = compiler_errors::record_type_not_found(expr.type_name);
        api_.warning(e.message, expr.location, e.hint);

        if (expr.fields.size() > CompilerLimits::k_max_arguments) {
            auto e2 = compiler_errors::too_many_record_fields(CompilerLimits::k_max_arguments);
            api_.error(e2.message, expr.location, e2.hint);
            return;
        }

        compile_record_fields(expr.fields, nullptr, expr.location);

        api_.emit_u16(Op::MakeRecord, type_name_idx, expr.location);
        api_.emit_raw_byte(static_cast<std::uint8_t>(expr.fields.size()));

        emit_record_field_names(expr.fields, expr.location);
    }
}

void CollectionCompiler::compile_record_with(const RecordWithExpression& expr) {
    api_.compile_expression(*expr.base);

    if (expr.overrides.size() > CompilerLimits::k_max_arguments) {
        auto e = compiler_errors::too_many_record_overrides(CompilerLimits::k_max_arguments);
        api_.error(e.message, expr.location, e.hint);
        return;
    }

    // The base record stays on the operand stack while each override value is
    // compiled; reserve a placeholder local per temporary so a value-producing
    // block override resolves correct local slots.
    std::size_t scratch = 0;
    for (const auto& override_field : expr.overrides) {
        api_.reserve_scratch_slots(1, expr.location);
        ++scratch;
        api_.compile_expression(*override_field.value);
    }
    api_.release_scratch_slots(scratch);

    api_.emit_u8(Op::RecordWith, static_cast<std::uint8_t>(expr.overrides.size()), expr.location);

    emit_field_name_indices(expr.overrides);
}

void CollectionCompiler::compile_index_access(const IndexAccessExpression& expr) {
    api_.compile_expression(*expr.object);
    // The object stays on the operand stack while the index is compiled.
    {
        const ScratchSlotGuard scratch{api_, 1, expr.location};
        api_.compile_expression(*expr.index);
    }

    if (expr.is_optional) {
        api_.emit(Op::IndexGetOpt, expr.location);
    } else {
        api_.emit(Op::IndexGet, expr.location);
    }
}

void CollectionCompiler::compile_record_fields(const std::vector<RecordFieldInit>& fields,
                                               const RecordDeclaration* decl, SourceLocation loc) {
    // Each compiled field value remains on the operand stack while later field
    // values are compiled; reserve a placeholder local per temporary so a
    // value-producing block field value resolves correct local slots.
    if (decl != nullptr) {
        // Compile field values in declaration order.
        const auto& decl_fields = decl->fields;

        std::size_t index = 0;
        for (const auto& decl_field : decl_fields) {
            if (index > 0) {
                api_.reserve_scratch_slots(1, loc);
            }
            ++index;

            auto init_it = std::ranges::find_if(
                fields, [&](const auto& f) { return f.name == decl_field.name; });

            if (init_it != fields.end()) {
                api_.compile_expression(*init_it->value);
            } else if (decl_field.default_value) {
                api_.compile_expression(*decl_field.default_value);
            } else {
                api_.emit(Op::None,
                          loc); // Required field missing — should be caught by type checker.
            }
        }

        if (decl_fields.size() > 1) {
            api_.release_scratch_slots(decl_fields.size() - 1);
        }
    } else {
        // No declaration found — use only explicit fields.
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) {
                api_.reserve_scratch_slots(1, loc);
            }
            api_.compile_expression(*fields[i].value);
        }

        if (fields.size() > 1) {
            api_.release_scratch_slots(fields.size() - 1);
        }
    }
}

void CollectionCompiler::compile_stacked_elements(const std::vector<ExpressionPtr>& elements,
                                                  SourceLocation loc) {
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) {
            api_.reserve_scratch_slots(1, loc);
        }
        api_.compile_expression(*elements[i]);
    }

    if (elements.size() > 1) {
        api_.release_scratch_slots(elements.size() - 1);
    }
}

void CollectionCompiler::emit_record_field_names(const std::vector<RecordFieldInit>& fields,
                                                 SourceLocation /*loc*/) {
    emit_field_name_indices(fields);
}

} // namespace luma
