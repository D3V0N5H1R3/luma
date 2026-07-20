#ifndef LUMA_INTERPRETER_VALUE_TYPE_HPP
#define LUMA_INTERPRETER_VALUE_TYPE_HPP

// Full Value class definition, the ValueVariantMember concept, and
// the ValueHash / ValueEqual utility types.
// Depends on value_fwd.hpp for forward declarations and enums.
//
// ── Error signaling conventions ──
//
// The Luma interpreter uses three error models, each appropriate for
// its context:
//
// 1. Exceptions (RuntimeError) — Programming errors and unrecoverable
//    failures.  Used in the VM, stdlib, and compiler for type mismatches,
//    bounds violations, and resource exhaustion.
//
// 2. std::optional<T> — Expected absence.  Used when a lookup may
//    legitimately find nothing (e.g., dictionary find, optional unwrap).
//
// 3. Result<T> / result types — Recoverable failures.  Used in I/O
//    operations, parsing, and network calls where failure is a normal
//    outcome that the caller should handle.
//
// Guideline: Use exceptions for bugs, optional for "not found", and
// result for "operation failed but caller can recover".
//
// See also: common/result.hpp, runtime/interpreter/runtime_exceptions.hpp

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "common/string_hash.hpp"
#include "runtime/interpreter/value_fwd.hpp"

