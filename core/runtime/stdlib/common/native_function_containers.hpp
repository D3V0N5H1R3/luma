#ifndef LUMA_STDLIB_NATIVE_FUNCTION_CONTAINERS_HPP
#define LUMA_STDLIB_NATIVE_FUNCTION_CONTAINERS_HPP

// ═══════════════════════════════════════════════════════════
// Container helpers for stdlib native functions
// ═══════════════════════════════════════════════════════════
//
// Sections:
//   Result building      — make_failure_value, make_success_value, failure_msg
//   Callable invocation  — invoke_callable
//   Error handling       — map/apply/safe_call/find _with_error_handling
//   Bounds checking      — check_bounds
//   Container cloning    — clone_array, clone_dict, clone_container, clone_and_remove_at
//   Container validation — check_not_empty, validate_container_size, append_bounded, make_tuple_pair
//   Functional helpers   — container_map/filter/reduce/each/partition
//   Iterator helpers     — iter_map/filter/reduce/partition/all/any/count
//
// Included transitively via native_function.hpp — stdlib code
// should not include this header directly.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/errors/error.hpp"
#include "common/index_validator.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/native_function_fwd.hpp"
#include "runtime/stdlib/common/native_function_validation.hpp"

namespace luma {

// ─── Naming helper ───

/// Formats a qualified function name as "Module.function".
/// Prefer this utility over manual std::format for consistency across the stdlib.
[[nodiscard]] inline std::string format_function_name(std::string_view module,
                                                      std::string_view function) {
    return std::format("{}.{}", module, function);
}

// ─── Result helpers ───

// Helper: create a failure result Value from a string message.
// Replaces the verbose `Value{ResultValue::failure(Value{...})}` pattern.
[[nodiscard]] inline Value make_failure_value(std::string message) {
    return Value{ResultValue::failure(Value{std::move(message)})};
}

// Helper: create a failure result Value with structured error metadata.
// The error_code is a machine-readable identifier (e.g. "index_out_of_bounds")
// and source_function is the stdlib function name (e.g. "Array.get").
[[nodiscard]] inline Value make_failure_value(std::string message, std::string error_code,
                                              std::string source_function) {
    return Value{ResultValue::failure(Value{std::move(message)}, std::move(error_code),
                                      std::move(source_function))};
}

// Helper: create a failure result with the enforced "Module.function: detail" format.
// Use this instead of manual std::format("{}.{}: ...", ...) to ensure consistency.
// Optionally attaches a machine-readable error_code for structured error handling.
[[nodiscard]] inline Value failure_msg(std::string_view module, std::string_view function,
                                       std::string_view detail, std::string_view error_code = "") {
    auto msg = std::format("{}.{}: {}", module, function, detail);
    if (error_code.empty()) {
        return make_failure_value(std::move(msg));
    }
    return make_failure_value(std::move(msg), std::string{error_code},
                              format_function_name(module, function));
}

// Helper: create a success result Value from a value.
[[nodiscard]] inline Value make_success_value(Value value) {
    return Value{ResultValue::success(std::move(value))};
}

// Helper: wrap a caught std::exception into a failure result Value.
// Replaces the repeated `make_failure_value(std::string{e.what()})` pattern
// found across task_module, socket_module, http_module, xml_module_parser, etc.
[[nodiscard]] inline Value failure_from_exception(const std::exception& e) {
    return make_failure_value(std::string{e.what()});
}

// ─── Callable invocation ───

// Invoke any callable value (native or user-defined).
[[nodiscard]] inline Value invoke_callable(const Value& fn, std::vector<Value>& args,
                                           const SourceLocation& loc) {
    if (fn.is_native_function()) {
        return fn.as_native_function()->function(args, loc);
    }

    if (detail::active_native_callable && *detail::active_native_callable) {
        return (*detail::active_native_callable)(fn, args, loc);
    }

    throw RuntimeError{"cannot call user-defined function in this context", loc,
                       "user-defined callbacks cannot be called outside the interpreter"};
}

// ─── Error handling wrappers ───

// Helper: apply a callable to each element of a container, wrapping the
// result in result<array<U>>.  Catches RuntimeError and std::exception
// to return failure.  Eliminates boilerplate try-catch in Array.map,
// Array.filter, Array.flat_map, etc.
//
// The mapper is invoked as `mapper(out, elem, callable, call_args, loc)` where
// `call_args` is a single-element buffer, reused across iterations and already
// primed with `call_args[0] = elem`, so mappers avoid allocating a fresh
// argument vector per element (see container_*/iter_* for the same pattern).
template <typename Container, typename Mapper>
[[nodiscard]] Value map_with_error_handling(const Container& elements, const Value& callable,
                                            Mapper mapper, const SourceLocation& loc) {
    auto result = std::make_shared<ArrayValue>();
    result->elements->reserve(elements.size());

    try {
        std::vector<Value> call_args(1);
        for (const auto& elem : elements) {
            call_args[0] = elem;
            mapper(*result->elements, elem, callable, call_args, loc);
        }
    } catch (const RuntimeError& e) {
        return failure_from_exception(e);
    } catch (const std::exception& e) {
        return failure_from_exception(e);
    }

    return make_success_value(Value{std::move(result)});
}

// Helper: apply a callable to each element, returning result<T> with a
// single accumulated value.
template <typename Func>
    requires std::invocable<Func>
[[nodiscard]] Value apply_with_error_handling(Func func) {
    try {
        return make_success_value(func());
    } catch (const RuntimeError& e) {
        return failure_from_exception(e);
    } catch (const std::exception& e) {
        return failure_from_exception(e);
    }
}

// Helper: execute a callable that produces a Value, wrapping exceptions in a
// structured failure result with module/function metadata.  Use this for stdlib
// functions where the try-catch is purely mechanical error translation.
template <typename Func>
    requires std::invocable<Func>
[[nodiscard]] Value safe_call(std::string_view module, std::string_view function, Func&& fn) {
    try {
        return make_success_value(fn());
    } catch (const RuntimeError& e) {
        return make_failure_value(std::string{e.what()}, "operation_failed",
                                  format_function_name(module, function));
    } catch (const std::exception& e) {
        return make_failure_value(std::string{e.what()}, "operation_failed",
                                  format_function_name(module, function));
    }
}

// Apply a predicate to each element, returning the first match as a success result.
// On exception, returns a failure result.  If no element matches, returns
// failure("element not found").
template <typename Iterator, typename Transform>
[[nodiscard]] Value find_with_error_handling(Iterator begin, Iterator end, const Value& predicate,
                                             Transform transform, const SourceLocation& loc) {
    try {
        std::vector<Value> call_args(1);
        for (auto it = begin; it != end; ++it) {
            call_args[0] = *it;
            const auto val = invoke_callable(predicate, call_args, loc);
            if (val.is_truthy()) {
                return make_success_value(transform(it));
            }
        }
    } catch (const RuntimeError& e) {
        return failure_from_exception(e);
    } catch (const std::exception& e) {
        return failure_from_exception(e);
    }
    return make_failure_value("element not found");
}

// ─── Bounds checking ───

// Helper: validate that an index is within [0, size).
// Returns a failure Value if out of bounds, or std::nullopt if valid.
[[nodiscard]] inline std::optional<Value> check_bounds(std::int64_t index, std::size_t size,
                                                       std::string_view function_name = "") {
    if (is_index_out_of_bounds(index, size)) {
        auto msg = ErrorMessages::index_out_of_bounds(index, size);
        if (function_name.empty()) {
            return make_failure_value(std::move(msg));
        }
        return make_failure_value(std::move(msg), std::string{error_codes::index_out_of_bounds},
                                  std::string{function_name});
    }

    return std::nullopt;
}

// ─── Container clone helpers ───

// Clone an array's elements for copy-on-write mutation.
//
// When extra_capacity > 0 the clone pre-allocates room for additional
// elements, avoiding a second heap allocation that would otherwise
// occur when the caller immediately pushes/inserts after cloning.
[[nodiscard]] inline std::shared_ptr<ArrayValue> clone_array(const std::shared_ptr<ArrayValue>& src,
                                                             std::size_t extra_capacity = 0) {
    assert(src && "clone_array: source array must not be null");
    auto arr = std::make_shared<ArrayValue>();
    const auto& src_elems = *src->elements;
    arr->elements->reserve(src_elems.size() + extra_capacity);
    arr->elements->assign(src_elems.begin(), src_elems.end());
    return arr;
}

// Clone a dictionary's entries for copy-on-write mutation.
[[nodiscard]] inline std::shared_ptr<DictionaryValue>
clone_dict(const std::shared_ptr<DictionaryValue>& src) {
    assert(src && "clone_dict: source dictionary must not be null");
    auto dict = std::make_shared<DictionaryValue>();
    dict->entries = src->entries;
    dict->rebuild_index();
    return dict;
}

// Generic container clone — works for any type with a public `elements` vector.
//
// When extra_capacity > 0 the clone pre-allocates room for additional
// elements, avoiding a second heap allocation that would otherwise
// occur when the caller immediately pushes after cloning.
template <typename Container>
[[nodiscard]] std::shared_ptr<Container> clone_container(const std::shared_ptr<Container>& src,
                                                         std::size_t extra_capacity = 0) {
    assert(src && "clone_container: source container must not be null");
    auto copy = std::make_shared<Container>();
    copy->elements.reserve(src->elements.size() + extra_capacity);
    copy->elements.assign(src->elements.begin(), src->elements.end());
    return copy;
}

/// Removes element at index from a cloned container (copy-on-write).
template <typename Container>
[[nodiscard]] std::shared_ptr<Container> clone_and_remove_at(const std::shared_ptr<Container>& src,
                                                             std::size_t index) {
    assert(src && "clone_and_remove_at: source must not be null");
    auto copy = clone_container(src);
    copy->elements.erase(copy->elements.begin() + static_cast<std::ptrdiff_t>(index));
    return copy;
}

// ─── Container validation helpers ───

// Check that a container is not empty, returning a structured failure if it is.
// Returns std::nullopt when the container has elements (i.e. the check passes).
[[nodiscard]] inline std::optional<Value> check_not_empty(const auto& elements,
                                                          std::string_view function_name) {
    if (elements.empty()) {
        return make_failure_value(ErrorMessages::empty_container(function_name),
                                  std::string{error_codes::empty_container},
                                  std::string{function_name});
    }
    return std::nullopt;
}

// Validate that `elements` is a non-empty array whose members all satisfy
// `is_element`.  Returns a structured failure Value describing the first
// problem, or std::nullopt when the array passes.  `element_type` names the
// expected element type in the message (e.g. "channel", "task").
//
// Shared by the array-of-T concurrency combinators (Channel.select, Task.race,
// Task.any) so their non-empty / element-type guard cannot drift apart.
template <typename Predicate>
[[nodiscard]] inline std::optional<Value>
check_non_empty_elements(const std::vector<Value>& elements, std::string_view module,
                         std::string_view function, std::string_view element_type,
                         Predicate is_element) {
    if (elements.empty()) {
        return failure_msg(module, function, "array must not be empty",
                           error_codes::invalid_argument);
    }
    for (const auto& elem : elements) {
        if (!is_element(elem)) {
            return failure_msg(module, function,
                               std::format("array element is not a {}", element_type),
                               error_codes::type_mismatch);
        }
    }
    return std::nullopt;
}

/// Validates that a container operation will not exceed the maximum size.
/// Call BEFORE the operation that would increase the size.
/// @param current_size  Current number of elements in the container.
/// @param adding        Number of elements about to be added (0 for post-construction checks).
/// @param max_size      Maximum allowed container size.
/// @param function_name Fully qualified function name for error messages (e.g. "Array.push").
/// @param loc           Source location for error reporting.
inline void validate_container_size(std::size_t current_size, std::size_t adding,
                                    std::size_t max_size, std::string_view function_name,
                                    const SourceLocation& loc) {
    // Use overflow-safe comparison: adding > max_size || current_size > max_size - adding
    // avoids the undefined behaviour that would result from current_size + adding wrapping.
    if (adding > max_size || current_size > max_size - adding) {
        throw RuntimeError{std::format("{}: exceeds maximum size", function_name), loc,
                           std::format("the maximum size is {} elements", max_size)};
    }
}

/// Convenience overload for adding a single element.
inline void validate_container_size(std::size_t current_size, std::size_t max_size,
                                    std::string_view function_name, const SourceLocation& loc) {
    validate_container_size(current_size, 1, max_size, function_name, loc);
}

/// Append every element of `src` to `dest`, first checking that the resulting
/// array stays within ResourceLimits::max_array_size.  Throws RuntimeError
/// (qualified with `function_name`, e.g. "Array.concat") on overflow, matching
/// the "Module.function: result exceeds maximum array size" convention.
///
/// Use in array-producing operations whose declared return type is a plain
/// array — Array.concat, Array.flatten, Array.flat_map — where an oversized
/// result must surface as a thrown RuntimeError rather than a result failure.
/// The check runs before any insertion, so no partial growth occurs on the
/// overflow path.
template <typename Range>
void append_bounded(std::vector<Value>& dest, const Range& src, const SourceLocation& loc,
                    std::string_view function_name) {
    const auto adding = static_cast<std::size_t>(std::size(src));

    // Overflow-safe comparison: `dest.size() + adding` could wrap past size_t
    // max, so rearrange to `dest.size() > max - adding` (adding <= max first).
    if (adding > ResourceLimits::max_array_size ||
        dest.size() > ResourceLimits::max_array_size - adding) {
        throw RuntimeError{
            std::format("{}: result exceeds maximum array size", function_name), loc,
            std::format("the maximum array size is {} elements", ResourceLimits::max_array_size)};
    }

    dest.insert(dest.end(), std::begin(src), std::end(src));
}

// Build a 2-element tuple Value.
[[nodiscard]] inline Value make_tuple_pair(Value first, Value second) {
    auto tup = std::make_shared<TupleValue>();
    tup->elements.reserve(2);
    tup->elements.push_back(std::move(first));
    tup->elements.push_back(std::move(second));
    return Value{std::move(tup)};
}

// ─── Container end-operation helpers (Stack / Queue) ───
//
// The vector-backed LIFO/FIFO containers (Stack, Queue) share three
// persistent, copy-on-write operations that differ only in which end they act
// on.  Centralising them keeps Stack.push / Queue.enqueue — and the pop/peek
// pairs — from drifting apart.

// Which end of the container an operation acts on.
enum class ContainerEnd {
    Front,
    Back
};

// Copy-on-write append of `elem` to the back of `src`, enforcing `max_size`
// before growing.  Returns the new container as a Value, or throws
// RuntimeError (qualified with `function_name`) when the size limit would be
// exceeded.  Shared by Stack.push and Queue.enqueue.
template <typename Container>
[[nodiscard]] Value push_back_bounded(const std::shared_ptr<Container>& src, const Value& elem,
                                      std::size_t max_size, std::string_view function_name,
                                      const SourceLocation& loc) {
    validate_container_size(src->elements.size(), max_size, function_name, loc);
    auto copy = clone_container(src, 1);
    copy->elements.push_back(elem);
    return Value{std::move(copy)};
}

// Remove one element from the given `end` of `src`, returning
// result<tuple<value, rest>> where `rest` is a fresh container of the same
// type that preserves element order.  Returns a structured failure when `src`
// is empty.  Shared by Stack.pop (Back) and Queue.dequeue (Front).
template <typename Container>
[[nodiscard]] Value pop_from_end(const std::shared_ptr<Container>& src, ContainerEnd end,
                                 std::string_view function_name) {
    const auto& elements = src->elements;
    if (auto fail = check_not_empty(elements, function_name)) {
        return *std::move(fail);
    }

    auto rest = std::make_shared<Container>();
    if (end == ContainerEnd::Back) {
        rest->elements.assign(elements.begin(), elements.end() - 1);
        return make_success_value(make_tuple_pair(elements.back(), Value{std::move(rest)}));
    }

    rest->elements.assign(elements.begin() + 1, elements.end());
    return make_success_value(make_tuple_pair(elements.front(), Value{std::move(rest)}));
}

// Return result<value> holding the element at the given `end` of `src` without
// removing it, or a structured failure when `src` is empty.  Shared by
// Stack.peek (Back) and Queue.peek (Front).
template <typename Container>
[[nodiscard]] Value peek_at_end(const std::shared_ptr<Container>& src, ContainerEnd end,
                                std::string_view function_name) {
    const auto& elements = src->elements;
    if (auto fail = check_not_empty(elements, function_name)) {
        return *std::move(fail);
    }
    return make_success_value(end == ContainerEnd::Back ? elements.back() : elements.front());
}

// ─── Container functional operation helpers ───
// These eliminate the near-identical map/filter/reduce/each/partition
// implementations across Queue, Stack, Set, and similar modules.

// Map each element through a callable, inserting each result into a new
// container of type Container via the provided InsertFn (signature:
// void(Container&, const Value&)).  Routing results through the inserter lets
// set-like containers preserve their uniqueness invariant (the inserter
// deduplicates) while sequence containers pass a plain push_back inserter.
// Returns result<Container>.
template <typename Container, typename InsertFn>
[[nodiscard]] Value container_map(const std::vector<Value>& elements, const Value& callable,
                                  InsertFn inserter, const SourceLocation& loc) {
    auto result = std::make_shared<Container>();
    result->elements.reserve(elements.size());

    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (const auto& elem : elements) {
            call_args[0] = elem;
            inserter(*result, invoke_callable(callable, call_args, loc));
        }
        return Value{std::move(result)};
    });
}

