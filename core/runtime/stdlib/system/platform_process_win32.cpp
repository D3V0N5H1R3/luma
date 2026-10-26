// Windows implementations of the platform_process primitives.
// Compiled only on Windows (see core/runtime/CMakeLists.txt).

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
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
// metacharacter injection when using CreateProcessW.  Declared in
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

namespace {

// Convert UTF-8 text (every Luma string) to UTF-16 for the wide Win32 APIs.
// Returns nullopt for ill-formed UTF-8.  The narrow "A" APIs must not be used:
// they decode the bytes in the ANSI code page, mangling non-ASCII program
// names, arguments and environment values.
[[nodiscard]] std::optional<std::wstring> utf8_to_wide(std::string_view utf8) {
    if (utf8.empty()) {
        return std::wstring{};
    }

    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    const int source_len = static_cast<int>(utf8.size());
    const int len =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), source_len, nullptr, 0);

    if (len <= 0) {
        return std::nullopt;
    }

    std::wstring wide(static_cast<std::size_t>(len), L'\0');

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), source_len, wide.data(),
                            len) != len) {
        return std::nullopt;
    }

    return wide;
}

// RAII owner for a Win32 HANDLE.  Every pipe end, process, thread and job handle
// in this file is held by one, so no early return or exception can leak it.
class UniqueHandle {
public:
    UniqueHandle() = default;

    explicit UniqueHandle(HANDLE handle) noexcept : handle_{handle} {}

    ~UniqueHandle() {
        reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_{std::exchange(other.handle_, nullptr)} {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }

        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE handle = nullptr) noexcept {
        if (valid()) {
            CloseHandle(handle_);
        }

        handle_ = handle;
    }

private:
    HANDLE handle_{nullptr};
};

// An anonymous pipe whose write end is handed to the child (inheritable) and
// whose read end stays private to the parent (non-inheritable).
struct ChildPipe {
    UniqueHandle read;
    UniqueHandle write;
};

[[nodiscard]] bool create_child_pipe(ChildPipe& pipe) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;

    if (CreatePipe(&read_end, &write_end, &sa, 0) == FALSE) {
        return false;
    }

    pipe.read.reset(read_end);
    pipe.write.reset(write_end);

    // The child must never inherit the parent's read end.
    return SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0) != FALSE;
}

// A PROC_THREAD_ATTRIBUTE_LIST carrying PROC_THREAD_ATTRIBUTE_HANDLE_LIST, which
// restricts the handles a child inherits to exactly `handles` even though
// CreateProcess is called with bInheritHandles=TRUE.  Without it the child would
// inherit EVERY inheritable handle in the interpreter — including the pipe write
// ends of other Process.* calls running concurrently on other tasks, whose
// readers would then never see EOF while this unrelated child lives.
class InheritedHandleList {
public:
    explicit InheritedHandleList(std::vector<HANDLE> handles) : handles_{std::move(handles)} {
        SIZE_T size{0};

        // The sizing call is documented to fail with ERROR_INSUFFICIENT_BUFFER.
        (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &size);

        if (size == 0) {
            return;
        }

        storage_.resize(size);

        if (InitializeProcThreadAttributeList(list(), 1, 0, &size) == FALSE) {
            return;
        }

        initialized_ = true;

        valid_ =
            UpdateProcThreadAttribute(list(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles_.data(),
                                      handles_.size() * sizeof(HANDLE), nullptr, nullptr) != FALSE;
    }

    ~InheritedHandleList() {
        if (initialized_) {
            DeleteProcThreadAttributeList(list());
        }
    }

    InheritedHandleList(const InheritedHandleList&) = delete;
    InheritedHandleList& operator=(const InheritedHandleList&) = delete;
    InheritedHandleList(InheritedHandleList&&) = delete;
    InheritedHandleList& operator=(InheritedHandleList&&) = delete;

    // The attribute list, or nullptr if it could not be built.
    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() noexcept {
        return valid_ ? list() : nullptr;
    }

private:
    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST list() noexcept {
        return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
    }

    // UpdateProcThreadAttribute stores a pointer to this array, so it must
    // outlive the CreateProcess call — hence a member, not a parameter.
    std::vector<HANDLE> handles_;
    std::vector<std::byte> storage_{};
    bool initialized_{false};
    bool valid_{false};
};

// The process and primary-thread handles of a launched child.
struct ChildProcess {
    UniqueHandle process;
    UniqueHandle thread;
};

// Call a Win32 "fill this buffer" API that returns the required size (including
// the terminator) when the buffer is too small and the written length
// (excluding it) on success.  Returns nullopt when the API fails.
template <typename Fill> [[nodiscard]] std::optional<std::wstring> fill_growing_buffer(Fill fill) {
    std::wstring buffer(MAX_PATH, L'\0');

    // Two attempts suffice: the first failure reports the exact size needed.
    // The path can change between calls, so give up rather than loop.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const DWORD capacity = static_cast<DWORD>(buffer.size());
        const DWORD length = fill(buffer.data(), capacity);

        if (length == 0) {
            return std::nullopt;
        }

        if (length < capacity) {
            buffer.resize(length);

            return buffer;
        }

        buffer.resize(length);
    }

