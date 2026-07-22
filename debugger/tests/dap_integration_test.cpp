// DAP integration tests — spawns luma_dap as a subprocess and sends
// scripted DAP messages over stdin/stdout to verify end-to-end behavior.

#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "json/json.hpp"
#include "protocol/message_frame.hpp"
#include "test_framework.hpp"

using luma::json::JsonValue;

// ─── DAP-specific assertion ───────────────────────────────────────

#define ASSERT_SUCCESS(resp)                                                                       \
    do {                                                                                           \
        if (!(resp)["success"].as_bool()) {                                                        \
            auto msg = (resp).has("message") ? (resp)["message"].as_string() : "(no message)";     \
            throw std::runtime_error{                                                              \
                std::format("{}:{}: response not successful: {}", __FILE__, __LINE__, msg)};       \
        }                                                                                          \
    } while (false)

namespace {

// ─── Magic line numbers in debug example programs ──────────────────
// Update these if the example files change.
namespace test_lines {
constexpr int variables_basic_stop = 10;
constexpr int variables_basic_set_variable = 13;
constexpr int breakpoint_basic_no_debug = 7;
constexpr int breakpoint_basic_conditional = 8;
constexpr int conditional_loop_body = 8;
constexpr int conditional_loop_range_start = 4;
constexpr int conditional_loop_range_end = 10;
constexpr int step_into_function_call = 9;
constexpr int step_into_function_body = 4;
constexpr int closure_lambda_body = 7;
constexpr int recursive_base_case = 6;
constexpr int structured_values_stop = 19;
constexpr int global_scope_stop = 11;
constexpr int infinite_eval_stop = 17;
constexpr int set_variable_typed_stop = 11;
} // namespace test_lines

// ─── DAP subprocess wrapper ────────────────────────────────────────

// Path to the luma_dap executable (set in main() based on argv[0]).
std::string dap_exe_path;

// Path to the examples/debug/ directory.
std::string debug_examples_dir;

// Extract the Content-Length value from an accumulated header block (terminated
// by a blank line).  Delegates to the shared protocol framing helpers so the
// per-platform readers below don't re-implement Content-Length parsing.
[[nodiscard]] std::size_t parse_dap_content_length(std::string_view headers) {
    std::size_t pos = 0;

    while (pos < headers.size()) {
        const auto eol = headers.find("\r\n", pos);
        const auto line = headers.substr(pos, eol == std::string_view::npos ? eol : eol - pos);

        if (const auto length = luma::protocol::try_parse_content_length(line)) {
            return *length;
        }

        if (eol == std::string_view::npos) {
            break;
        }

        pos = eol + 2;
    }

    throw std::runtime_error("Missing Content-Length in DAP response");
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class DapProcess {
public:
    explicit DapProcess() {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        // Create pipes for stdin/stdout communication.
        if (!CreatePipe(&child_stdout_read_, &child_stdout_write_, &sa, 0)) {
            throw std::runtime_error("Failed to create stdout pipe");
        }

        SetHandleInformation(child_stdout_read_, HANDLE_FLAG_INHERIT, 0);

        if (!CreatePipe(&child_stdin_read_, &child_stdin_write_, &sa, 0)) {
            throw std::runtime_error("Failed to create stdin pipe");
        }

        SetHandleInformation(child_stdin_write_, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.hStdInput = child_stdin_read_;
        si.hStdOutput = child_stdout_write_;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi{};

        if (!CreateProcessA(nullptr, const_cast<char*>(dap_exe_path.c_str()), nullptr, nullptr,
                            TRUE, 0, nullptr, nullptr, &si, &pi)) {
            throw std::runtime_error(
                std::format("Failed to start luma_dap: error {}", GetLastError()));
        }

        process_ = pi.hProcess;
        CloseHandle(pi.hThread);

        // Close the handles inherited by the child.
        CloseHandle(child_stdout_write_);
        child_stdout_write_ = INVALID_HANDLE_VALUE;
        CloseHandle(child_stdin_read_);
        child_stdin_read_ = INVALID_HANDLE_VALUE;
    }

    ~DapProcess() {
        close();
    }

    DapProcess(const DapProcess&) = delete;
    DapProcess& operator=(const DapProcess&) = delete;
    DapProcess(DapProcess&&) = delete;
    DapProcess& operator=(DapProcess&&) = delete;

    void send_message(const JsonValue& msg) {
        auto full = luma::protocol::write_framed_message(msg.to_string());

        DWORD written = 0;
        if (!WriteFile(child_stdin_write_, full.data(), static_cast<DWORD>(full.size()), &written,
                       nullptr) ||
            static_cast<std::size_t>(written) != full.size()) {
            throw std::runtime_error("Failed to write DAP message");
        }
    }

    std::optional<JsonValue> read_message(int timeout_ms = 5000) {
        // Read Content-Length header.
        std::string header_buf;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        while (true) {
            if (std::chrono::steady_clock::now() > deadline) {
                return std::nullopt;
            }

            DWORD available = 0;
            PeekNamedPipe(child_stdout_read_, nullptr, 0, nullptr, &available, nullptr);

            if (available == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            char c = 0;
            DWORD read_count = 0;

            if (!ReadFile(child_stdout_read_, &c, 1, &read_count, nullptr) || read_count == 0) {
                return std::nullopt;
            }

            header_buf += c;

            if (header_buf.size() >= 4 && header_buf.ends_with("\r\n\r\n")) {
                break;
            }
        }

        // Parse Content-Length using the shared protocol framing helper.
        const std::size_t content_length = parse_dap_content_length(header_buf);

        // Read body.
        std::string body(content_length, '\0');
        std::size_t total_read = 0;

        while (total_read < content_length) {
            if (std::chrono::steady_clock::now() > deadline) {
                return std::nullopt;
            }

            DWORD read_count = 0;

            if (!ReadFile(child_stdout_read_, body.data() + total_read,
                          static_cast<DWORD>(content_length - total_read), &read_count, nullptr) ||
                read_count == 0) {
                return std::nullopt;
            }

            total_read += read_count;
        }

        return JsonValue::parse(body);
    }

    // Wait for the adapter to exit and report whether it finished cleanly (exit
    // code 0).  Used to tell a benign clean shutdown apart from a crash when a
    // write races the adapter's exit.  Records a human-readable classification of
    // the exit code in last_exit_detail_ (crashes surface as large NTSTATUS-style
    // codes such as 0xC0000005) so a failing run is self-describing.
    bool wait_exited_cleanly() {
        if (process_ == INVALID_HANDLE_VALUE) {
            last_exit_detail_ = "already reaped";
            return true;
        }
        WaitForSingleObject(process_, 5000);
        DWORD code = 1;
        if (GetExitCodeProcess(process_, &code) == 0) {
            last_exit_detail_ = "GetExitCodeProcess failed";
            return false;
        }
        last_exit_detail_ =
            std::format("exited with code {} (0x{:08X})", static_cast<unsigned long>(code),
                        static_cast<unsigned long>(code));
        return code == 0;
    }

    [[nodiscard]] const std::string& last_exit_detail() const noexcept {
        return last_exit_detail_;
    }

    void close() {
        if (child_stdin_write_ != INVALID_HANDLE_VALUE) {
            CloseHandle(child_stdin_write_);
            child_stdin_write_ = INVALID_HANDLE_VALUE;
        }

        if (child_stdout_read_ != INVALID_HANDLE_VALUE) {
            CloseHandle(child_stdout_read_);
            child_stdout_read_ = INVALID_HANDLE_VALUE;
        }

        if (process_ != INVALID_HANDLE_VALUE) {
            WaitForSingleObject(process_, 3000);
            TerminateProcess(process_, 1);
            CloseHandle(process_);
            process_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE process_{INVALID_HANDLE_VALUE};
    HANDLE child_stdin_read_{INVALID_HANDLE_VALUE};
    HANDLE child_stdin_write_{INVALID_HANDLE_VALUE};
    HANDLE child_stdout_read_{INVALID_HANDLE_VALUE};
    HANDLE child_stdout_write_{INVALID_HANDLE_VALUE};
    std::string last_exit_detail_{"not waited"};
};

#else
// POSIX implementation using popen/pipes — simplified for Linux/macOS.
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

class DapProcess {
public:
    explicit DapProcess() {
        int stdin_pipe[2];
        int stdout_pipe[2];

        if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
            throw std::runtime_error("Failed to create pipes");
        }

        pid_ = fork();

        if (pid_ < 0) {
            throw std::runtime_error("Failed to fork");
        }

        if (pid_ == 0) {
            // Child process.
            ::close(stdin_pipe[1]);
            ::close(stdout_pipe[0]);
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            ::close(stdin_pipe[0]);
            ::close(stdout_pipe[1]);
            execl(dap_exe_path.c_str(), dap_exe_path.c_str(), nullptr);
            _exit(1);
        }

        // Parent process.
        ::close(stdin_pipe[0]);
        ::close(stdout_pipe[1]);
        write_fd_ = stdin_pipe[1];
        read_fd_ = stdout_pipe[0];
    }

    ~DapProcess() {
        close();
    }

    DapProcess(const DapProcess&) = delete;
    DapProcess& operator=(const DapProcess&) = delete;
    DapProcess(DapProcess&&) = delete;
    DapProcess& operator=(DapProcess&&) = delete;

    void send_message(const JsonValue& msg) {
        auto full = luma::protocol::write_framed_message(msg.to_string());
        const char* data = full.data();
        std::size_t remaining = full.size();

        while (remaining > 0) {
            auto n = ::write(write_fd_, data, remaining);

            if (n <= 0) {
                throw std::runtime_error("Failed to write DAP message");
            }

            data += n;
            remaining -= static_cast<std::size_t>(n);
        }
    }

    std::optional<JsonValue> read_message(int timeout_ms = 5000) {
        // Simplified blocking read with timeout via select.
        std::string header_buf;

        while (true) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(read_fd_, &fds);

            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            int ready = select(read_fd_ + 1, &fds, nullptr, nullptr, &tv);

            if (ready <= 0) {
                return std::nullopt;
            }

            char c = 0;

            if (::read(read_fd_, &c, 1) <= 0) {
                return std::nullopt;
            }

            header_buf += c;

            if (header_buf.size() >= 4 && header_buf.ends_with("\r\n\r\n")) {
                break;
            }
        }

        const auto content_length = parse_dap_content_length(header_buf);

        std::string body(content_length, '\0');
        std::size_t total_read = 0;

        while (total_read < content_length) {
            auto n = ::read(read_fd_, body.data() + total_read, content_length - total_read);

            if (n <= 0) {
                return std::nullopt;
            }

            total_read += n;
        }

        return JsonValue::parse(body);
    }

    // Wait for the adapter to exit and report whether it finished cleanly (exit
    // code 0, not killed by a signal).  Used to tell a benign clean shutdown apart
    // from a crash when a write races the adapter's exit.  Records a
    // human-readable classification of the exit (exit code vs terminating signal)
    // in last_exit_detail_ so a failing CI run pinpoints *why* the adapter exited
    // non-cleanly instead of only reporting the opaque write failure.
    bool wait_exited_cleanly() {
        if (pid_ <= 0) {
            last_exit_detail_ = "already reaped";
            return true;
        }
        int status = 0;
        const pid_t r = waitpid(pid_, &status, 0);
        pid_ = -1;
        if (r <= 0) {
            last_exit_detail_ = "waitpid failed";
            return true;
        }
        if (WIFEXITED(status)) {
            const int code = WEXITSTATUS(status);
            last_exit_detail_ = "exited with code " + std::to_string(code);
            return code == 0;
        }
        if (WIFSIGNALED(status)) {
            const int sig = WTERMSIG(status);
            last_exit_detail_ =
                "killed by signal " + std::to_string(sig) + " (" + signal_name(sig) + ")";
            return false;
        }
        last_exit_detail_ = "unknown wait status " + std::to_string(status);
        return false;
    }

    [[nodiscard]] const std::string& last_exit_detail() const noexcept {
        return last_exit_detail_;
    }

    void close() {
        if (write_fd_ >= 0) {
            ::close(write_fd_);
            write_fd_ = -1;
        }

        if (read_fd_ >= 0) {
            ::close(read_fd_);
            read_fd_ = -1;
        }

        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
    }

private:
    static const char* signal_name(int sig) noexcept {
        switch (sig) {
            case SIGSEGV:
                return "SIGSEGV";
            case SIGABRT:
                return "SIGABRT";
            case SIGBUS:
                return "SIGBUS";
            case SIGILL:
                return "SIGILL";
            case SIGFPE:
                return "SIGFPE";
            case SIGKILL:
                return "SIGKILL";
            case SIGTERM:
                return "SIGTERM";
            case SIGPIPE:
                return "SIGPIPE";
            case SIGINT:
                return "SIGINT";
            default:
                return "other";
        }
    }

    pid_t pid_{-1};
    int write_fd_{-1};
    int read_fd_{-1};
    std::string last_exit_detail_{"not waited"};
};
#endif

// ─── DAP message helpers ───────────────────────────────────────────

int next_seq{1};

JsonValue make_request(const std::string& command, const JsonValue& args = JsonValue()) {
    JsonValue::ObjectType msg;
    msg["seq"] = JsonValue(next_seq++);
    msg["type"] = JsonValue(std::string("request"));
    msg["command"] = JsonValue(command);

    if (args.is_object()) {
        msg["arguments"] = args;
    }

    return JsonValue(std::move(msg));
}

// Read messages until we find a response to the given command,
// collecting any events along the way.  JsonValue's move constructor is not
// noexcept, so ReadResult's implicit move can throw; in this test harness a
// throwing move would only fail the run, so the escape warning is not useful.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct ReadResult {
    JsonValue response;
    std::vector<JsonValue> events;
};

ReadResult read_until_response(DapProcess& proc, const std::string& command,
                               int timeout_ms = 5000) {
    ReadResult result;

    while (true) {
        auto msg = proc.read_message(timeout_ms);

        if (!msg.has_value()) {
            throw std::runtime_error(std::format("Timeout waiting for '{}' response", command));
        }

        if (msg->has("type")) {
            auto type = (*msg)["type"].as_string();

            if (type == "response" && msg->has("command") &&
                (*msg)["command"].as_string() == command) {
                result.response = std::move(*msg);
                return result;
            }

            if (type == "event") {
                result.events.push_back(std::move(*msg));
            }
        }
    }
}

// Check if any event in a list has a given event name and optional body field match.
bool has_event(const std::vector<JsonValue>& events, const std::string& event_name,
               const std::string& body_field = "", const std::string& body_value = "") {
    for (const auto& evt : events) {
        if (evt.has("event") && evt["event"].as_string() == event_name) {
            if (body_field.empty()) {
                return true;
            }

            if (evt.has("body") && evt["body"].has(body_field) &&
                evt["body"][body_field].as_string() == body_value) {
                return true;
            }
        }
    }

    return false;
}

// Wait for a specific event from the process, returning all events read.
bool wait_for_event(DapProcess& proc, const std::string& event_name,
                    const std::string& body_field = "", const std::string& body_value = "",
                    int max_attempts = 100, int timeout_per_read_ms = 200) {
    for (int i = 0; i < max_attempts; ++i) {
        auto msg = proc.read_message(timeout_per_read_ms);

        if (msg.has_value() && msg->has("event")) {
            if ((*msg)["event"].as_string() == event_name) {
                if (body_field.empty()) {
                    return true;
                }

                if (msg->has("body") && (*msg)["body"].has(body_field) &&
                    (*msg)["body"][body_field].as_string() == body_value) {
                    return true;
                }
            }
        }
    }

    return false;
}

// Wait for either a `terminated` or a `stopped` event, returning which arrived
// ("" on timeout). An unhandled exception can stop twice — once at the throw
// point (the exception hook) and once at the top-level unwind — so a client
// driving such a session to completion must keep continuing until it sees
// `terminated`, without knowing in advance how many stops there will be.
std::string wait_for_terminate_or_stop(DapProcess& proc, int max_attempts = 100,
                                       int timeout_per_read_ms = 200) {
    for (int i = 0; i < max_attempts; ++i) {
        auto msg = proc.read_message(timeout_per_read_ms);

        if (msg.has_value() && msg->has("event")) {
            auto name = (*msg)["event"].as_string();

            if (name == "terminated" || name == "stopped") {
                return name;
            }
        }
    }

    return "";
}

// ─── Session lifecycle helpers ─────────────────────────────────────

// Send a request and pump incoming messages until its response arrives or the
// session terminates. Unlike read_until_response, this never throws and never
// discards a `terminated` event: it returns as soon as termination is observed
// so the caller can stop cleanly. Used by the concurrency stress test, which
// races protocol requests against a freely-running debuggee.
struct PollResult {
    bool terminated{false};
    bool got_response{false};
    JsonValue response;
};

PollResult poll_request(DapProcess& proc, const std::string& command,
                        const JsonValue& args = JsonValue(), int timeout_ms = 4000) {
    PollResult result;

    // The debuggee is free-running and can terminate at any moment; a write that
    // races the adapter's clean exit surfaces as a "failed to write" error.  Treat
    // a clean adapter exit as termination (the success condition for this stress
    // test), but let a crash-induced write failure propagate as a real failure.
    try {
        proc.send_message(make_request(command, args));
    } catch (const std::runtime_error& write_error) {
        if (!proc.wait_exited_cleanly()) {
            // Preserve the fail-on-non-clean-exit semantics, but attach the exact
            // adapter exit classification (signal name/number or exit code) so a
            // rare CI failure is self-diagnosing instead of an opaque write error.
            throw std::runtime_error(std::string(write_error.what()) + " — adapter " +
                                     proc.last_exit_detail());
        }
        result.terminated = true;
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        auto msg = proc.read_message(200);

        if (!msg.has_value()) {
            continue;
        }

        if (msg->has("event") && (*msg)["event"].as_string() == "terminated") {
            result.terminated = true;
            return result;
        }

        if (msg->has("type") && (*msg)["type"].as_string() == "response" && msg->has("command") &&
            (*msg)["command"].as_string() == command) {
            result.response = std::move(*msg);
            result.got_response = true;
            return result;
        }
    }

    return result;
}

std::string example_path(const std::string& filename) {
    return (std::filesystem::path(debug_examples_dir) / filename).string();
}

void initialize(DapProcess& proc) {
    next_seq = 1;
    proc.send_message(make_request("initialize"));
    (void)read_until_response(proc, "initialize");
}

ReadResult launch_program(DapProcess& proc, const std::string& filename, bool stop_on_entry = false,
                          bool no_debug = false) {
    JsonValue::ObjectType args;
    args["program"] = JsonValue(example_path(filename));

    if (stop_on_entry) {
        args["stopOnEntry"] = JsonValue(true);
    }

    if (no_debug) {
        args["noDebug"] = JsonValue(true);
    }

    proc.send_message(make_request("launch", JsonValue(std::move(args))));
    return read_until_response(proc, "launch");
}

ReadResult launch_with_time_travel(DapProcess& proc, const std::string& filename,
                                   bool stop_on_entry = false) {
    JsonValue::ObjectType args;
    args["program"] = JsonValue(example_path(filename));
    args["timeTravel"] = JsonValue(true);

    if (stop_on_entry) {
        args["stopOnEntry"] = JsonValue(true);
    }

    proc.send_message(make_request("launch", JsonValue(std::move(args))));
    return read_until_response(proc, "launch");
}

ReadResult send_configuration_done(DapProcess& proc) {
    proc.send_message(make_request("configurationDone"));
    return read_until_response(proc, "configurationDone");
}

ReadResult set_line_breakpoint(DapProcess& proc, const std::string& program_path, int line) {
    JsonValue::ObjectType source;
    source["path"] = JsonValue(program_path);

    JsonValue::ArrayType bps;
    JsonValue::ObjectType bp;
    bp["line"] = JsonValue(line);
    bps.push_back(JsonValue(std::move(bp)));

    JsonValue::ObjectType args;
    args["source"] = JsonValue(std::move(source));
    args["breakpoints"] = JsonValue(std::move(bps));
    proc.send_message(make_request("setBreakpoints", JsonValue(std::move(args))));
    return read_until_response(proc, "setBreakpoints");
}

ReadResult set_conditional_breakpoint(DapProcess& proc, const std::string& program_path, int line,
                                      const std::string& condition,
                                      const std::string& hit_condition = "") {
    JsonValue::ObjectType source;
    source["path"] = JsonValue(program_path);

    JsonValue::ArrayType bps;
    JsonValue::ObjectType bp;
    bp["line"] = JsonValue(line);

    if (!condition.empty()) {
        bp["condition"] = JsonValue(condition);
    }

    if (!hit_condition.empty()) {
        bp["hitCondition"] = JsonValue(hit_condition);
    }

    bps.push_back(JsonValue(std::move(bp)));

    JsonValue::ObjectType args;
    args["source"] = JsonValue(std::move(source));
    args["breakpoints"] = JsonValue(std::move(bps));
    proc.send_message(make_request("setBreakpoints", JsonValue(std::move(args))));
    return read_until_response(proc, "setBreakpoints");
}

ReadResult set_log_breakpoint(DapProcess& proc, const std::string& program_path, int line,
                              const std::string& log_message) {
    JsonValue::ObjectType source;
    source["path"] = JsonValue(program_path);

    JsonValue::ArrayType bps;
    JsonValue::ObjectType bp;
    bp["line"] = JsonValue(line);
    bp["logMessage"] = JsonValue(log_message);
    bps.push_back(JsonValue(std::move(bp)));

    JsonValue::ObjectType args;
    args["source"] = JsonValue(std::move(source));
    args["breakpoints"] = JsonValue(std::move(bps));
    proc.send_message(make_request("setBreakpoints", JsonValue(std::move(args))));
    return read_until_response(proc, "setBreakpoints");
}

ReadResult set_function_breakpoints(DapProcess& proc,
                                    const std::vector<std::string>& function_names) {
    JsonValue::ArrayType bps;

    for (const auto& name : function_names) {
        JsonValue::ObjectType bp;
        bp["name"] = JsonValue(name);
        bps.push_back(JsonValue(std::move(bp)));
    }

    JsonValue::ObjectType args;
    args["breakpoints"] = JsonValue(std::move(bps));
    proc.send_message(make_request("setFunctionBreakpoints", JsonValue(std::move(args))));
    return read_until_response(proc, "setFunctionBreakpoints");
}

ReadResult continue_execution(DapProcess& proc, int thread_id = 1) {
    JsonValue::ObjectType args;
    args["threadId"] = JsonValue(thread_id);
    proc.send_message(make_request("continue", JsonValue(std::move(args))));
    return read_until_response(proc, "continue");
}

ReadResult get_stack_trace(DapProcess& proc, int thread_id = 1) {
    JsonValue::ObjectType args;
    args["threadId"] = JsonValue(thread_id);
    proc.send_message(make_request("stackTrace", JsonValue(std::move(args))));
    return read_until_response(proc, "stackTrace");
}

ReadResult get_variables(DapProcess& proc, int variables_reference) {
    JsonValue::ObjectType args;
    args["variablesReference"] = JsonValue(variables_reference);
    proc.send_message(make_request("variables", JsonValue(std::move(args))));
    return read_until_response(proc, "variables");
}

ReadResult set_variable_request(DapProcess& proc, int variables_reference, const std::string& name,
                                const std::string& value) {
    JsonValue::ObjectType args;
    args["variablesReference"] = JsonValue(variables_reference);
    args["name"] = JsonValue(name);
    args["value"] = JsonValue(value);
    proc.send_message(make_request("setVariable", JsonValue(std::move(args))));
    return read_until_response(proc, "setVariable");
}

void disconnect(DapProcess& proc) {
    proc.send_message(make_request("disconnect"));
    proc.close();
}

// ─── Integration tests ────────────────────────────────────────────

void test_initialize_capabilities() {
    next_seq = 1;
    DapProcess proc;

    // Send initialize.
    proc.send_message(make_request("initialize"));
    auto result = read_until_response(proc, "initialize");

    ASSERT_SUCCESS(result.response);

    auto& body = result.response["body"];
    ASSERT_TRUE(body["supportsConfigurationDoneRequest"].as_bool());
    ASSERT_TRUE(body["supportsConditionalBreakpoints"].as_bool());
    ASSERT_TRUE(body["supportsHitConditionalBreakpoints"].as_bool());
    ASSERT_TRUE(body["supportsSetVariable"].as_bool());
    ASSERT_TRUE(body["supportsCompletionsRequest"].as_bool());
    ASSERT_TRUE(body["supportsRestartRequest"].as_bool());
    ASSERT_TRUE(body["supportsExceptionInfoRequest"].as_bool());
    ASSERT_TRUE(body["supportsLogPoints"].as_bool());
    ASSERT_TRUE(body["supportsTerminateRequest"].as_bool());
    ASSERT_TRUE(body["supportsLoadedSourcesRequest"].as_bool());

    // The initialized event is sent AFTER the initialize response.
    bool found_initialized = has_event(result.events, "initialized") ||
                             wait_for_event(proc, "initialized", "", "", 10, 500);

    ASSERT_TRUE(found_initialized);

    disconnect(proc);
}

void test_launch_and_terminate() {
    DapProcess proc;
    initialize(proc);

    auto launch_result = launch_program(proc, "breakpoint_basic.luma");
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);
    ASSERT_SUCCESS(config_result.response);

    // Wait for terminated event (program should finish quickly).
    ASSERT_TRUE(has_event(config_result.events, "terminated") ||
                wait_for_event(proc, "terminated"));

    disconnect(proc);
}

void test_launch_compile_error() {
    DapProcess proc;
    initialize(proc);

    // Launch a non-existent file.
    JsonValue::ObjectType launch_args;
    launch_args["program"] = JsonValue(std::string("nonexistent.luma"));
    proc.send_message(make_request("launch", JsonValue(std::move(launch_args))));

    auto launch_result = read_until_response(proc, "launch");
    ASSERT_FALSE(launch_result.response["success"].as_bool());

    disconnect(proc);
}

void test_stop_on_entry() {
    DapProcess proc;
    initialize(proc);

    auto launch_result = launch_program(proc, "breakpoint_basic.luma", true);
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);
    ASSERT_SUCCESS(config_result.response);

    // The stopped event with reason "entry" may arrive with the launch
    // response, the configurationDone response, or after it.
    bool found_entry_stop = has_event(launch_result.events, "stopped", "reason", "entry") ||
                            has_event(config_result.events, "stopped", "reason", "entry") ||
                            wait_for_event(proc, "stopped", "reason", "entry");
    ASSERT_TRUE(found_entry_stop);

    (void)continue_execution(proc);
    ASSERT_TRUE(wait_for_event(proc, "terminated"));

    disconnect(proc);
}

void test_breakpoint_stop_and_variables() {
    DapProcess proc;
    initialize(proc);

    auto program_path = example_path("variables_basic.luma");
    auto bp_result = set_line_breakpoint(proc, program_path, test_lines::variables_basic_stop);
    ASSERT_SUCCESS(bp_result.response);

    auto launch_result = launch_program(proc, "variables_basic.luma");
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);

