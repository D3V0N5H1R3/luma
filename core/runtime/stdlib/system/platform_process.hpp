#ifndef LUMA_STDLIB_PLATFORM_PROCESS_HPP
#define LUMA_STDLIB_PLATFORM_PROCESS_HPP

// Platform-agnostic process and environment primitives.
//
// Abstracts the Windows (CreateProcess / Win32 environment APIs) and POSIX
// (fork + execvp / environ) differences behind a uniform interface so the
// Process module stays free of #ifdef branches.  The bodies live in
// platform_process_win32.cpp and platform_process_posix.cpp, compiled
// conditionally by CMake (mirroring the terminal_input split).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luma::platform_process {

// Read-buffer size (in bytes) used when draining a child process's stdout/stderr
// pipe in execute_command.  Shared by the Win32 and POSIX implementations so the
// two back-ends stay in lock-step.
inline constexpr std::size_t k_pipe_buffer_size{256};

// Execute a command and capture its stdout.  Uses CreateProcess on Windows and
// fork + execvp on POSIX to avoid shell metacharacter injection.  Returns
// {exit_code, output}; a negative exit code signals a spawn/execution failure.
// May throw RuntimeError if the command string has mismatched quotes.
[[nodiscard]] std::pair<int, std::string> execute_command(std::string_view cmd);

// Result of execute_command_captured: the child's exit code plus its stdout and
// stderr captured into SEPARATE buffers (unlike execute_command, which merges
// both onto one stream).  A negative exit_code signals a spawn/execution
// failure (or an output-size-limit overflow, which also empties the buffers).
struct CapturedOutput {
    int exit_code{-1};
    std::string standard_output{};
    std::string standard_error{};
};

// Execute a command, capturing stdout and stderr separately.  Like
// execute_command, uses CreateProcess on Windows and fork + execvp on POSIX (no
// shell, so no metacharacter injection) and may throw RuntimeError on mismatched
// quotes.  Backs Process.execute / Process.CommandOutput.  Both streams are
// drained concurrently (POSIX poll, Win32 helper thread) so neither can deadlock
// by filling its pipe while the other is read.
[[nodiscard]] CapturedOutput execute_command_captured(std::string_view cmd);

// Execute an explicit argument vector (argv[0] is the program), capturing stdout
// and stderr separately.  Unlike execute_command_captured, the arguments are NOT
// tokenized or passed through a shell — they are handed verbatim to execvp
// (POSIX) / CreateProcess (Windows), so shell metacharacters in any argument are
// inert.  Backs Process.run_command / Process.Command.  Returns a negative
// exit_code on spawn failure or output-size overflow; an empty argv returns a
// negative exit_code too.
[[nodiscard]] CapturedOutput execute_argv_captured(std::vector<std::string> argv_strings);

// Return the current process identifier.
[[nodiscard]] std::int64_t current_process_id();

// Set an environment variable.  Returns 0 on success (matching setenv/_putenv_s).
[[nodiscard]] int set_environment_variable(const std::string& name, const std::string& value);

// Return every environment variable as {name, value} pairs, or std::nullopt if
// the platform failed to read the environment block (only possible on Windows).
[[nodiscard]] std::optional<std::vector<std::pair<std::string, std::string>>>
all_environment_variables();

#ifdef _WIN32
// Quote an argv vector into a single Windows command-line string following the
// MSVC C runtime's parsing rules (backslash and quote doubling) so a child
// launched via CreateProcess reconstructs exactly the same arguments, closing
// the metacharacter-injection vector.  Declared here (defined in
// platform_process_win32.cpp) purely so the quoting grammar can be unit tested
// directly, mirroring how tokenize_command exposes the inverse operation.
[[nodiscard]] std::string build_windows_cmdline(const std::vector<std::string>& argv);
#endif

} // namespace luma::platform_process

#endif // LUMA_STDLIB_PLATFORM_PROCESS_HPP