    return std::nullopt;
}

// Resolve `program` to the file CreateProcess would start: a name with a
// directory or drive part is made absolute by GetFullPathNameW (which applies
// Win32 normalisation — `.` components, trailing dots and spaces), a bare name
// is searched for with SearchPathW, appending `.exe` only when it has no
// extension, as CreateProcess does (PATHEXT is never consulted).  An existing
// file's 8.3 short name is then expanded to its long name.  Returns nullopt
// when nothing resolves; the textual check still applies in that case.
[[nodiscard]] std::optional<std::wstring> resolve_launch_path(const std::wstring& program) {
    const bool has_directory = program.find_first_of(L"/\\:") != std::wstring::npos;

    auto resolved = fill_growing_buffer([&program, has_directory](wchar_t* out, DWORD capacity) {
        return has_directory
                   ? GetFullPathNameW(program.c_str(), capacity, out, nullptr)
                   : SearchPathW(nullptr, program.c_str(), L".exe", capacity, out, nullptr);
    });

    if (!resolved) {
        return std::nullopt;
    }

    if (auto long_path = fill_growing_buffer([&resolved](wchar_t* out, DWORD capacity) {
            return GetLongPathNameW(resolved->c_str(), out, capacity);
        })) {
        return long_path;
    }

    return resolved;
}

[[nodiscard]] std::string batch_script_refusal(std::string_view program) {
    return std::format("Process: refusing to launch the batch script '{}': Windows runs "
                       ".bat/.cmd files through cmd.exe, which would re-interpret the "
                       "arguments as shell syntax",
                       program);
}

} // namespace

std::optional<std::string> windows_launch_refusal(std::string_view program) {
    if (is_windows_batch_script(program)) {
        return batch_script_refusal(program);
    }

    if (has_normalised_away_suffix(program)) {
        return std::format("Process: refusing to launch '{}': Windows drops a trailing dot, "
                           "space, or path separator from a program name, which can disguise "
                           "a batch script",
                           program);
    }

    // A NUL would end the command line early, so CreateProcess would see a
    // different program name than the one checked above.
    if (program.find('\0') != std::string_view::npos) {
        return std::string{"Process: refusing to launch a program name containing a NUL character"};
    }

    const auto wide_program = utf8_to_wide(program);

    if (wide_program && !wide_program->empty()) {
        if (const auto resolved = resolve_launch_path(*wide_program)) {
            const std::string resolved_utf8 = wstring_to_utf8(*resolved);

            if (is_windows_batch_script(resolved_utf8)) {
                return batch_script_refusal(program);
            }
        }
    }

    return std::nullopt;
}

