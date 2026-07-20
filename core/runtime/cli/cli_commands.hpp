#ifndef LUMA_CLI_CLI_COMMANDS_HPP
#define LUMA_CLI_CLI_COMMANDS_HPP

// Executors for the `pkg` subcommand. Kept separate from cli_args.*
// (which is solely responsible for parsing argv) and grouped with the other
// command handlers in cli_runner.cpp and cli_tester.cpp.

#include <string>

#include "runtime/cli/cli_args.hpp"

namespace luma {

// Handle the `pkg` subcommand. Returns the exit code.
[[nodiscard]] int run_pkg_command(const ParsedArgs& args);

} // namespace luma

#endif // LUMA_CLI_CLI_COMMANDS_HPP
