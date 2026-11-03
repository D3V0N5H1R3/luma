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
// May throw RuntimeError if the command string has mismatched quotes, or — on
// Windows — if the program is a batch script (see is_windows_batch_script).
//
// Every launch path restricts what the child inherits to its own stdout /
// stderr pipe ends (POSIX: close-on-exec pipes; Windows:
// PROC_THREAD_ATTRIBUTE_HANDLE_LIST), so a child never holds a copy of another
// concurrent launch's pipe, an open socket, or a log file.
[[nodiscard]] std::pair<int, std::string> execute_command(std::string_view cmd);

// Result of execute_command_captured: the child's exit code plus its stdout and
// stderr captured into SEPARATE buffers (unlike execute_command, which merges
// both onto one stream).  A negative exit_code signals a spawn/execution
// failure (or an output-size-limit overflow, which also empties the buffers).
struct CapturedOutput {
    int exit_code{-1};
    std::string standard_output{};
    std::string standard_error{};

    // POSIX-style errno describing a *launch* failure (the program never ran),
    // or 0 when the process launched (regardless of its exit code).  On Windows
    // the platform layer maps GetLastError() onto the equivalent errno values
    // (ENOENT, EACCES, …).  Lets Process.run_command_typed classify a launch
    // failure (NotFound / PermissionDenied / …) rather than only reporting the
    // negative exit_code.  Only execute_argv_captured populates this today.
    int launch_errno{0};

    // True when the child was killed because it exceeded the caller-supplied
    // deadline (only execute_argv_captured_timeout sets this).  Distinguishes a
    // timeout — where exit_code/output are meaningless and left empty — from a
    // launch failure (launch_errno / negative exit_code), so
    // Process.run_command_timeout can report "timed out" specifically.
    bool timed_out{false};
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

// Like execute_argv_captured, but bounds the child's lifetime: if it has not
// exited within `timeout_ms` milliseconds it is forcibly killed together with
// its descendants (SIGKILL to its own process group on POSIX, a kill-on-close
// Job Object on Windows) and the returned CapturedOutput has
// `timed_out == true` (with empty output and a sentinel exit_code).  Backs
// Process.run_command_timeout.  On POSIX the child's own process group makes it
// a background job, so its stdin is /dev/null (it can never be stopped by
// SIGTTIN) and, on Linux, it is SIGKILLed if the launching thread dies.  A
// non-positive `timeout_ms` is the caller's responsibility to reject before
// calling.
[[nodiscard]] CapturedOutput execute_argv_captured_timeout(std::vector<std::string> argv_strings,
                                                           std::int64_t timeout_ms);

// Return the current process identifier.
[[nodiscard]] std::int64_t current_process_id();

// Portable process-termination request delivered by Process.signal.  The Windows
// mapping is necessarily lossy (Win32 has no POSIX signals): Terminate and Kill
// both call TerminateProcess, Interrupt sends a CTRL_C_EVENT to the target's
// console group where possible, and Hangup degrades to TerminateProcess.
enum class SignalKind {
    Terminate,
    Kill,
    Interrupt,
    Hangup
};

// Send `signal` to the process identified by `pid`.  Returns true when the OS
// accepted the request.  On POSIX this maps to kill(2) (SIGTERM / SIGKILL /
// SIGINT / SIGHUP); on Windows to the mapping documented on SignalKind above.
[[nodiscard]] bool send_signal(std::int64_t pid, SignalKind signal);

// Set an environment variable.  Returns 0 on success (matching setenv/_putenv_s).
[[nodiscard]] int set_environment_variable(const std::string& name, const std::string& value);

// Return every environment variable as {name, value} pairs, or std::nullopt if
// the platform failed to read the environment block (only possible on Windows).
[[nodiscard]] std::optional<std::vector<std::pair<std::string, std::string>>>
all_environment_variables();

// True when `program` (argv[0] of a launch request) names a Windows batch
// script — a `.bat` or `.cmd` file, compared case-insensitively.  CreateProcess
// silently runs such a file through `cmd.exe /c`, which re-parses the whole
// command line with cmd's own metacharacter rules (`&`, `|`, `<`, `>`, `^`, `%`)
// that the MSVC-CRT quoting in build_windows_cmdline cannot neutralise — the
// "BatBadBut" injection class.  The Windows launch paths therefore refuse batch
// scripts outright; a program that really wants cmd semantics must name
// `cmd.exe` explicitly.  Win32 path normalisation is mirrored so the check
// cannot be sidestepped: trailing path separators and `.` components
// (`x.bat\.`, `x.bat/.`, `x.cmd\.\.`), trailing dots / spaces (`x.bat. `), an
// NTFS alternate-data-stream suffix (`x.bat::$DATA`), the directory part, and a
// drive prefix (`C:x.bat`) are stripped repeatedly until nothing changes, and
// only then is the extension compared.  Pure string logic, so it is defined
// inline and unit tested on every platform; the Windows launch layer
// additionally checks the path CreateProcess would resolve (see
// windows_launch_refusal).
[[nodiscard]] inline bool is_windows_batch_script(std::string_view program) {
    const auto is_ascii_letter = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };

