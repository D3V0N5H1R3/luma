#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analysis/types/stdlib_type_handler.hpp"
#include "common/string_hash.hpp"
#include "stdlib/stdlib_catalog.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// ReturnTypeDesc → TypeInfo conversion
// ═══════════════════════════════════════════════════════════
//
// Converts a lightweight ReturnTypeDesc (from the shared stdlib
// catalog) into a full TypeInfo used by the type checker.

namespace {

[[nodiscard]] TypeInfo type_info_from_desc(const stdlib::ReturnTypeDesc& desc) {
    using K = TypeInfo::Kind;
    using RK = stdlib::ReturnTypeDesc::Kind;

    switch (desc.kind) {
        case RK::Integer:
            return TypeInfo::make(K::Integer);
        case RK::Number:
            return TypeInfo::make(K::Number);
        case RK::String:
            return TypeInfo::make(K::String);
        case RK::Boolean:
            return TypeInfo::make(K::Boolean);
        case RK::Void:
            return TypeInfo::make(K::Void);
        case RK::None:
            return TypeInfo::make(K::None);
        case RK::Any:
            return TypeInfo::make(K::StdlibAny);
        case RK::Func:
            return TypeInfo::make(K::Func);

        case RK::Array:
            return TypeInfo::make_array(type_info_from_desc(desc.inner[0]));

        case RK::Dictionary:
            return TypeInfo::make_dict(type_info_from_desc(desc.inner[0]));

        case RK::Result:
            // A two-parameter descriptor carries an explicit error type
            // (result<value, error>); the common single-parameter form uses the
            // default string-error result<value>.
            if (desc.inner.size() >= 2) {
                return TypeInfo::make_result(type_info_from_desc(desc.inner[0]),
                                             type_info_from_desc(desc.inner[1]));
            }
            return TypeInfo::make_result(type_info_from_desc(desc.inner[0]));

        case RK::Optional:
            return TypeInfo::make_optional(type_info_from_desc(desc.inner[0]));

        case RK::Channel:
            return TypeInfo::make_channel(type_info_from_desc(desc.inner[0]));

        case RK::Task:
            return TypeInfo::make_task(type_info_from_desc(desc.inner[0]));

        case RK::Reference:
            return TypeInfo::make_reference(type_info_from_desc(desc.inner[0]));

        case RK::Tuple: {
            std::vector<TypeInfo> elements;
            elements.reserve(desc.inner.size());

            for (const auto& elem : desc.inner) {
                elements.push_back(type_info_from_desc(elem));
            }

            return TypeInfo::make_tuple(std::move(elements));
        }

        case RK::Named:
            if (desc.named_type == "socket") {
                return TypeInfo::make(K::Socket);
            }
            if (desc.named_type == "decimal") {
                return TypeInfo::make(K::Decimal);
            }
            if (desc.named_type == "Log.Level") {
                return TypeInfo::make_named(K::Choice, "Log.Level");
            }
            if (desc.named_type == "DateTime.Weekday") {
                return TypeInfo::make_named(K::Choice, "DateTime.Weekday");
            }
            if (desc.named_type == "DateTime.Month") {
                return TypeInfo::make_named(K::Choice, "DateTime.Month");
            }
            if (desc.named_type == "DateTime.ParseError") {
                return TypeInfo::make_named(K::Choice, "DateTime.ParseError");
            }
            if (desc.named_type == "Ordering") {
                return TypeInfo::make_named(K::Choice, "Ordering");
            }
            if (desc.named_type == "Json.Value") {
                return TypeInfo::make_named(K::Choice, "Json.Value");
            }
            if (desc.named_type == "FileSystem.FileKind") {
                return TypeInfo::make_named(K::Choice, "FileSystem.FileKind");
            }
            if (desc.named_type == "FileSystem.IoError") {
                return TypeInfo::make_named(K::Choice, "FileSystem.IoError");
            }
            if (desc.named_type == "Sign") {
                return TypeInfo::make_named(K::Choice, "Sign");
            }
            if (desc.named_type == "Http.StatusClass") {
                return TypeInfo::make_named(K::Choice, "Http.StatusClass");
            }
            if (desc.named_type == "Http.Error") {
                return TypeInfo::make_named(K::Choice, "Http.Error");
            }
            if (desc.named_type == "Process.ExitStatus") {
                return TypeInfo::make_named(K::Choice, "Process.ExitStatus");
            }
            if (desc.named_type == "Process.Error") {
                return TypeInfo::make_named(K::Choice, "Process.Error");
            }
            if (desc.named_type == "Process.Signal") {
                return TypeInfo::make_named(K::Choice, "Process.Signal");
            }
            if (desc.named_type == "Log.Output") {
                return TypeInfo::make_named(K::Choice, "Log.Output");
            }
            if (desc.named_type == "Random.Distribution") {
                return TypeInfo::make_named(K::Choice, "Random.Distribution");
            }
            if (desc.named_type == "Terminal.Key") {
                return TypeInfo::make_named(K::Choice, "Terminal.Key");
            }
            if (desc.named_type == "Encoder.Encoding") {
                return TypeInfo::make_named(K::Choice, "Encoder.Encoding");
            }
            if (desc.named_type == "Xml.Node") {
                return TypeInfo::make_named(K::Choice, "Xml.Node");
            }
            if (desc.named_type == "Socket.IpAddress") {
                return TypeInfo::make_named(K::Choice, "Socket.IpAddress");
            }
            if (desc.named_type == "Socket.Error") {
                return TypeInfo::make_named(K::Choice, "Socket.Error");
            }
            if (desc.named_type == "RegularExpression.Error") {
                return TypeInfo::make_named(K::Choice, "RegularExpression.Error");
            }
            if (desc.named_type == "RegularExpression.Flags") {
                return TypeInfo::make_named(K::Choice, "RegularExpression.Flags");
            }
            if (desc.named_type == "Terminal.CursorStyle") {
                return TypeInfo::make_named(K::Choice, "Terminal.CursorStyle");
            }
            if (desc.named_type == "Compression.Format") {
                return TypeInfo::make_named(K::Choice, "Compression.Format");
            }
            if (desc.named_type == "Compression.Error") {
                return TypeInfo::make_named(K::Choice, "Compression.Error");
            }
            if (desc.named_type == "Encoder.Error") {
                return TypeInfo::make_named(K::Choice, "Encoder.Error");
            }
            if (desc.named_type == "Decimal.Error") {
                return TypeInfo::make_named(K::Choice, "Decimal.Error");
            }
            if (desc.named_type == "Color.Name") {
                return TypeInfo::make_named(K::Choice, "Color.Name");
            }
            if (desc.named_type == "Math.Angle") {
                return TypeInfo::make_named(K::Choice, "Math.Angle");
            }
            return TypeInfo::make_named(K::Record, desc.named_type);

        case RK::Unspecified:
            return TypeInfo::make(K::StdlibAny);
    }

    return TypeInfo::make(K::StdlibAny);
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Stdlib return-type registry — catalog synchronization
// ═══════════════════════════════════════════════════════════
//
// The shared stdlib catalog (stdlib_catalog.hpp) is the single source of
// truth for function arities and basic return types.  This file bridges
// the catalog into the type checker's type system:
//
//   • Arities — derived entirely from the catalog.  No manual entries.
//     Variadic functions with min-arity 0 are excluded (the checker
//     skips arity validation for them).
//
//   • Return types — derived from the catalog's ReturnTypeDesc when the
//     descriptor is not Unspecified.  Functions that involve generic type
//     relationships (e.g., Array.map returning array<U> where U depends
//     on a callback's return type) still require manual refinement logic
//     in refine_return_type() below, since the catalog's ReturnTypeDesc
//     cannot express these constraints.
//
// If you add a new stdlib function, add it to the shared catalog.  The
// type checker picks it up automatically — no parallel registry needed.

void StdlibTypeHandler::init_signatures() {
    // Populate from catalog descriptors— functions whose FunctionSpec
    // carries a non-Unspecified ReturnTypeDesc are converted automatically.
    for (const auto& [name, spec] : stdlib::catalog()) {
        if (spec.return_type.kind != stdlib::ReturnTypeDesc::Unspecified) {
            functions_[name].return_type = type_info_from_desc(spec.return_type);
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Stdlib return-type refinement
// ═══════════════════════════════════════════════════════════
//
// When a stdlib function's registered return type contains StdlibAny,
// try to refine it with the concrete element type from the arguments
// at the call site.  This eliminates most user-visible unrefined types.

// ── Return-type refinement helpers ───────────────────────────
//
// refine_return_type substitutes StdlibAny in a statically declared return
// type with the concrete element type inferred from call-site arguments.
// The logic is grouped by stdlib module so each refiner owns a single
// namespace.

// ─────────────────────────────────────────────────────────────────────────────
// Registry-based return-type refinement
// ─────────────────────────────────────────────────────────────────────────────
//
// Each stdlib function whose return type depends on its call-site argument
// types is registered below as a refiner callback keyed by qualified name.
// refine_return_type() performs a single O(1) hashed lookup instead of the
// former per-module chain of linear `name == "..."` ladders, giving a single
// registry view and removing repeated std::string comparisons on the
// type-check hot path.
//
// Each refiner receives:
//   • elem        — the element type inferred from the first argument
//                   (e.g. T for array<T>); only meaningful for refiners with
//                   needs_element == true.
//   • static_type — the statically declared return type, returned unchanged
//                   when refinement does not apply.
//   • arg_types   — the call-site argument types.
//
// Math refiners derive numeric types from scalar arguments, so they run before
// the element short-circuit (needs_element == false); every other refiner runs
// only once a concrete (non-StdlibAny) element type has been inferred — exactly
// reproducing the previous dispatcher ordering.

namespace {

struct RefineContext {
    const TypeInfo& elem;
    const TypeInfo& static_type;
    const std::vector<TypeInfo>& arg_types;
};

using RefinerFn = TypeInfo (*)(const RefineContext&);

struct Refiner {
    bool needs_element;
    RefinerFn fn;
};

[[nodiscard]] TypeInfo refiner_element_of_first(const std::vector<TypeInfo>& arg_types) {
    using K = TypeInfo::Kind;

    if (arg_types.empty()) {
        return TypeInfo::make(K::StdlibAny);
    }

    const auto& first_arg = arg_types[0];

    if ((first_arg.kind == K::Array || first_arg.kind == K::Dictionary ||
         first_arg.kind == K::Result || first_arg.kind == K::Optional ||
         first_arg.kind == K::Channel || first_arg.kind == K::Task ||
         first_arg.kind == K::Reference) &&
        !first_arg.inner_types.empty()) {
        return first_arg.inner_types[0];
    }

    return TypeInfo::make(K::StdlibAny);
}

[[nodiscard]] TypeInfo refiner_numeric_of_first(const std::vector<TypeInfo>& arg_types) {
    using K = TypeInfo::Kind;

    if (!arg_types.empty()) {
        if (arg_types[0].kind == K::Integer) {
            return TypeInfo::make(K::Integer);
        }

        if (arg_types[0].kind == K::Number) {
            return TypeInfo::make(K::Number);
        }
    }

    return TypeInfo::make(K::Number);
}

[[nodiscard]] TypeInfo refiner_numeric_element_of_array(const std::vector<TypeInfo>& arg_types) {
    using K = TypeInfo::Kind;

    if (!arg_types.empty() && arg_types[0].kind == K::Array && !arg_types[0].inner_types.empty()) {
        const auto& elem = arg_types[0].element_type();

        if (elem.kind == K::Integer) {
            return TypeInfo::make(K::Integer);
        }

        if (elem.kind == K::Number) {
            return TypeInfo::make(K::Number);
        }
    }

    return TypeInfo::make(K::Number);
}

[[nodiscard]] StringMap<Refiner> build_refiner_registry() {
    using K = TypeInfo::Kind;

    StringMap<Refiner> r;

    const auto add = [&r](std::initializer_list<std::string_view> names, bool needs_element,
                          RefinerFn fn) {
        for (const auto name : names) {
            r.emplace(std::string{name}, Refiner{needs_element, fn});
        }
    };

    // ── Math: numeric return types (scalar args, pre-element) ──
    add({"Math.absolute", "Math.remainder"}, false, [](const RefineContext& c) {
        return TypeInfo::make_result(refiner_numeric_of_first(c.arg_types));
    });
    add({"Math.sum"}, false, [](const RefineContext& c) {
        return TypeInfo::make_result(refiner_numeric_element_of_array(c.arg_types));
    });

    // ── Array: propagate element type ──
    add({"Array.push", "Array.reverse", "Array.unique", "Array.concat", "Array.intersperse",
         "Array.rotate", "Array.take", "Array.drop"},
        true, [](const RefineContext& c) { return TypeInfo::make_array(c.elem); });

    add({"Array.flatten"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.elem.kind == K::Array && !c.elem.inner_types.empty()) {
            return TypeInfo::make_array(c.elem.element_type());
        }

        return TypeInfo::make_array(c.elem);
    });

    add({"Array.zip"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.arg_types.size() >= 2 && c.arg_types[1].kind == K::Array &&
            !c.arg_types[1].inner_types.empty()) {
            return TypeInfo::make_array(
                TypeInfo::make_tuple({c.elem, c.arg_types[1].element_type()}));
        }

        return TypeInfo::make_array(c.elem);
    });

    add({"Array.enumerate"}, true, [](const RefineContext& c) {
        return TypeInfo::make_array(TypeInfo::make_tuple({TypeInfo::make(K::Integer), c.elem}));
    });

    add({"Array.chunk", "Array.windows"}, true, [](const RefineContext& c) {
        return TypeInfo::make_result(TypeInfo::make_array(TypeInfo::make_array(c.elem)));
    });

    add({"Array.pop"}, true, [](const RefineContext& c) {
        return TypeInfo::make_result(TypeInfo::make_tuple({TypeInfo::make_array(c.elem), c.elem}));
    });

    add({"Array.insert_at"}, true,
        [](const RefineContext& c) { return TypeInfo::make_result(TypeInfo::make_array(c.elem)); });

    add({"Array.remove_at"}, true, [](const RefineContext& c) {
        return TypeInfo::make_result(TypeInfo::make_tuple({TypeInfo::make_array(c.elem), c.elem}));
    });

    add({"Array.map", "Array.filter", "Array.sort", "Array.sort_by", "Array.slice",
         "Array.take_while", "Array.drop_while", "Array.flat_map", "Array.set", "Array.scan"},
        true,
        [](const RefineContext& c) { return TypeInfo::make_result(TypeInfo::make_array(c.elem)); });

    add({"Array.get", "Array.first", "Array.last", "Array.find", "Array.min", "Array.max",
         "Array.sum"},
        true, [](const RefineContext& c) { return TypeInfo::make_result(c.elem); });

    add({"Array.max_by", "Array.min_by"}, true,
        [](const RefineContext& c) { return TypeInfo::make_optional(c.elem); });

    add({"Array.reduce"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.arg_types.size() >= 2) {
            return TypeInfo::make_result(c.arg_types[1]);
        }

        return c.static_type;
    });

    add({"Array.group_by"}, true, [](const RefineContext& c) {
        return TypeInfo::make_result(TypeInfo::make_dict(TypeInfo::make_array(c.elem)));
    });

    add({"Array.partition"}, true, [](const RefineContext& c) {
        return TypeInfo::make_result(
            TypeInfo::make_tuple({TypeInfo::make_array(c.elem), TypeInfo::make_array(c.elem)}));
    });

    add({"Array.transpose"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.elem.kind == K::Array && !c.elem.inner_types.empty()) {
            return TypeInfo::make_result(
                TypeInfo::make_array(TypeInfo::make_array(c.elem.element_type())));
        }

        return TypeInfo::make_result(TypeInfo::make_array(TypeInfo::make_array(c.elem)));
    });

    // ── Dictionary: propagate value type ──
    add({"Dictionary.get"}, true,
        [](const RefineContext& c) { return TypeInfo::make_result(c.elem); });

    add({"Dictionary.get_or"}, true, [](const RefineContext& c) { return c.elem; });
    add({"Dictionary.values"}, true,
        [](const RefineContext& c) { return TypeInfo::make_array(c.elem); });

    add({"Dictionary.set", "Dictionary.remove", "Dictionary.merge", "Dictionary.deep_merge",
         "Dictionary.update"},
        true, [](const RefineContext& c) { return TypeInfo::make_dict(c.elem); });

    add({"Dictionary.invert"}, true,
        [](const RefineContext&) { return TypeInfo::make_dict(TypeInfo::make(K::String)); });

    add({"Dictionary.from_keys"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.arg_types.size() >= 2) {
            return TypeInfo::make_dict(c.arg_types[1]);
        }

        return TypeInfo::make_dict(c.elem);
    });

    add({"Dictionary.filter", "Dictionary.map_values", "Dictionary.map"}, true,
        [](const RefineContext& c) { return TypeInfo::make_result(TypeInfo::make_dict(c.elem)); });

    add({"Dictionary.reduce"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.arg_types.size() >= 2) {
            return TypeInfo::make_result(c.arg_types[1]);
        }

        return c.static_type;
    });

