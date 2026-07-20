#ifndef LUMA_STDLIB_PROCESS_MODULE_HPP
#define LUMA_STDLIB_PROCESS_MODULE_HPP

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

// Store the program arguments passed via the CLI.
void set_program_args(std::vector<std::string> args);

// Tokenise a command string into argv components for direct exec.
// Handles double-quoted and single-quoted substrings and backslash escapes.
[[nodiscard]] std::vector<std::string> tokenize_command(std::string_view cmd);

void register_process_ns(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_PROCESS_MODULE_HPP
