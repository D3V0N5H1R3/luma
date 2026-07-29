// Windows implementations of the platform_process primitives.
// Compiled only on Windows (see core/runtime/CMakeLists.txt).

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "common/platform_utils.hpp"
#include "common/resource_limits.hpp"
#include "runtime/stdlib/system/platform_process.hpp"
#include "runtime/stdlib/system/process_module.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace luma::platform_process {

// Build a safe Windows command-line string by individually quoting each
// argument according to the MSVC CRT parsing rules.  This prevents
// metacharacter injection when using CreateProcessA.  Declared in
// platform_process.hpp so the quoting grammar can be unit tested directly.
std::string build_windows_cmdline(const std::vector<std::string>& argv) {
    std::string cmdline{};

    bool first_arg{true};

    for (const auto& arg : argv) {
        if (!first_arg) {
            cmdline += ' ';
        }

        first_arg = false;

        if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) {
            cmdline += arg;

            continue;
        }

        cmdline += '"';

        for (auto it = arg.begin();; ++it) {
            std::size_t num_backslashes{0};

            while (it != arg.end() && *it == '\\') {
                ++num_backslashes;
                ++it;
            }

            if (it == arg.end()) {
                // Double trailing backslashes before the closing quote.
                cmdline.append(num_backslashes * 2, '\\');

                break;
            }

            if (*it == '"') {
                cmdline.append((num_backslashes * 2) + 1, '\\');

                cmdline += '"';
            } else {
                cmdline.append(num_backslashes, '\\');

                cmdline += *it;
            }
        }

        cmdline += '"';
    }

    return cmdline;
}

// Windows implementation: spawn via CreateProcessA with a pipe for stdout/stderr.
std::pair<int, std::string> execute_command(std::string_view cmd) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;

    if (CreatePipe(&read_pipe, &write_pipe, &sa, 0) == FALSE) {
        return {-1, ""};
    }

    // Prevent the read handle from being inherited by the child.
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;

    PROCESS_INFORMATION pi{};

    // Tokenize and re-quote to prevent metacharacter injection,
    // matching the POSIX path which uses tokenize + execvp.
    auto argv = tokenize_command(cmd);

    if (argv.empty()) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);

        return {-1, ""};
    }

    std::string safe_cmd{build_windows_cmdline(argv)};

    const BOOL ok = CreateProcessA(nullptr, safe_cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    // Close the write end — the child owns its copy.
    CloseHandle(write_pipe);

    if (ok == FALSE) {
        CloseHandle(read_pipe);

        return {-1, ""};
    }

    // Read child stdout.
    std::string output{};
    std::array<char, k_pipe_buffer_size> buffer{};
    DWORD bytes_read{0};

    while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                    nullptr) != FALSE &&
           bytes_read > 0) {
        if (output.size() + bytes_read > ResourceLimits::max_process_output_size) {
            TerminateProcess(pi.hProcess, 1);

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(read_pipe);

            return {-1, ""};
        }
        output.append(buffer.data(), bytes_read);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code{0};

    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(read_pipe);

    // Windows DWORD exit codes > INT_MAX are clamped to -1 to match POSIX WEXITSTATUS behaviour.
    const int safe_code = (exit_code > static_cast<DWORD>(std::numeric_limits<int>::max()))
                              ? -1
                              : static_cast<int>(exit_code);

    return {safe_code, std::move(output)};
}

namespace {

// Drain a pipe read handle into `out`, enforcing the process output-size limit.
// Returns false and sets `overflow` if the limit is exceeded.  Shared by the
// stdout (main-thread) and stderr (helper-thread) readers in
// execute_command_captured.
void drain_pipe(HANDLE read_handle, std::string& out, std::atomic<bool>& overflow,
                HANDLE process_handle) {
    std::array<char, k_pipe_buffer_size> buffer{};
    DWORD bytes_read{0};

    while (ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                    nullptr) != FALSE &&
           bytes_read > 0) {
        if (out.size() + bytes_read > ResourceLimits::max_process_output_size) {
            overflow.store(true);

            // Kill the child so the peer stream also unblocks and its reader
            // (the other thread) finishes instead of deadlocking on a full pipe.
            TerminateProcess(process_handle, 1);

            return;
        }

        out.append(buffer.data(), bytes_read);
    }
}

} // namespace