namespace luma {

// ─────────────────────── Value ───────────────────────

// Constrains T to types that the Value variant can hold.
// Provides clear compile errors when holds<T>() or as<T>() is called
// with an invalid type.
namespace detail {

template <typename T, typename Variant> struct is_variant_member : std::false_type {};

template <typename T, typename... Ts>
struct is_variant_member<T, std::variant<Ts...>>
    : std::bool_constant<(std::same_as<T, Ts> || ...)> {};

// Maps a compound value type T to its ValueType discriminant.
// Primary template is undefined; only specialised types are accepted.
template <typename T> struct value_type_for;

/// Declares a value_type_for specialisation mapping CppType to ValueType::EnumValue.
#define LUMA_VALUE_TYPE_FOR(CppType, EnumValue)                                                    \
    template <> struct value_type_for<CppType> {                                                   \
        static constexpr auto value = ValueType::EnumValue;                                        \
    };                                                                                             \
    static_assert(true, "")

// clang-format off
LUMA_VALUE_TYPE_FOR(ArrayValue,          Array);
LUMA_VALUE_TYPE_FOR(DictionaryValue,     Dictionary);
LUMA_VALUE_TYPE_FOR(TupleValue,          Tuple);
LUMA_VALUE_TYPE_FOR(ResultValue,         Result);
LUMA_VALUE_TYPE_FOR(RecordValue,         Record);
LUMA_VALUE_TYPE_FOR(RangeValue,          Range);
LUMA_VALUE_TYPE_FOR(ChoiceValue,         Choice);
LUMA_VALUE_TYPE_FOR(FunctionValue,       Function);
LUMA_VALUE_TYPE_FOR(NativeFunctionValue, NativeFunction);
LUMA_VALUE_TYPE_FOR(TaskValue,           Task);
LUMA_VALUE_TYPE_FOR(ChannelValue,        Channel);
LUMA_VALUE_TYPE_FOR(SocketValue,         Socket);
LUMA_VALUE_TYPE_FOR(ReferenceValue,      Reference);
LUMA_VALUE_TYPE_FOR(QueueValue,          Queue);
LUMA_VALUE_TYPE_FOR(StackValue,          Stack);
LUMA_VALUE_TYPE_FOR(SetValue,            Set);
LUMA_VALUE_TYPE_FOR(XmlValue,            Xml);
LUMA_VALUE_TYPE_FOR(KeyValueStoreValue,  KeyValueStore);
LUMA_VALUE_TYPE_FOR(HashSetValue,        HashSet);
LUMA_VALUE_TYPE_FOR(LinkedListValue,     LinkedList);
LUMA_VALUE_TYPE_FOR(BinaryTreeValue,     BinaryTree);
LUMA_VALUE_TYPE_FOR(GraphValue,          Graph);
// clang-format on

#undef LUMA_VALUE_TYPE_FOR

} // namespace detail

template <typename T>
concept ValueVariantMember = detail::is_variant_member<
    T,
    std::variant<
        NullValue, bool, std::int64_t, double, std::string, std::shared_ptr<ArrayValue>,
        std::shared_ptr<DictionaryValue>, std::shared_ptr<TupleValue>, std::shared_ptr<ResultValue>,
        std::shared_ptr<RecordValue>, std::shared_ptr<RangeValue>, std::shared_ptr<ChoiceValue>,
        std::shared_ptr<FunctionValue>, std::shared_ptr<NativeFunctionValue>,
        std::shared_ptr<TaskValue>, std::shared_ptr<ChannelValue>, std::shared_ptr<SocketValue>,
        std::shared_ptr<CollectionObject>, std::shared_ptr<ReferenceValue>>>::value;

// Compound value type whose shared_ptr is stored directly in Value::Variant.
template <typename T>
concept DirectVariantValue =
    requires { detail::value_type_for<T>::value; } && ValueVariantMember<std::shared_ptr<T>>;

// Collection subtype stored as shared_ptr<CollectionObject> in the variant.
// Defined out-of-line in value_collections.hpp where the full hierarchy is
// available.
template <typename T>
concept CollectionSubtype =
    requires { detail::value_type_for<T>::value; } && !ValueVariantMember<std::shared_ptr<T>>;

// ─────────────────────────────────────────────────────────────────────────────
// Value — the universal runtime value type for the Luma interpreter.
//
// Value is a tagged union (discriminated variant) that can hold any Luma
// runtime value: primitives (null, bool, integer, number, string), compound
// types (array, dictionary, tuple, record, choice, result, range, function),
// concurrency types (task, channel), and collection types (queue, stack, set,
// hash_set, linked_list, binary_tree, graph, xml, key_value_store).
//
// ─── Design ──────────────────────────────────────────────────────────────
//
// Storage uses a std::variant (data_) paired with a ValueType discriminant
// (type_) for O(1) type queries without visiting the variant.  Compound
// types are heap-allocated behind std::shared_ptr for reference semantics
// and safe sharing (e.g. closures capturing mutable arrays).
//
// Collection subtypes (QueueValue, StackValue, SetValue, etc.) share a
// single variant slot via std::shared_ptr<CollectionObject>.  The type_
// discriminant distinguishes them; as_*() accessors use static_pointer_cast
// to recover the concrete type.  The is_collection() predicate returns true
// for any CollectionObject-derived type.
//
// ─── Performance ─────────────────────────────────────────────────────────
//
// The is_*() queries and as_*() accessors are intentionally inline and
// trivial — they compile to a single integer comparison or variant access.
// This is critical because hot interpreter loops (VM dispatch, stdlib
// functions) call these on every value operation.
//
// ─── Organisation ────────────────────────────────────────────────────────
//
// The class declaration is grouped into four sections:
//   1. Construction & Type Identity — variant typedef, constructors, holds()
//   2. Type Queries (is_*)          — one per Luma type, plus composites
//   3. Type Accessors (as_*)        — unchecked extraction (caller must
//                                     verify type first)
//   4. Operations                   — conversion, comparison, deep copy
//   5. Internal Storage             — variant data and type discriminant
// ─────────────────────────────────────────────────────────────────────────────

class Value {
public:
    // ================================================================
    // Construction & Type Identity
    // ================================================================

