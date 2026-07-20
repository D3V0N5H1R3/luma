#ifndef LUMA_ERRORS_ERROR_HPP
#define LUMA_ERRORS_ERROR_HPP

#include <any>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "analysis/source/source_location.hpp"

namespace luma {

/// RuntimeError is the sole exception type for runtime errors thrown by the
/// VM and standard library.  It carries a source location, an optional hint,
/// and an optional typed error payload (for result<T, E> propagation).
///
/// All analysis-phase errors are reported via the Diagnostic/DiagnosticEmitter
/// system (see analysis/diagnostics/diagnostic.hpp).  Only runtime errors use
/// exceptions.
class RuntimeError : public std::runtime_error {
public:
    explicit RuntimeError(std::string_view message, SourceLocation location = {})
        : std::runtime_error{std::string{message}}, location_{location} {}

    RuntimeError(std::string_view message, SourceLocation location, std::string hint)
        : std::runtime_error{std::string{message}}, location_{location}, hint_{std::move(hint)} {}

    [[nodiscard]] const SourceLocation& location() const noexcept {
        return location_;
    }

    [[nodiscard]] const std::optional<std::string>& hint() const noexcept {
        return hint_;
    }

    /// Attach an arbitrary error payload (typically a luma::Value).
    /// This preserves the typed error from result<T, E> through propagation.
    void set_error_payload(std::any payload) {
        error_payload_ = std::move(payload);
    }

    [[nodiscard]] bool has_error_payload() const noexcept {
        return error_payload_.has_value();
    }

    [[nodiscard]] const std::any& error_payload() const noexcept {
        return error_payload_;
    }

    /// Type-safe accessor for the error payload.
    /// Returns std::nullopt if no payload is set or if the stored type
    /// does not match T.
    template <typename T> [[nodiscard]] std::optional<T> get_error_payload() const {
        if (const T* value = std::any_cast<T>(&error_payload_)) {
            return *value;
        }
        return std::nullopt;
    }

private:
    SourceLocation location_;
    std::optional<std::string> hint_;
    std::any error_payload_;
};

} // namespace luma

#endif // LUMA_ERRORS_ERROR_HPP
