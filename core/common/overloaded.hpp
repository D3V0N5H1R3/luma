#ifndef LUMA_COMMON_OVERLOADED_HPP
#define LUMA_COMMON_OVERLOADED_HPP

namespace luma {

// Combines multiple callable objects into a single overload set.
// Uses C++17 CTAD (class template argument deduction) so callers can write:
//   overloaded{lambda1, lambda2, lambda3}
//
// Standard C++20 pattern for std::visit with multiple lambdas.
template <typename... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> overloaded(Ts...) -> overloaded<Ts...>;

} // namespace luma

#endif // LUMA_COMMON_OVERLOADED_HPP
