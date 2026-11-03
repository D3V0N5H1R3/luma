// DAP stdio transport.  The Content-Length framing lives in
// shared/protocol/stdio_transport.cpp; this file isolates the debuggee's stdin
// from the protocol input stream.

#include "dap_transport.hpp"

#include <cstdio>
#include <optional>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace luma::dap {

#ifdef _WIN32

std::optional<int> detach_protocol_input_from_stdin() {
    const int stdin_fd = _fileno(stdin);
    const int protocol_fd = _dup(stdin_fd);

    if (protocol_fd < 0) {
        return std::nullopt;
    }

    int null_fd{-1};

    if (_sopen_s(&null_fd, "NUL", _O_RDONLY | _O_BINARY, _SH_DENYNO, _S_IREAD) != 0) {
        _close(protocol_fd);
        return std::nullopt;
    }

    const bool redirected = _dup2(null_fd, stdin_fd) == 0;
    _close(null_fd);

    if (!redirected) {
        _close(protocol_fd);
        return std::nullopt;
    }

    _setmode(protocol_fd, _O_BINARY);

    // Win32 console APIs (Terminal input) read through the standard handle, not
    // the CRT descriptor, so retarget it at the null device as well.
    const auto null_handle = reinterpret_cast<HANDLE>(_get_osfhandle(stdin_fd));

    if (null_handle != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_INPUT_HANDLE, null_handle);
    }

    return protocol_fd;
}

#else

std::optional<int> detach_protocol_input_from_stdin() {
    // Close-on-exec so programs launched by the debuggee (Process.run) do not
    // inherit the protocol pipe.
    const int protocol_fd = ::fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);

    if (protocol_fd < 0) {
        return std::nullopt;
    }

    const int null_fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);

    if (null_fd < 0) {
        ::close(protocol_fd);
        return std::nullopt;
    }

    const bool redirected = ::dup2(null_fd, STDIN_FILENO) == STDIN_FILENO;
    ::close(null_fd);

    if (!redirected) {
        ::close(protocol_fd);
        return std::nullopt;
    }

    return protocol_fd;
}

#endif

} // namespace luma::dap
