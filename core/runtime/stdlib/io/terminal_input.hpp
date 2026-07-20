#ifndef LUMA_STDLIB_TERMINAL_INPUT_HPP
#define LUMA_STDLIB_TERMINAL_INPUT_HPP

#include <atomic>
#include <cstdint>
#include <string>

#include "analysis/source/source_location.hpp"

namespace luma::terminal_detail {

/// Configurable escape sequence timeout in milliseconds.
/// Used by POSIX `read_byte_timeout()` to wait for subsequent bytes
/// of multi-byte escape sequences.  The default is 50 ms.
inline std::atomic<std::int64_t> escape_timeout_ms{50};

struct InputResult {
    std::string key;
    bool timed_out{false};
};

// Platform-dispatched key read (blocking when timeout_ms < 0, otherwise waiting
// up to timeout_ms).  Delegates to the Windows or POSIX reader so the Terminal
// module stays free of #ifdef at its call sites.
[[nodiscard]] InputResult read_input(std::int64_t timeout_ms, const SourceLocation& loc,
                                     bool mouse_mode_active);

#ifdef _WIN32

[[nodiscard]] InputResult read_input_windows(std::int64_t timeout_ms, const SourceLocation& loc,
                                             bool mouse_mode_active);

#else

[[nodiscard]] InputResult read_input_posix(std::int64_t timeout_ms, const SourceLocation& loc,
                                           bool mouse_mode_active);

[[nodiscard]] bool read_cpr_response(int& row, int& col);

#endif

} // namespace luma::terminal_detail

#endif // LUMA_STDLIB_TERMINAL_INPUT_HPP