    const auto is_normalised_away = [](char c) {
        return c == '.' || c == ' ' || c == '/' || c == '\\';
    };

    const auto has_batch_extension = [](std::string_view name) {
        constexpr std::size_t k_extension_length{4};

        if (name.size() < k_extension_length) {
            return false;
        }

        std::string extension{name.substr(name.size() - k_extension_length)};

        for (char& c : extension) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }

        return extension == ".bat" || extension == ".cmd";
    };

    std::string_view name = program;

    if (has_batch_extension(name)) {
        return true;
    }

    // Each step can expose a suffix another step removes ("x.bat. ::$DATA\."),
    // so iterate to a fixed point.  Every intermediate form is checked too
    // (conservatively also refusing e.g. a `.bat`-named stream of another
    // file), so no ordering of the steps can hide the extension.
    for (std::size_t previous_size = name.size() + 1; name.size() != previous_size;) {
        previous_size = name.size();

        while (!name.empty() && is_normalised_away(name.back())) {
            name.remove_suffix(1);
        }

        if (has_batch_extension(name)) {
            return true;
        }

        if (const auto separator = name.find_last_of("/\\"); separator != std::string_view::npos) {
            name.remove_prefix(separator + 1);
        }

        if (name.size() >= 2 && name[1] == ':' && is_ascii_letter(name[0])) {
            name.remove_prefix(2);
        }

        if (const auto colon = name.find(':'); colon != std::string_view::npos) {
            name = name.substr(0, colon);
        }

        if (has_batch_extension(name)) {
            return true;
        }
    }

    return false;
}

// True when `program` ends in a character Win32 path normalisation silently
// drops — a dot, a space, or a path separator.  No real program needs such a
// name, and it is exactly the shape used to disguise a batch script from an
// extension check, so the Windows launch layer refuses it outright.
[[nodiscard]] inline bool has_normalised_away_suffix(std::string_view program) {
    return !program.empty() && (program.back() == '.' || program.back() == ' ' ||
                                program.back() == '/' || program.back() == '\\');
}

#ifdef _WIN32
// Why the Windows launch layer refuses to start `program` (argv[0]), or nullopt
// when it may be launched.  Refuses a batch script by name
// (is_windows_batch_script), a name ending in a character Win32 normalises away
// (has_normalised_away_suffix), and — because the textual check cannot see
// everything CreateProcess does — a program whose resolved path is a batch
// script: a name with a directory part is resolved with GetFullPathNameW, a bare
// name is searched for with SearchPathW (`.exe` appended to an extension-less
// name, as CreateProcess does; never PATHEXT), and an existing file's 8.3 short
// name is expanded with GetLongPathNameW before the extension is checked again.
[[nodiscard]] std::optional<std::string> windows_launch_refusal(std::string_view program);

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
