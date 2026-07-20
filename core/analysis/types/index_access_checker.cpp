#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include "analysis/ast/expression.hpp"
#include "analysis/types/expression_type_checker.hpp"
#include "analysis/types/type_checker.hpp"

namespace luma {

TypeInfo ExpressionTypeChecker::visit_index_access(const IndexAccessExpression& expr) {
    const auto object_type = infer_expression_type(*expr.object);
    const auto index_type = infer_expression_type(*expr.index);

    // An optional index access (?[i]) marks the result as potentially null so
    // assignment sites can warn.  Auto-flatten an already-optional element type
    // to optional<T> rather than optional<optional<T>>.
    const auto with_optional_null = [&expr](TypeInfo type) -> TypeInfo {
        if (expr.is_optional && type.kind != TypeInfo::Kind::Optional) {
            return TypeInfo::make_optional(std::move(type));
        }

        return type;
    };

    if (object_type.kind == TypeInfo::Kind::Array) {
        return with_optional_null(check_array_index(expr, object_type, index_type));
    }

    if (object_type.kind == TypeInfo::Kind::Dictionary) {
        return with_optional_null(check_dict_index(expr, object_type, index_type));
    }

    if (object_type.kind == TypeInfo::Kind::String) {
        return with_optional_null(check_string_index(expr, index_type));
    }

    if (object_type.kind == TypeInfo::Kind::Tuple) {
        return with_optional_null(check_tuple_index(expr, object_type));
    }

    // Optional chaining: optional<T>[i] — unwrap T and check indexability.
    if (object_type.kind == TypeInfo::Kind::Optional && !object_type.inner_types.empty()) {
        return with_optional_null(check_optional_index(object_type));
    }

    if (object_type.kind != TypeInfo::Kind::StdlibAny &&
        object_type.kind != TypeInfo::Kind::Unknown) {
        tc_.error(std::format("index access requires an array, string, dictionary, "
                              "or tuple, got '{}'",
                              object_type.to_string()),
                  expr.location,
                  "only arrays, strings, dictionaries, and tuples support index access");
    }

    return with_optional_null(TypeInfo::make(TypeInfo::Kind::StdlibAny));
}

// ─── visit_index_access helpers ─────────────────────────────────────────

TypeInfo ExpressionTypeChecker::check_array_index(const IndexAccessExpression& expr,
                                                  const TypeInfo& object_type,
                                                  const TypeInfo& index_type) {
    // Slice: array[range] → array of same type.
    if (index_type.kind == TypeInfo::Kind::Range) {
        return object_type;
    }

    if (index_type.kind != TypeInfo::Kind::Integer &&
        index_type.kind != TypeInfo::Kind::StdlibAny &&
        index_type.kind != TypeInfo::Kind::Unknown) {
        tc_.error(
            std::format("array index must be integer or range, got '{}'", index_type.to_string()),
            expr.index->location,
            "use an integer value or a range expression to index into an array");
    }

    // Compile-time bounds check: literal index on a literal array.
    if (expr.object->kind == ExpressionKind::ArrayLiteral) {
        if (const auto idx = get_integer_value(*expr.index)) {
            const auto& arr = static_cast<const ArrayLiteralExpression&>(*expr.object);
            const auto len = static_cast<std::int64_t>(arr.elements.size());

            if (*idx < 0 || *idx >= len) {
                tc_.error(std::format("index {} out of bounds for array of length {}", *idx, len),
                          expr.index->location,
                          len == 0 ? std::string{"the array is empty, so no index is valid"}
                                   : std::format("valid indices are 0 to {}", len - 1));
            }
        }
    }

    if (!object_type.inner_types.empty()) {
        return object_type.element_type();
    }

    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

TypeInfo ExpressionTypeChecker::check_dict_index(const IndexAccessExpression& expr,
                                                 const TypeInfo& object_type,
                                                 const TypeInfo& index_type) {
    if (index_type.kind != TypeInfo::Kind::String && index_type.kind != TypeInfo::Kind::StdlibAny &&
        index_type.kind != TypeInfo::Kind::Unknown) {
        tc_.error(std::format("dictionary key must be string, got '{}'", index_type.to_string()),
                  expr.index->location, "dictionary keys must be string values");
    }

    if (!object_type.inner_types.empty()) {
        return object_type.value_type();
    }

    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

TypeInfo ExpressionTypeChecker::check_string_index(const IndexAccessExpression& expr,
                                                   const TypeInfo& index_type) {
    // Slice: string[range] → string.
    if (index_type.kind == TypeInfo::Kind::Range) {
        return TypeInfo::make(TypeInfo::Kind::String);
    }

    if (index_type.kind != TypeInfo::Kind::Integer &&
        index_type.kind != TypeInfo::Kind::StdlibAny &&
        index_type.kind != TypeInfo::Kind::Unknown) {
        tc_.error(
            std::format("string index must be integer or range, got '{}'", index_type.to_string()),
            expr.index->location,
            "use an integer value or a range expression to index into a string");
    }

    return TypeInfo::make(TypeInfo::Kind::String);
}

TypeInfo ExpressionTypeChecker::check_tuple_index(const IndexAccessExpression& expr,
                                                  const TypeInfo& object_type) {
    // A compile-time integer literal index yields the specific element type.
    if (const auto idx = get_integer_value(*expr.index)) {
        if (*idx >= 0 && static_cast<std::size_t>(*idx) < object_type.inner_types.size()) {
            return object_type.inner_types[static_cast<std::size_t>(*idx)];
        }

        // Out-of-bounds literal index on a tuple.
        const auto len = static_cast<std::int64_t>(object_type.inner_types.size());

        tc_.error(std::format("index {} out of bounds for tuple of length {}", *idx, len),
                  expr.index->location,
                  len == 0 ? std::string{"the tuple is empty, so no index is valid"}
                           : std::format("valid indices are 0 to {}", len - 1));
    }

    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

TypeInfo ExpressionTypeChecker::check_optional_index(const TypeInfo& object_type) {
    const auto& inner = object_type.element_type();

    if (inner.kind == TypeInfo::Kind::Array) {
        if (!inner.inner_types.empty()) {
            return inner.element_type();
        }

        return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    if (inner.kind == TypeInfo::Kind::Dictionary) {
        if (!inner.inner_types.empty()) {
            return inner.value_type();
        }

        return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    if (inner.kind == TypeInfo::Kind::String) {
        return TypeInfo::make(TypeInfo::Kind::String);
    }

    // Tuple or unknown inner type — fall through to StdlibAny.
    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

} // namespace luma