namespace {

// Refuse to launch a batch script.  CreateProcess would silently run it through
// `cmd.exe /c`, whose own metacharacter parsing re-interprets the arguments that
// build_windows_cmdline quoted for the MSVC CRT ("BatBadBut").  Thrown before
// any pipe or handle is acquired; the Process module reports it as a failure
// result.  An argument containing NUL is refused too: the command line is a
// C string, so the NUL would silently drop every later argument.
void ensure_not_batch_script(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        return;
    }

    if (auto refusal = windows_launch_refusal(argv.front())) {
        throw RuntimeError{*std::move(refusal),
                           {},
                           "invoke cmd.exe explicitly (e.g. Process.command(\"cmd.exe\", [\"/c\", "
                           "\"script.bat\", ...])) and quote the arguments for cmd yourself"};
    }

    if (std::ranges::any_of(
            argv, [](const std::string& arg) { return arg.find('\0') != std::string::npos; })) {
        throw RuntimeError{"Process: refusing to launch with an argument containing a NUL "
                           "character",
                           {},
                           "remove the NUL character from the argument"};
    }
}

// Launch `argv` with stdout / stderr redirected to the given inheritable write
// handles, which are the ONLY handles the child inherits.  Returns the child on
// success, or nullopt with the Win32 error code in `error_out`.
[[nodiscard]] std::optional<ChildProcess> launch_child(const std::vector<std::string>& argv,
                                                       HANDLE out_write, HANDLE err_write,
                                                       DWORD extra_flags, DWORD& error_out) {
    std::vector<HANDLE> inherited{out_write};

    if (err_write != out_write) {
        inherited.push_back(err_write);
    }

    InheritedHandleList attributes{std::move(inherited)};

    if (attributes.get() == nullptr) {
        error_out = ERROR_INVALID_PARAMETER;

        return std::nullopt;
    }

    // The quoted command line is UTF-8; launch through the wide API so
    // non-ASCII program names and arguments reach the child intact.
    auto cmdline = utf8_to_wide(build_windows_cmdline(argv));

    if (!cmdline) {
        error_out = ERROR_NO_UNICODE_TRANSLATION;

        return std::nullopt;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = out_write;
    si.StartupInfo.hStdError = err_write;
    si.lpAttributeList = attributes.get();

    PROCESS_INFORMATION pi{};

    const BOOL ok = CreateProcessW(nullptr, cmdline->data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | extra_flags,
                                   nullptr, nullptr, &si.StartupInfo, &pi);

    if (ok == FALSE) {
        error_out = GetLastError();

        return std::nullopt;
    }

    return ChildProcess{.process = UniqueHandle{pi.hProcess}, .thread = UniqueHandle{pi.hThread}};
}

// Windows DWORD exit codes > INT_MAX are clamped to -1 to match POSIX
// WEXITSTATUS behaviour.
[[nodiscard]] int clamp_exit_code(DWORD exit_code) {
    return (exit_code > static_cast<DWORD>(std::numeric_limits<int>::max()))
               ? -1
               : static_cast<int>(exit_code);
}

// Wait for the child to exit and return its clamped exit code.
[[nodiscard]] int wait_for_exit_code(HANDLE process) {
    WaitForSingleObject(process, INFINITE);

    DWORD exit_code{0};

    GetExitCodeProcess(process, &exit_code);

    return clamp_exit_code(exit_code);
}

// Drain a pipe read handle into `out`, enforcing the process output-size limit.
// Sets `overflow` and kills the child if the limit is exceeded, so the peer
// stream also unblocks and its reader finishes instead of deadlocking on a full
// pipe.  Shared by every capture path in this file.
void drain_pipe(HANDLE read_handle, std::string& out, std::atomic<bool>& overflow,
                HANDLE process_handle) {
    std::array<char, k_pipe_buffer_size> buffer{};
    DWORD bytes_read{0};

    while (ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                    nullptr) != FALSE &&
           bytes_read > 0) {
        if (out.size() + bytes_read > ResourceLimits::max_process_output_size) {
            overflow.store(true);

            TerminateProcess(process_handle, 1);

            return;
        }

        out.append(buffer.data(), bytes_read);
    }
}

