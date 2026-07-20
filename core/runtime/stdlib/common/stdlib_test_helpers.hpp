// ─────────────────────────────────────────────────────────────────────────────
// Standard Library Test Helpers
// ─────────────────────────────────────────────────────────────────────────────
// Convenience utilities for writing C++ unit tests that exercise stdlib
// functions directly via Value construction and SourceLocation stubs.
//
// These helpers complement the existing test infrastructure:
//   - tests/test_framework.hpp           — assertion macros and test runner
//   - tests/shared_eval.hpp              — full-pipeline eval helpers
//   - tests/runtime/stdlib_test_helpers.hpp — eval() wrapper for stdlib tests
//
// This header provides Value factory functions for use in tests that need
// to construct Luma values from C++ literals without going through the
// full compilation pipeline.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_STDLIB_TEST_HELPERS_HPP
#define LUMA_STDLIB_TEST_HELPERS_HPP

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma::testing {

// ─────────────────────── Value Factories ───────────────────────

/// Creates a Value from common C++ types for use in tests.
/// Value already has implicit constructors for primitives, but these
/// named factories improve readability at call sites.
inline Value make_value(std::int64_t v) {
    return Value{v};
}

inline Value make_value(double v) {
    return Value{v};
}

inline Value make_value(bool v) {
    return Value{v};
}

inline Value make_value(const char* v) {
    return Value{std::string(v)};
}

inline Value make_value(std::string v) {
    return Value{std::move(v)};
}

/// Creates a null Value.
inline Value make_null() {
    return Value{};
}

/// Creates an array Value from a list of values.
inline Value make_array(std::initializer_list<Value> elements) {
    auto arr = std::make_shared<ArrayValue>();
    arr->elements->assign(elements);
    return Value{std::move(arr)};
}

/// Creates a tuple Value from a list of values.
inline Value make_tuple(std::initializer_list<Value> elements) {
    auto tup = std::make_shared<TupleValue>();
    tup->elements.assign(elements);
    return Value{std::move(tup)};
}

// ─────────────────────── Test Source Location ───────────────────────

/// A dummy source location for test invocations of native functions.
inline SourceLocation test_location() {
    return SourceLocation{};
}

} // namespace luma::testing

#endif // LUMA_STDLIB_TEST_HELPERS_HPP