// Map each element through a callable into a new Container, preserving
// first-seen order and discarding later duplicates via a temporary ValueSet —
// O(n) average.  Used by set-like containers whose map may collapse distinct
// inputs onto equal outputs and must keep the result unique.  Deduplicating by
// structural hash (the same rule as dedup_in_order / Set.from_array) rather than
// a linear equals-scan per element avoids the O(n^2) cost of push_unique.
template <typename Container>
[[nodiscard]] Value container_map_unique(const std::vector<Value>& elements, const Value& callable,
                                         const SourceLocation& loc) {
    auto result = std::make_shared<Container>();
    result->elements.reserve(elements.size());

    return apply_with_error_handling([&]() -> Value {
        ValueSet seen;
        seen.reserve(elements.size());
        std::vector<Value> call_args(1);
        for (const auto& elem : elements) {
            call_args[0] = elem;
            auto mapped = invoke_callable(callable, call_args, loc);
            if (seen.insert(mapped).second) {
                result->elements.push_back(std::move(mapped));
            }
        }
        return Value{std::move(result)};
    });
}

// Concatenate two element lists into a new Container, preserving first-seen
// order and keeping only unique values via a temporary ValueSet — O(n+m)
// average.  Used by set-like containers whose concat must preserve the
// set-uniqueness invariant: a's elements come first, then b's elements that are
// not already present.  Deduplicating by structural hash (the same rule as
// container_map_unique / dedup_in_order) rather than a linear contains-scan per
// element avoids the O(n*m) cost of push_unique.  Returns the container value
// directly (concat runs no user callback, so — unlike map/filter/partition — it
// is not result-wrapped).
template <typename Container>
[[nodiscard]] Value container_concat_unique(const std::vector<Value>& a,
                                            const std::vector<Value>& b) {
    auto result = std::make_shared<Container>();
    result->elements.reserve(a.size() + b.size());

    ValueSet seen;
    seen.reserve(a.size() + b.size());
    for (const auto& elem : a) {
        if (seen.insert(elem).second) {
            result->elements.push_back(elem);
        }
    }
    for (const auto& elem : b) {
        if (seen.insert(elem).second) {
            result->elements.push_back(elem);
        }
    }
    return Value{std::move(result)};
}

