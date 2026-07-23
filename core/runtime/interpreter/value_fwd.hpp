#ifndef LUMA_INTERPRETER_VALUE_FWD_HPP
#define LUMA_INTERPRETER_VALUE_FWD_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Value System — 3-Header Split (RT-11)
// ─────────────────────────────────────────────────────────────────────────────
//
// The Value type is split across three headers to control compile-time
// coupling and break circular dependencies:
//
//   value_fwd.hpp  (this file)
//     Forward declarations, enums (ValueType, CollectionKind), type aliases
//     (NativeFunction), and sentinel types (NullValue).  Include this when
//     only type names are needed — e.g. in other headers that receive Value
//     by pointer or reference without inspecting its internals.  Keeps
//     transitive include cost minimal.
//
//   value_type.hpp
//     Full Value class definition with the std::variant, accessor methods,
//     type-query helpers, and ValueHash/ValueEqual functors.  Include this
//     when code needs to construct, inspect, or pattern-match on Values.
//
//   value.hpp  (umbrella)
//     Includes value_fwd.hpp + value_type.hpp + value_collections.hpp.  Exists
//     for backward compatibility so existing code that includes "value.hpp"
//     continues to work without changes.
//
// This split reduces recompilation when only forward declarations change
// and lets headers like environment.hpp avoid pulling in the full variant
// machinery.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <functional>
#include <memory>
#include <span>

#include "analysis/source/source_location.hpp"

