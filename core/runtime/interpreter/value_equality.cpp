#include <algorithm>
#include <cassert>
#include <cmath>

#include "runtime/interpreter/recursion_guard.hpp"
#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/interpreter/value_collection_helpers.hpp"

// Value dispatch — uses switch(value_type()) for exhaustive dispatch.
// The top-level equals() switch covers all ValueType enumerators with
// no default case, so the compiler warns when a new type is added.
// See value_dispatch.hpp for dispatch conventions.

using luma::collection_helpers::elements_equal;
using luma::collection_helpers::sequential_collection_equals;

namespace {

// ─────────── equals helpers ───────────

// Exact comparison of an integer against a number.  Promoting the integer to
// double would lose precision above 2^53 and break transitivity (both
// int(2^53) and int(2^53 + 1) would equal 2^53.0 while differing from each
// other), so the number must be a whole value inside the int64 range and equal
// to the integer exactly.  NaN and ±Infinity never equal an integer.
[[nodiscard]] bool integer_equals_number(std::int64_t integer, double number) {
    // [-2^63, 2^63): both bounds are exactly representable as doubles.
    constexpr double k_int64_lower = -9223372036854775808.0;
    constexpr double k_int64_upper = 9223372036854775808.0;

    if (!(number >= k_int64_lower && number < k_int64_upper) || std::trunc(number) != number) {
        return false;
    }

    return static_cast<std::int64_t>(number) == integer;
}

// Compare numeric and boolean values (same-type and cross-numeric).
//
// Integer/number comparisons are exact (see integer_equals_number); ValueHash
// hashes an integer as its promoted double, so an integer and the number equal
// to it always share a hash.
[[nodiscard]] bool equals_numeric(const luma::Value& a, const luma::Value& b) {
    if (a.is_bool() && b.is_bool()) {
        return a.as_bool() == b.as_bool();
    }

    if (a.is_integer() && b.is_integer()) {
        return a.as_integer() == b.as_integer();
    }

    if (a.is_number() && b.is_number()) {
        const double an = a.as_number();
        const double bn = b.as_number();

        // Canonical structural equality: every NaN compares equal to every
        // other NaN so that `number` values stay reflexive (a.equals(a) is
        // always true) and are therefore usable as Set/dictionary keys
        // and with Array.contains.  This intentionally differs from IEEE 754
        // `==` (where NaN != NaN); use Math.is_not_a_number(x) to test for NaN.
        // ValueHash canonicalises NaN to match (see value_hash.cpp).
        if (std::isnan(an) && std::isnan(bn)) {
            return true;
        }

        return an == bn;
    }

    // Cross-numeric comparison (exact — see integer_equals_number).
    if (a.is_integer() && b.is_number()) {
        return integer_equals_number(a.as_integer(), b.as_number());
    }

    if (a.is_number() && b.is_integer()) {
        return integer_equals_number(b.as_integer(), a.as_number());
    }

    return false;
}

// Structural dictionary equality (order-independent).
[[nodiscard]] bool equals_dictionary(const luma::Value& a, const luma::Value& b) {
    const auto& da = a.as_dictionary()->entries;
    const auto& b_dict = b.as_dictionary();
    const auto& db = b_dict->entries;

    if (da.size() != db.size()) {
        return false;
    }

    for (const auto& [a_key, a_val] : da) {
        const auto* b_val = b_dict->find(a_key);

        if ((b_val == nullptr) || !a_val.equals(*b_val)) {
            return false;
        }
    }

    return true;
}

// Structural result equality.
[[nodiscard]] bool equals_result(const luma::Value& a, const luma::Value& b) {
    const auto& ra = *a.as_result();
    const auto& rb = *b.as_result();

    if (ra.is_success != rb.is_success) {
        return false;
    }

    return ra.owned_inner->equals(*rb.owned_inner);
}

// Structural record equality: same type name and all fields equal.
[[nodiscard]] bool equals_record(const luma::Value& a, const luma::Value& b) {
    const auto& ra = *a.as_record();
    const auto& rb = *b.as_record();

    if (ra.type_name != rb.type_name) {
        return false;
    }

    if (ra.fields.size() != rb.fields.size()) {
        return false;
    }

    for (std::size_t i{0}; i < ra.fields.size(); ++i) {
        const auto& [a_name, a_val] = ra.fields[i];
        const auto& [b_name, b_val] = rb.fields[i];

        if (a_name != b_name) {
            return false;
        }

        if (!a_val.equals(b_val)) {
            return false;
        }
    }

    return true;
}

// Range equality.
[[nodiscard]] bool equals_range(const luma::Value& a, const luma::Value& b) {
    const auto& ra = *a.as_range();
    const auto& rb = *b.as_range();

    return ra.start == rb.start && ra.end == rb.end && ra.inclusive == rb.inclusive;
}

// Compare collection types: delegates to virtual equals_to().  By-reference
// collections (Xml, KeyValueStore) use identity: a value equals itself (and
// any copy sharing the same object) but never a structurally similar one.
[[nodiscard]] bool equals_collection(const luma::Value& a, const luma::Value& b) {
    if (a.value_type() != b.value_type()) {
        return false;
    }

    const auto& collection = a.as_collection();

    if (collection->equals_kind() == luma::CollectionObject::EqualsKind::ByReference) {
        return collection.get() == b.as_collection().get();
    }

    return collection->equals_to(*b.as_collection());
}

// Identity of a task: every handle to one spawned task (including deep copies,
// which clone the TaskValue wrapper) shares its completion notifier.  A task
// created already resolved has none, so fall back to the wrapper's address.
[[nodiscard]] bool same_task(const luma::Value& a, const luma::Value& b) {
    const auto& ta = a.as_task();
    const auto& tb = b.as_task();

    if (ta->completion != nullptr) {
        return ta->completion == tb->completion;
    }

    return ta.get() == tb.get();
}

} // anonymous namespace

