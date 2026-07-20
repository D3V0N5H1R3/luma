#ifndef LUMA_LSP_TYPE_FORMATTER_HPP
#define LUMA_LSP_TYPE_FORMATTER_HPP

#include <string>
#include <string_view>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"

namespace luma::lsp::util {

// The sentinel type name produced when a type cannot be resolved or displayed.
inline constexpr std::string_view k_unknown_type = "unknown";

// True when `type` names a resolved, displayable type — i.e. it is non-empty
// and not the "unknown" sentinel. This is the single source of truth for the
// "is this a usable type?" predicate used across hover, inlay hints, and
// refactorings.
[[nodiscard]] inline constexpr bool is_known_type(std::string_view type) {
    return !type.empty() && type != k_unknown_type;
}

// Forward declaration for recursive use by annotation helpers.
[[nodiscard]] inline std::string annotation_to_string(const TypeAnnotation& ann);

// Join a collection of TypeAnnotation elements with a separator, wrapped by
// prefix and suffix strings.
template <typename Range>
[[nodiscard]] inline std::string join_annotations(std::string_view prefix, const Range& items,
                                                  std::string_view suffix) {
    std::string s{prefix};
    bool first = true;
    for (const auto& item : items) {
        if (!first) {
            s += ", ";
        }
        first = false;
        s += annotation_to_string(item);
    }
    s += suffix;
    return s;
}

// Render a tuple TypeAnnotation as "(T, U, ...)".
[[nodiscard]] inline std::string tuple_annotation_to_string(const TypeAnnotation& ann) {
    return join_annotations("(", ann.tuple_elements(), ")");
}

// Render a function TypeAnnotation as "func(P1, P2) -> R".
[[nodiscard]] inline std::string function_annotation_to_string(const TypeAnnotation& ann) {
    std::string s = join_annotations("func(", ann.function_params(), ")");
    if (ann.return_type_ptr()) {
        s += " -> ";
        s += annotation_to_string(ann.return_type_ref());
    }
    return s;
}

// Render a named/generic TypeAnnotation as "name" or "name<T, U>".
[[nodiscard]] inline std::string generic_annotation_to_string(const TypeAnnotation& ann) {
    if (ann.type_params().empty()) {
        return ann.name();
    }
    return ann.name() + join_annotations("<", ann.type_params(), ">");
}

// Render a TypeAnnotation back to a source-like string.
[[nodiscard]] inline std::string annotation_to_string(const TypeAnnotation& ann) {
    if (ann.kind() == TypeAnnotationKind::Tuple) {
        return tuple_annotation_to_string(ann);
    }

    if (ann.kind() == TypeAnnotationKind::Function) {
        return function_annotation_to_string(ann);
    }

    if (ann.name().empty()) {
        return std::string{k_unknown_type};
    }

    return generic_annotation_to_string(ann);
}

} // namespace luma::lsp::util

#endif // LUMA_LSP_TYPE_FORMATTER_HPP
