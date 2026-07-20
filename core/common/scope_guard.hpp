#ifndef LUMA_COMMON_SCOPE_GUARD_HPP
#define LUMA_COMMON_SCOPE_GUARD_HPP

#include <utility>

namespace luma {

// RAII scope guard — calls a cleanup function on destruction.
// Use for push/pop scope patterns to prevent leaks on exceptions.
//
// Example:
//     push_scope();
//     ScopeGuard guard{[this] { pop_scope(); }};
//     // ... work that might throw ...
//     // pop_scope() is called automatically.
template <typename Func> class ScopeGuard {
public:
    explicit ScopeGuard(Func&& fn) noexcept : fn_{std::move(fn)} {}

    ~ScopeGuard() noexcept {
        if (active_) {
            try {
                fn_();
            } catch (...) { // NOLINT(bugprone-empty-catch)
                // A cleanup function must never throw out of this noexcept
                // destructor; swallow to preserve the no-throw guarantee.
            }
        }
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    ScopeGuard(ScopeGuard&& other) noexcept : fn_{std::move(other.fn_)}, active_{other.active_} {
        other.active_ = false;
    }

    ScopeGuard& operator=(ScopeGuard&&) = delete;

private:
    Func fn_;
    bool active_{true};
};

// Deduction guide for convenient construction.
template <typename Func> ScopeGuard(Func) -> ScopeGuard<Func>;

} // namespace luma

#endif // LUMA_COMMON_SCOPE_GUARD_HPP