    using Variant = std::variant<
        NullValue, bool, std::int64_t, double, std::string, std::shared_ptr<ArrayValue>,
        std::shared_ptr<DictionaryValue>, std::shared_ptr<TupleValue>, std::shared_ptr<ResultValue>,
        std::shared_ptr<RecordValue>, std::shared_ptr<RangeValue>, std::shared_ptr<ChoiceValue>,
        std::shared_ptr<FunctionValue>, std::shared_ptr<NativeFunctionValue>,
        std::shared_ptr<TaskValue>, std::shared_ptr<ChannelValue>, std::shared_ptr<SocketValue>,
        std::shared_ptr<CollectionObject>, std::shared_ptr<ReferenceValue>>;

    Value() = default;

    // Implicit constructors for primitive and string types — intentional
    // by design (§15 exception).  These are the natural Luma value types;
    // implicit conversion keeps interpreter and stdlib code readable.
    Value(NullValue) : data_{NullValue{}}, type_{ValueType::Null} {}

    Value(bool v) : data_{v}, type_{ValueType::Bool} {}

    Value(std::int64_t v) : data_{v}, type_{ValueType::Integer} {}

    Value(int v) : data_{static_cast<std::int64_t>(v)}, type_{ValueType::Integer} {}

    Value(double v) : data_{v}, type_{ValueType::Number} {}

    Value(std::string v) : data_{std::move(v)}, type_{ValueType::String} {}

    Value(const char* v) : data_{std::string{v}}, type_{ValueType::String} {}

    // Explicit template constructor for compound types stored directly
    // in the variant — prevents silent implicit conversions from shared_ptr
    // to Value.  The value_type_for trait maps each type to its ValueType
    // discriminant; the DirectVariantValue concept constrains to types whose
    // shared_ptr appears in the Variant.
    template <DirectVariantValue T>
    explicit Value(std::shared_ptr<T> v)
        : data_{std::move(v)}, type_{detail::value_type_for<T>::value} {}

    // Explicit template constructor for collection subtypes stored as
    // shared_ptr<CollectionObject> in the variant.  Defined out-of-line
    // in value_collections.hpp where the full CollectionObject hierarchy is
    // available.
    template <CollectionSubtype T> explicit Value(std::shared_ptr<T> v);

    // ================================================================
    // Type Queries (is_*)
    // ================================================================
    // Each predicate compares the cached type_ discriminant for O(1)
    // performance.  is_collection() checks the variant alternative
    // directly since all collection subtypes share one variant slot.

    template <ValueVariantMember T> [[nodiscard]] bool holds() const {
        return std::holds_alternative<T>(data_);
    }

    [[nodiscard]] ValueType value_type() const noexcept {
        return type_;
    }

    [[nodiscard]] bool is_null() const {
        return type_ == ValueType::Null;
    }

    [[nodiscard]] bool is_some() const {
        return type_ != ValueType::Null;
    }

    [[nodiscard]] bool is_bool() const {
        return type_ == ValueType::Bool;
    }

    [[nodiscard]] bool is_integer() const {
        return type_ == ValueType::Integer;
    }

    [[nodiscard]] bool is_number() const {
        return type_ == ValueType::Number;
    }

    [[nodiscard]] bool is_string() const {
        return type_ == ValueType::String;
    }

    [[nodiscard]] bool is_array() const {
        return type_ == ValueType::Array;
    }

    [[nodiscard]] bool is_dictionary() const {
        return type_ == ValueType::Dictionary;
    }

    [[nodiscard]] bool is_tuple() const {
        return type_ == ValueType::Tuple;
    }

    [[nodiscard]] bool is_result() const {
        return type_ == ValueType::Result;
    }

    [[nodiscard]] bool is_record() const {
        return type_ == ValueType::Record;
    }

    [[nodiscard]] bool is_range() const {
        return type_ == ValueType::Range;
    }

    [[nodiscard]] bool is_choice() const {
        return type_ == ValueType::Choice;
    }

    [[nodiscard]] bool is_function() const {
        return type_ == ValueType::Function;
    }

    [[nodiscard]] bool is_native_function() const {
        return type_ == ValueType::NativeFunction;
    }