namespace luma {

// Forward declarations.
class CancellationToken;
class Environment;
using EnvPtr = std::shared_ptr<Environment>;

class Channel;
class Task;

// ─────────────────────────────────────────────────────────────────────────────
// Runtime Value Type System
// ─────────────────────────────────────────────────────────────────────────────
//
// The runtime value system consists of three layers:
//
//   Value          The central tagged variant that holds any Luma runtime
//                  value.  Every variable, expression result, and stack slot
//                  in the VM is a Value.  Defined in value_type.hpp.
//
//   ValueType      Cached type discriminant enum — avoids repeated
//                  std::holds_alternative calls on the variant.  Stored
//                  alongside the variant data inside each Value.
//
//   ValueHash /    Hash and equality functors for Value, enabling use in
//   ValueEqual     unordered containers (e.g. dictionary keys, sets).
//                  Defined in value_type.hpp.
//
// Compound value types (ArrayValue, FunctionValue, etc.) are heap-allocated
// structs whose shared_ptr is stored in the Value variant.  Their definitions
// live in value_collections.hpp to keep Value itself focused.
//
// Function-related types form a three-part hierarchy:
//
//   CompiledFunction     Compile-time artifact: bytecode chunk, arity, upvalue
//                        descriptors, and debug metadata.  Owned by the
//                        compiler output; lives in chunk.hpp.
//
//   FunctionValue        Runtime closure: pairs a CompiledFunction pointer
//                        with captured upvalues.  This is what the VM stores
//                        in a Value when a function is defined at runtime.
//
//   NativeFunctionValue  A C++ function exposed to Luma code (stdlib bindings).
//                        Wraps a NativeFunction callable with parameter and
//                        return type metadata for error reporting.
// ─────────────────────────────────────────────────────────────────────────────

class Value;

// Null sentinel type.
struct NullValue {
    bool operator==(const NullValue&) const = default;
};

// Heap-allocated compound types — forward-declared here,
// defined after Value (MSVC requires complete types for std::pair/vector).
struct ArrayValue;
struct BinaryTreeNode;
struct BinaryTreeValue;
struct ChannelValue;
struct ChoiceValue;
struct LinkedListNode;
struct CompiledFunction; // Compile-time bytecode (chunk.hpp).
struct DecimalValue;
struct DictionaryValue;
struct FunctionValue; // Runtime closure (value_collections.hpp).
struct GraphValue;

struct HashSetValue;
struct KeyValueStoreValue;
struct LinkedListValue;
struct NativeFunctionValue; // C++ stdlib binding (value_collections.hpp).
struct QueueValue;
struct RangeValue;
struct RecordValue;
struct ReferenceValue;
struct ResultValue;
struct SetValue;
struct SocketValue;
struct StackValue;
struct TaskValue;
struct TupleValue;

struct XmlValue;

// Kind discriminant for collection types consolidated into a single variant slot.
// These types have no VM opcodes and are only accessed via stdlib functions,
// so consolidation reduces the variant size without affecting hot paths.
enum class CollectionKind : std::uint8_t {
    Queue,
    Stack,
    Set,
    Xml,
    KeyValueStore,
    HashSet,
    LinkedList,
    BinaryTree,
    Graph,
};

// Abstract base for rarely-used collection types — one variant slot for all.
struct CollectionObject;

// A native (C++) function.
using NativeFunction = std::function<Value(std::span<const Value>, SourceLocation)>;

// Cached type discriminant for Value — avoids repeated std::holds_alternative calls.
enum class ValueType : std::uint8_t {
    Null,
    Bool,
    Integer,
    Number,
    String,
    Array,
    Dictionary,
    Tuple,
    Result,
    Record,
    Range,
    Choice,
    Function,
    NativeFunction,
    Task,
    Channel,
    Socket,
    Queue,
    Stack,
    Set,
    Xml,
    KeyValueStore,
    HashSet,
    LinkedList,
    BinaryTree,
    Graph,
    Reference,
    Decimal,
};

// ─────────────────────────────────────────────────────────────────────────────
// Value Categories — Bitflags for grouping related value types.
// ─────────────────────────────────────────────────────────────────────────────

/// Categories for grouping related value types.
enum class ValueCategory : std::uint32_t {
    None = 0,
    Numeric = 1 << 0,    // integer, number
    Callable = 1 << 1,   // function, native_function
    Collection = 1 << 2, // array, dictionary, queue, stack, linked_list, etc.
    Primitive = 1 << 3,  // null, bool, integer, number, string
    Iterable = 1 << 4,   // array, dictionary, string, range, queue, stack, etc.
};

constexpr ValueCategory operator|(ValueCategory a, ValueCategory b) noexcept {
    return static_cast<ValueCategory>(static_cast<std::uint32_t>(a) |
                                      static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr bool has_category(ValueCategory categories, ValueCategory query) noexcept {
    return (static_cast<std::uint32_t>(categories) & static_cast<std::uint32_t>(query)) != 0;
}

/// Returns the categories for a given value type.
[[nodiscard]] constexpr ValueCategory categories_of(ValueType type) noexcept {
    using enum ValueCategory;
    switch (type) {
        case ValueType::Null:
        case ValueType::Bool:
            return Primitive;
        case ValueType::Integer:
        case ValueType::Number:
            return Numeric | Primitive;
        case ValueType::String:
            return Primitive | Iterable;
        case ValueType::Array:
        case ValueType::Dictionary:
            return Collection | Iterable;
        case ValueType::Tuple:
        case ValueType::Result:
        case ValueType::Record:
            return None;
        case ValueType::Range:
            return Iterable;
        case ValueType::Choice:
            return None;
        case ValueType::Function:
        case ValueType::NativeFunction:
            return Callable;
        case ValueType::Task:
        case ValueType::Channel:
        case ValueType::Socket:
            return None;
        case ValueType::Queue:
        case ValueType::Stack:
        case ValueType::Set:
            return Collection | Iterable;
        case ValueType::Xml:
            return Collection;
        case ValueType::KeyValueStore:
        case ValueType::HashSet:
        case ValueType::LinkedList:
        case ValueType::BinaryTree:
            return Collection | Iterable;
        case ValueType::Graph:
            return Collection;
        case ValueType::Reference:
            return None;
        case ValueType::Decimal:
            return Numeric | Primitive;
    }
    return None;
}

} // namespace luma

#endif // LUMA_INTERPRETER_VALUE_FWD_HPP
