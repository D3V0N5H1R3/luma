#ifndef LUMA_STDLIB_TERMINAL_MODULE_HPP
#define LUMA_STDLIB_TERMINAL_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

// Registers the Terminal module.  With `sandbox` set (--box) the functions that
// read the interactive console or switch it into an input mode (enable_raw_mode,
// enable_mouse, read_key, read_key_timeout, get_input, read_line,
// get_cursor_position) are withheld; the output-only surface stays available.
void register_terminal_ns(const EnvPtr& env, bool sandbox = false);

// Internal sub-registration (split for readability).  `sandbox` withholds
// get_cursor_position, which reads the terminal's cursor report from stdin.
void register_terminal_ansi(const EnvPtr& env, bool sandbox);

// Headless interaction-testing API (Terminal.test_*), split into
// terminal_testing.cpp.
void register_terminal_testing(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_TERMINAL_MODULE_HPP