// Filter elements through a predicate callable, inserting matches via
// the provided InsertFn.  InsertFn signature: void(Container&, const Value&).
// Returns result<Container>.
template <typename Container, typename InsertFn>
[[nodiscard]] Value container_filter(const std::vector<Value>& elements, const Value& callable,
                                     InsertFn inserter, const SourceLocation& loc) {
    auto result = std::make_shared<Container>();

    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (const auto& elem : elements) {
            call_args[0] = elem;
            const auto val = invoke_callable(callable, call_args, loc);
            if (val.is_truthy()) {
                inserter(*result, elem);
            }
        }
        return Value{std::move(result)};
    });
}

// Reduce elements using a binary callable and initial accumulator.
// Returns result<T>.
[[nodiscard]] inline Value container_reduce(const std::vector<Value>& elements, Value accumulator,
                                            const Value& callable, const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(2);
        for (const auto& elem : elements) {
            call_args[0] = accumulator;
            call_args[1] = elem;
            accumulator = invoke_callable(callable, call_args, loc);
        }
        return std::move(accumulator);
    });
}

// Apply a callable to each element for side effects.  Iterates over
// [begin, end).  Returns null.
template <typename Iterator>
[[nodiscard]] Value container_each(Iterator begin, Iterator end, const Value& callable,
                                   const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (auto it = begin; it != end; ++it) {
            call_args[0] = *it;
            static_cast<void>(invoke_callable(callable, call_args, loc));
        }
        return Value{NullValue{}};
    });
}