    // Wait for stopped event at breakpoint.
    bool found_breakpoint_stop =
        has_event(launch_result.events, "stopped", "reason", "breakpoint") ||
        has_event(config_result.events, "stopped", "reason", "breakpoint") ||
        wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_breakpoint_stop);

    // Get threads.
    proc.send_message(make_request("threads"));
    auto threads_result = read_until_response(proc, "threads");
    ASSERT_TRUE(threads_result.response["success"].as_bool());
    ASSERT_GE(threads_result.response["body"]["threads"].as_array().size(), 1U);

    // Get stack trace.
    auto st_result = get_stack_trace(proc);
    ASSERT_TRUE(st_result.response["success"].as_bool());

    auto& stack_frames = st_result.response["body"]["stackFrames"].as_array();
    ASSERT_FALSE(stack_frames.empty());

    // Get scopes for the top frame.
    auto top_frame_id = stack_frames[0]["id"].as_integer();

    JsonValue::ObjectType scopes_args;
    scopes_args["frameId"] = JsonValue(static_cast<int>(top_frame_id));
    proc.send_message(make_request("scopes", JsonValue(std::move(scopes_args))));
    auto scopes_result = read_until_response(proc, "scopes");
    ASSERT_TRUE(scopes_result.response["success"].as_bool());

    auto& scopes = scopes_result.response["body"]["scopes"].as_array();
    ASSERT_FALSE(scopes.empty());
    ASSERT_EQ(scopes[0]["name"].as_string(), "Local");

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