    // ── Result: propagate inner type ──
    add({"Result.unwrap", "Result.unwrap_or"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Result &&
            !c.arg_types[0].inner_types.empty()) {
            return c.arg_types[0].result_value_type();
        }

        return c.static_type;
    });

    add({"Result.error"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Result &&
            c.arg_types[0].inner_types.size() >= 2) {
            return c.arg_types[0].result_error_type();
        }

        return TypeInfo::make(K::String);
    });

    add({"Result.map", "Result.flat_map", "Result.map_failure", "Result.filter", "Result.recover",
         "Result.tap", "Result.or", "Result.collect"},
        true, [](const RefineContext& c) -> TypeInfo {
            if (!c.arg_types.empty() && c.arg_types[0].kind == K::Result &&
                !c.arg_types[0].inner_types.empty()) {
                return TypeInfo::make_result(c.arg_types[0].result_value_type());
            }

            return c.static_type;
        });

    add({"Result.flatten"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Result &&
            !c.arg_types[0].inner_types.empty() &&
            c.arg_types[0].result_value_type().kind == K::Result &&
            !c.arg_types[0].result_value_type().inner_types.empty()) {
            return TypeInfo::make_result(c.arg_types[0].result_value_type().result_value_type());
        }

        return c.static_type;
    });

    add({"Result.to_optional"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Result &&
            !c.arg_types[0].inner_types.empty()) {
            return TypeInfo::make_optional(c.arg_types[0].result_value_type());
        }

        return c.static_type;
    });

    add({"Result.map_boolean"}, true,
        [](const RefineContext&) { return TypeInfo::make_result(TypeInfo::make(K::Boolean)); });
    add({"Result.map_integer"}, true,
        [](const RefineContext&) { return TypeInfo::make_result(TypeInfo::make(K::Integer)); });
    add({"Result.map_number"}, true,
        [](const RefineContext&) { return TypeInfo::make_result(TypeInfo::make(K::Number)); });
    add({"Result.map_string"}, true,
        [](const RefineContext&) { return TypeInfo::make_result(TypeInfo::make(K::String)); });

    // ── Optional: propagate inner type ──
    add({"Optional.unwrap", "Optional.unwrap_or"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Optional &&
            !c.arg_types[0].inner_types.empty()) {
            return c.arg_types[0].element_type();
        }

        return c.static_type;
    });

    add({"Optional.map"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.arg_types.size() >= 2 && c.arg_types[1].kind == K::Func &&
            c.arg_types[1].return_type && c.arg_types[1].return_type->kind != K::None) {
            return TypeInfo::make_optional(*c.arg_types[1].return_type);
        }

        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Optional &&
            !c.arg_types[0].inner_types.empty()) {
            return TypeInfo::make_optional(c.arg_types[0].element_type());
        }

        return c.static_type;
    });

    add({"Optional.flat_map"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.arg_types.size() >= 2 && c.arg_types[1].kind == K::Func &&
            c.arg_types[1].return_type) {
            return *c.arg_types[1].return_type;
        }

        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Optional &&
            !c.arg_types[0].inner_types.empty()) {
            return TypeInfo::make_optional(c.arg_types[0].element_type());
        }

        return c.static_type;
    });

    add({"Optional.filter", "Optional.or", "Optional.tap"}, true,
        [](const RefineContext& c) -> TypeInfo {
            if (!c.arg_types.empty() && c.arg_types[0].kind == K::Optional &&
                !c.arg_types[0].inner_types.empty()) {
                return TypeInfo::make_optional(c.arg_types[0].element_type());
            }

            return c.static_type;
        });

    add({"Optional.zip"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.arg_types.size() >= 2 && c.arg_types[0].kind == K::Optional &&
            !c.arg_types[0].inner_types.empty() && c.arg_types[1].kind == K::Optional &&
            !c.arg_types[1].inner_types.empty()) {
            return TypeInfo::make_optional(TypeInfo::make_tuple(
                {c.arg_types[0].element_type(), c.arg_types[1].element_type()}));
        }

        return c.static_type;
    });

    add({"Optional.flatten"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Optional &&
            !c.arg_types[0].inner_types.empty() &&
            c.arg_types[0].element_type().kind == K::Optional &&
            !c.arg_types[0].element_type().inner_types.empty()) {
            return TypeInfo::make_optional(c.arg_types[0].element_type().element_type());
        }

        return c.static_type;
    });

    add({"Optional.to_result"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Optional &&
            !c.arg_types[0].inner_types.empty()) {
            return TypeInfo::make_result(c.arg_types[0].element_type());
        }

        return c.static_type;
    });

    // ── Channel: propagate inner type ──
    add({"Channel.receive", "Channel.try_receive", "Channel.receive_timeout"}, true,
        [](const RefineContext& c) -> TypeInfo {
            if (!c.arg_types.empty() && c.arg_types[0].kind == K::Channel &&
                !c.arg_types[0].inner_types.empty()) {
                return TypeInfo::make_result(c.arg_types[0].element_type());
            }

            return c.static_type;
        });

    add({"Channel.receive_all"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Channel &&
            !c.arg_types[0].inner_types.empty()) {
            return TypeInfo::make_array(c.arg_types[0].element_type());
        }

        return c.static_type;
    });

    // ── Task: propagate inner type ──
    add({"Task.race", "Task.map", "Task.flat_map", "Task.timeout", "Task.retry"}, true,
        [](const RefineContext& c) -> TypeInfo {
            if (!c.arg_types.empty() && c.arg_types[0].kind == K::Task &&
                !c.arg_types[0].inner_types.empty()) {
                return TypeInfo::make_result(c.arg_types[0].element_type());
            }

            return c.static_type;
        });

    add({"Task.all", "Task.map_n"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Array &&
            !c.arg_types[0].inner_types.empty() && c.arg_types[0].element_type().kind == K::Task &&
            !c.arg_types[0].element_type().inner_types.empty()) {
            return TypeInfo::make_result(
                TypeInfo::make_array(c.arg_types[0].element_type().element_type()));
        }

        return c.static_type;
    });

    // ── Random: propagate array element type ──
    add({"Random.choice"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Array &&
            !c.arg_types[0].inner_types.empty()) {
            return TypeInfo::make_result(c.arg_types[0].element_type());
        }

        return c.static_type;
    });

    add({"Random.shuffle"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Array &&
            !c.arg_types[0].inner_types.empty()) {
            return TypeInfo::make_array(c.arg_types[0].element_type());
        }

        return c.static_type;
    });

    add({"Random.sample"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Array &&
            !c.arg_types[0].inner_types.empty()) {
            return TypeInfo::make_result(TypeInfo::make_array(c.arg_types[0].element_type()));
        }

        return c.static_type;
    });

    // ── Set / HashSet / Reference / Resource ──
    add({"Set.to_array"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.elem.kind != K::StdlibAny) {
            return TypeInfo::make_array(c.elem);
        }

        return c.static_type;
    });

    add({"HashSet.reduce"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.arg_types.size() >= 2) {
            return TypeInfo::make_result(c.arg_types[1]);
        }

        return c.static_type;
    });

    add({"Reference.get"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty() && c.arg_types[0].kind == K::Reference &&
            !c.arg_types[0].inner_types.empty()) {
            return c.arg_types[0].element_type();
        }

        return c.static_type;
    });

    add({"Reference.new"}, true, [](const RefineContext& c) -> TypeInfo {
        if (!c.arg_types.empty()) {
            return TypeInfo::make_reference(c.arg_types[0]);
        }

        return c.static_type;
    });

    add({"Resource.with", "Resource.using"}, true, [](const RefineContext& c) -> TypeInfo {
        if (c.arg_types.size() >= 2 && c.arg_types[1].kind == K::Func &&
            c.arg_types[1].return_type) {
            return *c.arg_types[1].return_type;
        }

        return c.static_type;
    });

    return r;
}

