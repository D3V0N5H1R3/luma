// ─────────────────────────────────────────────────────────────────────────────
// Type Resolution and Assignability       (TypeChecker partial implementation)
// ─────────────────────────────────────────────────────────────────────────────
// This file implements the type resolution and compatibility methods of
// TypeChecker.  These are separated from the main type_checker.cpp for
// organisational clarity:
//
//   resolve_type()                — Convert TypeAnnotation AST nodes to TypeInfo
//                                   values, expanding type aliases and resolving
//                                   built-in, generic, and user-defined types.
//   is_assignable()              — Determine whether a source type can be
//                                   assigned to a target type, including numeric
//                                   promotion, optional wrapping, container
//                                   covariance/invariance, and function
//                                   contravariance.
//
// Interface satisfaction (satisfies_interface, satisfies_interface_interface)
// lives in generic_inference.cpp.
//
// These methods remain on TypeChecker (rather than a separate TypeResolver
// class) because they access nearly all TypeChecker members: symbol registries,
// generics, context, caches, and diagnostic emission.
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <array>
#include <format>
#include <optional>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/types/type_checker.hpp"
#include "common/resource_limits.hpp"
#include "common/scope_guard.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// Type resolution
// ═══════════════════════════════════════════════════════════

bool TypeChecker::reject_internal_access(const std::string& qualified_name) {
    if (!internal_members_.contains(qualified_name)) {
        return false;
    }

    const auto split = split_module(qualified_name);

    if (!split) {
        return false;
    }

    const auto ns = split->first;

    // Access from inside the owning namespace is always permitted.
    if (ctx_.current_namespace == ns) {
        return false;
    }

    const auto short_name = split->second;

    error(std::format("'{}' is internal to namespace '{}' and cannot "
                      "be accessed from outside",
                      short_name, ns),
          SourceLocation{},
          "internal members are private to their namespace — use a public API "
          "instead");

    return true;
}