void test_exception_breakpoint() {
    DapProcess proc;
    initialize(proc);

    // Set exception breakpoints for uncaught.
    JsonValue::ArrayType filters;
    filters.push_back(JsonValue(std::string("uncaught")));

    JsonValue::ObjectType exc_args;
    exc_args["filters"] = JsonValue(std::move(filters));
    proc.send_message(make_request("setExceptionBreakpoints", JsonValue(std::move(exc_args))));
    (void)read_until_response(proc, "setExceptionBreakpoints");

    auto launch_result = launch_program(proc, "exception_unhandled.luma");
    auto config_result = send_configuration_done(proc);

    // Wait for stopped event with reason "exception".
    bool found_exception_stop = has_event(launch_result.events, "stopped", "reason", "exception") ||
                                has_event(config_result.events, "stopped", "reason", "exception") ||
                                wait_for_event(proc, "stopped", "reason", "exception");
    ASSERT_TRUE(found_exception_stop);

    // Get exception info.
    proc.send_message(make_request("exceptionInfo"));
    auto exc_info = read_until_response(proc, "exceptionInfo");
    ASSERT_TRUE(exc_info.response["success"].as_bool());
    ASSERT_EQ(exc_info.response["body"]["exceptionId"].as_string(), "RuntimeError");

    // Continue — should terminate since exception is unrecoverable.
    (void)continue_execution(proc);

    disconnect(proc);
}