// Partition elements into two containers based on a predicate.  InsertFn
// signature: void(Container&, const Value&).  Returns result<(matches, rest)>.
template <typename Container, typename InsertFn>
[[nodiscard]] Value container_partition(const std::vector<Value>& elements, const Value& callable,
                                        InsertFn inserter, const SourceLocation& loc) {
    auto matches = std::make_shared<Container>();
    auto rest = std::make_shared<Container>();

    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (const auto& elem : elements) {
            call_args[0] = elem;
            const auto val = invoke_callable(callable, call_args, loc);
            if (val.is_truthy()) {
                inserter(*matches, elem);
            } else {
                inserter(*rest, elem);
            }
        }
        return make_tuple_pair(Value{std::move(matches)}, Value{std::move(rest)});
    });
}

// Deduplicate a range of Values, preserving first-seen order.  Invokes
// emit(const Value&) once for the first occurrence of each element, tracking
// membership in a ValueSet so the whole pass is O(n) on average.  Accepts any
// input range — contiguous vectors as well as node-chain iterators — so the
// sequence (Array), set (Set), and linked-list modules can share a single
// dedup rule instead of hand-rolling the seen-set bookkeeping each time.
template <typename Range, typename Emit> void dedup_in_order(const Range& elements, Emit&& emit) {
    ValueSet seen;
    if constexpr (requires { std::size(elements); }) {
        seen.reserve(std::size(elements));
    }
    for (const auto& elem : elements) {
        if (seen.insert(elem).second) {
            emit(elem);
        }
    }
}