// Maps a Win32 CreateProcess failure code (from GetLastError) onto the
// POSIX-style errno that CapturedOutput::launch_errno carries, so
// Process.run_command_typed classifies a Windows launch failure with the same
// logic as POSIX.  Unmapped codes collapse to a generic launch failure (errno 0
// would be misread as "launched", so a non-zero sentinel is used).
namespace {

[[nodiscard]] int launch_errno_from_win32(DWORD code) {
    switch (code) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
            return ENOENT;
        case ERROR_ACCESS_DENIED:
            return EACCES;
        case ERROR_BAD_EXE_FORMAT:
        case ERROR_BAD_FORMAT:
            return ENOEXEC;
        default:
            // Generic, unclassified launch failure.  EINVAL is the agreed generic
            // sentinel that process_error_variant() (process_module.cpp) routes to
            // Process.Error.LaunchFailed — it must NOT be a code that maps to a more
            // specific variant there.
            return EINVAL;
    }
}

} // namespace

// Windows implementation: spawn via CreateProcessA with two pipes so stdout and
// stderr are captured separately.  stderr is drained on a helper thread while
// stdout is drained on the calling thread, so neither pipe filling can deadlock
// the other.
CapturedOutput execute_command_captured(std::string_view cmd) {
    // Tokenize before acquiring any pipe handles so an unclosed-quote error
    // (which tokenize_command throws) surfaces cleanly without leaking handles,
    // then hand the argv vector to the shared implementation.
    return execute_argv_captured(tokenize_command(cmd));
}

