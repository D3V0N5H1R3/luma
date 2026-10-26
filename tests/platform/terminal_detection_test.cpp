// Unit tests for the cross-platform terminal-detection helpers in
// core/common/platform_utils.hpp (is_stdin_terminal / is_stdout_terminal /
// is_stderr_terminal). These consolidate the isatty()/_isatty() checks that
// used to be duplicated across core/runtime/cli/terminal.hpp, the Terminal
// stdlib module (platform_terminal_posix.cpp / platform_terminal_win32.cpp),
// and the REPL line editor (line_editor_posix.cpp / line_editor_win32.cpp).
// This target runs on every OS (see tests/CMakeLists.txt).

#include "common/platform_utils.hpp"
#include "common/terminal.hpp"
#include "runtime/cli/terminal.hpp" // Deprecated forwarding header; must still compile.
#include "test_framework.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// is_stdin_terminal / is_stdout_terminal / is_stderr_terminal
// ═══════════════════════════════════════════════════════════
//
// Whether stdin/stdout/stderr are attached to a real TTY depends on how the
// test runner is invoked (interactive shell vs. CI pipe/redirect), so these
// tests do not assert a specific true/false value. Instead they pin the
// characterization that matters for callers: the query is side-effect-free
// and returns the same answer on every call for a given stream.

static void test_is_stdin_terminal_is_stable() {
    const bool first = is_stdin_terminal();
    const bool second = is_stdin_terminal();
    ASSERT_EQ(first, second);
}

static void test_is_stdout_terminal_is_stable() {
    const bool first = is_stdout_terminal();
    const bool second = is_stdout_terminal();
    ASSERT_EQ(first, second);
}

static void test_is_stderr_terminal_is_stable() {
    const bool first = is_stderr_terminal();
    const bool second = is_stderr_terminal();
    ASSERT_EQ(first, second);
}

// ═══════════════════════════════════════════════════════════
// Colour helpers follow the TTY state of the stream they target
// ═══════════════════════════════════════════════════════════
//
// The REPL wrote stderr messages with the stdout helpers (out_red), leaking
// ANSI codes into a redirected stderr whenever stdout was a terminal.  Pin the
// contract the call sites rely on: the plain helpers track stderr and the
// out_* helpers track stdout.

static void test_stderr_colour_helpers_follow_stderr() {
    ASSERT_EQ(term::red().empty(), !is_stderr_terminal());
    ASSERT_EQ(term::yellow().empty(), !is_stderr_terminal());
    ASSERT_EQ(term::reset().empty(), !is_stderr_terminal());
}

static void test_stdout_colour_helpers_follow_stdout() {
    ASSERT_EQ(term::out_red().empty(), !is_stdout_terminal());
    ASSERT_EQ(term::out_cyan().empty(), !is_stdout_terminal());
    ASSERT_EQ(term::out_reset().empty(), !is_stdout_terminal());
}

// ─── main ───

int main() {
    RUN(test_stderr_colour_helpers_follow_stderr);
    RUN(test_stdout_colour_helpers_follow_stdout);
    RUN(test_is_stdin_terminal_is_stable);
    RUN(test_is_stdout_terminal_is_stable);
    RUN(test_is_stderr_terminal_is_stable);

    return SUMMARY();
}