// ─── Dictionary entry-iteration helpers ───
// Mirror the container_* helpers above, but pass each entry as a (key, value)
// pair — Dictionary's callbacks take two arguments where sequence containers
// take one.  Each reuses a single hoisted call_args buffer, avoiding the
// per-entry allocation that an inline `std::vector<Value>{...}` would incur.

// Apply a callable to each (key, value) entry for side effects.  Returns null.
[[nodiscard]] inline Value dict_each(const DictionaryValue& src, const Value& callable,
                                     const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(2);
        for (const auto& [k, v] : src.entries) {
            call_args[0] = Value{k};
            call_args[1] = v;
            static_cast<void>(invoke_callable(callable, call_args, loc));
        }
        return Value{NullValue{}};
    });
}

// Map each (key, value) entry through a callable, building a new dictionary that
// keeps the original keys and stores the transformed values.  The callback
// receives (key, value).  Returns result<dictionary>.
[[nodiscard]] inline Value dict_map(const DictionaryValue& src, const Value& callable,
                                    const SourceLocation& loc) {
    auto result = std::make_shared<DictionaryValue>();
    // Build the (empty) hash index up front so each set() below is O(1) rather
    // than a linear scan, keeping the build O(n) in the number of entries.
    result->rebuild_index();
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(2);
        for (const auto& [k, v] : src.entries) {
            call_args[0] = Value{k};
            call_args[1] = v;
            result->set(k, invoke_callable(callable, call_args, loc));
        }
        return Value{std::move(result)};
    });
}

