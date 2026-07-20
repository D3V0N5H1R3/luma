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