// A client that issues `continue` after an unhandled-exception stop must not
// hang the debuggee. Regression test for a lost-wakeup: the execution thread
// emitted the exception `stopped` event *before* marking itself paused, so a
// racing `continue` could see is_paused == false, skip the unpause, and notify
// an unregistered waiter — the wakeup was lost and the execution thread blocked
// forever. An unhandled exception stops twice (at the throw point, then at the
// top-level unwind), so the test continues until termination; a reintroduced
// race would strand one of those continues and the harness read timeout turns
// the hang into a failure, not a deadlock. Repeated to raise the odds of
// catching the race.
void test_continue_after_unhandled_exception() {
    constexpr int iterations = 3;

    for (int i = 0; i < iterations; ++i) {
        DapProcess proc;
        initialize(proc);

        JsonValue::ArrayType filters;
        filters.push_back(JsonValue(std::string("uncaught")));
        JsonValue::ObjectType exc_args;
        exc_args["filters"] = JsonValue(std::move(filters));
        proc.send_message(make_request("setExceptionBreakpoints", JsonValue(std::move(exc_args))));
        (void)read_until_response(proc, "setExceptionBreakpoints");

        auto launch_result = launch_program(proc, "exception_unhandled.luma");
        auto config_result = send_configuration_done(proc);

        bool found_exception_stop =
            has_event(launch_result.events, "stopped", "reason", "exception") ||
            has_event(config_result.events, "stopped", "reason", "exception") ||
            wait_for_event(proc, "stopped", "reason", "exception");
        ASSERT_TRUE(found_exception_stop);

        // Auto-continue on each exception stop — every continue must make
        // progress (re-stop or terminate), never hang, and the session must
        // ultimately reach termination.
        bool terminated = false;

        for (int attempt = 0; attempt < 6 && !terminated; ++attempt) {
            auto cont_result = continue_execution(proc);

            if (has_event(cont_result.events, "terminated")) {
                terminated = true;
                break;
            }

            // The re-stop or termination may arrive either before the continue
            // response (already collected) or after it (read further).
            if (has_event(cont_result.events, "stopped", "reason", "exception")) {
                continue;
            }

            auto next = wait_for_terminate_or_stop(proc);

            if (next == "terminated") {
                terminated = true;
            } else if (next.empty()) {
                break; // no progress — a reintroduced lost wakeup lands here
            }
            // Otherwise the adapter re-stopped at the top-level unwind; loop.
        }

        ASSERT_TRUE(terminated);

        disconnect(proc);
    }
}

void test_loaded_sources() {
    DapProcess proc;
    initialize(proc);

    (void)launch_program(proc, "breakpoint_basic.luma", true);
    auto config_result = send_configuration_done(proc);

    // Wait for stopped event.
    (void)(has_event(config_result.events, "stopped") || wait_for_event(proc, "stopped"));

    // Get loaded sources.
    proc.send_message(make_request("loadedSources"));
    auto sources_result = read_until_response(proc, "loadedSources");
    ASSERT_TRUE(sources_result.response["success"].as_bool());

    auto& sources = sources_result.response["body"]["sources"].as_array();
    ASSERT_FALSE(sources.empty());

    disconnect(proc);
}

// ─── Step-over integration test ────────────────────────────────────

