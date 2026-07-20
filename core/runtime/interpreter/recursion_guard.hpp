#ifndef LUMA_RUNTIME_INTERPRETER_RECURSION_GUARD_HPP
#define LUMA_RUNTIME_INTERPRETER_RECURSION_GUARD_HPP

#include <array>
#include <cstddef>

#include "common/resource_limits.hpp"

namespace luma::runtime {

/// Thread-safe RAII guard for tracking recursion depth in value operations.
/// Each operation type has its own independent depth counter.
enum class RecursionKind {
    to_string,
    equals,
    deep_copy,
    COUNT // sentinel — must remain last
};

class RecursionGuard {
public:
    /// Attempts to enter a recursive operation. Returns false if the maximum
    /// depth has been reached (caller should handle the overflow case).
    [[nodiscard]] static bool try_enter(RecursionKind kind) noexcept {
        auto& depth = counter(kind);
        if (depth >= luma::CompileTimeLimits::max_display_depth) [[unlikely]] {
            return false;
        }
        ++depth;
        return true;
    }

    /// Leaves a recursive operation, decrementing the depth counter.
    static void leave(RecursionKind kind) noexcept {
        --counter(kind);
    }

    /// RAII helper — construct to enter, destructor leaves automatically.
    /// Check `entered()` after construction to see if the depth was exceeded.
    explicit RecursionGuard(RecursionKind kind) noexcept : kind_{kind}, entered_{try_enter(kind)} {}

    ~RecursionGuard() noexcept {
        if (entered_) {
            leave(kind_);
        }
    }

    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

    /// Returns true if the recursion depth was within limits and the counter
    /// was incremented. Returns false if the maximum depth was reached.
    [[nodiscard]] bool entered() const noexcept {
        return entered_;
    }

private:
    static constexpr std::size_t kind_count = static_cast<std::size_t>(RecursionKind::COUNT);

    // Use unsigned int to prevent undefined behaviour from signed overflow
    // if the counter is ever incremented past INT_MAX.
    static unsigned int& counter(RecursionKind kind) noexcept {
        thread_local std::array<unsigned int, kind_count> depths{};
        return depths[static_cast<std::size_t>(kind)];
    }

    RecursionKind kind_;
    bool entered_;
};

} // namespace luma::runtime

#endif // LUMA_RUNTIME_INTERPRETER_RECURSION_GUARD_HPP
