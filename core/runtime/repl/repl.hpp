#ifndef LUMA_REPL_REPL_HPP
#define LUMA_REPL_REPL_HPP

#include "runtime/repl/repl_detail.hpp"

namespace luma {

void run_repl(bool sandbox = false);

// Evaluate a complete Luma program read from standard input and return a process
// exit code. Unlike running a file, no @main function is required, so bare
// top-level statements execute directly — this backs the editor Playground,
// which pipes a snippet to `luma --eval`.
[[nodiscard]] int run_eval(bool sandbox = false);

// Legacy alias — prefer repl_detail::compute_brace_depth_delta() directly.
[[nodiscard]] inline int compute_brace_depth_delta(std::string_view line) noexcept {
    return repl_detail::compute_brace_depth_delta(line);
}

} // namespace luma

#endif // LUMA_REPL_REPL_HPP
