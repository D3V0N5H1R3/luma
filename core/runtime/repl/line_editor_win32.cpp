// Windows terminal-control primitives for LineEditor.  Compiled only on Windows
// (see core/runtime/CMakeLists.txt); the POSIX counterpart lives in
// line_editor_posix.cpp.  Behaviour mirrors the inline #ifdef _WIN32 branches
// that previously lived in line_editor.cpp.

#include <array>
#include <cstdio>
#include <memory>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>

#include "common/platform_utils.hpp"
#include "runtime/repl/line_editor.hpp"

namespace luma {

// Platform-specific saved console state for Windows, kept out of the header so
// <windows.h> does not leak to every LineEditor consumer.
struct TerminalGuard::Impl {
    HANDLE handle{};
    DWORD saved_mode{};
    bool has_saved_mode{false};
};

TerminalGuard::TerminalGuard() : impl_{std::make_unique<Impl>()} {
    impl_->handle = GetStdHandle(STD_INPUT_HANDLE);

    if (GetConsoleMode(impl_->handle, &impl_->saved_mode) == 0) {
        impl_->has_saved_mode = false;
        return;
    }

    impl_->has_saved_mode = true;

    // Raw input: no line buffering, echo, or Ctrl+C processing.  Virtual
    // terminal input makes the console deliver arrow/Home/End/Delete keys as
    // the same ANSI escape sequences a POSIX terminal sends (ESC [ A, ...);
    // without it ReadConsoleA never sees them and history/cursor keys do
    // nothing.  Fall back to plain raw mode on consoles that predate VT input.
    const DWORD raw_mode =
        impl_->saved_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);

    if (SetConsoleMode(impl_->handle, raw_mode | ENABLE_VIRTUAL_TERMINAL_INPUT) == 0) {
        SetConsoleMode(impl_->handle, raw_mode);
    }
}

TerminalGuard::~TerminalGuard() noexcept {
    if (impl_->has_saved_mode) {
        SetConsoleMode(impl_->handle, impl_->saved_mode);
    }
}

bool LineEditor::read_char(char& c) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD n{};
    return (ReadConsoleA(h, &c, 1, &n, nullptr) != 0) && n > 0;
}

namespace {

// How long to wait for the bytes that follow an Escape before treating it as
// a bare Escape key press (matches the POSIX VTIME of 100 ms).
constexpr ULONGLONG k_escape_timeout_ms = 100;

// Wait until a character-producing key press is queued on the console input
// buffer, or until `deadline` (GetTickCount64 time) passes.  The input handle
// is also signalled by key releases, focus, mouse, and resize events, which
// ReadConsoleA skips while it blocks, so those are drained here; otherwise a
// bare Escape followed by a key release would block until the next key press
// and swallow it.
[[nodiscard]] bool wait_for_char(HANDLE handle, ULONGLONG deadline) {
    while (true) {
        const ULONGLONG now = GetTickCount64();

        if (now >= deadline ||
            WaitForSingleObject(handle, static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0) {
            return false;
        }

        INPUT_RECORD record{};
        DWORD count{};

        if (PeekConsoleInputW(handle, &record, 1, &count) == 0 || count == 0) {
            return false;
        }

        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown != 0 &&
            record.Event.KeyEvent.uChar.UnicodeChar != 0) {
            return true;
        }

        if (ReadConsoleInputW(handle, &record, 1, &count) == 0) {
            return false;
        }
    }
}

} // namespace

bool LineEditor::read_escape_sequence_bytes(std::array<char, 3>& seq) {
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    const ULONGLONG deadline = GetTickCount64() + k_escape_timeout_ms;

    if (!wait_for_char(handle, deadline) || !read_char(seq[0])) {
        return false; // Bare Escape — nothing followed within the timeout.
    }

    if (seq[0] == '[') {
        if (!wait_for_char(handle, deadline) || !read_char(seq[1])) {
            return false;
        }
    }

    return true;
}

bool LineEditor::stdin_is_terminal() {
    return is_stdin_terminal();
}

} // namespace luma
