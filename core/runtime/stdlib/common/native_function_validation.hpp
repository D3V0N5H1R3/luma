#ifndef LUMA_STDLIB_NATIVE_FUNCTION_VALIDATION_HPP
#define LUMA_STDLIB_NATIVE_FUNCTION_VALIDATION_HPP

// ═══════════════════════════════════════════════════════════
// Type-validation helpers for stdlib native functions
// ═══════════════════════════════════════════════════════════
//
// Centralised expect_* helpers that validate argument types and
// return the unwrapped typed value, or throw a RuntimeError with
// a consistent "Module.function: expected X, got 'Y'" message.
//
// Included transitively via native_function.hpp — stdlib code
// should not include this header directly.

#include <concepts>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// ─── Value concept ───
// Constrains types that expose the expected Value interface.
// Use this instead of unconstrained auto or typename T when a
// template argument is expected to behave like a Luma Value.
template <typename T>
concept ValueLike = requires(T v) {
    { v.is_array() } -> std::convertible_to<bool>;
    { v.display_type_name() } -> std::convertible_to<std::string>;
};

// ─── Argument validation helpers ───
// expect_args     — exact argument count.
// expect_min_args — minimum argument count (variadic functions).

template <typename Compare>
inline void expect_args_impl(std::string_view name, std::size_t actual, std::size_t expected,
                             Compare comp, const char* relation, const SourceLocation& loc) {
    if (!comp(actual, expected)) {
        throw RuntimeError{
            std::format("{}: expected {} {} argument(s), got {}", name, relation, expected, actual),
            loc};
    }
}

inline void expect_args(std::string_view name, std::span<const Value> args, std::size_t expected,
                        const SourceLocation& loc) {
    expect_args_impl(name, args.size(), expected, std::equal_to<>{}, "exactly", loc);
}

inline void expect_min_args(std::string_view name, std::span<const Value> args,
                            std::size_t min_count, const SourceLocation& loc) {
    expect_args_impl(name, args.size(), min_count, std::greater_equal<>{}, "at least", loc);
}

// ─── Centralized type-validation helpers ───
// These replace the per-module expect_* helpers that were duplicated across
// every stdlib module.  Each validates the type of a Value argument and
// returns the unwrapped typed pointer, or throws a RuntimeError with a
// helpful message and hint.

// Generic type-validation helper.  Throws a RuntimeError when the predicate
// returns false for the given value.  Use at call sites where the check-and-
// throw pattern is identical (same format, same intent).
template <typename Pred>
inline void validate_type(const Value& v, Pred pred, std::string_view expected,
                          std::string_view func_name, const SourceLocation& loc,
                          std::string_view hint = {}) {
    if (!pred(v)) {
        auto msg =
            std::format("{}: expected {}, got '{}'", func_name, expected, v.display_type_name());
        if (hint.empty()) {
            throw RuntimeError{std::move(msg), loc};
        }
        throw RuntimeError{std::move(msg), loc, std::string{hint}};
    }
}

[[nodiscard]] inline const std::shared_ptr<ArrayValue>&
expect_array(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_array(); }, "array", name, loc,
        "pass an array value");
    return v.as_array();
}

[[nodiscard]] inline const std::shared_ptr<DictionaryValue>&
expect_dict(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_dictionary(); }, "dictionary", name, loc,
        "pass a dictionary value");
    return v.as_dictionary();
}

[[nodiscard]] inline const std::string& expect_string_key(const Value& v, std::string_view name,
                                                          const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_string(); }, "string key", name, loc,
        "pass a string as the key");
    return v.as_string();
}

[[nodiscard]] inline const std::string& expect_string(const Value& v, std::string_view name,
                                                      const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_string(); }, "string", name, loc,
        "pass a string value");
    return v.as_string();
}

[[nodiscard]] inline double expect_numeric(const Value& v, std::string_view name,
                                           const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_integer() || val.is_number(); }, "numeric", name,
        loc, "pass an integer or number");
    return v.to_numeric();
}

[[nodiscard]] inline bool expect_boolean(const Value& v, std::string_view name,
                                         const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_bool(); }, "boolean", name, loc,
        "pass a boolean value");
    return v.as_bool();
}

