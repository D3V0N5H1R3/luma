// ─────────────────────────────────────────────────────────────────────────────
// TypeInfo                                          (TypeInfo implementation)
// ─────────────────────────────────────────────────────────────────────────────
// Implements TypeInfo: operator==, to_string(), factory methods.
// ─────────────────────────────────────────────────────────────────────────────

#include "analysis/types/type_info.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <string>

namespace luma {

namespace {

// Format a comma-separated list of type names from a TypeInfo vector.
std::string format_type_list(const std::vector<TypeInfo>& types) {
    std::string result;
    // Rough pre-size: most type names are short, and each element after the
    // first also contributes a ", " separator.  Avoids a couple of reallocations
    // on the diagnostics path without over-committing for the common 1-2 element
    // case.
    result.reserve(types.size() * 12);
    for (const auto& t : types) {
        if (!result.empty()) {
            result += ", ";
        }
        result += t.to_string();
    }
    return result;
}

// Compare Record, Interface, or Choice types by name and type arguments.
//
// Both helpers live in the anonymous namespace because they are only called
// from TypeInfo::operator==.  If type unification or inference is added in the
// future they should be promoted to a TypeComparator namespace in type_info.hpp
// or type_check_helpers.hpp so that the expression and statement type-checkers
// can share the same logic without going through operator==.
bool compare_named_types(const TypeInfo& a, const TypeInfo& b) {
    if (a.name != b.name) {
        return false;
    }

    return a.inner_types == b.inner_types;
}

// Compare Function types by parameter types and return type.
// See compare_named_types above for the rationale on placement and future promotion.
bool compare_function_types(const TypeInfo& a, const TypeInfo& b) {
    if (a.inner_types.size() != b.inner_types.size()) {
        return false;
    }

    if (!std::ranges::equal(a.inner_types, b.inner_types)) {
        return false;
    }

    if ((a.return_type == nullptr) != (b.return_type == nullptr)) {
        return false;
    }

    if (a.return_type && *a.return_type != *b.return_type) {
        return false;
    }

    return true;
}

} // namespace

// ═══════════════════════════════════════════════════════════
// TypeInfo
// ═══════════════════════════════════════════════════════════

bool TypeInfo::operator==(const TypeInfo& other) const {
    if (kind != other.kind) {
        return false;
    }

    if (kind == Kind::Record || kind == Kind::Interface || kind == Kind::Choice) {
        return compare_named_types(*this, other);
    }

    return compare_function_types(*this, other);
}

namespace {

// Format a single-inner-type generic, e.g. "array<integer>".
std::string format_generic(std::string_view type_name, const std::vector<TypeInfo>& inner_types) {
    if (inner_types.empty()) {
        return std::format("{}<?>", type_name);
    }

    return std::format("{}<{}>", type_name, inner_types[0].to_string());
}

// Format a callable (function) type, e.g. "function(integer, string) -> boolean".
std::string format_callable_type(const std::vector<TypeInfo>& inner_types,
                                 const std::shared_ptr<TypeInfo>& return_type) {
    return std::format("function({}) -> {}", format_type_list(inner_types),
                       return_type ? return_type->to_string() : "void");
}

} // namespace

std::string TypeInfo::to_string() const {
    switch (kind) {
        case Kind::Boolean:
            return "boolean";

        case Kind::Integer:
            return "integer";

        case Kind::Number:
            return "number";

        case Kind::String:
            return "string";

        case Kind::StdlibAny:
            return "unknown";

        case Kind::Void:
            return "void";

        case Kind::None:
            return "none";

        case Kind::Range:
            return "range";

        case Kind::Namespace:
            return "namespace";

        case Kind::Unknown:
            return name.empty() ? "unknown" : name;

        case Kind::Choice:
        case Kind::Record:
        case Kind::Interface: {
            if (inner_types.empty()) {
                return name;
            }

            return std::format("{}<{}>", name, format_type_list(inner_types));
        }

        case Kind::Array:
            return format_generic("array", inner_types);

        case Kind::Dictionary:
            return format_generic("dictionary", inner_types);

        case Kind::Result: {
            if (inner_types.empty()) {
                return "result<?>";
            }

            if (inner_types.size() >= 2 && result_error_type().kind != Kind::StdlibAny) {
                return std::format("result<{}, {}>", result_value_type().to_string(),
                                   result_error_type().to_string());
            }

            return std::format("result<{}>", result_value_type().to_string());
        }

        case Kind::Optional:
            return format_generic("optional", inner_types);

        case Kind::Task:
            return format_generic("task", inner_types);

        case Kind::Channel:
            return format_generic("channel", inner_types);

        case Kind::Reference:
            return format_generic("reference", inner_types);

        case Kind::Tuple:
            return std::format("({})", format_type_list(inner_types));

        case Kind::Socket:
            return "socket";

        case Kind::Func:
            return format_callable_type(inner_types, return_type);
    }

    return "unknown";
}

bool TypeInfo::is_numeric() const {
    return kind == Kind::Integer || kind == Kind::Number;
}

const std::string& TypeInfo::to_string_cached(const TypeInfo& type, ToStringCache& cache) {
    auto [it, inserted] = cache.try_emplace(&type);
    if (inserted) {
        it->second = type.to_string();
    }
    return it->second;
}

TypeInfo TypeInfo::make(Kind k) {
    return TypeInfo{.kind = k, .name = {}, .inner_types = {}, .return_type = {}};
}

// File-local helper: constructs a wrapper type with a single inner type.
namespace {

TypeInfo make_single_inner(TypeInfo::Kind k, TypeInfo inner) {
    TypeInfo info{.kind = k, .name = {}, .inner_types = {}, .return_type = {}};
    info.inner_types.push_back(std::move(inner));
    return info;
}

} // namespace

TypeInfo TypeInfo::make_array(TypeInfo element) {
    return make_single_inner(Kind::Array, std::move(element));
}

TypeInfo TypeInfo::make_dict(TypeInfo value) {
    return make_single_inner(Kind::Dictionary, std::move(value));
}

TypeInfo TypeInfo::make_result(TypeInfo value) {
    TypeInfo info{.kind = Kind::Result, .name = {}, .inner_types = {}, .return_type = {}};
    info.inner_types.reserve(2);
    info.inner_types.push_back(std::move(value));
    info.inner_types.push_back(TypeInfo::make(Kind::StdlibAny)); // default error type (permissive)

    return info;
}

TypeInfo TypeInfo::make_result(TypeInfo value, TypeInfo error) {
    TypeInfo info{.kind = Kind::Result, .name = {}, .inner_types = {}, .return_type = {}};
    info.inner_types.reserve(2);
    info.inner_types.push_back(std::move(value));
    info.inner_types.push_back(std::move(error));

    return info;
}

TypeInfo TypeInfo::make_optional(TypeInfo inner) {
    return make_single_inner(Kind::Optional, std::move(inner));
}

TypeInfo TypeInfo::make_task(TypeInfo inner) {
    return make_single_inner(Kind::Task, std::move(inner));
}

TypeInfo TypeInfo::make_channel(TypeInfo inner) {
    return make_single_inner(Kind::Channel, std::move(inner));
}

TypeInfo TypeInfo::make_reference(TypeInfo inner) {
    return make_single_inner(Kind::Reference, std::move(inner));
}

TypeInfo TypeInfo::make_func(std::vector<TypeInfo> param_types, TypeInfo ret) {
    TypeInfo info{
        .kind = Kind::Func, .name = {}, .inner_types = std::move(param_types), .return_type = {}};
    info.return_type = std::make_shared<TypeInfo>(std::move(ret));
    return info;
}

TypeInfo TypeInfo::make_tuple(std::vector<TypeInfo> elements) {
    TypeInfo info{.kind = Kind::Tuple, .name = {}, .inner_types = {}, .return_type = {}};
    info.inner_types = std::move(elements);

    return info;
}

TypeInfo TypeInfo::make_named(Kind k, const std::string& type_name) {
    return TypeInfo{.kind = k, .name = type_name, .inner_types = {}, .return_type = {}};
}

TypeInfo TypeInfo::make_generic(Kind k, std::string type_name, std::vector<TypeInfo> type_args) {
    TypeInfo info{.kind = k, .name = std::move(type_name), .inner_types = {}, .return_type = {}};
    info.inner_types = std::move(type_args);

    return info;
}

} // namespace luma