    [[nodiscard]] bool is_task() const {
        return type_ == ValueType::Task;
    }

    [[nodiscard]] bool is_channel() const {
        return type_ == ValueType::Channel;
    }

    [[nodiscard]] bool is_socket() const {
        return type_ == ValueType::Socket;
    }

    [[nodiscard]] bool is_queue() const {
        return type_ == ValueType::Queue;
    }

    [[nodiscard]] bool is_stack() const {
        return type_ == ValueType::Stack;
    }

    [[nodiscard]] bool is_set() const {
        return type_ == ValueType::Set;
    }

    [[nodiscard]] bool is_xml() const {
        return type_ == ValueType::Xml;
    }

    [[nodiscard]] bool is_key_value_store() const {
        return type_ == ValueType::KeyValueStore;
    }

    [[nodiscard]] bool is_hash_set() const {
        return type_ == ValueType::HashSet;
    }

    [[nodiscard]] bool is_linked_list() const {
        return type_ == ValueType::LinkedList;
    }

    [[nodiscard]] bool is_binary_tree() const {
        return type_ == ValueType::BinaryTree;
    }

    [[nodiscard]] bool is_graph() const {
        return type_ == ValueType::Graph;
    }

    [[nodiscard]] bool is_reference() const {
        return type_ == ValueType::Reference;
    }

    /// Returns true if this value holds any CollectionObject-derived type
    /// (queue, stack, set, hash_set, xml, key_value_store, linked_list, binary_tree, graph).
    [[nodiscard]] bool is_collection() const noexcept {
        return std::holds_alternative<std::shared_ptr<CollectionObject>>(data_);
    }

    [[nodiscard]] bool is_callable() const {
        return is_function() || is_native_function();
    }

    /// Returns true if this value's type belongs to the given category.
    [[nodiscard]] bool has_category(ValueCategory cat) const noexcept {
        return luma::has_category(categories_of(type_), cat);
    }

    // ================================================================
    // Type Accessors (as_*)
    // ================================================================
    // Unchecked extraction — callers must verify the type first (via
    // is_*() or value_type()).  Calling an accessor on the wrong type
    // throws std::bad_variant_access.  Direct-variant types return a
    // const shared_ptr reference; collection subtypes return a new
    // shared_ptr obtained via static_pointer_cast.
    [[nodiscard]] bool as_bool() const {
        return std::get<bool>(data_);
    }

    [[nodiscard]] std::int64_t as_integer() const {
        return std::get<std::int64_t>(data_);
    }

    [[nodiscard]] double as_number() const {
        return std::get<double>(data_);
    }

    [[nodiscard]] const std::string& as_string() const {
        return std::get<std::string>(data_);
    }

    [[nodiscard]] std::string& as_string_mut() {
        return std::get<std::string>(data_);
    }