void test_step_over() {
    DapProcess proc;
    initialize(proc);

    (void)launch_program(proc, "step_over_loop.luma", true);
    auto config_result = send_configuration_done(proc);

    // Wait for stopped at entry.
    bool found_entry = has_event(config_result.events, "stopped", "reason", "entry") ||
                       wait_for_event(proc, "stopped", "reason", "entry");
    ASSERT_TRUE(found_entry);

    // Step over (next) — should stop on the next line.
    JsonValue::ObjectType next_args;
    next_args["threadId"] = JsonValue(1);
    proc.send_message(make_request("next", JsonValue(std::move(next_args))));
    auto next_result = read_until_response(proc, "next");
    ASSERT_SUCCESS(next_result.response);

    // Should receive a stopped event with reason "step".
    bool found_step = has_event(next_result.events, "stopped", "reason", "step") ||
                      wait_for_event(proc, "stopped", "reason", "step");
    ASSERT_TRUE(found_step);

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Step-into integration test ────────────────────────────────────

void test_step_into() {
    DapProcess proc;
    initialize(proc);

    // Set breakpoint on the line that calls add() — line 9.
    auto program_path = example_path("step_into_function.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::step_into_function_call);

    (void)launch_program(proc, "step_into_function.luma");
    auto config_result = send_configuration_done(proc);

    // Wait for breakpoint stop.
    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    // Step into the add function.
    JsonValue::ObjectType step_args;
    step_args["threadId"] = JsonValue(1);
    proc.send_message(make_request("stepIn", JsonValue(std::move(step_args))));
    auto step_result = read_until_response(proc, "stepIn");
    ASSERT_SUCCESS(step_result.response);

    // Wait for stopped with reason "step".
    bool found_step = has_event(step_result.events, "stopped", "reason", "step") ||
                      wait_for_event(proc, "stopped", "reason", "step");
    ASSERT_TRUE(found_step);

    // Verify we're inside the add function by checking the stack trace.
    auto st_result = get_stack_trace(proc);
    ASSERT_TRUE(st_result.response["success"].as_bool());

    auto& frames = st_result.response["body"]["stackFrames"].as_array();
    ASSERT_GE(frames.size(), 2U); // add + main frames.

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Step-out integration test ─────────────────────────────────────

void test_step_out() {
    DapProcess proc;
    initialize(proc);

    // Set breakpoint inside the add function — line 4 (return a + b).
    auto program_path = example_path("step_into_function.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::step_into_function_body);

    (void)launch_program(proc, "step_into_function.luma");
    auto config_result = send_configuration_done(proc);

    // Wait for breakpoint stop inside add().
    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    // Verify we're inside add — should have >= 2 frames.
    auto st_result = get_stack_trace(proc);
    auto& frames_before = st_result.response["body"]["stackFrames"].as_array();
    auto depth_before = frames_before.size();
    ASSERT_GE(depth_before, 2U);

    // Step out — should return to main().
    JsonValue::ObjectType step_out_args;
    step_out_args["threadId"] = JsonValue(1);
    proc.send_message(make_request("stepOut", JsonValue(std::move(step_out_args))));
    auto step_result = read_until_response(proc, "stepOut");
    ASSERT_SUCCESS(step_result.response);

    // Wait for stopped with reason "step".
    bool found_step = has_event(step_result.events, "stopped", "reason", "step") ||
                      wait_for_event(proc, "stopped", "reason", "step");
    ASSERT_TRUE(found_step);

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Evaluate expression integration test ──────────────────────────

void test_evaluate_expression() {
    DapProcess proc;
    initialize(proc);

    // Set breakpoint on line 10 of variables_basic.luma (after variables are set).
    auto program_path = example_path("variables_basic.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::variables_basic_stop);

    (void)launch_program(proc, "variables_basic.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    // Get stack trace to find the frame ID.
    auto st_result = get_stack_trace(proc);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    // Evaluate a local variable — use "watch" context for full evaluation.
    JsonValue::ObjectType eval_args;
    eval_args["expression"] = JsonValue(std::string("count"));
    eval_args["frameId"] = JsonValue(static_cast<int>(frame_id));
    eval_args["context"] = JsonValue(std::string("watch"));
    proc.send_message(make_request("evaluate", JsonValue(std::move(eval_args))));
    auto eval_result = read_until_response(proc, "evaluate");
    ASSERT_SUCCESS(eval_result.response);

    // Evaluate should return a result.
    ASSERT_TRUE(eval_result.response["body"].has("result"));

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// Compound expressions (not bare locals) must be compiled and executed on the
// scratch VM. Regression test: the evaluator previously wrapped the expression
// in an @main function and captured a stubbed print()'s argument, which threw
// at execution and surfaced "<evaluation error>" for every compound watch/REPL
// expression. The evaluator now compiles `function __bp_eval__() { return
// <expr> }` and reads the value back via VM::execute_function.
void test_evaluate_compound_expression() {
    DapProcess proc;
    initialize(proc);

    auto program_path = example_path("variables_basic.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::variables_basic_stop);

    (void)launch_program(proc, "variables_basic.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    auto st_result = get_stack_trace(proc);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    // A self-contained compound expression — exercises scratch-VM compilation
    // and execution rather than a bare-local lookup.
    {
        JsonValue::ObjectType eval_args;
        eval_args["expression"] = JsonValue(std::string("1 + 1"));
        eval_args["frameId"] = JsonValue(static_cast<int>(frame_id));
        eval_args["context"] = JsonValue(std::string("watch"));
        proc.send_message(make_request("evaluate", JsonValue(std::move(eval_args))));
        auto eval_result = read_until_response(proc, "evaluate");
        ASSERT_SUCCESS(eval_result.response);
        ASSERT_EQ(eval_result.response["body"]["result"].as_string(), std::string("2"));
    }

    // A compound expression that references a program local — the local must be
    // resolved through the scratch environment (free-variable support).
    {
        JsonValue::ObjectType eval_args;
        eval_args["expression"] = JsonValue(std::string("count > 5"));
        eval_args["frameId"] = JsonValue(static_cast<int>(frame_id));
        eval_args["context"] = JsonValue(std::string("watch"));
        proc.send_message(make_request("evaluate", JsonValue(std::move(eval_args))));
        auto eval_result = read_until_response(proc, "evaluate");
        ASSERT_SUCCESS(eval_result.response);
        ASSERT_EQ(eval_result.response["body"]["result"].as_string(), std::string("true"));
    }

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// Compound expressions that reference the program's own top-level functions
// must resolve through the scratch environment. Regression test:
// build_scratch_environment previously seeded the scratch VM with only the
// stdlib and the current frame's locals, so any compound
// watch/evaluate/conditional-breakpoint/logpoint expression that *called* a
// user-defined program global evaluated to an error. The scratch environment
// now also snapshots the target VM's global bindings.
void test_evaluate_global_reference() {
    DapProcess proc;
    initialize(proc);

    auto program_path = example_path("global_scope.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::global_scope_stop);

    (void)launch_program(proc, "global_scope.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    auto st_result = get_stack_trace(proc);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    // A top-level function applied to a frame local — the function is a program
    // global that must be copied into the scratch environment.
    {
        JsonValue::ObjectType eval_args;
        eval_args["expression"] = JsonValue(std::string("triple(amount)"));
        eval_args["frameId"] = JsonValue(static_cast<int>(frame_id));
        eval_args["context"] = JsonValue(std::string("watch"));
        proc.send_message(make_request("evaluate", JsonValue(std::move(eval_args))));
        auto eval_result = read_until_response(proc, "evaluate");
        ASSERT_SUCCESS(eval_result.response);
        ASSERT_EQ(eval_result.response["body"]["result"].as_string(), std::string("15"));
    }

    // The same top-level function combined with a frame local — both the call
    // and the local must resolve within the scratch environment.
    {
        JsonValue::ObjectType eval_args;
        eval_args["expression"] = JsonValue(std::string("triple(amount) + amount"));
        eval_args["frameId"] = JsonValue(static_cast<int>(frame_id));
        eval_args["context"] = JsonValue(std::string("watch"));
        proc.send_message(make_request("evaluate", JsonValue(std::move(eval_args))));
        auto eval_result = read_until_response(proc, "evaluate");
        ASSERT_SUCCESS(eval_result.response);
        ASSERT_EQ(eval_result.response["body"]["result"].as_string(), std::string("20"));
    }

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// A watch/evaluate expression that never terminates must abort at the
// evaluation deadline instead of hanging the adapter. Regression test: the
// scratch-VM timeout was only measured *after* execute_function returned, so a
// non-terminating expression looped forever and froze the protocol thread (the
// evaluate would never return). The scratch VM now enforces the deadline
// cooperatively via a per-line debug hook. `_spin()` loops forever; the
// evaluate must come back — well within a bound far below "never" — reporting a
// timeout, and the session must stay responsive afterwards.
void test_evaluate_timeout_does_not_hang() {
    DapProcess proc;
    initialize(proc);

    auto program_path = example_path("infinite_eval.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::infinite_eval_stop);

    (void)launch_program(proc, "infinite_eval.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    auto st_result = get_stack_trace(proc);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    JsonValue::ObjectType eval_args;
    eval_args["expression"] = JsonValue(std::string("_spin()"));
    eval_args["frameId"] = JsonValue(static_cast<int>(frame_id));
    eval_args["context"] = JsonValue(std::string("watch"));
    proc.send_message(make_request("evaluate", JsonValue(std::move(eval_args))));

    // Allow generously more than the 5s eval deadline, but the point is that it
    // returns at all: without the cooperative deadline the read simply times out
    // (the adapter is wedged) and this throws, failing the test.
    auto eval_result = read_until_response(proc, "evaluate", 20000);
    auto result_text = eval_result.response["body"]["result"].as_string();
    ASSERT_TRUE(result_text.find("timeout") != std::string::npos);

    // The adapter must still process requests after aborting the evaluation.
    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── noDebug launch integration test ───────────────────────────────

void test_no_debug_launch() {
    DapProcess proc;
    initialize(proc);

    // Set a breakpoint that should NOT be hit in noDebug mode.
    auto program_path = example_path("breakpoint_basic.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::breakpoint_basic_no_debug);

    auto launch_result = launch_program(proc, "breakpoint_basic.luma", false, true);
    ASSERT_SUCCESS(launch_result.response);

    (void)send_configuration_done(proc);

    // Program should terminate without stopping at the breakpoint.
    ASSERT_TRUE(wait_for_event(proc, "terminated"));

    disconnect(proc);
}

// ─── Conditional breakpoint integration test ───────────────────────

void test_conditional_breakpoint() {
    DapProcess proc;
    initialize(proc);

    // Verify conditional breakpoint request is accepted and returns breakpoints.
    auto program_path = example_path("breakpoint_basic.luma");
    auto bp_result = set_conditional_breakpoint(proc, program_path,
                                                test_lines::breakpoint_basic_conditional, "true");
    ASSERT_SUCCESS(bp_result.response);

    // Verify the response contains breakpoint entries.
    ASSERT_TRUE(bp_result.response["body"].has("breakpoints"));
    auto& bps = bp_result.response["body"]["breakpoints"].as_array();
    ASSERT_FALSE(bps.empty());

    // Also test setting a breakpoint with both condition and hit condition.
    auto bp_result2 = set_conditional_breakpoint(
        proc, program_path, test_lines::breakpoint_basic_conditional, "x > 5", "3");
    ASSERT_SUCCESS(bp_result2.response);
    ASSERT_TRUE(bp_result2.response["body"].has("breakpoints"));

    disconnect(proc);
}

// ─── Hit condition breakpoint integration test ─────────────────────

void test_hit_condition_breakpoint() {
    DapProcess proc;
    initialize(proc);

    // Set a breakpoint with hit condition: stop on the 5th hit.
    auto program_path = example_path("conditional_loop.luma");
    auto bp_result =
        set_conditional_breakpoint(proc, program_path, test_lines::conditional_loop_body, "", "5");
    ASSERT_SUCCESS(bp_result.response);

    auto launch_result = launch_program(proc, "conditional_loop.luma");
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);

    // Should stop at the breakpoint on the 5th hit.
    bool found_bp = has_event(launch_result.events, "stopped", "reason", "breakpoint") ||
                    has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// A breakpoint carrying *both* a condition and a hit condition must count only
// condition-true hits and evaluate the condition before the hit condition.
// Regression test: check_breakpoint previously incremented the hit counter on
// every line visit and tested the hit condition *before* the condition, so a
// "3rd time the condition holds" breakpoint fired on the wrong iteration (or,
// as here, never fired at all).
//
// The loop is `for i in 1 .. 11`; line 8 (`total += i`) runs with `total`
// holding the sum of the *earlier* iterations: 0, 1, 3, 6, 10, 15, 21, ... The
// condition `total >= 10` first holds at i == 5 (total 10), and the hit
// condition `==3` selects the 3rd such hit — i == 7, where total == 21. The
// buggy order tested `==3` against the raw line-visit count (matching only the
// 3rd visit, i == 3, whose condition `total >= 10` is false at total 3) and so
// never stopped. (The loop variable `i` is not introspectable in a breakpoint
// condition, so the discriminating expression is the function local `total`.)
void test_conditional_and_hit_condition_breakpoint() {
    DapProcess proc;
    initialize(proc);

    auto program_path = example_path("conditional_loop.luma");
    auto bp_result = set_conditional_breakpoint(
        proc, program_path, test_lines::conditional_loop_body, "total >= 10", "==3");
    ASSERT_SUCCESS(bp_result.response);

    auto launch_result = launch_program(proc, "conditional_loop.luma");
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(launch_result.events, "stopped", "reason", "breakpoint") ||
                    has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    // The break must land on the 3rd condition-true hit (i == 7, total == 21).
    auto st_result = get_stack_trace(proc);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    JsonValue::ObjectType eval_args;
    eval_args["expression"] = JsonValue(std::string("total"));
    eval_args["frameId"] = JsonValue(static_cast<int>(frame_id));
    eval_args["context"] = JsonValue(std::string("watch"));
    proc.send_message(make_request("evaluate", JsonValue(std::move(eval_args))));
    auto eval_result = read_until_response(proc, "evaluate");
    ASSERT_SUCCESS(eval_result.response);
    ASSERT_EQ(eval_result.response["body"]["result"].as_string(), std::string("21"));

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Function breakpoint integration test ──────────────────────────

void test_function_breakpoint() {
    DapProcess proc;
    initialize(proc);

    // Verify function breakpoint request is accepted and returns a response.
    auto bp_result = set_function_breakpoints(proc, {"double_value", "triple_value"});
    ASSERT_SUCCESS(bp_result.response);

    // Verify the response contains breakpoint entries for each function.
    ASSERT_TRUE(bp_result.response["body"].has("breakpoints"));
    auto& bps = bp_result.response["body"]["breakpoints"].as_array();
    ASSERT_EQ(bps.size(), static_cast<std::size_t>(2));

    // Clear function breakpoints with empty list.
    auto clear_result = set_function_breakpoints(proc, {});
    ASSERT_SUCCESS(clear_result.response);

    disconnect(proc);
}

// ─── Set variable integration test ─────────────────────────────────

void test_set_variable() {
    DapProcess proc;
    initialize(proc);

    // Use variables_basic.luma with a breakpoint on line 13 (known to work).
    auto program_path = example_path("variables_basic.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::variables_basic_set_variable);

    auto launch_result = launch_program(proc, "variables_basic.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(launch_result.events, "stopped", "reason", "breakpoint") ||
                    has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    // Get scopes to find the variables reference.
    auto st_result = get_stack_trace(proc);
    ASSERT_SUCCESS(st_result.response);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    JsonValue::ObjectType scopes_args;
    scopes_args["frameId"] = JsonValue(static_cast<int>(frame_id));
    proc.send_message(make_request("scopes", JsonValue(std::move(scopes_args))));
    auto scopes_result = read_until_response(proc, "scopes");
    ASSERT_SUCCESS(scopes_result.response);

    auto& scopes = scopes_result.response["body"]["scopes"].as_array();
    ASSERT_FALSE(scopes.empty());

    auto local_ref = scopes[0]["variablesReference"].as_integer();

    // Try to set a variable. Variables in Luma may or may not be mutable.
    JsonValue::ObjectType set_args;
    set_args["variablesReference"] = JsonValue(static_cast<int>(local_ref));
    set_args["name"] = JsonValue(std::string("count"));
    set_args["value"] = JsonValue(std::string("99"));
    proc.send_message(make_request("setVariable", JsonValue(std::move(set_args))));
    auto set_result = read_until_response(proc, "setVariable");

    // The request should get a response (success or error — both are valid).
    ASSERT_TRUE(set_result.response.has("command"));
    ASSERT_EQ(set_result.response["command"].as_string(), "setVariable");

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// A mutable variable's declared type must be respected by setVariable (B07):
// a same-type edit succeeds, but a type-changing edit is rejected with an error
// instead of silently storing a wrongly-typed value in the slot.
void test_set_variable_respects_type() {
    DapProcess proc;
    initialize(proc);

    auto program_path = example_path("set_variable.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::set_variable_typed_stop);

    auto launch_result = launch_program(proc, "set_variable.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(launch_result.events, "stopped", "reason", "breakpoint") ||
                    has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    auto st_result = get_stack_trace(proc);
    ASSERT_SUCCESS(st_result.response);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    JsonValue::ObjectType scopes_args;
    scopes_args["frameId"] = JsonValue(static_cast<int>(frame_id));
    proc.send_message(make_request("scopes", JsonValue(std::move(scopes_args))));
    auto scopes_result = read_until_response(proc, "scopes");
    ASSERT_SUCCESS(scopes_result.response);

    auto& scopes = scopes_result.response["body"]["scopes"].as_array();
    ASSERT_FALSE(scopes.empty());
    auto local_ref = static_cast<int>(scopes[0]["variablesReference"].as_integer());

    // Same-type edit: setting the mutable integer to another integer succeeds.
    auto ok_result = set_variable_request(proc, local_ref, "count", "7");
    ASSERT_SUCCESS(ok_result.response);
    ASSERT_EQ(ok_result.response["body"]["value"].as_string(), "7");

    // Type-changing edits are rejected rather than silently retyping the slot.
    auto text_result = set_variable_request(proc, local_ref, "count", "abc");
    ASSERT_FALSE(text_result.response["success"].as_bool());

    auto decimal_result = set_variable_request(proc, local_ref, "count", "2.5");
    ASSERT_FALSE(decimal_result.response["success"].as_bool());

    // A string variable still accepts arbitrary text.
    auto label_result = set_variable_request(proc, local_ref, "label", "renamed");
    ASSERT_SUCCESS(label_result.response);

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Restart integration test ──────────────────────────────────────

void test_restart() {
    DapProcess proc;
    initialize(proc);

    // Launch with stopOnEntry.
    (void)launch_program(proc, "breakpoint_basic.luma", true);
    auto config_result = send_configuration_done(proc);

    bool found_entry = has_event(config_result.events, "stopped", "reason", "entry") ||
                       wait_for_event(proc, "stopped", "reason", "entry");
    ASSERT_TRUE(found_entry);

    // Send restart request — verify the response is successful.
    proc.send_message(make_request("restart"));
    auto restart_result = read_until_response(proc, "restart");
    ASSERT_SUCCESS(restart_result.response);

    // After restart, wait for any stopped or terminated event.
    // The debugger may re-launch and stop, or the program may run to completion.
    bool found_event = wait_for_event(proc, "stopped", "", "", 50, 200) ||
                       wait_for_event(proc, "terminated", "", "", 50, 200);

    if (found_event) {
        (void)continue_execution(proc);
    }

    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Log point integration test ────────────────────────────────────

void test_log_point() {
    DapProcess proc;
    initialize(proc);

    // Set a log breakpoint — should log output without stopping.
    auto program_path = example_path("conditional_loop.luma");
    auto bp_result = set_log_breakpoint(proc, program_path, test_lines::conditional_loop_body,
                                        "Loop iteration: {i}");
    ASSERT_SUCCESS(bp_result.response);

    auto launch_result = launch_program(proc, "conditional_loop.luma");
    ASSERT_SUCCESS(launch_result.response);

    (void)send_configuration_done(proc);

    // Log points should NOT stop execution — program terminates normally.
    bool terminated =
        has_event(launch_result.events, "terminated") || wait_for_event(proc, "terminated");
    ASSERT_TRUE(terminated);

    disconnect(proc);
}

// ─── Pause integration test ────────────────────────────────────────

void test_pause() {
    DapProcess proc;
    initialize(proc);

    // Launch with stopOnEntry.
    (void)launch_program(proc, "long_loop.luma", true);
    auto config_result = send_configuration_done(proc);

    bool found_entry = has_event(config_result.events, "stopped", "reason", "entry") ||
                       wait_for_event(proc, "stopped", "reason", "entry");
    ASSERT_TRUE(found_entry);

    // Continue execution.
    (void)continue_execution(proc);

    // Give the program time to start running.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Send pause request — verify the protocol is accepted.
    JsonValue::ObjectType pause_args;
    pause_args["threadId"] = JsonValue(1);
    proc.send_message(make_request("pause", JsonValue(std::move(pause_args))));
    auto pause_result = read_until_response(proc, "pause");
    ASSERT_SUCCESS(pause_result.response);

    // Program may or may not have paused (timing dependent).
    // Either wait for stop or terminated.
    (void)wait_for_event(proc, "stopped", "", "", 20, 100);
    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Completions integration test ──────────────────────────────────

void test_completions() {
    DapProcess proc;
    initialize(proc);

    // Set breakpoint and launch to get a paused state.
    auto program_path = example_path("variables_basic.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::variables_basic_stop);

    (void)launch_program(proc, "variables_basic.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    // Get frame ID for completions context.
    auto st_result = get_stack_trace(proc);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    // Request completions for "co" prefix.
    JsonValue::ObjectType comp_args;
    comp_args["frameId"] = JsonValue(static_cast<int>(frame_id));
    comp_args["text"] = JsonValue(std::string("co"));
    comp_args["column"] = JsonValue(3);
    proc.send_message(make_request("completions", JsonValue(std::move(comp_args))));
    auto comp_result = read_until_response(proc, "completions");
    ASSERT_SUCCESS(comp_result.response);

    // Should return a targets array.
    ASSERT_TRUE(comp_result.response["body"].has("targets"));
    ASSERT_TRUE(comp_result.response["body"]["targets"].is_array());

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Exception info: caught exception ──────────────────────────────

void test_exception_info_caught() {
    DapProcess proc;
    initialize(proc);

    // Set exception breakpoints for both caught and uncaught.
    JsonValue::ArrayType filters;
    filters.push_back(JsonValue(std::string("caught")));
    filters.push_back(JsonValue(std::string("uncaught")));

    JsonValue::ObjectType exc_args;
    exc_args["filters"] = JsonValue(std::move(filters));
    proc.send_message(make_request("setExceptionBreakpoints", JsonValue(std::move(exc_args))));
    (void)read_until_response(proc, "setExceptionBreakpoints");

    auto launch_result = launch_program(proc, "exception_unhandled.luma");
    auto config_result = send_configuration_done(proc);

    // Wait for exception stop.
    bool found_exc = has_event(launch_result.events, "stopped", "reason", "exception") ||
                     has_event(config_result.events, "stopped", "reason", "exception") ||
                     wait_for_event(proc, "stopped", "reason", "exception");
    ASSERT_TRUE(found_exc);

    // Get exception info — should report breakMode.
    JsonValue::ObjectType info_args;
    info_args["threadId"] = JsonValue(1);
    proc.send_message(make_request("exceptionInfo", JsonValue(std::move(info_args))));
    auto info_result = read_until_response(proc, "exceptionInfo");
    ASSERT_SUCCESS(info_result.response);

    auto& body = info_result.response["body"];
    ASSERT_TRUE(body.has("exceptionId"));
    ASSERT_TRUE(body.has("breakMode"));

    // breakMode should be either "always" or "unhandled".
    auto break_mode = body["breakMode"].as_string();
    ASSERT_TRUE(break_mode == "always" || break_mode == "unhandled");

    // Should have a description.
    if (body.has("description")) {
        ASSERT_FALSE(body["description"].as_string().empty());
    }

    (void)continue_execution(proc);

    disconnect(proc);
}

// ─── Concurrent tasks: threads listing ─────────────────────────────

void test_concurrent_tasks_threads() {
    DapProcess proc;
    initialize(proc);

    // Launch the concurrent tasks program with stopOnEntry.
    (void)launch_program(proc, "concurrent_tasks.luma", true);
    auto config_result = send_configuration_done(proc);

    bool found_entry = has_event(config_result.events, "stopped", "reason", "entry") ||
                       wait_for_event(proc, "stopped", "reason", "entry");
    ASSERT_TRUE(found_entry);

    // Get threads — should have at least the main thread.
    proc.send_message(make_request("threads"));
    auto threads_result = read_until_response(proc, "threads");
    ASSERT_SUCCESS(threads_result.response);

    auto& threads = threads_result.response["body"]["threads"].as_array();
    ASSERT_GE(threads.size(), 1U);

    // Main thread should be thread 1.
    bool found_main = false;

    for (const auto& t : threads) {
        if (t["id"].as_integer() == 1) {
            found_main = true;
        }
    }

    ASSERT_TRUE(found_main);

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// Concurrency stress regression guard for the task-VM dangling-pointer window
// (B06). Each spawned task builds and tears down its own VM; the task exit hook
// nulls that task's ThreadState::vm. The exit guard is now declared *after*
// task_vm (so it is destroyed *first*), nulling the pointer while the VM is
// still alive. Previously the guard was destroyed after task_vm, so a
// protocol-thread stackTrace/scopes/variables landing in that window
// dereferenced a freed VM (use-after-free). This test drives the exact
// scenario — many short-lived tasks inspected while they finish — so the
// adapter must stay responsive and reach a clean `terminated`. The
// use-after-free is surfaced deterministically only under ASan/TSan (the CI
// sanitizer job); locally this guards against crashes and hangs.
void test_task_spawn_exit_stress() {
    DapProcess proc;
    initialize(proc);

    // Free-running launch so tasks spawn and exit concurrently with the
    // inspection requests issued below.
    (void)launch_program(proc, "task_stress.luma");
    auto config_result = send_configuration_done(proc);

    bool terminated = has_event(config_result.events, "terminated");

    for (int round = 0; round < 500 && !terminated; ++round) {
        auto threads = poll_request(proc, "threads");

        if (threads.terminated) {
            terminated = true;
            break;
        }

        if (!threads.got_response || !threads.response.has("body") ||
            !threads.response["body"].has("threads")) {
            continue;
        }

        for (const auto& thread : threads.response["body"]["threads"].as_array()) {
            const int thread_id = static_cast<int>(thread["id"].as_integer());

            // stackTrace forces resolve_vm on a possibly-exiting task thread.
            JsonValue::ObjectType st_args;
            st_args["threadId"] = JsonValue(thread_id);
            auto stack = poll_request(proc, "stackTrace", JsonValue(std::move(st_args)));

            if (stack.terminated) {
                terminated = true;
                break;
            }

            if (!stack.got_response || !stack.response.has("body") ||
                !stack.response["body"].has("stackFrames")) {
                continue;
            }

            const auto& frames = stack.response["body"]["stackFrames"].as_array();

            if (frames.empty()) {
                continue;
            }

            // scopes + variables drive the VMIntrospector dereference directly.
            const int frame_id = static_cast<int>(frames[0]["id"].as_integer());
            JsonValue::ObjectType scope_args;
            scope_args["frameId"] = JsonValue(frame_id);
            auto scopes = poll_request(proc, "scopes", JsonValue(std::move(scope_args)));

            if (scopes.terminated) {
                terminated = true;
                break;
            }

            if (!scopes.got_response || !scopes.response.has("body") ||
                !scopes.response["body"].has("scopes")) {
                continue;
            }

            for (const auto& scope : scopes.response["body"]["scopes"].as_array()) {
                const int variables_reference =
                    static_cast<int>(scope["variablesReference"].as_integer());

                if (variables_reference <= 0) {
                    continue;
                }

                JsonValue::ObjectType var_args;
                var_args["variablesReference"] = JsonValue(variables_reference);
                auto variables = poll_request(proc, "variables", JsonValue(std::move(var_args)));

                if (variables.terminated) {
                    terminated = true;
                    break;
                }
            }

            if (terminated) {
                break;
            }
        }
    }

    if (!terminated) {
        terminated = wait_for_event(proc, "terminated", "", "", 300, 100);
    }

    // The debuggee must run to a clean finish: no adapter crash, no hang.
    ASSERT_TRUE(terminated);

    disconnect(proc);
}

void test_data_breakpoint() {
    DapProcess proc;
    initialize(proc);

    // Launch with stopOnEntry so we can set data breakpoints while paused.
    auto launch_result = launch_program(proc, "data_breakpoint.luma", true);
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);

    bool found_entry = has_event(launch_result.events, "stopped", "reason", "entry") ||
                       has_event(config_result.events, "stopped", "reason", "entry") ||
                       wait_for_event(proc, "stopped", "reason", "entry");
    ASSERT_TRUE(found_entry);

    // Query dataBreakpointInfo for the variable "counter".
    JsonValue::ObjectType info_args;
    info_args["name"] = JsonValue(std::string("counter"));
    proc.send_message(make_request("dataBreakpointInfo", JsonValue(std::move(info_args))));
    auto info_result = read_until_response(proc, "dataBreakpointInfo");
    ASSERT_SUCCESS(info_result.response);

    auto& info_body = info_result.response["body"];
    ASSERT_TRUE(info_body.has("dataId"));
    ASSERT_TRUE(info_body["dataId"].is_string());
    ASSERT_TRUE(info_body.has("accessTypes"));

    auto data_id = info_body["dataId"].as_string();

    // Set a data breakpoint on "counter".
    JsonValue::ArrayType data_bps;
    JsonValue::ObjectType data_bp;
    data_bp["dataId"] = JsonValue(data_id);
    data_bp["accessType"] = JsonValue(std::string("write"));
    data_bps.push_back(JsonValue(std::move(data_bp)));

    JsonValue::ObjectType set_data_args;
    set_data_args["breakpoints"] = JsonValue(std::move(data_bps));
    proc.send_message(make_request("setDataBreakpoints", JsonValue(std::move(set_data_args))));
    auto set_data_result = read_until_response(proc, "setDataBreakpoints");
    ASSERT_SUCCESS(set_data_result.response);

    // Verify the breakpoint was registered.
    auto& data_bp_list = set_data_result.response["body"]["breakpoints"].as_array();
    ASSERT_FALSE(data_bp_list.empty());
    ASSERT_TRUE(data_bp_list[0]["verified"].as_bool());

    // Continue — should stop on data breakpoint when counter is written.
    auto cont_result = continue_execution(proc);

    bool found_data_bp = has_event(cont_result.events, "stopped", "reason", "data breakpoint") ||
                         wait_for_event(proc, "stopped", "reason", "data breakpoint", 100, 200);
    ASSERT_TRUE(found_data_bp);

    // Continue past the data breakpoint to termination.
    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// Data breakpoints must survive session recreation (B02). Set a data breakpoint,
// trigger luma/hotReload (which resets and relaunches the session), and confirm
// the reloaded program still stops on the watched write. Line, function, and
// exception breakpoints were mirrored into pending state and reapplied on reload,
// but data breakpoints were not, so they were silently dropped and the reloaded
// program ran straight to termination.
void test_data_breakpoint_survives_hot_reload() {
    DapProcess proc;
    initialize(proc);

    // Launch stopped on entry so we can register the data breakpoint while paused.
    auto launch_result = launch_program(proc, "data_breakpoint.luma", true);
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);

    bool found_entry = has_event(launch_result.events, "stopped", "reason", "entry") ||
                       has_event(config_result.events, "stopped", "reason", "entry") ||
                       wait_for_event(proc, "stopped", "reason", "entry");
    ASSERT_TRUE(found_entry);

    // Resolve the dataId for "counter", then set a write breakpoint on it.
    JsonValue::ObjectType info_args;
    info_args["name"] = JsonValue(std::string("counter"));
    proc.send_message(make_request("dataBreakpointInfo", JsonValue(std::move(info_args))));
    auto info_result = read_until_response(proc, "dataBreakpointInfo");
    ASSERT_SUCCESS(info_result.response);
    auto data_id = info_result.response["body"]["dataId"].as_string();

    JsonValue::ArrayType data_bps;
    JsonValue::ObjectType data_bp;
    data_bp["dataId"] = JsonValue(data_id);
    data_bp["accessType"] = JsonValue(std::string("write"));
    data_bps.push_back(JsonValue(std::move(data_bp)));
    JsonValue::ObjectType set_data_args;
    set_data_args["breakpoints"] = JsonValue(std::move(data_bps));
    proc.send_message(make_request("setDataBreakpoints", JsonValue(std::move(set_data_args))));
    auto set_data_result = read_until_response(proc, "setDataBreakpoints");
    ASSERT_SUCCESS(set_data_result.response);

    // Hot reload tears down the session (and its breakpoint manager) and relaunches
    // free-running.  The mirrored data breakpoint must be reapplied to the new
    // session so the reloaded program still stops on the write to "counter".
    proc.send_message(make_request("luma/hotReload"));
    auto reload_result = read_until_response(proc, "luma/hotReload");
    ASSERT_SUCCESS(reload_result.response);

    bool found_data_bp = has_event(reload_result.events, "stopped", "reason", "data breakpoint") ||
                         wait_for_event(proc, "stopped", "reason", "data breakpoint", 200, 100);
    ASSERT_TRUE(found_data_bp);

    // Drive the reloaded program to a clean finish.
    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// Time-travel + concurrency race regression guard (B01). With timeTravel the main
// VM's line hook is propagated into spawned task VMs; if the hook captured the main
// VM by reference, a task worker thread would snapshot the main thread's stack
// concurrently — a data race on the main VM.  Each thread must record against its
// own VM. The program is driven free-running to a clean `terminated`; the CI
// sanitizer job surfaces the race deterministically, and locally this guards
// against crashes and hangs.
void test_time_travel_concurrent_tasks() {
    DapProcess proc;
    initialize(proc);

    auto launch_result = launch_with_time_travel(proc, "time_travel_tasks.luma");
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);

    bool terminated = has_event(launch_result.events, "terminated") ||
                      has_event(config_result.events, "terminated") ||
                      wait_for_event(proc, "terminated", "", "", 300, 100);
    ASSERT_TRUE(terminated);

    disconnect(proc);
}

// Reverse stepping must invalidate cached watch state (B03). The forward
// continue/step path calls invalidate_watches()/invalidate_refs() before
// resuming, emitting an `invalidated` event so the client refetches variables;
// the time-travel path previously only emitted `stopped`, so a rewind left the
// watch cache and generational references stale.  After the fix a stepBack
// flushes them and emits `invalidated`, exactly like the forward path.
void test_step_back_invalidates_watch_state() {
    DapProcess proc;
    initialize(proc);

    auto program_path = example_path("conditional_loop.luma");

    // Stop a few iterations into the loop so there is recorded history to rewind
    // and `total` has changed, then launch with time travel enabled.
    auto bp_result =
        set_conditional_breakpoint(proc, program_path, test_lines::conditional_loop_body, "", "5");
    ASSERT_SUCCESS(bp_result.response);

    auto launch_result = launch_with_time_travel(proc, "conditional_loop.luma");
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(launch_result.events, "stopped", "reason", "breakpoint") ||
                    has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    // Resolve a frame and read a watch expression so references/watch entries exist.
    auto st_result = get_stack_trace(proc);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    JsonValue::ObjectType eval_args;
    eval_args["expression"] = JsonValue(std::string("total"));
    eval_args["frameId"] = JsonValue(static_cast<int>(frame_id));
    eval_args["context"] = JsonValue(std::string("watch"));
    proc.send_message(make_request("evaluate", JsonValue(std::move(eval_args))));
    auto eval_result = read_until_response(proc, "evaluate");
    ASSERT_SUCCESS(eval_result.response);

    // Step back one snapshot: the fix flushes the watch cache/refs and emits an
    // `invalidated` event (before the stopped event) just like the forward path.
    JsonValue::ObjectType step_args;
    step_args["threadId"] = JsonValue(1);
    proc.send_message(make_request("stepBack", JsonValue(std::move(step_args))));
    auto step_result = read_until_response(proc, "stepBack");
    ASSERT_SUCCESS(step_result.response);

    bool invalidated = has_event(step_result.events, "invalidated") ||
                       wait_for_event(proc, "invalidated", "", "", 50, 100);
    ASSERT_TRUE(invalidated);

    disconnect(proc);
}

// ─── Breakpoint locations integration test ─────────────────────────
void test_breakpoint_locations() {
    DapProcess proc;
    initialize(proc);

    auto program_path = example_path("conditional_loop.luma");

    // Launch the program to get source maps loaded.
    auto launch_result = launch_program(proc, "conditional_loop.luma", true);
    ASSERT_SUCCESS(launch_result.response);

    auto config_result = send_configuration_done(proc);

    bool found_entry = has_event(launch_result.events, "stopped", "reason", "entry") ||
                       has_event(config_result.events, "stopped", "reason", "entry") ||
                       wait_for_event(proc, "stopped", "reason", "entry");
    ASSERT_TRUE(found_entry);

    // Query breakpoint locations for lines 4-10.
    JsonValue::ObjectType source;
    source["path"] = JsonValue(program_path);

    JsonValue::ObjectType loc_args;
    loc_args["source"] = JsonValue(std::move(source));
    loc_args["line"] = JsonValue(test_lines::conditional_loop_range_start);
    loc_args["endLine"] = JsonValue(test_lines::conditional_loop_range_end);
    proc.send_message(make_request("breakpointLocations", JsonValue(std::move(loc_args))));
    auto loc_result = read_until_response(proc, "breakpointLocations");
    ASSERT_SUCCESS(loc_result.response);

    // Should return at least one breakpoint location.
    ASSERT_TRUE(loc_result.response["body"].has("breakpoints"));
    auto& locations = loc_result.response["body"]["breakpoints"].as_array();
    ASSERT_FALSE(locations.empty());

    // Each location should have a line number.
    for (const auto& loc : locations) {
        ASSERT_TRUE(loc.has("line"));
        ASSERT_TRUE(loc["line"].is_integer());
    }

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Caught exception integration test ─────────────────────────────

void test_caught_exception() {
    DapProcess proc;
    initialize(proc);

    // Enable only the 'caught' filter — the try/catch should pause the
    // debugger at the throw site before the handler runs.
    JsonValue::ArrayType filters;
    filters.push_back(JsonValue(std::string("caught")));

    JsonValue::ObjectType exc_args;
    exc_args["filters"] = JsonValue(std::move(filters));
    proc.send_message(make_request("setExceptionBreakpoints", JsonValue(std::move(exc_args))));
    (void)read_until_response(proc, "setExceptionBreakpoints");

    auto launch_result = launch_program(proc, "exception_caught.luma");
    auto config_result = send_configuration_done(proc);

    // The caught division-by-zero should pause with reason "exception".
    bool found_exc = has_event(launch_result.events, "stopped", "reason", "exception") ||
                     has_event(config_result.events, "stopped", "reason", "exception") ||
                     wait_for_event(proc, "stopped", "reason", "exception");
    ASSERT_TRUE(found_exc);

    // Exception info should be available while paused on the caught exception.
    JsonValue::ObjectType info_args;
    info_args["threadId"] = JsonValue(1);
    proc.send_message(make_request("exceptionInfo", JsonValue(std::move(info_args))));
    auto info_result = read_until_response(proc, "exceptionInfo");
    ASSERT_SUCCESS(info_result.response);
    ASSERT_TRUE(info_result.response["body"].has("breakMode"));

    // Continue — the catch block handles the error and the program ends.
    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Closure scope integration test ────────────────────────────────

void test_closure_variables() {
    DapProcess proc;
    initialize(proc);

    // Break inside the lambda body, where 'base' is a captured upvalue.
    auto program_path = example_path("closure_variables.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::closure_lambda_body);

    (void)launch_program(proc, "closure_variables.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    // The lambda frame should expose a dedicated Closure scope.
    auto st_result = get_stack_trace(proc);
    ASSERT_SUCCESS(st_result.response);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    JsonValue::ObjectType scopes_args;
    scopes_args["frameId"] = JsonValue(static_cast<int>(frame_id));
    proc.send_message(make_request("scopes", JsonValue(std::move(scopes_args))));
    auto scopes_result = read_until_response(proc, "scopes");
    ASSERT_SUCCESS(scopes_result.response);

    int closure_ref = 0;

    for (const auto& scope : scopes_result.response["body"]["scopes"].as_array()) {
        if (scope["name"].as_string() == "Closure") {
            closure_ref = static_cast<int>(scope["variablesReference"].as_integer());
        }
    }

    ASSERT_GT(closure_ref, 0);

    // The captured 'base' upvalue should appear in the Closure scope.
    auto vars_result = get_variables(proc, closure_ref);
    ASSERT_SUCCESS(vars_result.response);

    bool found_base = false;

    for (const auto& var : vars_result.response["body"]["variables"].as_array()) {
        if (var["name"].as_string() == "base") {
            found_base = true;
        }
    }

    ASSERT_TRUE(found_base);

    // The lambda is invoked twice, so the breakpoint may fire again.
    (void)continue_execution(proc);
    (void)wait_for_event(proc, "stopped", "", "", 20, 100);
    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Recursive call stack integration test ─────────────────────────

void test_recursive_stack() {
    DapProcess proc;
    initialize(proc);

    // Break on the base case, reached at the deepest recursion level.
    auto program_path = example_path("recursive_stack.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::recursive_base_case);

    (void)launch_program(proc, "recursive_stack.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    // factorial(5) recursion should yield a deep multi-frame stack
    // (main plus factorial called for 5, 4, 3, 2, 1).
    auto st_result = get_stack_trace(proc);
    ASSERT_SUCCESS(st_result.response);

    auto& frames = st_result.response["body"]["stackFrames"].as_array();
    ASSERT_GE(frames.size(), 5U);

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

// ─── Structured value inspection integration test ──────────────────

void test_structured_values() {
    DapProcess proc;
    initialize(proc);

    // Break after the record and choice values are constructed.
    auto program_path = example_path("structured_values.luma");
    (void)set_line_breakpoint(proc, program_path, test_lines::structured_values_stop);

    (void)launch_program(proc, "structured_values.luma");
    auto config_result = send_configuration_done(proc);

    bool found_bp = has_event(config_result.events, "stopped", "reason", "breakpoint") ||
                    wait_for_event(proc, "stopped", "reason", "breakpoint");
    ASSERT_TRUE(found_bp);

    auto st_result = get_stack_trace(proc);
    ASSERT_SUCCESS(st_result.response);
    auto frame_id = st_result.response["body"]["stackFrames"].as_array()[0]["id"].as_integer();

    JsonValue::ObjectType scopes_args;
    scopes_args["frameId"] = JsonValue(static_cast<int>(frame_id));
    proc.send_message(make_request("scopes", JsonValue(std::move(scopes_args))));
    auto scopes_result = read_until_response(proc, "scopes");
    ASSERT_SUCCESS(scopes_result.response);

    auto local_ref =
        scopes_result.response["body"]["scopes"].as_array()[0]["variablesReference"].as_integer();

    // The local scope should hold an expandable structured variable
    // (a record or choice value with a non-zero variablesReference).
    auto locals_result = get_variables(proc, static_cast<int>(local_ref));
    ASSERT_SUCCESS(locals_result.response);

    int structured_ref = 0;

    for (const auto& var : locals_result.response["body"]["variables"].as_array()) {
        if (var.has("variablesReference") && var["variablesReference"].as_integer() != 0) {
            structured_ref = static_cast<int>(var["variablesReference"].as_integer());
            break;
        }
    }

    ASSERT_GT(structured_ref, 0);

    // Expanding the structured value should reveal named child fields.
    auto children_result = get_variables(proc, structured_ref);
    ASSERT_SUCCESS(children_result.response);
    ASSERT_FALSE(children_result.response["body"]["variables"].as_array().empty());

    (void)continue_execution(proc);
    (void)wait_for_event(proc, "terminated");

    disconnect(proc);
}

} // namespace

// ─── Entry point ───────────────────────────────────────────────────

// This integration harness performs filesystem discovery and child-process I/O
// that can legitimately throw; an exception escaping main simply aborts the test
// run with a non-zero status, which the test runner treats as a failure.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int /*argc*/, char* argv[]) {
#ifndef _WIN32
    // The tests write DAP requests to the luma_dap child's stdin pipe. If the
    // child has already exited (e.g. after a terminate/disconnect handshake),
    // that write delivers SIGPIPE, whose default disposition would kill the
    // whole test binary before ::write can return EPIPE. Ignore it so the
    // failed write instead surfaces as a normal error for that one case.
    ::signal(SIGPIPE, SIG_IGN);
#endif
    std::cout << "=== DAP Integration Tests ===\n\n";

    // Determine paths based on executable location.
    std::filesystem::path exe_dir = std::filesystem::path(argv[0]).parent_path();

    // luma_dap may be in the same directory (single-config generators)
    // or in a sibling build directory (multi-config generators like MSVC).
#ifdef _WIN32
    constexpr auto dap_name = "luma_dap.exe";
#else
    constexpr auto dap_name = "luma_dap";
#endif

    // Search candidates: same dir, then sibling debugger dirs for multi-config.
    for (const auto& candidate :
         {exe_dir / dap_name,
          exe_dir.parent_path().parent_path() / "debugger" / exe_dir.filename() / dap_name,
          exe_dir.parent_path() / "debugger" / dap_name}) {
        if (std::filesystem::exists(candidate)) {
            dap_exe_path = candidate.string();
            break;
        }
    }

    if (dap_exe_path.empty() || !std::filesystem::exists(dap_exe_path)) {
        std::cerr << "luma_dap not found (searched from: " << exe_dir.string() << ")\n";
        return 1;
    }

    // Find examples/debug/ directory.
    auto workspace_root = exe_dir.parent_path(); // build/Release -> build -> workspace

    // Try a few possible locations.
    for (const auto& candidate :
         {workspace_root / "examples" / "debug",
          workspace_root.parent_path() / "examples" / "debug",
          workspace_root.parent_path().parent_path() / "examples" / "debug"}) {
        if (std::filesystem::exists(candidate)) {
            debug_examples_dir = candidate.string();
            break;
        }
    }

    if (debug_examples_dir.empty()) {
        std::cerr << "examples/debug/ directory not found\n";
        return 1;
    }

    std::cout << "DAP executable: " << dap_exe_path << "\n";
    std::cout << "Debug examples: " << debug_examples_dir << "\n\n";

    // Run integration tests.
    RUN(test_initialize_capabilities);
    RUN(test_launch_and_terminate);
    RUN(test_launch_compile_error);
    RUN(test_stop_on_entry);
    RUN(test_breakpoint_stop_and_variables);
    RUN(test_exception_breakpoint);
    RUN(test_continue_after_unhandled_exception);
    RUN(test_loaded_sources);
    RUN(test_step_over);
    RUN(test_step_into);
    RUN(test_step_out);
    RUN(test_evaluate_expression);
    RUN(test_evaluate_compound_expression);
    RUN(test_evaluate_global_reference);
    RUN(test_evaluate_timeout_does_not_hang);
    RUN(test_no_debug_launch);

    // Conditional and hit-condition breakpoints.
    RUN(test_conditional_breakpoint);
    RUN(test_hit_condition_breakpoint);
    RUN(test_conditional_and_hit_condition_breakpoint);

    // Function breakpoints.
    RUN(test_function_breakpoint);

    // Set variable.
    RUN(test_set_variable);
    RUN(test_set_variable_respects_type);

    // Restart.
    RUN(test_restart);

    // Log points.
    RUN(test_log_point);

    // Pause.
    RUN(test_pause);

    // Completions.
    RUN(test_completions);

    // Exception info.
    RUN(test_exception_info_caught);

    // Concurrent tasks threads.
    RUN(test_concurrent_tasks_threads);
    RUN(test_task_spawn_exit_stress);

    // Data breakpoints.
    RUN(test_data_breakpoint);
    RUN(test_data_breakpoint_survives_hot_reload);

    // Time-travel debugging.
    RUN(test_time_travel_concurrent_tasks);
    RUN(test_step_back_invalidates_watch_state);

    // Breakpoint locations.
    RUN(test_breakpoint_locations);

    // Caught exceptions, closures, recursion, and structured values.
    RUN(test_caught_exception);
    RUN(test_closure_variables);
    RUN(test_recursive_stack);
    RUN(test_structured_values);

    return SUMMARY();
}
