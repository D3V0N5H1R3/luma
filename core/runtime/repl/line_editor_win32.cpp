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

#include "runtime/repl/line_editor.hpp"

namespace luma {

// Platform-specific saved console state for Windows, kept out of the header so
// <windows.h> does not leak to every LineEditor consumer.
struct TerminalGuard::Impl {
    HANDLE handle{};
    DWORD saved_mode{};
};

TerminalGuard::TerminalGuard() : impl_{std::make_unique<Impl>()} {
    impl_->handle = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(impl_->handle, &impl_->saved_mode);
    SetConsoleMode(impl_->handle, impl_->saved_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                                                        ENABLE_PROCESSED_INPUT));
}

TerminalGuard::~TerminalGuard() noexcept {
    SetConsoleMode(impl_->handle, impl_->saved_mode);
}

bool LineEditor::read_char(char& c) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD n{};
    return (ReadConsoleA(h, &c, 1, &n, nullptr) != 0) && n > 0;
}

bool LineEditor::read_escape_sequence_bytes(std::array<char, 3>& seq) {
    if (!read_char(seq[0])) {
        return false;
    }

    if (seq[0] == '[') {
        if (!read_char(seq[1])) {
            return false;
        }
    }

    return true;
}

bool LineEditor::stdin_is_terminal() {
    return _isatty(_fileno(stdin)) != 0;
}

} // namespace luma