namespace luma {

// ─────────── ChoiceValue::operator== ───────────

bool ChoiceValue::operator==(const ChoiceValue& other) const {
    if (type_name != other.type_name || variant != other.variant) {
        return false;
    }

    return std::ranges::equal(fields, other.fields,
                              [](const auto& a, const auto& b) { return a.equals(b); });
}

// ─────────── Value::equals ───────────

bool Value::equals(const Value& other) const {
    const runtime::RecursionGuard guard{runtime::RecursionKind::Equals};
    if (!guard.entered()) {
        throw RuntimeError{"equals: maximum nesting depth exceeded", {}};
    }

    if (is_null() && other.is_null()) {
        return true;
    }

    if (is_null() || other.is_null()) {
        return false;
    }

    switch (value_type()) {
        case ValueType::Null:
            return false; // Already handled above.

        case ValueType::Bool:
            return other.is_bool() && as_bool() == other.as_bool();

        case ValueType::Integer:
        case ValueType::Number:
            return equals_numeric(*this, other);

        case ValueType::String:
            return other.is_string() && as_string() == other.as_string();

        case ValueType::Array:
            return other.is_array() &&
                   elements_equal(*as_array()->elements, *other.as_array()->elements);

        case ValueType::Dictionary:
            return other.is_dictionary() && equals_dictionary(*this, other);

        case ValueType::Tuple:
            return other.is_tuple() &&
                   elements_equal(as_tuple()->elements, other.as_tuple()->elements);

        case ValueType::Result:
            return other.is_result() && equals_result(*this, other);

        case ValueType::Record:
            return other.is_record() && equals_record(*this, other);

        case ValueType::Range:
            return other.is_range() && equals_range(*this, other);

        case ValueType::Choice:
            return other.is_choice() && *as_choice() == *other.as_choice();

        case ValueType::Reference:
            return other.is_reference() && as_reference().get() == other.as_reference().get();

        // Exact decimals compare by value, so 1.5 and 1.50 are equal — matching
        // the value-based hash so they behave correctly as dictionary/set keys.
        case ValueType::Decimal:
            return other.is_decimal() &&
                   as_decimal()->value.compare(other.as_decimal()->value) == 0;

        // Collection subtypes — delegates to virtual equals_to().
        case ValueType::Queue:
        case ValueType::Stack:
        case ValueType::Set:
        case ValueType::Xml:
        case ValueType::KeyValueStore:
            return equals_collection(*this, other);

        // Opaque handles and callables — identity: equal only to themselves
        // (the same underlying object), which keeps equality reflexive so
        // they work as Set elements and with Array.contains.  ValueHash hashes
        // them by type tag, which is consistent with any identity relation.
        // A closure's identity survives deep_copy, so a `mutable` copy of a
        // function equals the original exactly as an immutable alias does.
        case ValueType::Function:
            return other.is_function() && as_function()->identity == other.as_function()->identity;

        case ValueType::NativeFunction:
            return other.is_native_function() &&
                   as_native_function().get() == other.as_native_function().get();

        case ValueType::Task:
            return other.is_task() && same_task(*this, other);

        case ValueType::Channel:
            return other.is_channel() &&
                   as_channel()->channel.get() == other.as_channel()->channel.get();

        case ValueType::Socket:
            return other.is_socket() && as_socket().get() == other.as_socket().get();
    }

    return false;
}

// ─────────── CollectionObject::equals_to implementations ───────────

// QueueValue, StackValue — sequential, order-dependent equality via shared helper.

bool QueueValue::equals_to(const CollectionObject& other) const {
    return sequential_collection_equals(*this, other);
}

bool StackValue::equals_to(const CollectionObject& other) const {
    return sequential_collection_equals(*this, other);
}

// SetValue
// O(n) comparison using the other set's cached hash index for O(1) lookups.
bool SetValue::equals_to(const CollectionObject& other) const {
    assert(other.collection_kind() == CollectionKind::Set);
    const auto& b = static_cast<const SetValue&>(other);

    if (elements.size() != b.elements.size()) {
        return false;
    }

    return std::ranges::all_of(elements, [&](const Value& elem) { return b.contains(elem); });
}

// XmlValue reports EqualsKind::ByReference, so equals_collection() compares
// identity before equals_to() is ever called.  It inherits the base
// CollectionObject::equals_to() (which returns false) — no override needed.

// KeyValueStoreValue

// KeyValueStoreValue reports EqualsKind::ByReference — identity semantics
// (equal only to itself), never structurally equal.  It inherits the base
// equals_to() default.

} // namespace luma
