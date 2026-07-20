#ifndef LUMA_STDLIB_RETURN_TYPE_HPP
#define LUMA_STDLIB_RETURN_TYPE_HPP

#include <string>
#include <utility>
#include <vector>

namespace luma::stdlib {

// ═══════════════════════════════════════════════════════════════════
// ReturnTypeDesc — Lightweight type descriptor for stdlib functions
// ═══════════════════════════════════════════════════════════════════
//
// Describes the return type of a stdlib function without depending on
// the TypeInfo class from core/analysis.  This lets the shared stdlib
// catalog carry return-type information that can be converted to
// TypeInfo by the type checker, eliminating the need to maintain
// return types separately in stdlib_type_handler.cpp.
//
// Design note: ReturnTypeDesc and TypeInfo (core/analysis/types/type_checker.hpp)
// intentionally remain separate representations.  ReturnTypeDesc is the
// lightweight, dependency-free descriptor that lets the shared stdlib catalog
// describe return types without depending on the analysis library; the type
// checker converts each descriptor to a full TypeInfo via type_info_from_desc()
// (see core/analysis/types/stdlib_type_signatures.cpp).  This separation is a
// deliberate layering boundary — shared/ must not depend on core/analysis — and
// is not a duplication to be merged.
//
// Usage:
//   using R = ReturnTypeDesc;
//   R::result(R::integer_type())     // result<integer>
//   R::array(R::string_type())       // array<string>
//   R::named("TimeParts")            // a record/choice type by name

struct ReturnTypeDesc {
    enum Kind : uint8_t {
        Integer,
        Number,
        String,
        Boolean,
        Void,
        None,
        Any, // StdlibAny — unrefined generic type
        Array,
        Dictionary,
        Result,
        Optional,
        Channel,
        Task,
        Reference,
        Tuple,
        Named,       // Record or choice type referred to by name
        Unspecified, // No return type declared — use manual override
        Func,        // Callable / function value
    };

    Kind kind{Unspecified};
    std::string named_type;
    std::vector<ReturnTypeDesc> inner;

    // ── Primitive factories ─────────────────────────────────
    //
    // Naming convention: every primitive factory carries a `_type` suffix so
    // the names are uniform and callers never have to remember which are bare
    // and which are suffixed.  The suffix is also mandatory for void_type()
    // because `void` is a C++ keyword.  Each returns a type descriptor, not a
    // value.

    [[nodiscard]] static ReturnTypeDesc integer_type() {
        return {Integer, {}, {}};
    }

    [[nodiscard]] static ReturnTypeDesc number_type() {
        return {Number, {}, {}};
    }

    [[nodiscard]] static ReturnTypeDesc string_type() {
        return {String, {}, {}};
    }

    [[nodiscard]] static ReturnTypeDesc boolean_type() {
        return {Boolean, {}, {}};
    }

    [[nodiscard]] static ReturnTypeDesc void_type() {
        return {Void, {}, {}};
    }

    [[nodiscard]] static ReturnTypeDesc none_type() {
        return {None, {}, {}};
    }

    [[nodiscard]] static ReturnTypeDesc any_type() {
        return {Any, {}, {}};
    }

    [[nodiscard]] static ReturnTypeDesc unspecified_type() {
        return {Unspecified, {}, {}};
    }

    [[nodiscard]] static ReturnTypeDesc func_type() {
        return {Func, {}, {}};
    }

    // ── Generic type factories ──────────────────────────────

    [[nodiscard]] static ReturnTypeDesc array(ReturnTypeDesc elem) {
        return {Array, "", {std::move(elem)}};
    }

    [[nodiscard]] static ReturnTypeDesc dict(ReturnTypeDesc value) {
        return {Dictionary, "", {std::move(value)}};
    }

    [[nodiscard]] static ReturnTypeDesc result(ReturnTypeDesc value) {
        return {Result, "", {std::move(value)}};
    }

    [[nodiscard]] static ReturnTypeDesc optional(ReturnTypeDesc value) {
        return {Optional, "", {std::move(value)}};
    }

    [[nodiscard]] static ReturnTypeDesc channel(ReturnTypeDesc value) {
        return {Channel, "", {std::move(value)}};
    }

    [[nodiscard]] static ReturnTypeDesc task(ReturnTypeDesc value) {
        return {Task, "", {std::move(value)}};
    }

    [[nodiscard]] static ReturnTypeDesc reference(ReturnTypeDesc value) {
        return {Reference, "", {std::move(value)}};
    }

    [[nodiscard]] static ReturnTypeDesc tuple(std::vector<ReturnTypeDesc> elements) {
        return {Tuple, "", std::move(elements)};
    }

    [[nodiscard]] static ReturnTypeDesc named(std::string name) {
        return {Named, std::move(name), {}};
    }

    // ── Convenience shortcuts ───────────────────────────────

    [[nodiscard]] static ReturnTypeDesc result_integer() {
        return result(integer_type());
    }

    [[nodiscard]] static ReturnTypeDesc result_number() {
        return result(number_type());
    }

    [[nodiscard]] static ReturnTypeDesc result_string() {
        return result(string_type());
    }

    [[nodiscard]] static ReturnTypeDesc result_boolean() {
        return result(boolean_type());
    }

    [[nodiscard]] static ReturnTypeDesc result_void() {
        return result(void_type());
    }

    [[nodiscard]] static ReturnTypeDesc result_any() {
        return result(any_type());
    }

    [[nodiscard]] static ReturnTypeDesc result_array_any() {
        return result(array(any_type()));
    }

    [[nodiscard]] static ReturnTypeDesc result_named(std::string name) {
        return result(named(std::move(name)));
    }

    [[nodiscard]] static ReturnTypeDesc array_any() {
        return array(any_type());
    }

    [[nodiscard]] static ReturnTypeDesc array_string() {
        return array(string_type());
    }

    [[nodiscard]] static ReturnTypeDesc array_integer() {
        return array(integer_type());
    }

    [[nodiscard]] static ReturnTypeDesc array_number() {
        return array(number_type());
    }

    [[nodiscard]] static ReturnTypeDesc array_array_number() {
        return array(array(number_type()));
    }

    [[nodiscard]] static ReturnTypeDesc dict_any() {
        return dict(any_type());
    }

    [[nodiscard]] static ReturnTypeDesc dict_string() {
        return dict(string_type());
    }

    [[nodiscard]] static ReturnTypeDesc optional_any() {
        return optional(any_type());
    }

    [[nodiscard]] static ReturnTypeDesc channel_any() {
        return channel(any_type());
    }

    [[nodiscard]] static ReturnTypeDesc task_any() {
        return task(any_type());
    }

    [[nodiscard]] static ReturnTypeDesc reference_any() {
        return reference(any_type());
    }
};

} // namespace luma::stdlib

#endif // LUMA_STDLIB_RETURN_TYPE_HPP