    [[nodiscard]] const std::shared_ptr<ArrayValue>& as_array() const {
        return std::get<std::shared_ptr<ArrayValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<DictionaryValue>& as_dictionary() const {
        return std::get<std::shared_ptr<DictionaryValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<TupleValue>& as_tuple() const {
        return std::get<std::shared_ptr<TupleValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<ResultValue>& as_result() const {
        return std::get<std::shared_ptr<ResultValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<RecordValue>& as_record() const {
        return std::get<std::shared_ptr<RecordValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<RangeValue>& as_range() const {
        return std::get<std::shared_ptr<RangeValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<ChoiceValue>& as_choice() const {
        return std::get<std::shared_ptr<ChoiceValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<FunctionValue>& as_function() const {
        return std::get<std::shared_ptr<FunctionValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<NativeFunctionValue>& as_native_function() const {
        return std::get<std::shared_ptr<NativeFunctionValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<TaskValue>& as_task() const {
        return std::get<std::shared_ptr<TaskValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<ChannelValue>& as_channel() const {
        return std::get<std::shared_ptr<ChannelValue>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<SocketValue>& as_socket() const {
        return std::get<std::shared_ptr<SocketValue>>(data_);
    }

    [[nodiscard]] std::shared_ptr<QueueValue> as_queue() const {
        return std::static_pointer_cast<QueueValue>(
            std::get<std::shared_ptr<CollectionObject>>(data_));
    }

    [[nodiscard]] std::shared_ptr<StackValue> as_stack() const {
        return std::static_pointer_cast<StackValue>(
            std::get<std::shared_ptr<CollectionObject>>(data_));
    }

    [[nodiscard]] std::shared_ptr<SetValue> as_set() const {
        return std::static_pointer_cast<SetValue>(
            std::get<std::shared_ptr<CollectionObject>>(data_));
    }

    [[nodiscard]] std::shared_ptr<XmlValue> as_xml() const {
        return std::static_pointer_cast<XmlValue>(
            std::get<std::shared_ptr<CollectionObject>>(data_));
    }

    [[nodiscard]] std::shared_ptr<KeyValueStoreValue> as_key_value_store() const {
        return std::static_pointer_cast<KeyValueStoreValue>(
            std::get<std::shared_ptr<CollectionObject>>(data_));
    }

    [[nodiscard]] std::shared_ptr<HashSetValue> as_hash_set() const {
        return std::static_pointer_cast<HashSetValue>(
            std::get<std::shared_ptr<CollectionObject>>(data_));
    }

    [[nodiscard]] std::shared_ptr<LinkedListValue> as_linked_list() const {
        return std::static_pointer_cast<LinkedListValue>(
            std::get<std::shared_ptr<CollectionObject>>(data_));
    }

    [[nodiscard]] std::shared_ptr<BinaryTreeValue> as_binary_tree() const {
        return std::static_pointer_cast<BinaryTreeValue>(
            std::get<std::shared_ptr<CollectionObject>>(data_));
    }

    [[nodiscard]] std::shared_ptr<GraphValue> as_graph() const {
        return std::static_pointer_cast<GraphValue>(
            std::get<std::shared_ptr<CollectionObject>>(data_));
    }

    [[nodiscard]] const std::shared_ptr<CollectionObject>& as_collection() const {
        return std::get<std::shared_ptr<CollectionObject>>(data_);
    }

    [[nodiscard]] const std::shared_ptr<ReferenceValue>& as_reference() const {
        return std::get<std::shared_ptr<ReferenceValue>>(data_);
    }

    // ================================================================
    // Operations (conversion, comparison, deep copy)
    // ================================================================

    // Get numeric value as double (works for integer or number).
    // This is the canonical numeric coercion point — all cross-numeric
    // comparisons and arithmetic promotions should use this method.
    // Throws RuntimeError if called on a non-numeric type.
    [[nodiscard]] double to_numeric() const {
        if (is_integer()) {
            return static_cast<double>(as_integer());
        }

        if (is_number()) {
            return as_number();
        }

        throw RuntimeError{"to_numeric() called on non-numeric type: " + display_type_name(), {}};
    }

    // Convert to string representation.
    [[nodiscard]] std::string to_string() const;

    // Type name as string.
    [[nodiscard]] std::string display_type_name() const;

    // Truthiness.
    [[nodiscard]] bool is_truthy() const;

    // Equality comparison.
    [[nodiscard]] bool equals(const Value& other) const;

    // Three-way comparison with partial ordering (NaN makes numeric ordering
    // partial).  Handles integer×integer, number×number, and mixed numeric
    // pairs, plus string×string.  All other type combinations return
    // std::partial_ordering::unordered.
    [[nodiscard]] friend std::partial_ordering operator<=>(const Value& a,
                                                           const Value& b) noexcept {
        if (a.is_integer() && b.is_integer()) {
            return a.as_integer() <=> b.as_integer();
        }
        if (a.is_number() && b.is_number()) {
            return a.as_number() <=> b.as_number();
        }
        if (a.is_integer() && b.is_number()) {
            return static_cast<double>(a.as_integer()) <=> b.as_number();
        }
        if (a.is_number() && b.is_integer()) {
            return a.as_number() <=> static_cast<double>(b.as_integer());
        }
        if (a.is_string() && b.is_string()) {
            const std::strong_ordering ord = a.as_string() <=> b.as_string();
            return ord;
        }
        return std::partial_ordering::unordered;
    }

    // Deep copy — creates independent copies of compound types
    // (arrays, dictionaries, tuples, records, results) so that the
    // returned Value shares no mutable state with the original.
    [[nodiscard]] Value deep_copy() const;

    // ================================================================
    // Internal Storage
    // ================================================================

private:
    Variant data_{NullValue{}};

    // ── Cached type discriminant ──
    //
    // type_ caches the variant discriminant as a ValueType enum for O(1)
    // type queries.  This duplicates the variant's built-in index() but
    // provides:
    //   1. Named enum values (more readable than numeric indices)
    //   2. Guaranteed O(1) type checks (variant::index() may or may not
    //      be O(1) depending on implementation)
    //   3. Stable ABI (variant index order is fragile if types are reordered)
    //
    // Trade-off: 4-8 extra bytes per Value (56 → 48 if removed).
    // TODO(perf): Benchmark removing type_ and deriving from variant index.
    ValueType type_{ValueType::Null};

public:
#ifndef NDEBUG
    /// Debug-only: asserts that type_ matches the variant's active alternative.
    void verify_type_consistency() const;
#endif
};

// Hash and equality functors for Value — enable use as keys in unordered
// containers (e.g. DictionaryValue, HashSetValue).
//
// ValueHash implements structural (content-based) hashing:
//
//   Primitive types (null, bool, integer, number, string) hash by value.
//
//   Compound types (tuple, array, dictionary) hash structurally up to a
//   maximum nesting depth of 8 levels, then fall back to the type tag.
//   This depth limit bounds the worst-case cost to O(n^8) but in practice
//   Luma programs do not nest homogeneous collections that deeply.
//
//   All other types (functions, channels, tasks, etc.) hash by type tag
//   only — they are not meaningful dictionary keys.
//
// Invariant: ValueHash is consistent with ValueEqual.  Two values that
// compare equal under ValueEqual always produce the same hash.
// (Proof: ValueEqual delegates to Value::equals(), which is structural for
// tuples/arrays/dicts.  ValueHash below is also structural for those types.)
//
// Implementation: The operator() delegates to detail::hash_value_structural(),
// which is defined in value_hash.cpp.  That translation unit includes
// value_collections.hpp to access ArrayValue::elements, DictionaryValue::entries,
// and TupleValue::elements, which are incomplete types at this point in the header.

namespace detail {
/// Computes a structural hash for v up to `depth` levels of nesting.
/// Requires full definitions of ArrayValue, DictionaryValue, TupleValue
/// (provided by value_collections.hpp); implemented in value_hash.cpp.
[[nodiscard]] std::size_t hash_value_structural(const Value& v, int depth) noexcept;
} // namespace detail

struct ValueHash {
    [[nodiscard]] std::size_t operator()(const Value& v) const noexcept {
        return detail::hash_value_structural(v, 0);
    }
};

struct ValueEqual {
    bool operator()(const Value& a, const Value& b) const {
        return a.equals(b);
    }
};

/// Convenience alias for unordered maps keyed by Value.
template <typename V> using ValueMap = std::unordered_map<Value, V, ValueHash, ValueEqual>;

/// Convenience alias for unordered sets of Values.
using ValueSet = std::unordered_set<Value, ValueHash, ValueEqual>;

} // namespace luma

#endif // LUMA_INTERPRETER_VALUE_TYPE_HPP