// Maps a Win32 CreateProcess failure code (from GetLastError) onto the
// POSIX-style errno that CapturedOutput::launch_errno carries, so
// Process.run_command_typed classifies a Windows launch failure with the same
// logic as POSIX.  Unmapped codes collapse to a generic launch failure (errno 0
// would be misread as "launched", so a non-zero sentinel is used).
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

[[nodiscard]] CapturedOutput launch_failure(DWORD create_error) {
    CapturedOutput failure{};
    failure.launch_errno = launch_errno_from_win32(create_error);

    return failure;
}

} // namespace

// Windows implementation: spawn via CreateProcessW with one pipe shared by
// stdout and stderr.
std::pair<int, std::string> execute_command(std::string_view cmd) {
    // Tokenize (which throws on an unclosed quote) and vet the program before
    // acquiring any handle, matching the POSIX path which uses tokenize + execvp.
    const auto argv = tokenize_command(cmd);

    if (argv.empty()) {
        return {-1, ""};
    }

    ensure_not_batch_script(argv);

    ChildPipe pipe{};

    if (!create_child_pipe(pipe)) {
        return {-1, ""};
    }

    DWORD create_error{0};
    auto child = launch_child(argv, pipe.write.get(), pipe.write.get(), 0, create_error);

    // Close the write end — the child owns its copy, and our reads only see EOF
    // once every write end is closed.
    pipe.write.reset();

    if (!child) {
        return {-1, ""};
    }

    std::string output{};
    std::array<char, k_pipe_buffer_size> buffer{};
    DWORD bytes_read{0};

    while (ReadFile(pipe.read.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                    nullptr) != FALSE &&
           bytes_read > 0) {
        if (output.size() + bytes_read > ResourceLimits::max_process_output_size) {
            TerminateProcess(child->process.get(), 1);

            return {-1, ""};
        }

        output.append(buffer.data(), bytes_read);
    }

    return {wait_for_exit_code(child->process.get()), std::move(output)};
}