[[nodiscard]] inline const std::shared_ptr<ResultValue>&
expect_result(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_result(); }, "result", name, loc,
        "pass a result value from success(), failure(), "
        "or a function that returns result<T>");
    return v.as_result();
}

[[nodiscard]] inline std::shared_ptr<SetValue> expect_set(const Value& v, std::string_view name,
                                                          const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_set(); }, "set", name, loc,
        "create a set with Set.new() or Set.from_array() first");
    return v.as_set();
}

[[nodiscard]] inline std::shared_ptr<QueueValue> expect_queue(const Value& v, std::string_view name,
                                                              const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_queue(); }, "queue", name, loc,
        "create a queue with Queue.new() first");
    return v.as_queue();
}

[[nodiscard]] inline std::shared_ptr<StackValue> expect_stack(const Value& v, std::string_view name,
                                                              const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_stack(); }, "stack", name, loc,
        "create a stack with Stack.new() first");
    return v.as_stack();
}

[[nodiscard]] inline const std::shared_ptr<ChannelValue>&
expect_channel(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_channel(); }, "channel", name, loc,
        "pass a channel created with Channel.new()");
    return v.as_channel();
}

[[nodiscard]] inline std::shared_ptr<BinaryTreeValue>
expect_tree(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_binary_tree(); }, "binary_tree", name, loc,
        "create a binary tree with BinaryTree.new() first");
    return v.as_binary_tree();
}

[[nodiscard]] inline std::shared_ptr<XmlValue> expect_xml(const Value& v, std::string_view name,
                                                          const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_xml(); }, "xml", name, loc,
        "pass an XML node value");
    return v.as_xml();
}

// Helper: extract an integer from a Value, throwing if not an integer.
[[nodiscard]] inline std::int64_t expect_integer(const Value& v, std::string_view name,
                                                 const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_integer(); }, "integer", name, loc,
        "pass an integer value");
    return v.as_integer();
}

// Helper: extract a positive integer (> 0) from a Value, throwing if not valid.
[[nodiscard]] inline std::int64_t expect_positive_integer(const Value& v, std::string_view name,
                                                          const SourceLocation& loc) {
    const auto val = expect_integer(v, name, loc);
    if (val <= 0) {
        throw RuntimeError{std::format("{}: expected positive integer, got {}", name, val), loc,
                           "pass a value greater than 0"};
    }
    return val;
}

// Helper: extract a non-negative integer (>= 0) from a Value, throwing if not valid.
[[nodiscard]] inline std::int64_t expect_non_negative_integer(const Value& v, std::string_view name,
                                                              const SourceLocation& loc) {
    const auto val = expect_integer(v, name, loc);
    if (val < 0) {
        throw RuntimeError{std::format("{}: expected non-negative integer, got {}", name, val), loc,
                           "pass a value of 0 or greater"};
    }
    return val;
}

// Helper: extract a socket from a Value, throwing if not a socket.
[[nodiscard]] inline std::shared_ptr<SocketValue>
expect_socket(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_socket(); }, "socket", name, loc,
        "pass a socket value");
    return v.as_socket();
}

// Helper: extract a decimal from a Value, throwing if not a decimal.
[[nodiscard]] inline std::shared_ptr<DecimalValue>
expect_decimal(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_decimal(); }, "decimal", name, loc,
        "create a decimal with Decimal.from_string() or Decimal.from_number() first");
    return v.as_decimal();
}

// Helper: extract a key-value store from a Value, throwing if not a key_value_store.
[[nodiscard]] inline std::shared_ptr<KeyValueStoreValue>
expect_key_value_store(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_key_value_store(); }, "key_value_store", name, loc,
        "pass a key_value_store value");
    return v.as_key_value_store();
}

// Helper: extract a reference from a Value, throwing if not a reference.
[[nodiscard]] inline const std::shared_ptr<ReferenceValue>&
expect_reference(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_reference(); }, "reference", name, loc,
        "pass a reference created with Reference.new()");
    return v.as_reference();
}