// Windows implementation shared by execute_command_captured (which tokenizes a
// command string first) and Process.run_command (which supplies an explicit,
// un-tokenized argv).  Each argument is individually quoted by
// build_windows_cmdline, so shell metacharacters never reach a shell.
CapturedOutput execute_argv_captured(std::vector<std::string> argv) {
    if (argv.empty()) {
        return {};
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    HANDLE err_read = nullptr;
    HANDLE err_write = nullptr;

    if (CreatePipe(&out_read, &out_write, &sa, 0) == FALSE) {
        return {};
    }

    if (CreatePipe(&err_read, &err_write, &sa, 0) == FALSE) {
        CloseHandle(out_read);
        CloseHandle(out_write);

        return {};
    }

    // The child must not inherit either read handle.
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_write;
    si.hStdError = err_write;

    PROCESS_INFORMATION pi{};

    std::string safe_cmd{build_windows_cmdline(argv)};

    const BOOL ok = CreateProcessA(nullptr, safe_cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    // Capture the failure code immediately, before the CloseHandle calls below
    // can overwrite the thread's last error.
    const DWORD create_error = (ok == FALSE) ? GetLastError() : 0;

    // The child owns its copies of the write ends.
    CloseHandle(out_write);
    CloseHandle(err_write);

    if (ok == FALSE) {
        CloseHandle(out_read);
        CloseHandle(err_read);

        CapturedOutput launch_failure{};
        launch_failure.launch_errno = launch_errno_from_win32(create_error);

        return launch_failure;
    }

    std::string out_output{};
    std::string err_output{};
    std::atomic<bool> out_overflow{false};
    std::atomic<bool> err_overflow{false};

    // Drain stderr on a helper thread so a full stdout pipe and a full stderr
    // pipe cannot deadlock each other.
    std::thread err_thread([&]() { drain_pipe(err_read, err_output, err_overflow, pi.hProcess); });

    drain_pipe(out_read, out_output, out_overflow, pi.hProcess);

    err_thread.join();

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code{0};

    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(out_read);
    CloseHandle(err_read);

    if (out_overflow.load() || err_overflow.load()) {
        return {};
    }

    CapturedOutput captured{};
    // Windows DWORD exit codes > INT_MAX are clamped to -1 to match POSIX WEXITSTATUS behaviour.
    captured.exit_code = (exit_code > static_cast<DWORD>(std::numeric_limits<int>::max()))
                             ? -1
                             : static_cast<int>(exit_code);
    captured.standard_output = std::move(out_output);
    captured.standard_error = std::move(err_output);

    return captured;
}

// Windows implementation of the timeout-bounded variant.  Both streams are
// drained on helper threads so the calling thread is free to wait on the child
// with a finite timeout.  To make the timeout robust, the child is placed in a
// Job Object (kill-on-close) before it runs: on expiry — or once the top-level
// child has finished — the WHOLE job is terminated, which closes every inherited
// pipe write handle (including any held by surviving grandchildren) so the drain
// threads' ReadFile calls return and the joins are guaranteed to complete.  A
// bare TerminateProcess would kill only the direct child, so a grandchild that
// inherited the pipe could keep it open and hang the join forever — defeating
// the timeout.  Consequence: a timeout-bounded run reaps its entire process tree
// (matching the "bounded execution" contract), unlike the blocking run_command.
CapturedOutput execute_argv_captured_timeout(std::vector<std::string> argv,
                                             std::int64_t timeout_ms) {
    if (argv.empty()) {
        return {};
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    HANDLE err_read = nullptr;
    HANDLE err_write = nullptr;

    if (CreatePipe(&out_read, &out_write, &sa, 0) == FALSE) {
        return {};
    }

    if (CreatePipe(&err_read, &err_write, &sa, 0) == FALSE) {
        CloseHandle(out_read);
        CloseHandle(out_write);

        return {};
    }

    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

    // Job Object that kills every process it contains when its last handle is
    // closed, so a crash on our side also cleans up the child tree.  Best-effort:
    // if it cannot be created we fall back to terminating just the direct child.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);

    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
        job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &job_limits,
                                sizeof(job_limits));
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_write;
    si.hStdError = err_write;

    PROCESS_INFORMATION pi{};

    std::string safe_cmd{build_windows_cmdline(argv)};

    // Create suspended so the child is assigned to the job BEFORE it can run and
    // spawn grandchildren — otherwise a grandchild could escape the job and hold
    // the pipe open past the timeout.
    const BOOL ok = CreateProcessA(nullptr, safe_cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);

    const DWORD create_error = (ok == FALSE) ? GetLastError() : 0;

    CloseHandle(out_write);
    CloseHandle(err_write);

    if (ok == FALSE) {
        CloseHandle(out_read);
        CloseHandle(err_read);

        if (job != nullptr) {
            CloseHandle(job);
        }

        CapturedOutput launch_failure{};
        launch_failure.launch_errno = launch_errno_from_win32(create_error);

        return launch_failure;
    }

    // Assign to the job (best-effort: nested jobs are supported on Windows 8+),
    // then release the suspended primary thread so the child begins executing.
    const bool have_job = (job != nullptr) && (AssignProcessToJobObject(job, pi.hProcess) != 0);

    ResumeThread(pi.hThread);

    std::string out_output{};
    std::string err_output{};
    std::atomic<bool> out_overflow{false};
    std::atomic<bool> err_overflow{false};

    // Drain BOTH streams on helper threads so the calling thread can block on the
    // process handle with a timeout instead of on a ReadFile.
    std::thread out_thread([&]() { drain_pipe(out_read, out_output, out_overflow, pi.hProcess); });
    std::thread err_thread([&]() { drain_pipe(err_read, err_output, err_overflow, pi.hProcess); });

    // Clamp the timeout to a valid finite DWORD: INFINITE (0xFFFFFFFF) would mean
    // "wait forever", defeating the deadline, so cap just below it.
    const DWORD wait_ms = (timeout_ms >= static_cast<std::int64_t>(INFINITE))
                              ? (INFINITE - 1)
                              : static_cast<DWORD>(timeout_ms);
    const DWORD wait_result = WaitForSingleObject(pi.hProcess, wait_ms);

    const bool timed_out = (wait_result == WAIT_TIMEOUT);

    // Tear down the child tree so every inherited pipe write handle closes and the
    // drain threads below are guaranteed to reach EOF and return.  Prefer the job
    // (kills grandchildren too); fall back to the direct child only when the job
    // is unavailable, and then only on timeout to preserve the non-timeout path's
    // behaviour of letting the child exit on its own.
    if (have_job) {
        TerminateJobObject(job, 1);
    } else if (timed_out) {
        TerminateProcess(pi.hProcess, 1);
    }

    out_thread.join();
    err_thread.join();

    // The process has now exited (naturally or via termination); reap its exit
    // code and release every handle.
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code{0};

    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(out_read);
    CloseHandle(err_read);

    if (job != nullptr) {
        CloseHandle(job);
    }

    if (timed_out) {
        CapturedOutput timeout_result{};
        timeout_result.timed_out = true;

        return timeout_result;
    }

    if (out_overflow.load() || err_overflow.load()) {
        return {};
    }

    CapturedOutput captured{};
    // Windows DWORD exit codes > INT_MAX are clamped to -1 to match POSIX WEXITSTATUS behaviour.
    captured.exit_code = (exit_code > static_cast<DWORD>(std::numeric_limits<int>::max()))
                             ? -1
                             : static_cast<int>(exit_code);
    captured.standard_output = std::move(out_output);
    captured.standard_error = std::move(err_output);

    return captured;
}

std::int64_t current_process_id() {
    return static_cast<std::int64_t>(GetCurrentProcessId());
}

bool send_signal(std::int64_t pid, SignalKind signal) {
    if (pid < 0 || pid > static_cast<std::int64_t>(std::numeric_limits<DWORD>::max())) {
        return false;
    }

    const auto process_id = static_cast<DWORD>(pid);

    // Windows has no POSIX signals.  Interrupt maps to a console CTRL_C_EVENT for
    // the target's process group (best-effort: it only reaches a process that
    // shares this console); everything else — Terminate, Kill, and a degraded
    // Hangup — forcibly ends the process with TerminateProcess.
    if (signal == SignalKind::Interrupt) {
        return GenerateConsoleCtrlEvent(CTRL_C_EVENT, process_id) != 0;
    }

    const HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
    if (handle == nullptr) {
        return false;
    }

    const BOOL ok = TerminateProcess(handle, 1);
    CloseHandle(handle);

    return ok != 0;
}

int set_environment_variable(const std::string& name, const std::string& value) {
    return _putenv_s(name.c_str(), value.c_str());
}

std::optional<std::vector<std::pair<std::string, std::string>>> all_environment_variables() {
    std::vector<std::pair<std::string, std::string>> result;

    auto* const env_block = GetEnvironmentStringsW();

    if (!env_block) {
        return std::nullopt;
    }

    // Each entry is "KEY=VALUE\0", double-null terminates.
    for (const wchar_t* p = env_block; *p != L'\0';) {
        const std::wstring entry{p};
        p += entry.size() + 1;

        const auto eq = entry.find(L'=');

        if (eq == 0 || eq == std::wstring::npos) {
            continue; // Skip entries like "=C:=C:\\"
        }

        result.emplace_back(wstring_to_utf8(entry.substr(0, eq)),
                            wstring_to_utf8(entry.substr(eq + 1)));
    }

    FreeEnvironmentStringsW(env_block);

    return result;
}

} // namespace luma::platform_process