// Windows implementation: spawn via CreateProcessW with two pipes so stdout and
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

    ensure_not_batch_script(argv);

    ChildPipe out_pipe{};
    ChildPipe err_pipe{};

    if (!create_child_pipe(out_pipe) || !create_child_pipe(err_pipe)) {
        return {};
    }

    DWORD create_error{0};
    auto child = launch_child(argv, out_pipe.write.get(), err_pipe.write.get(), 0, create_error);

    // The child owns its copies of the write ends.
    out_pipe.write.reset();
    err_pipe.write.reset();

    if (!child) {
        return launch_failure(create_error);
    }

    std::string out_output{};
    std::string err_output{};
    std::atomic<bool> out_overflow{false};
    std::atomic<bool> err_overflow{false};
    const HANDLE process = child->process.get();

    // Drain stderr on a helper thread so a full stdout pipe and a full stderr
    // pipe cannot deadlock each other.
    std::thread err_thread(
        [&]() { drain_pipe(err_pipe.read.get(), err_output, err_overflow, process); });

    drain_pipe(out_pipe.read.get(), out_output, out_overflow, process);

    err_thread.join();

    const int exit_code = wait_for_exit_code(process);

    if (out_overflow.load() || err_overflow.load()) {
        return {};
    }

    CapturedOutput captured{};
    captured.exit_code = exit_code;
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

    ensure_not_batch_script(argv);

    ChildPipe out_pipe{};
    ChildPipe err_pipe{};

    if (!create_child_pipe(out_pipe) || !create_child_pipe(err_pipe)) {
        return {};
    }

    // Job Object that kills every process it contains when its last handle is
    // closed, so a crash on our side also cleans up the child tree.  Best-effort:
    // if it cannot be created we fall back to terminating just the direct child.
    UniqueHandle job{CreateJobObjectW(nullptr, nullptr)};

    if (job.valid()) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
        job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &job_limits,
                                sizeof(job_limits));
    }

    // Create suspended so the child is assigned to the job BEFORE it can run and
    // spawn grandchildren — otherwise a grandchild could escape the job and hold
    // the pipe open past the timeout.
    DWORD create_error{0};
    auto child = launch_child(argv, out_pipe.write.get(), err_pipe.write.get(), CREATE_SUSPENDED,
                              create_error);

    out_pipe.write.reset();
    err_pipe.write.reset();

    if (!child) {
        return launch_failure(create_error);
    }

    const HANDLE process = child->process.get();

    // Assign to the job (best-effort: nested jobs are supported on Windows 8+),
    // then release the suspended primary thread so the child begins executing.
    const bool have_job = job.valid() && (AssignProcessToJobObject(job.get(), process) != 0);

    ResumeThread(child->thread.get());

    std::string out_output{};
    std::string err_output{};
    std::atomic<bool> out_overflow{false};
    std::atomic<bool> err_overflow{false};

    // Drain BOTH streams on helper threads so the calling thread can block on the
    // process handle with a timeout instead of on a ReadFile.
    std::thread out_thread(
        [&]() { drain_pipe(out_pipe.read.get(), out_output, out_overflow, process); });
    std::thread err_thread(
        [&]() { drain_pipe(err_pipe.read.get(), err_output, err_overflow, process); });

    // Clamp the timeout to a valid finite DWORD: INFINITE (0xFFFFFFFF) would mean
    // "wait forever", defeating the deadline, so cap just below it.
    const DWORD wait_ms = (timeout_ms >= static_cast<std::int64_t>(INFINITE))
                              ? (INFINITE - 1)
                              : static_cast<DWORD>(timeout_ms);
    const DWORD wait_result = WaitForSingleObject(process, wait_ms);

    const bool timed_out = (wait_result == WAIT_TIMEOUT);

    // Tear down the child tree so every inherited pipe write handle closes and the
    // drain threads below are guaranteed to reach EOF and return.  Prefer the job
    // (kills grandchildren too); fall back to the direct child only when the job
    // is unavailable, and then only on timeout to preserve the non-timeout path's
    // behaviour of letting the child exit on its own.
    if (have_job) {
        TerminateJobObject(job.get(), 1);
    } else if (timed_out) {
        TerminateProcess(process, 1);
    }

    out_thread.join();
    err_thread.join();

    // The process has now exited (naturally or via termination); reap its exit
    // code.  Every handle is released by its UniqueHandle owner.
    const int exit_code = wait_for_exit_code(process);

    if (timed_out) {
        CapturedOutput timeout_result{};
        timeout_result.timed_out = true;

        return timeout_result;
    }

    if (out_overflow.load() || err_overflow.load()) {
        return {};
    }

    CapturedOutput captured{};
    captured.exit_code = exit_code;
    captured.standard_output = std::move(out_output);
    captured.standard_error = std::move(err_output);

    return captured;
}

std::int64_t current_process_id() {
    return static_cast<std::int64_t>(GetCurrentProcessId());
}

bool send_signal(std::int64_t pid, SignalKind signal) {
    if (pid <= 0 || pid > static_cast<std::int64_t>(std::numeric_limits<DWORD>::max())) {
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

    const UniqueHandle handle{OpenProcess(PROCESS_TERMINATE, FALSE, process_id)};

    if (!handle.valid()) {
        return false;
    }

    return TerminateProcess(handle.get(), 1) != 0;
}

int set_environment_variable(const std::string& name, const std::string& value) {
    // Wide API, mirroring safe_getenv (which reads the wide environment and
    // returns UTF-8): _putenv_s would store the UTF-8 bytes as ANSI text.
    const auto wide_name = utf8_to_wide(name);
    const auto wide_value = utf8_to_wide(value);

    if (!wide_name || !wide_value) {
        return EINVAL;
    }

    return _wputenv_s(wide_name->c_str(), wide_value->c_str());
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