// Map each value through a unary callable, building a new dictionary that keeps
// the original keys and stores the transformed values.  The callback receives
// only the value.  Returns result<dictionary>.
[[nodiscard]] inline Value dict_map_values(const DictionaryValue& src, const Value& callable,
                                           const SourceLocation& loc) {
    auto result = std::make_shared<DictionaryValue>();
    // Build the (empty) hash index up front so each set() below is O(1) rather
    // than a linear scan, keeping the build O(n) in the number of entries.
    result->rebuild_index();
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (const auto& [k, v] : src.entries) {
            call_args[0] = v;
            result->set(k, invoke_callable(callable, call_args, loc));
        }
        return Value{std::move(result)};
    });
}

// Filter (key, value) entries through a predicate, keeping matches.  The
// callback receives (key, value).  Returns result<dictionary>.
[[nodiscard]] inline Value dict_filter(const DictionaryValue& src, const Value& callable,
                                       const SourceLocation& loc) {
    auto result = std::make_shared<DictionaryValue>();
    // Build the (empty) hash index up front so each set() below is O(1) rather
    // than a linear scan, keeping the build O(n) in the number of entries.
    result->rebuild_index();
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(2);
        for (const auto& [k, v] : src.entries) {
            call_args[0] = Value{k};
            call_args[1] = v;
            if (invoke_callable(callable, call_args, loc).is_truthy()) {
                result->set(k, v);
            }
        }
        return Value{std::move(result)};
    });
}

