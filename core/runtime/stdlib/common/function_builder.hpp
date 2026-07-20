#ifndef LUMA_STDLIB_FUNCTION_BUILDER_HPP
#define LUMA_STDLIB_FUNCTION_BUILDER_HPP

// ═══════════════════════════════════════════════════════════════════
// Function Builder DSL — Fluent API for Stdlib Registration
// ═══════════════════════════════════════════════════════════════════
//
// Reduces boilerplate when registering stdlib functions by providing
// a declarative builder that auto-generates arity checking, type
// validation, and error messages.
//
// Usage:
//
//   ModuleBuilder{"MyModule", env}
//       .func("length", 1)
//           .extract_body(expect_array,
//               [](const auto& arr, const Args&, SourceLocation) -> Value {
//                   return Value{static_cast<std::int64_t>(arr->elements->size())};
//               })
//       .func("get", 2)
//           .extract_body(expect_array,
//               [](const auto& arr, const Args& args, SourceLocation loc) -> Value {
//                   const auto idx = expect_integer_index(args[1], "MyModule.get", loc);
//                   if (auto fail = check_bounds(idx, arr->elements->size(), "MyModule.get")) {
//                       return *std::move(fail);
//                   }
//                   return (*arr->elements)[static_cast<std::size_t>(idx)];
//               })
//       .constant("pi", Value{3.14159265358979});
//
// Numeric convenience helpers (avoid repetitive unary wrappers):
//
//   ModuleBuilder{"Math", env}
//       .checked_unary("sine", [](double x) { return std::sin(x); })
//       .checked_unary_to_int("floor", [](double x) { return std::floor(x); })
//       .positive_unary("log_e", [](double x) { return std::log(x); });

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "runtime/interpreter/environment.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/numeric_helpers.hpp"

namespace luma {

// ─── FuncBuilder ───
// Intermediate builder returned by ModuleBuilder::func().  Call
// extract_body(extractor, body) to complete a method-style definition with
// automatic self-extraction, raw_unary()/raw_binary() for fixed-arity helpers,
// or raw_body() for a raw NativeFunction lambda.

template <typename Self> class FuncBuilder {
public:
    FuncBuilder(Self& parent, std::string qualified_name, std::size_t arity, const EnvPtr& env)
        : parent_{parent}, qualified_name_{std::move(qualified_name)}, arity_{arity}, env_{env} {}

    // Register a method-style function with automatic self-extraction.
    //
    // ExtractFn must have signature:
    //   auto(const Value&, string_view, const SourceLocation&)
    // BodyFn must have signature:
    //   Value(const auto& self, const Args& args, SourceLocation loc)
    //
    // COW (copy-on-write) contract
    // ────────────────────────────
    // `self` is const — never mutate it directly.  Luma values are
    // reference-counted and shared; mutating `self` in-place would
    // corrupt every other binding that holds the same pointer.
    //
    // To produce a modified value, clone first and mutate the copy:
    //
    //   auto copy = clone_array(self);     // allocates a new, writable copy
    //   copy->elements->push_back(args[1]);
    //   return Value{std::move(copy)};
    //
    // The same pattern applies to every container type:
    //   clone_array(src)                   → ArrayValue
    //   clone_container<QueueValue>(src)   → QueueValue / StackValue / etc.
    //   std::make_shared<DictValue>(*src)  → DictionaryValue
    //
    // Never do:  src->elements->push_back(...);  // mutates aliased storage!
    template <typename ExtractFn, typename BodyFn>
    Self& extract_body(ExtractFn extractor, BodyFn body) {
        define_method(env_, qualified_name_, arity_, std::move(extractor), std::move(body));
        return parent_;
    }

    /// Registers a function that takes exactly one argument (auto-validates arity).
    /// Fn must have signature: Value(const Value&, SourceLocation)
    template <typename Fn> Self& raw_unary(Fn fn) {
        return raw_body(
            [fn = std::move(fn)](std::span<const Value> args, SourceLocation loc) -> Value {
                return fn(args[0], loc);
            });
    }

    /// Registers a function that takes exactly two arguments (auto-validates arity).
    /// Fn must have signature: Value(const Value&, const Value&, SourceLocation)
    template <typename Fn> Self& raw_binary(Fn fn) {
        return raw_body(
            [fn = std::move(fn)](std::span<const Value> args, SourceLocation loc) -> Value {
                return fn(args[0], args[1], loc);
            });
    }

    // Register a raw native function (no auto-extraction).
    // BodyFn must have signature: Value(std::span<const Value>, SourceLocation)
    template <typename BodyFn> Self& raw_body(BodyFn body) {
        auto name = qualified_name_;
        auto arity = arity_;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702) // unreachable code (after expect_args may throw)
#endif
        define_native(env_, qualified_name_,
                      [n = std::move(name), arity, body = std::move(body)](
                          std::span<const Value> args, SourceLocation loc) -> Value {
                          expect_args(n, args, arity, loc);
                          return body(args, loc);
                      });
#ifdef _MSC_VER
#pragma warning(pop)
#endif
        return parent_;
    }

private:
    Self& parent_;
    std::string qualified_name_;
    std::size_t arity_;
    EnvPtr env_;
};

// ─── ModuleBuilder ───
// Fluent builder for registering a stdlib module's functions.

class ModuleBuilder {
public:
    ModuleBuilder(std::string_view module_name, const EnvPtr& env)
        : module_name_{module_name}, env_{env} {}

    // Start defining a function.
    [[nodiscard]] FuncBuilder<ModuleBuilder> func(std::string_view name, std::size_t arity) {
        auto qualified = std::string{module_name_} + "." + std::string{name};
        return FuncBuilder<ModuleBuilder>{*this, std::move(qualified), arity, env_};
    }

