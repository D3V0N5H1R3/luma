#ifndef LUMA_COMMON_OVERFLOW_HPP
#define LUMA_COMMON_OVERFLOW_HPP

#include <concepts>
#include <limits>

namespace luma {

// Compile-time overflow predicates for checked integer arithmetic.
// Each function returns true when the operation WOULD overflow.

template <std::signed_integral T>
[[nodiscard]] constexpr bool would_overflow_add(T a, T b) noexcept {
    return (b > 0 && a > std::numeric_limits<T>::max() - b) ||
           (b < 0 && a < std::numeric_limits<T>::min() - b);
}

template <std::signed_integral T>
[[nodiscard]] constexpr bool would_overflow_sub(T a, T b) noexcept {
    return (b < 0 && a > std::numeric_limits<T>::max() + b) ||
           (b > 0 && a < std::numeric_limits<T>::min() + b);
}

template <std::signed_integral T>
[[nodiscard]] constexpr bool would_overflow_mul(T a, T b) noexcept {
    if (a == 0 || b == 0) {
        return false;
    }

    const bool same_sign = (a > 0) == (b > 0);

    if (same_sign) {
        // Both positive or both negative — check against max.
        // For two negatives: a < max/b because a,b < 0 and a*b > 0.
        return a > 0 ? a > std::numeric_limits<T>::max() / b
                     : a < std::numeric_limits<T>::max() / b;
    }

    // Mixed signs — check against min.
    // Divide min by the positive operand to get the lower bound.
    return a > 0 ? b < std::numeric_limits<T>::min() / a : a < std::numeric_limits<T>::min() / b;
}

template <std::signed_integral T>
[[nodiscard]] constexpr bool would_overflow_div(T a, T b) noexcept {
    return a == std::numeric_limits<T>::min() && b == T{-1};
}

} // namespace luma

#endif // LUMA_COMMON_OVERFLOW_HPP
