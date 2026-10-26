#ifndef LUMA_STDLIB_CONSOLE_MODULE_HPP
#define LUMA_STDLIB_CONSOLE_MODULE_HPP

#include <cstddef>
#include <iosfwd>
#include <string>

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_console_ns(const EnvPtr& env);

namespace console_detail {

// Outcome of read_bounded_line.
enum class LineStatus {
    Ok,         // A line was read (possibly the final, unterminated one).
    EndOfInput, // Nothing left to read, or the stream is in a failed state.
    TooLong,    // The line exceeded `limit`; its remainder was discarded.
};

// Reads one '\n'-terminated line from `in` into `line` (newline stripped, like
// std::getline) without ever buffering more than `limit` bytes — std::getline
// would allocate an arbitrarily long newline-free line in full before any size
// check could run.  Backs every line-oriented Console function; exposed for
// unit testing.
[[nodiscard]] LineStatus read_bounded_line(std::istream& in, std::size_t limit, std::string& line);

} // namespace console_detail

} // namespace luma

#endif // LUMA_STDLIB_CONSOLE_MODULE_HPP
