// POSIX implementations of the platform_process primitives.
// Compiled only on non-Windows platforms (see core/runtime/CMakeLists.txt).

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common/resource_limits.hpp"
#include "runtime/stdlib/system/platform_process.hpp"
#include "runtime/stdlib/system/process_module.hpp"

// The POSIX process environment is exposed through the global `environ`;
// declare it at namespace-global scope so it is not mistaken for a
// `luma::environ` when referenced from within the luma namespace below.
extern char** environ;

namespace luma::platform_process {

// POSIX implementation: fork + execvp with a pipe for stdout/stderr.
std::pair<int, std::string> execute_command(std::string_view cmd) {
    std::array<int, 2> pipe_fd{};

    if (pipe(pipe_fd.data()) == -1) {
        return {-1, ""};
    }

    // Tokenize before forking so that errors (e.g. unclosed quotes)
    // are reported cleanly in the parent instead of crashing the child.
    auto argv_strings = tokenize_command(cmd);

    if (argv_strings.empty()) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);

        return {-1, ""};
    }

    // Build argv before forking: operator new / malloc is not async-signal-safe,
    // so it must not run in the child between fork() and execvp(). In a
    // multithreaded interpreter (the thread pool runs Process.run from worker
    // tasks) another thread may hold the allocator lock at the instant of the
    // fork, which would deadlock the child's allocation forever. The pointers
    // reference argv_strings, which the child inherits at identical addresses
    // via copy-on-write, so they stay valid after the fork.
    std::vector<char*> argv{};
    argv.reserve(argv_strings.size() + 1);

    for (auto& s : argv_strings) {
        argv.push_back(s.data());
    }

    argv.push_back(nullptr);

    // Self-pipe to detect an execvp failure in the child.  The write end is
    // marked close-on-exec, so a successful exec closes it and the parent's read
    // returns EOF, whereas a failed exec writes errno before _exit.  This lets
    // the parent report an unknown command as a launch failure (negative exit
    // code) instead of surfacing the child's 127 exit as a successful run,
    // matching the Windows CreateProcessA path.
    std::array<int, 2> exec_err_fd{};

    if (pipe(exec_err_fd.data()) == -1) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);

        return {-1, ""};
    }

    fcntl(exec_err_fd[1], F_SETFD, fcntl(exec_err_fd[1], F_GETFD) | FD_CLOEXEC);

    const pid_t pid = fork();

    if (pid == -1) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        close(exec_err_fd[0]);
        close(exec_err_fd[1]);

        return {-1, ""};
    }

    if (pid == 0) {
        // Child -- redirect stdout/stderr to the pipe and exec directly
        // without involving a shell, to prevent command injection.  Only
        // async-signal-safe calls are permitted here (argv was built before
        // the fork above), so the child touches no allocator locks.
        close(pipe_fd[0]);
        close(exec_err_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);

        execvp(argv[0], argv.data());

        // execvp only returns on failure.  Report errno to the parent through
        // the close-on-exec pipe (write() is async-signal-safe), then exit.
        const int exec_errno = errno;
        const ssize_t written = write(exec_err_fd[1], &exec_errno, sizeof(exec_errno));

        (void)written;

        _exit(127);
    }

    // Parent — read from the pipe.
    close(pipe_fd[1]);
    close(exec_err_fd[1]);

    // A successful exec closed the child's write end (read returns EOF); a
    // failed exec delivered errno, meaning the command could not be launched.
    int child_exec_errno{0};
    const ssize_t exec_err_read = read(exec_err_fd[0], &child_exec_errno, sizeof(child_exec_errno));

    close(exec_err_fd[0]);

    if (exec_err_read == static_cast<ssize_t>(sizeof(child_exec_errno))) {
        close(pipe_fd[0]);
        waitpid(pid, nullptr, 0);

        return {-1, ""};
    }

    std::string output{};
    std::array<char, k_pipe_buffer_size> buffer{};
    ssize_t n{0};

    while ((n = read(pipe_fd[0], buffer.data(), buffer.size())) > 0) {
        if (output.size() + static_cast<std::size_t>(n) > ResourceLimits::max_process_output_size) {
            close(pipe_fd[0]);
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);

            return {-1, ""};
        }

        output.append(buffer.data(), static_cast<std::size_t>(n));
    }

    close(pipe_fd[0]);

    int status{0};

    waitpid(pid, &status, 0);

    const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    return {exit_code, std::move(output)};
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
// un-tokenized argv).  See execute_command for the rationale on the
// async-signal-safety constraints in the child below.
CapturedOutput execute_argv_captured(std::vector<std::string> argv_strings) {
    if (argv_strings.empty()) {
        return {};
    }

    std::array<int, 2> out_fd{};
    std::array<int, 2> err_fd{};

    if (pipe(out_fd.data()) == -1) {
        return {};
    }

    if (pipe(err_fd.data()) == -1) {
        close(out_fd[0]);
        close(out_fd[1]);

        return {};
    }

    std::vector<char*> argv{};
    argv.reserve(argv_strings.size() + 1);

    for (auto& s : argv_strings) {
        argv.push_back(s.data());
    }

    argv.push_back(nullptr);

    // Self-pipe to distinguish an execvp failure from a successful launch, as in
    // execute_command.
    std::array<int, 2> exec_err_fd{};

    if (pipe(exec_err_fd.data()) == -1) {
        close(out_fd[0]);
        close(out_fd[1]);
        close(err_fd[0]);
        close(err_fd[1]);

        return {};
    }

    fcntl(exec_err_fd[1], F_SETFD, fcntl(exec_err_fd[1], F_GETFD) | FD_CLOEXEC);

    const pid_t pid = fork();

    if (pid == -1) {
        close(out_fd[0]);
        close(out_fd[1]);
        close(err_fd[0]);
        close(err_fd[1]);
        close(exec_err_fd[0]);
        close(exec_err_fd[1]);

        return {};
    }

    if (pid == 0) {
        // Child -- redirect stdout and stderr to their separate pipes, then exec
        // directly (no shell).  Only async-signal-safe calls here.
        close(out_fd[0]);
        close(err_fd[0]);
        close(exec_err_fd[0]);
        dup2(out_fd[1], STDOUT_FILENO);
        dup2(err_fd[1], STDERR_FILENO);
        close(out_fd[1]);
        close(err_fd[1]);

        execvp(argv[0], argv.data());

        const int exec_errno = errno;
        const ssize_t written = write(exec_err_fd[1], &exec_errno, sizeof(exec_errno));

        (void)written;

        _exit(127);
    }

    // Parent.
    close(out_fd[1]);
    close(err_fd[1]);
    close(exec_err_fd[1]);

    int child_exec_errno{0};
    const ssize_t exec_err_read = read(exec_err_fd[0], &child_exec_errno, sizeof(child_exec_errno));

    close(exec_err_fd[0]);

    if (exec_err_read == static_cast<ssize_t>(sizeof(child_exec_errno))) {
        close(out_fd[0]);
        close(err_fd[0]);
        waitpid(pid, nullptr, 0);

        // The child's execvp failed; surface the captured errno so the caller
        // can classify the launch failure (ENOENT → NotFound, EACCES → …).
        CapturedOutput launch_failure{};
        launch_failure.launch_errno = child_exec_errno;

        return launch_failure;
    }

    CapturedOutput captured{};
    std::array<char, k_pipe_buffer_size> buffer{};

    std::array<pollfd, 2> fds{};
    fds[0] = pollfd{.fd = out_fd[0], .events = POLLIN, .revents = 0};
    fds[1] = pollfd{.fd = err_fd[0], .events = POLLIN, .revents = 0};

    int open_streams{2};
    bool overflow{false};

    while (open_streams > 0 && !overflow) {
        const int ready = poll(fds.data(), fds.size(), -1);

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
                const ssize_t n = read(fds[i].fd, buffer.data(), buffer.size());

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

                // EOF (n == 0) or read error: retire this stream.
                close(fds[i].fd);
                fds[i].fd = -1;
                --open_streams;
            } else if ((fds[i].revents & POLLERR) != 0) {
                close(fds[i].fd);
                fds[i].fd = -1;
                --open_streams;
            }
        }
    }

    for (auto& entry : fds) {
        if (entry.fd >= 0) {
            close(entry.fd);
        }
    }

    if (overflow) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);

        return {};
    }

    int status{0};

    waitpid(pid, &status, 0);

    captured.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    return captured;
}

std::int64_t current_process_id() {
    return static_cast<std::int64_t>(getpid());
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