// Reduce (key, value) entries using a callable and an initial accumulator.  The
// callback receives (accumulator, key, value).  Returns result<T>.
[[nodiscard]] inline Value dict_reduce(const DictionaryValue& src, Value accumulator,
                                       const Value& callable, const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(3);
        for (const auto& [k, v] : src.entries) {
            call_args[0] = accumulator;
            call_args[1] = Value{k};
            call_args[2] = v;
            accumulator = invoke_callable(callable, call_args, loc);
        }
        return std::move(accumulator);
    });
}

// Partition (key, value) entries into two dictionaries via a predicate.  The
// callback receives (key, value).  Returns result<(matches, rest)>.
[[nodiscard]] inline Value dict_partition(const DictionaryValue& src, const Value& callable,
                                          const SourceLocation& loc) {
    auto matches = std::make_shared<DictionaryValue>();
    auto rest = std::make_shared<DictionaryValue>();
    // Build the (empty) hash indexes up front so each set() below is O(1) rather
    // than a linear scan, keeping the build O(n) in the number of entries.
    matches->rebuild_index();
    rest->rebuild_index();
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(2);
        for (const auto& [k, v] : src.entries) {
            call_args[0] = Value{k};
            call_args[1] = v;
            if (invoke_callable(callable, call_args, loc).is_truthy()) {
                matches->set(k, v);
            } else {
                rest->set(k, v);
            }
        }
        return make_tuple_pair(Value{std::move(matches)}, Value{std::move(rest)});
    });
}

// Count entries whose (key, value) satisfy a predicate.  The callback receives
// (key, value).  Returns result<integer>.
[[nodiscard]] inline Value dict_count(const DictionaryValue& src, const Value& callable,
                                      const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::int64_t count{0};
        std::vector<Value> call_args(2);
        for (const auto& [k, v] : src.entries) {
            call_args[0] = Value{k};
            call_args[1] = v;
            if (invoke_callable(callable, call_args, loc).is_truthy()) {
                ++count;
            }
        }
        return Value{count};
    });
}

// ─── Iterator-based container operation helpers ───
// These work with any forward iterator over Values (e.g. LinkedListNodeIterator),
// decoupling functional operations from vector storage assumptions.

