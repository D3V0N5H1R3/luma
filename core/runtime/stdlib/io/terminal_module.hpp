#ifndef LUMA_STDLIB_TERMINAL_MODULE_HPP
#define LUMA_STDLIB_TERMINAL_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_terminal_ns(const EnvPtr& env);

// Internal sub-registration (split for readability).
void register_terminal_ansi(const EnvPtr& env);

// Headless interaction-testing API (Terminal.test_*), split into
// terminal_testing.cpp.
void register_terminal_testing(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_TERMINAL_MODULE_HPP