TypeInfo TypeChecker::resolve_type(const TypeAnnotation& ann) {
    // Guard against excessively nested types (e.g. array<array<array<...>>>).
    if (++ctx_.type_resolve_depth > ResourceLimits::max_parse_depth) {
        --ctx_.type_resolve_depth;
        error("type annotation nesting depth exceeded", {}, "simplify the type or reduce nesting");
        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    // RAII decrement to restore depth on all exit paths.
    const ScopeGuard guard{[this] {
        --ctx_.type_resolve_depth;
    }};

    // Fast-path: cache hit for simple (non-parameterised, non-tuple, non-func)
    // annotations when no generic bindings are active and the name is not a
    // type alias.  This avoids the long chain of string comparisons for types
    // that appear many times in a program (e.g. "integer", "string", record names).
    const bool cacheable = ann.is_plain() && ann.type_params().empty() &&
                           generics_.bindings().empty() && !internal_members_.contains(ann.name());

    if (cacheable) {
        if (const auto cached = resolved_type_cache_.find(ann.name());
            cached != resolved_type_cache_.end()) {
            return cached->second;
        }
    }

    if (ann.kind() == TypeAnnotationKind::Tuple) {
        return resolve_tuple_type(ann);
    }

    if (ann.kind() == TypeAnnotationKind::Function) {
        return resolve_function_type(ann);
    }

    if (const auto aliased = resolve_alias_type(ann)) {
        return *aliased;
    }

    return resolve_named_type(ann, cacheable);
}

// ── Tuple / function / alias resolution ──────────────────────
TypeInfo TypeChecker::resolve_tuple_type(const TypeAnnotation& ann) {
    std::vector<TypeInfo> elements;

    for (const auto& element : ann.tuple_elements()) {
        elements.push_back(resolve_type(element));
    }

    return TypeInfo::make_tuple(std::move(elements));
}

TypeInfo TypeChecker::resolve_function_type(const TypeAnnotation& ann) {
    std::vector<TypeInfo> param_types;

    for (const auto& param : ann.function_params()) {
        param_types.push_back(resolve_type(param));
    }

    TypeInfo ret = (ann.return_type_ptr() != nullptr) ? resolve_type(ann.return_type_ref())
                                                      : TypeInfo::make(TypeInfo::Kind::Void);

    return TypeInfo::make_func(std::move(param_types), std::move(ret));
}

std::optional<TypeInfo> TypeChecker::resolve_alias_type(const TypeAnnotation& ann) {
    // Check type aliases first — detect recursive aliases.
    const auto alias_it = type_aliases_.find(ann.name());

    if (alias_it == type_aliases_.end()) {
        return std::nullopt;
    }

    // An `internal` type alias is private to its namespace: reject qualified
    // access from outside before resolving the underlying target type.
    if (reject_internal_access(ann.name())) {
        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    if (resolving_aliases_.contains(ann.name())) {
        error(std::format("recursive type alias: '{}'", ann.name()), SourceLocation{},
              "a type alias cannot refer to itself, directly or indirectly");

        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    resolving_aliases_.insert(ann.name());
    const ScopeGuard alias_guard{[&] {
        resolving_aliases_.erase(ann.name());
    }};

    // Push type param bindings if alias is generic and type args are provided.
    const auto ap_it = generics_.alias_params().find(ann.name());

    std::vector<std::pair<std::string, TypeInfo>> alias_entries;
    if (ap_it != generics_.alias_params().end() && !ann.type_params().empty()) {
        for (std::size_t i{0}; i < ap_it->second.size() && i < ann.type_params().size(); ++i) {
            alias_entries.emplace_back(ap_it->second[i].name, resolve_type(ann.type_params()[i]));
        }
    }
    const GenericResolver::ParamGuard alias_bindings_guard{generics_.bindings(), alias_entries};

    const auto resolved = resolve_type(alias_it->second);

    return resolved;
}

// ── Named-type resolution ────────────────────────────────────
template <typename T>
std::optional<TypeInfo> TypeChecker::resolve_user_named(const StringMap<const T*>& map,
                                                        TypeInfo::Kind kind,
                                                        const TypeAnnotation& ann, bool cacheable) {
    const auto it = map.find(ann.name());

    if (it == map.end()) {
        return std::nullopt;
    }

    if (reject_internal_access(ann.name())) {
        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    // Choice identity uses the (possibly qualified) map key, not the bare
    // declaration name.  A namespaced choice (e.g. Terminal.Color, keyed
    // "Terminal.Color") and a top-level choice named Color (keyed "Color") must
    // stay distinguishable; collapsing both to the bare name "Color" would
    // defeat assignability and match-exhaustiveness checks.  Records and
    // interfaces keep their bare declaration name — their identity is
    // bare-name based, and record/interface names do not collide this way.
    const auto& name = (kind == TypeInfo::Kind::Choice) ? it->first : it->second->name;

    if (!ann.type_params().empty()) {
        std::vector<TypeInfo> type_args;
        type_args.reserve(ann.type_params().size());

        for (const auto& type_param : ann.type_params()) {
            type_args.push_back(resolve_type(type_param));
        }

        return TypeInfo::make_generic(kind, name, std::move(type_args));
    }

    TypeInfo result = TypeInfo::make_named(kind, name);

    if (cacheable) {
        resolved_type_cache_.emplace(ann.name(), result);
    }

    return result;
}

TypeInfo TypeChecker::resolve_named_type(const TypeAnnotation& ann, bool cacheable) {
    // Helper: store the result in the cache (when eligible) and return it.
    // This avoids duplicating the caching logic at every return site below.
    auto cache_result = [&](TypeInfo result) -> TypeInfo {
        if (cacheable) {
            resolved_type_cache_.emplace(ann.name(), result);
        }
        return result;
    };

    // Primitive (zero-parameter) types — table-driven to mirror the
    // wrapper-type table below and avoid a long if-else chain.
    struct PrimitiveTypeEntry {
        std::string_view name;
        TypeInfo::Kind kind;
    };

    static constexpr std::array primitive_types = {
        PrimitiveTypeEntry{.name = "boolean", .kind = TypeInfo::Kind::Boolean},
        PrimitiveTypeEntry{.name = "integer", .kind = TypeInfo::Kind::Integer},
        PrimitiveTypeEntry{.name = "number", .kind = TypeInfo::Kind::Number},
        PrimitiveTypeEntry{.name = "string", .kind = TypeInfo::Kind::String},
        PrimitiveTypeEntry{.name = "none", .kind = TypeInfo::Kind::None},
        PrimitiveTypeEntry{.name = "void", .kind = TypeInfo::Kind::Void},
        PrimitiveTypeEntry{.name = "socket", .kind = TypeInfo::Kind::Socket},
        PrimitiveTypeEntry{.name = "decimal", .kind = TypeInfo::Kind::Decimal},
    };

    for (const auto& entry : primitive_types) {
        if (ann.name() == entry.name) {
            return cache_result(TypeInfo::make(entry.kind));
        }
    }

    // Generic types.
    if (ann.name() == "dictionary") {
        // dictionary<V> or dictionary<string, V>
        if (ann.type_params().size() >= 2) {
            return TypeInfo::make_dict(resolve_type(ann.type_params()[1]));
        }

        if (ann.type_params().size() == 1) {
            return TypeInfo::make_dict(resolve_type(ann.type_params()[0]));
        }

        return cache_result(TypeInfo::make_dict(TypeInfo::make(TypeInfo::Kind::StdlibAny)));
    }

    if (ann.name() == "result") {
        if (ann.type_params().size() >= 2) {
            return TypeInfo::make_result(resolve_type(ann.type_params()[0]),
                                         resolve_type(ann.type_params()[1]));
        }

        if (!ann.type_params().empty()) {
            return TypeInfo::make_result(resolve_type(ann.type_params()[0]));
        }

        return cache_result(TypeInfo::make_result(TypeInfo::make(TypeInfo::Kind::StdlibAny)));
    }

    // Single-inner-type wrapper types: all follow the same pattern.
    struct WrapperTypeEntry {
        std::string_view name;
        TypeInfo::Kind kind;
    };

    static constexpr std::array wrapper_types = {
        WrapperTypeEntry{.name = "array", .kind = TypeInfo::Kind::Array},
        WrapperTypeEntry{.name = "optional", .kind = TypeInfo::Kind::Optional},
        WrapperTypeEntry{.name = "task", .kind = TypeInfo::Kind::Task},
        WrapperTypeEntry{.name = "channel", .kind = TypeInfo::Kind::Channel},
        WrapperTypeEntry{.name = "reference", .kind = TypeInfo::Kind::Reference},
    };

    auto make_wrapper = [](TypeInfo::Kind kind, TypeInfo inner) -> TypeInfo {
        switch (kind) {
            case TypeInfo::Kind::Array:
                return TypeInfo::make_array(std::move(inner));
            case TypeInfo::Kind::Optional:
                return TypeInfo::make_optional(std::move(inner));
            case TypeInfo::Kind::Task:
                return TypeInfo::make_task(std::move(inner));
            case TypeInfo::Kind::Channel:
                return TypeInfo::make_channel(std::move(inner));
            case TypeInfo::Kind::Reference:
                return TypeInfo::make_reference(std::move(inner));
            default:
                return TypeInfo::make(kind); // unreachable
        }
    };

    for (const auto& [type_name, kind] : wrapper_types) {
        if (ann.name() == type_name) {
            if (!ann.type_params().empty()) {
                return make_wrapper(kind, resolve_type(ann.type_params()[0]));
            }
            return cache_result(make_wrapper(kind, TypeInfo::make(TypeInfo::Kind::StdlibAny)));
        }
    }

    // Built-in types that map directly to named Record types.
    static constexpr auto named_record_types = std::to_array<std::string_view>({
        "widget",
        "xml",
        "set",
        "key_value_store",
        "queue",
        "stack",
    });

    if (std::ranges::find(named_record_types, ann.name()) != named_record_types.end()) {
        return cache_result(TypeInfo::make_named(TypeInfo::Kind::Record, ann.name()));
    }

    // Named types.
    if (auto result = resolve_user_named(records_, TypeInfo::Kind::Record, ann, cacheable)) {
        return *result;
    }

    if (auto result = resolve_user_named(choices_, TypeInfo::Kind::Choice, ann, cacheable)) {
        return *result;
    }

    if (auto result = resolve_user_named(interfaces_, TypeInfo::Kind::Interface, ann, cacheable)) {
        return *result;
    }

    // Active type parameter binding (e.g., T in function<T> or record Box<T>).
    const auto tp_it = generics_.bindings().find(ann.name());

    if (tp_it != generics_.bindings().end()) {
        return tp_it->second;
    }

    // Don't cache Unknown — the name might resolve after a later `use` import.
    return TypeInfo::make_named(TypeInfo::Kind::Unknown, ann.name());
}

// ═══════════════════════════════════════════════════════════
// Type assignability
// ═══════════════════════════════════════════════════════════

bool TypeChecker::is_assignable(const TypeInfo& target, const TypeInfo& source) {
    // Unknown types are permissive (avoid cascading errors).
    if (target.kind == TypeInfo::Kind::Unknown || source.kind == TypeInfo::Kind::Unknown) {
        return true;
    }

    // StdlibAny target accepts any type (used in stdlib param type checking).
    if (target.kind == TypeInfo::Kind::StdlibAny) {
        return true;
    }

    // None is only assignable to optional targets (or exact none types).
    if (source.kind == TypeInfo::Kind::None) {
        return target.kind == TypeInfo::Kind::None || target.kind == TypeInfo::Kind::Optional;
    }

    // StdlibAny source is permissive (backwards-compat for unregistered stdlib).
    if (source.kind == TypeInfo::Kind::StdlibAny) {
        return true;
    }

    // widget ↔ dictionary: at runtime widgets are dictionaries.
    if (target.kind == TypeInfo::Kind::Record && target.name == "widget" &&
        source.kind == TypeInfo::Kind::Dictionary) {
        return true;
    }

    if (source.kind == TypeInfo::Kind::Record && source.name == "widget" &&
        target.kind == TypeInfo::Kind::Dictionary) {
        return true;
    }

    // xml ↔ named Record "xml": at runtime Xml nodes are opaque values
    // but the type checker treats them as Record("xml").
    // (No special conversion needed — they share the same Kind::Record.)

    // Exact match.
    if (target == source) {
        return true;
    }

    // integer → number promotion (allowed in assignments and arithmetic).
    if (target.kind == TypeInfo::Kind::Number && source.kind == TypeInfo::Kind::Integer) {
        return true;
    }

    // A value of type T is assignable to optional<T>.
    if (target.kind == TypeInfo::Kind::Optional) {
        if (!target.inner_types.empty()) {
            return is_assignable(target.element_type(), source);
        }

        return true;
    }

    // Choice type name aliasing: qualified ("Shapes.Shape") and bare ("Shape")
    // names may refer to the same choice declaration via namespace + use imports.
    if (const auto choice_alias = is_choice_alias_assignable(target, source)) {
        return *choice_alias;
    }

    // Generic Record/Interface/Choice compatibility: same name, compatible type args.
    // Only applies when names are the same (generic instantiations).
    // Different-name Record/Interface types fall through to structural checks.
    if (target.kind == source.kind &&
        (target.kind == TypeInfo::Kind::Record || target.kind == TypeInfo::Kind::Interface ||
         target.kind == TypeInfo::Kind::Choice) &&
        target.name == source.name) {
        return inner_types_assignable(target, source);
    }

    // Record → Interface structural satisfaction.
    if (target.kind == TypeInfo::Kind::Interface && source.kind == TypeInfo::Kind::Record) {
        return satisfies_interface(source.name, target.name, source.inner_types,
                                   target.inner_types);
    }

    // Interface → Interface: target's fields must all exist (with assignable
    // types) in the source interface.
    if (target.kind == TypeInfo::Kind::Interface && source.kind == TypeInfo::Kind::Interface) {
        return satisfies_interface_interface(source.name, target.name, source.inner_types,
                                             target.inner_types);
    }

    // Generic container assignability (Array, Dictionary, Result, Task, Channel):
    // both sides must be the same container kind; first inner type must be assignable.
    if (target.kind == source.kind) {
        const auto container_result = is_container_assignable(target, source);

        if (container_result.has_value()) {
            return *container_result;
        }
    }

    // Tuple assignability: same number of elements, each assignable.
    if (target.kind == TypeInfo::Kind::Tuple && source.kind == TypeInfo::Kind::Tuple) {
        return is_tuple_assignable(target, source);
    }

    return false;
}

std::optional<bool> TypeChecker::is_choice_alias_assignable(const TypeInfo& target,
                                                            const TypeInfo& source) {
    if (target.kind != TypeInfo::Kind::Choice || source.kind != TypeInfo::Kind::Choice ||
        target.name == source.name) {
        return std::nullopt;
    }

    const auto target_it = choices_.find(target.name);
    const auto source_it = choices_.find(source.name);
    const ChoiceDeclaration* target_decl =
        target_it != choices_.end() ? target_it->second : nullptr;
    const ChoiceDeclaration* source_decl =
        source_it != choices_.end() ? source_it->second : nullptr;

    // Same declaration pointer, or if one key is missing (no `use`), fall back
    // to comparing the declaration's name against the other.
    const bool same_choice =
        ((target_decl != nullptr) && (source_decl != nullptr) && target_decl == source_decl) ||
        ((target_decl != nullptr) && (source_decl == nullptr) &&
         target_decl->name == source.name) ||
        ((source_decl != nullptr) && (target_decl == nullptr) && source_decl->name == target.name);

    if (!same_choice) {
        return std::nullopt;
    }

    return inner_types_assignable(target, source);
}

bool TypeChecker::inner_types_assignable(const TypeInfo& target, const TypeInfo& source) {
    if (target.inner_types.empty() || source.inner_types.empty()) {
        return true;
    }

    if (target.inner_types.size() != source.inner_types.size()) {
        return false;
    }

    for (std::size_t i{0}; i < target.inner_types.size(); ++i) {
        if (!is_assignable(target.inner_types[i], source.inner_types[i])) {
            return false;
        }
    }

    return true;
}

std::optional<bool> TypeChecker::is_container_assignable(const TypeInfo& target,
                                                         const TypeInfo& source) {
    switch (target.kind) {
        // Array, Dictionary, Task, and Optional are covariant in their inner
        // type: an assignment copies the container, and every collection
        // operation returns a fresh copy (Luma values are immutable unless a
        // `mutable` binding re-assigns the whole variable). Because there is no
        // shared, in-place mutation of the inner elements through the target
        // binding, a widening inner assignment (e.g. array<integer> →
        // array<number>) can never be observed as a type-unsafe write-back, so
        // covariance is sound here. Channel and Reference below are the
        // exception: they permit aliased writes and therefore require
        // invariance.
        case TypeInfo::Kind::Array:
        case TypeInfo::Kind::Dictionary:
        case TypeInfo::Kind::Task:
        case TypeInfo::Kind::Optional:
            if (target.inner_types.empty() || source.inner_types.empty()) {
                return true;
            }

            return is_assignable(target.inner_types[0], source.inner_types[0]);

        // Channel and Reference are mutable containers — require invariant
        // (exact) inner type matching to prevent type-unsafe reads/writes.
        case TypeInfo::Kind::Channel:
        case TypeInfo::Kind::Reference:
            if (target.inner_types.empty() || source.inner_types.empty()) {
                return true;
            }

            return is_assignable(target.element_type(), source.element_type()) &&
                   is_assignable(source.element_type(), target.element_type());

        case TypeInfo::Kind::Result:
            if (target.inner_types.empty() || source.inner_types.empty()) {
                return true;
            }

            // Check both success type (inner_types[0]) and error type (inner_types[1]).
            if (!is_assignable(target.result_value_type(), source.result_value_type())) {
                return false;
            }

            if (target.inner_types.size() > 1 && source.inner_types.size() > 1) {
                return is_assignable(target.result_error_type(), source.result_error_type());
            }

            return true;

        case TypeInfo::Kind::Func:
            return is_function_assignable(target, source);

        default:
            return std::nullopt;
    }
}

bool TypeChecker::is_function_assignable(const TypeInfo& target, const TypeInfo& source) {
    // Param arity must match.
    if (target.inner_types.size() != source.inner_types.size()) {
        return false;
    }

    // Function parameters are contravariant: a function accepting
    // a broader type can substitute for one accepting a narrower type.
    for (std::size_t i{0}; i < target.inner_types.size(); ++i) {
        if (!is_assignable(source.inner_types[i], target.inner_types[i])) {
            return false;
        }
    }

    // Return types: if source has no return type or 'stdlib_any'
    // return, accept it (e.g. block-body lambda without annotation).
    if (!source.return_type || source.return_type->kind == TypeInfo::Kind::StdlibAny) {
        return true;
    }

    if (!target.return_type) {
        return true;
    }

    return is_assignable(*target.return_type, *source.return_type);
}

bool TypeChecker::is_tuple_assignable(const TypeInfo& target, const TypeInfo& source) {
    if (target.inner_types.size() != source.inner_types.size()) {
        return false;
    }

    for (std::size_t i{0}; i < target.inner_types.size(); ++i) {
        if (!is_assignable(target.inner_types[i], source.inner_types[i])) {
            return false;
        }
    }

    return true;
}

} // namespace luma