// Map elements from [begin, end) through a callable, collecting results via
// an emitter function.  EmitFn signature: void(Value&&).  Returns result<T>
// where T is produced by the finalize callable.
template <typename Iterator, typename EmitFn, typename FinalizeFn>
[[nodiscard]] Value iter_map(Iterator begin, Iterator end, const Value& callable, EmitFn emit,
                             FinalizeFn finalize, const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (auto it = begin; it != end; ++it) {
            call_args[0] = *it;
            emit(invoke_callable(callable, call_args, loc));
        }
        return finalize();
    });
}

// Filter elements from [begin, end) through a predicate, emitting matches.
// EmitFn signature: void(const Value&).  Returns result<T> via finalize.
template <typename Iterator, typename EmitFn, typename FinalizeFn>
[[nodiscard]] Value iter_filter(Iterator begin, Iterator end, const Value& callable, EmitFn emit,
                                FinalizeFn finalize, const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (auto it = begin; it != end; ++it) {
            call_args[0] = *it;
            const auto val = invoke_callable(callable, call_args, loc);
            if (val.is_truthy()) {
                emit(*it);
            }
        }
        return finalize();
    });
}

// Reduce elements from [begin, end) using a binary callable and initial accumulator.
// Returns result<T>.
template <typename Iterator>
[[nodiscard]] Value iter_reduce(Iterator begin, Iterator end, Value accumulator,
                                const Value& callable, const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(2);
        for (auto it = begin; it != end; ++it) {
            call_args[0] = accumulator;
            call_args[1] = *it;
            accumulator = invoke_callable(callable, call_args, loc);
        }
        return std::move(accumulator);
    });
}

// Partition elements from [begin, end) into two groups via a predicate.
// EmitMatchFn/EmitRestFn signature: void(const Value&).  Returns result<T>
// via finalize.
template <typename Iterator, typename EmitMatchFn, typename EmitRestFn, typename FinalizeFn>
[[nodiscard]] Value iter_partition(Iterator begin, Iterator end, const Value& callable,
                                   EmitMatchFn emit_match, EmitRestFn emit_rest,
                                   FinalizeFn finalize, const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (auto it = begin; it != end; ++it) {
            call_args[0] = *it;
            const auto val = invoke_callable(callable, call_args, loc);
            if (val.is_truthy()) {
                emit_match(*it);
            } else {
                emit_rest(*it);
            }
        }
        return finalize();
    });
}

// ─── Short-circuiting predicate scans ───
// Apply a predicate to each element of [begin, end), reusing a single argument
// buffer.  Shared by Array.all / any / count and available to future
// containers that lack a vector backing store.

// Returns success(true) when the predicate holds for every element (or the
// range is empty), success(false) on the first element that fails.  Stops at
// the first failing element.
template <typename Iterator>
[[nodiscard]] Value iter_all(Iterator begin, Iterator end, const Value& predicate,
                             const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (auto it = begin; it != end; ++it) {
            call_args[0] = *it;
            if (!invoke_callable(predicate, call_args, loc).is_truthy()) {
                return Value{false};
            }
        }
        return Value{true};
    });
}

// Returns success(true) on the first element satisfying the predicate,
// success(false) when none do (or the range is empty).  Stops at the first
// matching element.
template <typename Iterator>
[[nodiscard]] Value iter_any(Iterator begin, Iterator end, const Value& predicate,
                             const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::vector<Value> call_args(1);
        for (auto it = begin; it != end; ++it) {
            call_args[0] = *it;
            if (invoke_callable(predicate, call_args, loc).is_truthy()) {
                return Value{true};
            }
        }
        return Value{false};
    });
}

// Returns success(count) — the number of elements satisfying the predicate.
template <typename Iterator>
[[nodiscard]] Value iter_count(Iterator begin, Iterator end, const Value& predicate,
                               const SourceLocation& loc) {
    return apply_with_error_handling([&]() -> Value {
        std::int64_t count{0};
        std::vector<Value> call_args(1);
        for (auto it = begin; it != end; ++it) {
            call_args[0] = *it;
            if (invoke_callable(predicate, call_args, loc).is_truthy()) {
                ++count;
            }
        }
        return Value{count};
    });
}

} // namespace luma

#endif // LUMA_STDLIB_NATIVE_FUNCTION_CONTAINERS_HPP
