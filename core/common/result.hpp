#ifndef LUMA_COMMON_RESULT_HPP
#define LUMA_COMMON_RESULT_HPP

#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace luma {

// A generic result type representing either a success value or an error.
// Modeled after Rust's Result<T, E>.
//
// ── Error handling policy ────────────────────────────────────────────────────
//
// Luma uses three mechanisms for error signaling, each with a distinct role:
//
//   1. Result<T, E>  — for operations where failure is an expected, recoverable
//      outcome that the caller must explicitly handle (e.g. parsing user input,
//      file I/O, type-checked operations).  Use this instead of exceptions when
//      failure is a normal part of the control flow.
//
//   2. C++ exceptions — for unexpected or unrecoverable errors: programming
//      mistakes (pre-condition violations), resource exhaustion, or
//      environmental failures that the current call stack cannot recover from.
//      Exceptions propagate until caught at a well-defined boundary (e.g. the
//      CLI top-level handler in cli_internal.hpp) where they are mapped to exit
//      codes.
//
//   3. std::optional<T> — when absence is expected and the reason is obvious
//      to the caller (no diagnostics needed).
//
// Stdlib module convention:
//   - Prefer returning Luma Result values (Value wrapping a result<T>) to the
//     Luma runtime so that Luma programs can inspect and recover from errors.
//   - Use C++ exceptions only for internal error propagation that must be
//     caught at module call boundaries (e.g. unexpected host-system failures).
//
// ─────────────────────────────────────────────────────────────────────────────
//
// This is one of three error signaling mechanisms in Luma — see
// runtime/interpreter/value_type.hpp for the full conventions.
//
// Use Result<T, E> instead of std::optional<T> when the caller needs to
// know WHY an operation failed (not just that it failed).
//
// Guidelines:
//   - std::optional<T>: absence is expected and the reason is obvious
//   - Result<T, E>: failure reasons vary and must be communicated
//
// Move semantics (RT-19):
//   Result is move-friendly by design.  The ok() and err() factory
//   functions accept by value and std::move into the internal variant.
//   The value() and error() accessors are overloaded for lvalue-ref
//   (returns T&/E&) and rvalue-ref (returns T&&/E via std::move).
//   Implicit move/copy constructors and assignment operators are
//   compiler-generated via std::variant and behave correctly.  No
//   explicit move constructor is needed.
template <typename T, typename E = std::string> class Result {
    static_assert(!std::is_same_v<T, E>, "Result<T, E> requires T and E to be distinct types. "
                                         "Wrap one in a named struct to disambiguate.");

public:
    // Construction.
    [[nodiscard]] static Result ok(T value) {
        return Result{std::move(value)};
    }

    [[nodiscard]] static Result err(E error) {
        return Result{std::move(error), ErrorTag{}};
    }

    // Observers.
    [[nodiscard]] bool is_ok() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    [[nodiscard]] bool is_err() const noexcept {
        return std::holds_alternative<E>(data_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return is_ok();
    }

    // Access.
    [[nodiscard]] T& value() & {
        if (is_err()) {
            throw std::logic_error{"Result::value() called on error"};
        }
        return std::get<T>(data_);
    }

    [[nodiscard]] const T& value() const& {
        if (is_err()) {
            throw std::logic_error{"Result::value() called on error"};
        }
        return std::get<T>(data_);
    }

    [[nodiscard]] T&& value() && {
        if (is_err()) {
            throw std::logic_error{"Result::value() called on error"};
        }
        return std::get<T>(std::move(data_));
    }

    [[nodiscard]] E& error() & {
        if (is_ok()) {
            throw std::logic_error{"Result::error() called on success"};
        }
        return std::get<E>(data_);
    }

    [[nodiscard]] const E& error() const& {
        if (is_ok()) {
            throw std::logic_error{"Result::error() called on success"};
        }
        return std::get<E>(data_);
    }

    [[nodiscard]] E error() && {
        if (is_ok()) {
            throw std::logic_error{"Result::error() called on success"};
        }
        return std::get<E>(std::move(data_));
    }

    // Convenience: value_or returns value if ok, or the provided default.
    [[nodiscard]] T value_or(T default_value) const& {
        return is_ok() ? std::get<T>(data_) : std::move(default_value);
    }

    // Rvalue overload: moves the contained value when the Result is an rvalue.
    [[nodiscard]] T value_or(T default_value) && {
        if (is_ok()) {
            return std::get<T>(std::move(data_));
        }
        return default_value;
    }

    // Monadic chaining: applies f to the value if ok, propagates the error otherwise.
    // F must accept const T& and return Result<U, E> for some U.
    template <typename F>
    [[nodiscard]] auto and_then(F&& f) const& -> std::invoke_result_t<F, const T&> {
        using ResultType = std::invoke_result_t<F, const T&>;
        if (is_ok()) {
            return std::forward<F>(f)(value());
        }
        return ResultType::err(error());
    }

    // Rvalue overload: moves the contained value into the continuation.
    template <typename F> [[nodiscard]] auto and_then(F&& f) && -> std::invoke_result_t<F, T&&> {
        using ResultType = std::invoke_result_t<F, T&&>;
        if (is_ok()) {
            return std::forward<F>(f)(std::get<T>(std::move(data_)));
        }
        return ResultType::err(std::get<E>(std::move(data_)));
    }

    // Monadic mapping: transforms the success value with f, propagates the error otherwise.
    // F must accept const T& and return U.
    template <typename F>
    [[nodiscard]] auto map(F&& f) const& -> Result<std::invoke_result_t<F, const T&>, E> {
        using U = std::invoke_result_t<F, const T&>;
        if (is_ok()) {
            return Result<U, E>::ok(std::forward<F>(f)(value()));
        }
        return Result<U, E>::err(error());
    }

    // Monadic error chaining: applies f to the error if err, propagates the value otherwise.
    // F must accept const E& and return Result<T, E2> for some E2.
    template <typename F>
    [[nodiscard]] auto or_else(F&& f) const& -> std::invoke_result_t<F, const E&> {
        using ResultType = std::invoke_result_t<F, const E&>;
        if (is_err()) {
            return std::forward<F>(f)(error());
        }
        return ResultType::ok(value());
    }

    // Monadic error mapping: transforms the error value with f, propagates the success value.
    // F must accept const E& and return E2.
    template <typename F>
    [[nodiscard]] auto map_err(F&& f) const& -> Result<T, std::invoke_result_t<F, const E&>> {
        using E2 = std::invoke_result_t<F, const E&>;
        if (is_err()) {
            return Result<T, E2>::err(std::forward<F>(f)(error()));
        }
        return Result<T, E2>::ok(value());
    }

    // Convenience: like value_or but takes a callable that receives the error.
    // F must accept const E& and return T.
    template <typename F> [[nodiscard]] T value_or_else(F&& f) const& {
        return is_ok() ? value() : std::forward<F>(f)(error());
    }

private:
    struct ErrorTag {};

    explicit Result(T value) : data_{std::move(value)} {}

    explicit Result(E error, ErrorTag) : data_{std::move(error)} {}

    std::variant<T, E> data_;
};

} // namespace luma

#endif // LUMA_COMMON_RESULT_HPP