    /// Conditionally register a native function. No-op when condition is false.
    /// Equivalent to calling func(name, arity).raw_body(fn) but guarded by condition.
    ModuleBuilder& func_if(bool condition, std::string_view name, std::size_t arity,
                           NativeFunction fn) {
        if (condition) {
            func(name, arity).raw_body(std::move(fn));
        }
        return *this;
    }

    // Register a named constant.
    ModuleBuilder& constant(std::string_view name, Value value) {
        auto qualified = std::string{module_name_} + "." + std::string{name};
        env_->define(std::move(qualified), std::move(value), false);
        return *this;
    }

    // Register a raw native function without using the builder chain.
    ModuleBuilder& native(std::string_view name, NativeFunction fn) {
        auto qualified = std::string{module_name_} + "." + std::string{name};
        define_native(env_, qualified, std::move(fn));
        return *this;
    }

    // ─── Numeric convenience helpers ─────────────────────────────────
    //
    // Shorthand registrations for the most common unary numeric
    // patterns found in Math and similar modules.  Each handles arity
    // checking, numeric extraction, error formatting, and result
    // wrapping automatically.

    // Unary numeric → result<number>.
    // Applies fn, checks for NaN/Inf, returns result<number>.
    // Use for: sine, cosine, tangent, exponential, etc.
    template <typename Fn> ModuleBuilder& checked_unary(std::string_view name, Fn fn) {
        auto [qualified, mod, fn_name] = make_unary_names(name);
        auto reg_name = qualified;
        define_native(
            env_, reg_name,
            [q = std::move(qualified), m = std::move(mod), n = std::move(fn_name),
             fn = std::move(fn)](std::span<const Value> args, SourceLocation loc) -> Value {
                expect_args(q, args, 1, loc);
                const auto x = expect_numeric(args[0], q, loc);
                const auto r = fn(x);
                if (!stdlib::is_valid_numeric(r)) {
                    return make_failure_value(error_msg(m, n, "result is not a real number"));
                }
                return make_success_value(Value{r});
            });
        return *this;
    }

    // Unary numeric → result<integer>.
    // Applies fn (e.g. std::floor), converts to int64, returning
    // failure if the result is out of integer range.
    // Use for: floor, ceil, round, truncate.
    template <typename Fn> ModuleBuilder& checked_unary_to_int(std::string_view name, Fn fn) {
        auto [qualified, mod, fn_name] = make_unary_names(name);
        auto reg_name = qualified;
        define_native(env_, reg_name,
                      [q = std::move(qualified), fn = std::move(fn)](std::span<const Value> args,
                                                                     SourceLocation loc) -> Value {
                          expect_args(q, args, 1, loc);
                          const auto x = expect_numeric(args[0], q, loc);
                          const auto r = fn(x);
                          if (const auto int_result = stdlib::safe_to_int64(r)) {
                              return make_success_value(Value{*int_result});
                          }
                          return make_failure_value(
                              std::format("{}: result out of integer range", q));
                      });
        return *this;
    }

    // Unary numeric (positive domain) → result<number>.
    // Validates the argument is positive, applies fn, returns
    // result<number>.  Use for: log_e, log_2, log_10.
    template <typename Fn> ModuleBuilder& positive_unary(std::string_view name, Fn fn) {
        auto [qualified, mod, fn_name] = make_unary_names(name);
        auto reg_name = qualified;
        define_native(
            env_, reg_name,
            [q = std::move(qualified), m = std::move(mod), n = std::move(fn_name),
             fn = std::move(fn)](std::span<const Value> args, SourceLocation loc) -> Value {
                expect_args(q, args, 1, loc);
                const auto x = expect_numeric(args[0], q, loc);
                if (x <= 0) {
                    return make_failure_value(error_msg(m, n, "non-positive argument"));
                }
                return make_success_value(Value{fn(x)});
            });
        return *this;
    }

    // ─── String predicate convenience helper ─────────────────────────
    //
    // Registers a unary string predicate that returns true when every
    // character satisfies `pred` — an ASCII-oriented `bool(unsigned char)`
    // callback (the helper performs the char → unsigned char cast).
    //
    // `empty_result` is returned for the empty string.  The character-class
    // predicates deliberately disagree on the empty case (is_ascii / is_blank
    // treat it as vacuously true; is_alpha / is_digit / is_whitespace / … treat
    // it as false), so the empty result is an explicit parameter rather than a
    // normalised default.
    template <typename Pred>
    ModuleBuilder& char_predicate(std::string_view name, Pred pred, bool empty_result) {
        return func(name, 1).extract_body(expect_string,
                                          [pred = std::move(pred), empty_result](
                                              const auto& s, const Args&, SourceLocation) -> Value {
                                              if (s.empty()) {
                                                  return Value{empty_result};
                                              }
                                              return Value{std::ranges::all_of(s, [&pred](char c) {
                                                  return pred(static_cast<unsigned char>(c));
                                              })};
                                          });
    }

private:
    std::string module_name_;
    EnvPtr env_;

    // Builds the qualified name, module name, and function name strings
    // needed by the numeric unary helpers.  The string_view parameters
    // do not outlive the current call, so conversion to std::string is
    // required for lambda capture.
    struct UnaryNames {
        std::string qualified;
        std::string mod;
        std::string fn_name;
    };

    [[nodiscard]] UnaryNames make_unary_names(std::string_view name) const {
        return {std::string{module_name_} + "." + std::string{name}, std::string{module_name_},
                std::string{name}};
    }
};

} // namespace luma

#endif // LUMA_STDLIB_FUNCTION_BUILDER_HPP
