#pragma once

// Shared dispatch utilities for Value type operations.
//
// The Luma Value type uses three dispatch mechanisms across its
// implementation files (formatting, copying, equality).  This header
// provides shared utilities to keep them consistent and ensure
// exhaustive type coverage:
//
//   overloaded<Ts...> — Aggregate helper for std::visit with multiple
//                      lambdas (luma::overloaded from common/overloaded.hpp).
//                      Used in value_formatting.cpp where each variant
//                      alternative has unique handling.
//
//   switch (value_type())
//                    — Preferred for operations where groups of types
//                      share behaviour (e.g. all primitives are shallow-
//                      copied).  Omit `default:` to get compiler warnings
//                      on unhandled ValueType enumerators.
//
//   apply_to_elements / sequence_element_count
//                    — Uniform element access for array and tuple values,
//                      abstracting the ArrayValue COW shared_ptr vs the
//                      TupleValue direct vector.  Used in value_hash.cpp
//                      to avoid duplicating the array/tuple iteration.
//
// When adding a new type to the Value variant:
//   1. Add the ValueType enumerator in value_fwd.hpp
//   2. Each switch/visit across value_*.cpp will require a new case —
//      the compiler will flag missing cases.

// value_collections.hpp provides the full definitions of ArrayValue and
// TupleValue needed to access their elements members below.
#include "common/overloaded.hpp"
#include "runtime/interpreter/value_collections.hpp"

namespace luma {

// Apply fn to each element of a sequential value (array or tuple).
// For other value types this is a no-op.
template <typename Fn> void apply_to_elements(const Value& v, Fn fn) {
    if (v.is_array()) {
        for (const auto& elem : *v.as_array()->elements) {
            fn(elem);
        }
    } else if (v.is_tuple()) {
        for (const auto& elem : v.as_tuple()->elements) {
            fn(elem);
        }
    }
}

// Return the number of elements in a sequential value (array or tuple).
// Returns 0 for other value types.
[[nodiscard]] inline std::size_t sequence_element_count(const Value& v) {
    if (v.is_array()) {
        return v.as_array()->elements->size();
    }
    if (v.is_tuple()) {
        return v.as_tuple()->elements.size();
    }
    return 0;
}

} // namespace luma