// Single-lookup dispatcher: resolve `name` to its refiner, applying the
// element short-circuit for element-dependent refiners.
[[nodiscard]] TypeInfo registry_refine(std::string_view name, const TypeInfo& static_type,
                                       const std::vector<TypeInfo>& arg_types) {
    using K = TypeInfo::Kind;

    static const StringMap<Refiner> registry = build_refiner_registry();

    const auto it = registry.find(name);

    if (it == registry.end()) {
        return static_type;
    }

    const Refiner& refiner = it->second;

    if (!refiner.needs_element) {
        const auto any_element = TypeInfo::make(K::StdlibAny);
        const RefineContext ctx{any_element, static_type, arg_types};
        return refiner.fn(ctx);
    }

    const auto elem = refiner_element_of_first(arg_types);

    if (elem.kind == K::StdlibAny) {
        return static_type;
    }

    const RefineContext ctx{elem, static_type, arg_types};
    return refiner.fn(ctx);
}

} // namespace

// ── Dispatcher ───────────────────────────────────────────────
TypeInfo StdlibTypeHandler::refine_return_type(std::string_view name, const TypeInfo& static_type,
                                               const std::vector<TypeInfo>& arg_types) const {
    return registry_refine(name, static_type, arg_types);
}

// ═══════════════════════════════════════════════════════════
// Stdlib parameter-type registry — derived from the shared catalog.
// ═══════════════════════════════════════════════════════════
// Parameter types are now stored in FunctionSpec::param_types in the
// shared stdlib catalog (stdlib_catalog.cpp).  This function converts
// the catalog's ReturnTypeDesc param types into TypeInfo objects for
// use by the type checker.

void StdlibTypeHandler::init_param_types() {
    for (const auto& [name, spec] : stdlib::catalog()) {
        if (!spec.param_types.empty()) {
            std::vector<TypeInfo> types;
            types.reserve(spec.param_types.size());

            for (const auto& param_type : spec.param_types) {
                types.push_back(type_info_from_desc(param_type));
            }

            functions_[name].param_types = std::move(types);
        }
    }
}

} // namespace luma
