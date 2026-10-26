// POSIX implementations of the platform_process primitives.
// Compiled only on non-Windows platforms (see core/runtime/CMakeLists.txt).

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "common/resource_limits.hpp"
#include "runtime/stdlib/system/platform_process.hpp"
#include "runtime/stdlib/system/process_module.hpp"

// The POSIX process environment is exposed through the global `environ`;
// declare it at namespace-global scope so it is not mistaken for a
// `luma::environ` when referenced from within the luma namespace below.
extern char** environ;

namespace luma::platform_process {

namespace {

// How often run_command_timeout re-checks a child whose output streams already
// reached EOF but which has not exited yet.
constexpr std::chrono::milliseconds k_exit_poll_interval{10};

// RAII owner for a file descriptor.  Every pipe end in this file is held by one,
// so no early return (or a throw from an allocation) can leak a descriptor.
class UniqueFd {
public:
    UniqueFd() = default;

    explicit UniqueFd(int fd) noexcept : fd_{fd} {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_{std::exchange(other.fd_, -1)} {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }

        return *this;
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }

        fd_ = fd;
    }

private:
    int fd_{-1};
};

struct Pipe {
    UniqueFd read;
    UniqueFd write;
};

// Create a pipe whose BOTH ends are close-on-exec, so neither end leaks into a
// child spawned concurrently by another thread (which would keep our reader
// from ever seeing EOF).  The child's own stdout/stderr are created by dup2,
// which clears the flag on the duplicate, so redirection still works.  pipe2
// sets the flag atomically where available; elsewhere (macOS) it is set right
// after pipe(), leaving only a tiny race window.
[[nodiscard]] bool make_cloexec_pipe(Pipe& pipe) {
    std::array<int, 2> fds{-1, -1};

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    if (::pipe2(fds.data(), O_CLOEXEC) == -1) {
        return false;
    }
#else
    if (::pipe(fds.data()) == -1) {
        return false;
    }

    for (const int fd : fds) {
        ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
    }
#endif

    pipe.read.reset(fds[0]);
    pipe.write.reset(fds[1]);

    return true;
}

// Open /dev/null read-only and close-on-exec, at a descriptor above 2 so the
// child's stdout / stderr dup2 calls can never overwrite it.  Opened in the
// parent before fork: the child may only make async-signal-safe calls, and
// doing the open there would also hide its failure.  O_CLOEXEC and
// F_DUPFD_CLOEXEC are POSIX.1-2008 and available on Linux, macOS, and the BSDs.
[[nodiscard]] UniqueFd open_dev_null_for_stdin() {
    constexpr int k_first_non_standard_fd{3};

    UniqueFd fd{::open("/dev/null", O_RDONLY | O_CLOEXEC)};

    if (fd.get() >= 0 && fd.get() < k_first_non_standard_fd) {
        fd = UniqueFd{::fcntl(fd.get(), F_DUPFD_CLOEXEC, k_first_non_standard_fd)};
    }

    return fd;
}

// Build the NULL-terminated argv array BEFORE forking: operator new / malloc is
// not async-signal-safe, so it must not run in the child between fork() and
// execvp().  In a multithreaded interpreter (the thread pool runs Process.* from
// worker tasks) another thread may hold the allocator lock at the instant of the
// fork, which would deadlock the child's allocation forever.  The pointers
// reference `argv_strings`, which the child inherits at identical addresses via
// copy-on-write, so they stay valid after the fork.
[[nodiscard]] std::vector<char*> make_argv(std::vector<std::string>& argv_strings) {
    std::vector<char*> argv{};
    argv.reserve(argv_strings.size() + 1);

    for (auto& s : argv_strings) {
        argv.push_back(s.data());
    }

    argv.push_back(nullptr);

    return argv;
}

// What exec_child needs to set up the child; filled in by the parent before
// fork, so the child only reads it.
struct ChildLaunch {
    int out_fd{-1};
    int err_fd{-1};
    int exec_err_fd{-1};

