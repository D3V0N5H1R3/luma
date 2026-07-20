#include "runtime/cli/terminal.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace luma::term {

void enable_ansi_escapes() noexcept {
    const auto enable_vt = [](DWORD handle_id) {
        HANDLE h = GetStdHandle(handle_id);

        if (h != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;

            if (GetConsoleMode(h, &mode)) {
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            }
        }
    };

    enable_vt(STD_OUTPUT_HANDLE);
    enable_vt(STD_ERROR_HANDLE);
}

} // namespace luma::term

#endif