// Helper: extract an integer index from a Value, throwing if not an integer.
[[nodiscard]] inline std::int64_t expect_integer_index(const Value& v, std::string_view name,
                                                       const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_integer(); }, "integer index", name, loc,
        "pass an integer index");
    return v.as_integer();
}

// Helper: validate that a Value is callable (native function or user-defined function).
inline void expect_callable(const Value& v, std::string_view name, const SourceLocation& loc) {
    validate_type(
        v, [](const Value& val) { return val.is_callable(); }, "callable", name, loc,
        "pass a function");
}

// Helper: validate that all arguments are strings.  `hint` customises the
// remediation hint attached to the error (defaults to a generic message).
inline void expect_all_strings(std::span<const Value> args, std::string_view function_name,
                               const SourceLocation& loc,
                               std::string_view hint = "pass a string value") {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (!args[i].is_string()) {
            throw RuntimeError{std::format("{}: expected string for argument {}, got '{}'",
                                           function_name, i + 1, args[i].display_type_name()),
                               loc, std::string{hint}};
        }
    }
}

// Helper: validate that every element of an array Value is a string, throwing a
// RuntimeError otherwise.  Unlike expect_all_strings (which validates a span of
// function arguments), this validates the *elements* of a single array value —
// the "all keys must be strings" guard shared by Dictionary.from_keys / pick /
// omit, whose key arrays must contain only strings.
inline void expect_string_elements(const ArrayValue& array, std::string_view function_name,
                                   const SourceLocation& loc) {
    for (const auto& element : *array.elements) {
        if (!element.is_string()) {
            throw RuntimeError{std::format("{}: all keys must be strings", function_name), loc,
                               "ensure the array contains only string values"};
        }
    }
}

// Helper: validate that all arguments are numeric (integer or number).
inline void expect_all_numeric(std::span<const Value> args, std::string_view function_name,
                               const SourceLocation& loc) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (!args[i].is_integer() && !args[i].is_number()) {
            throw RuntimeError{std::format("{}: expected numeric for argument {}, got '{}'",
                                           function_name, i + 1, args[i].display_type_name()),
                               loc, "pass an integer or number"};
        }
    }
}

// ─── Composite validation helpers ───
// These combine arity checking + type checking + extraction in a single call,
// reducing the common multi-step "expect_args + expect_T + extract" pattern
// to a one-liner.

// Validate arity is 1 and extract the single argument as a string reference.
[[nodiscard]] inline const std::string&
extract_string(std::span<const Value> args, std::string_view fn_name, const SourceLocation& loc) {
    expect_args(fn_name, args, 1, loc);
    return expect_string(args[0], fn_name, loc);
}

// Validate arity is 1 and extract the single argument as an integer.
[[nodiscard]] inline std::int64_t
extract_integer(std::span<const Value> args, std::string_view fn_name, const SourceLocation& loc) {
    expect_args(fn_name, args, 1, loc);
    return expect_integer(args[0], fn_name, loc);
}

// Validate arity is 1 and extract the single argument as a numeric (integer or number).
[[nodiscard]] inline double extract_numeric(std::span<const Value> args, std::string_view fn_name,
                                            const SourceLocation& loc) {
    expect_args(fn_name, args, 1, loc);
    return expect_numeric(args[0], fn_name, loc);
}

// Validate arity is 1 and extract the single argument as an array.
[[nodiscard]] inline const std::shared_ptr<ArrayValue>&
extract_array(std::span<const Value> args, std::string_view fn_name, const SourceLocation& loc) {
    expect_args(fn_name, args, 1, loc);
    return expect_array(args[0], fn_name, loc);
}

// Validate arity is 2, extract first arg as array, and validate second arg is callable.
[[nodiscard]] inline std::pair<const std::shared_ptr<ArrayValue>&, const Value&>
extract_array_and_callable(std::span<const Value> args, std::string_view fn_name,
                           const SourceLocation& loc) {
    expect_args(fn_name, args, 2, loc);
    const auto& arr = expect_array(args[0], fn_name, loc);
    expect_callable(args[1], fn_name, loc);
    return {arr, args[1]};
}

} // namespace luma

#endif // LUMA_STDLIB_NATIVE_FUNCTION_VALIDATION_HPP