    // When >= 0, becomes the child's stdin.  The parent opens /dev/null
    // (O_CLOEXEC, and above fd 2) before fork, so the child makes no open()
    // call; dup2 clears the close-on-exec flag on the duplicate.
    int stdin_fd{-1};

    // Lead a new process group so a timeout can SIGKILL the whole tree.  Such
    // a child is a background job: Ctrl+C at the terminal no longer reaches it,
    // and reading the terminal would stop it with SIGTTIN — hence stdin_fd and
    // the parent-death signal below.
    bool new_process_group{false};

    // The launching process; the child exits at once if it has already been
    // re-parented (the parent died before prctl took effect).
    pid_t parent_pid{-1};
};

// Child side of a launch: redirect stdin / stdout / stderr and exec directly
// (no shell, so no command injection).  Only async-signal-safe calls are made
// here.  On an execvp failure the errno is reported to the parent through
// `exec_err_fd`, whose close-on-exec flag makes a SUCCESSFUL exec close it
// instead — so the parent's read returns EOF on success and the errno on
// failure.  Every other pipe end is close-on-exec and needs no explicit close.
[[noreturn]] void exec_child(char* const* argv, const ChildLaunch& launch) noexcept {
    if (launch.new_process_group) {
        (void)::setpgid(0, 0);

#ifdef __linux__
        // Out of the terminal's foreground group, the child would outlive an
        // interpreter killed by Ctrl+C; have the kernel SIGKILL it when the
        // parent dies instead.  PR_SET_PDEATHSIG fires when the forking
        // *thread* exits, which is safe here: that thread blocks in
        // execute_argv_captured_impl until the child is reaped or killed.
        // macOS (and the BSDs) have no PDEATHSIG; there the deadline-driven
        // group kill is the only bound on the child's lifetime.
        (void)::prctl(PR_SET_PDEATHSIG, SIGKILL);

        // The parent may have died between fork() and prctl(), in which case
        // the signal will never come; getppid() then no longer names it.
        if (::getppid() != launch.parent_pid) {
            ::_exit(127);
        }
#endif
    }

    ::dup2(launch.out_fd, STDOUT_FILENO);
    ::dup2(launch.err_fd, STDERR_FILENO);

    // dup2 clears FD_CLOEXEC on the duplicate, except when the source already IS
    // the target descriptor (dup2 is then a no-op); clear it explicitly so the
    // redirected streams always survive the exec.
    ::fcntl(STDOUT_FILENO, F_SETFD, 0);
    ::fcntl(STDERR_FILENO, F_SETFD, 0);

    // Redirected last: if the parent ran with fd 0 closed, an output pipe may
    // itself be fd 0, and it has been duplicated onto 1 / 2 by now.  stdin_fd
    // is kept above 2 by the parent, so the dup2 calls above cannot clobber it.
    if (launch.stdin_fd >= 0) {
        ::dup2(launch.stdin_fd, STDIN_FILENO);
    }

    ::execvp(argv[0], argv);

    // execvp only returns on failure.  write() is async-signal-safe.
    const int exec_errno = errno;
    const ssize_t written = ::write(launch.exec_err_fd, &exec_errno, sizeof(exec_errno));

    (void)written;

    ::_exit(127);
}

// Parent side of the exec-error self-pipe: returns the child's execvp errno, or
// nullopt when the exec succeeded (EOF).  Closes the read end.
[[nodiscard]] std::optional<int> read_exec_errno(UniqueFd& exec_err_read) {
    int child_errno{0};
    ssize_t n{0};

    do {
        n = ::read(exec_err_read.get(), &child_errno, sizeof(child_errno));
    } while (n == -1 && errno == EINTR);

    exec_err_read.reset();

    if (n == static_cast<ssize_t>(sizeof(child_errno))) {
        return child_errno;
    }

    return std::nullopt;
}

// Block until `pid` has been reaped, retrying on EINTR.
void reap(pid_t pid, int* status) {
    while (::waitpid(pid, status, 0) == -1 && errno == EINTR) {}
}

// SIGKILL the child — and, when it leads its own process group, every process
// in that group (its descendants), mirroring the Windows Job Object teardown.
void kill_child(pid_t pid, bool process_group) {
    if (process_group) {
        (void)::kill(-pid, SIGKILL);
    }

    (void)::kill(pid, SIGKILL);
}

[[nodiscard]] int exit_code_from_status(int status) {
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

// POSIX implementation: fork + execvp with one pipe shared by stdout/stderr.
std::pair<int, std::string> execute_command(std::string_view cmd) {
    // Tokenize before acquiring any pipe or forking, so an unclosed-quote error
    // (which tokenize_command throws) is reported cleanly in the parent and
    // cannot leak a descriptor.
    auto argv_strings = tokenize_command(cmd);

    if (argv_strings.empty()) {
        return {-1, ""};
    }

    auto argv = make_argv(argv_strings);

    Pipe output{};
    Pipe exec_err{};

    if (!make_cloexec_pipe(output) || !make_cloexec_pipe(exec_err)) {
        return {-1, ""};
    }

    const pid_t pid = ::fork();

    if (pid == -1) {
        return {-1, ""};
    }

    if (pid == 0) {
        exec_child(argv.data(), ChildLaunch{.out_fd = output.write.get(),
                                            .err_fd = output.write.get(),
                                            .exec_err_fd = exec_err.write.get(),
                                            .stdin_fd = -1,
                                            .new_process_group = false,
                                            .parent_pid = -1});
    }

    // Parent: drop our copies of the write ends so reads see EOF once the child
    // (and anything it spawned) is done.
    output.write.reset();
    exec_err.write.reset();

    // A failed exec means the command could not be launched: report it as a
    // launch failure (negative exit code) rather than surfacing the child's 127
    // exit as a successful run, matching the Windows CreateProcessA path.
    if (read_exec_errno(exec_err.read)) {
        reap(pid, nullptr);

        return {-1, ""};
    }

    std::string out_text{};
    std::array<char, k_pipe_buffer_size> buffer{};

    while (true) {
        const ssize_t n = ::read(output.read.get(), buffer.data(), buffer.size());

        if (n == -1 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            break;
        }

        if (out_text.size() + static_cast<std::size_t>(n) >
            ResourceLimits::max_process_output_size) {
            output.read.reset();
            kill_child(pid, false);
            reap(pid, nullptr);

            return {-1, ""};
        }

        out_text.append(buffer.data(), static_cast<std::size_t>(n));
    }

    output.read.reset();

    int status{0};

    reap(pid, &status);

    return {exit_code_from_status(status), std::move(out_text)};
}

// POSIX implementation: fork + execvp with two pipes so stdout and stderr are
// captured separately.  Both read ends are drained via poll() to avoid a
// deadlock when the child fills one pipe while we block reading the other.
CapturedOutput execute_command_captured(std::string_view cmd) {
    // Tokenize before acquiring any pipes so an unclosed-quote error (which
    // tokenize_command throws) surfaces cleanly in the parent without leaking
    // pipe descriptors, then hand the argv vector to the shared implementation.
    return execute_argv_captured(tokenize_command(cmd));
}

// POSIX implementation shared by execute_command_captured (which tokenizes a
// command string first) and Process.run_command (which supplies an explicit,
// un-tokenized argv).  When `timeout_ms` is set, the child leads its own process
// group (with /dev/null as stdin and, on Linux, a parent-death SIGKILL), every
// wait (the output drain AND the final wait for exit) is bounded by
// the deadline, and on expiry the whole group is SIGKILLed and a CapturedOutput
// with `timed_out == true` is returned.
namespace {

CapturedOutput execute_argv_captured_impl(std::vector<std::string> argv_strings,
                                          std::optional<std::int64_t> timeout_ms) {
    if (argv_strings.empty()) {
        return {};
    }

    auto argv = make_argv(argv_strings);

    const bool own_group = timeout_ms.has_value();

    Pipe out_pipe{};
    Pipe err_pipe{};
    Pipe exec_err{};

    if (!make_cloexec_pipe(out_pipe) || !make_cloexec_pipe(err_pipe) ||
        !make_cloexec_pipe(exec_err)) {
        return {};
    }

    // A timeout child runs in its own (background) process group, where reading
    // the terminal would stop it with SIGTTIN until the deadline kills it; give
    // it /dev/null as stdin instead.
    UniqueFd null_stdin{};

    if (own_group) {
        null_stdin = open_dev_null_for_stdin();

        if (null_stdin.get() < 0) {
            return {};
        }
    }

    const pid_t parent_pid = ::getpid();
    const pid_t pid = ::fork();

    if (pid == -1) {
        return {};
    }

    if (pid == 0) {
        exec_child(argv.data(), ChildLaunch{.out_fd = out_pipe.write.get(),
                                            .err_fd = err_pipe.write.get(),
                                            .exec_err_fd = exec_err.write.get(),
                                            .stdin_fd = null_stdin.get(),
                                            .new_process_group = own_group,
                                            .parent_pid = parent_pid});
    }

    null_stdin.reset();

    if (own_group) {
        // Also set the group from the parent, closing the race in which a kill
        // could be issued before the child has run its own setpgid.  Failure
        // (e.g. the child already exec'd) is harmless: the child set it itself.
        (void)::setpgid(pid, pid);
    }

    out_pipe.write.reset();
    err_pipe.write.reset();
    exec_err.write.reset();

    if (const auto child_errno = read_exec_errno(exec_err.read)) {
        reap(pid, nullptr);

        // The child's execvp failed; surface the captured errno so the caller
        // can classify the launch failure (ENOENT → NotFound, EACCES → …).
        CapturedOutput launch_failure{};
        launch_failure.launch_errno = *child_errno;

        return launch_failure;
    }

    CapturedOutput captured{};
    std::array<char, k_pipe_buffer_size> buffer{};

    const std::array<UniqueFd*, 2> streams{&out_pipe.read, &err_pipe.read};
    std::array<pollfd, 2> fds{};

    for (std::size_t i{0}; i < fds.size(); ++i) {
        fds[i] = pollfd{.fd = streams[i]->get(), .events = POLLIN, .revents = 0};
    }

    int open_streams{2};
    bool overflow{false};
    bool timed_out{false};

    const std::optional<std::chrono::steady_clock::time_point> deadline =
        timeout_ms ? std::optional{std::chrono::steady_clock::now() +
                                   std::chrono::milliseconds(*timeout_ms)}
                   : std::nullopt;

    const auto retire = [&](std::size_t i) {
        streams[i]->reset();
        fds[i].fd = -1;
        --open_streams;
    };

    while (open_streams > 0 && !overflow) {
        // Bound each poll by the time left until the deadline; an infinite wait
        // (-1) when no timeout was requested.
        int poll_timeout{-1};

        if (deadline) {
            const auto now = std::chrono::steady_clock::now();

            if (now >= *deadline) {
                timed_out = true;

                break;
            }

            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count();
            poll_timeout = static_cast<int>(
                std::min<std::int64_t>(remaining, std::numeric_limits<int>::max()));
        }

        const int ready = ::poll(fds.data(), fds.size(), poll_timeout);

        if (ready == 0) {
            // Only reachable with a finite poll_timeout, i.e. the deadline
            // expired before the child produced more output or exited.
            timed_out = true;

            break;
        }

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            break;
        }

        for (std::size_t i{0}; i < fds.size(); ++i) {
            if (fds[i].fd < 0 || fds[i].revents == 0) {
                continue;
            }

            if ((fds[i].revents & (POLLIN | POLLHUP)) != 0) {
                const ssize_t n = ::read(fds[i].fd, buffer.data(), buffer.size());

                if (n > 0) {
                    std::string& target =
                        (i == 0) ? captured.standard_output : captured.standard_error;

                    if (target.size() + static_cast<std::size_t>(n) >
                        ResourceLimits::max_process_output_size) {
                        overflow = true;

                        break;
                    }

                    target.append(buffer.data(), static_cast<std::size_t>(n));

                    continue;
                }

                if (n == -1 && errno == EINTR) {
                    continue;
                }

                // EOF (n == 0) or read error: retire this stream.
                retire(i);
            } else if ((fds[i].revents & (POLLERR | POLLNVAL)) != 0) {
                retire(i);
            }
        }
    }

    // Close our read ends before any kill so descendants writing to them get
    // EPIPE instead of blocking.
    out_pipe.read.reset();
    err_pipe.read.reset();

    int status{0};
    bool reaped{false};

    // Both streams can reach EOF while the child keeps running (it closed or
    // redirected its stdout/stderr), so keep honouring the deadline while
    // waiting for it to exit instead of blocking in waitpid forever.
    if (deadline && !timed_out && !overflow) {
        while (true) {
            const pid_t waited = ::waitpid(pid, &status, WNOHANG);

            if (waited == pid) {
                reaped = true;

                break;
            }

            if (waited == -1 && errno != EINTR) {
                // ECHILD: nothing left to wait for.
                reaped = true;

                break;
            }

            if (std::chrono::steady_clock::now() >= *deadline) {
                timed_out = true;

                break;
            }

            std::this_thread::sleep_for(k_exit_poll_interval);
        }
    }

    if (timed_out) {
        // The child overran its deadline: kill it (and its process group) and
        // reap it so it cannot linger as a zombie, then report the timeout
        // distinctly from a launch failure.
        kill_child(pid, own_group);
        reap(pid, nullptr);

        CapturedOutput timeout_result{};
        timeout_result.timed_out = true;

        return timeout_result;
    }

    if (overflow) {
        kill_child(pid, own_group);
        reap(pid, nullptr);

        return {};
    }

    if (!reaped) {
        reap(pid, &status);
    }

    captured.exit_code = exit_code_from_status(status);

    return captured;
}

} // namespace

CapturedOutput execute_argv_captured(std::vector<std::string> argv_strings) {
    return execute_argv_captured_impl(std::move(argv_strings), std::nullopt);
}

CapturedOutput execute_argv_captured_timeout(std::vector<std::string> argv_strings,
                                             std::int64_t timeout_ms) {
    return execute_argv_captured_impl(std::move(argv_strings), timeout_ms);
}

std::int64_t current_process_id() {
    return static_cast<std::int64_t>(getpid());
}

bool send_signal(std::int64_t pid, SignalKind signal) {
    // kill(2) gives non-positive pids special meaning (0 = our own process group,
    // -1 = every process we may signal, -N = process group N), so a pid that
    // does not fit pid_t must be rejected rather than truncated: 4294967295
    // would otherwise wrap to kill(-1, …).
    if (pid <= 0 || pid > static_cast<std::int64_t>(std::numeric_limits<pid_t>::max())) {
        return false;
    }

    int sig = SIGTERM;
    switch (signal) {
        case SignalKind::Terminate:
            sig = SIGTERM;
            break;
        case SignalKind::Kill:
            sig = SIGKILL;
            break;
        case SignalKind::Interrupt:
            sig = SIGINT;
            break;
        case SignalKind::Hangup:
            sig = SIGHUP;
            break;
    }

    return ::kill(static_cast<pid_t>(pid), sig) == 0;
}

int set_environment_variable(const std::string& name, const std::string& value) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): no thread-safe std alternative for setenv.
    return setenv(name.c_str(), value.c_str(), 1);
}

std::optional<std::vector<std::pair<std::string, std::string>>> all_environment_variables() {
    std::vector<std::pair<std::string, std::string>> result;

    for (char** ep = environ; ep && *ep; ++ep) {
        const std::string entry{*ep};
        const auto eq = entry.find('=');

        if (eq == std::string::npos) {
            continue;
        }

        result.emplace_back(entry.substr(0, eq), entry.substr(eq + 1));
    }

    return result;
}

} // namespace luma::platform_process
