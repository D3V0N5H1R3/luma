#include <string>

#include <sys/select.h>
#include <unistd.h>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/stdlib/io/terminal_input.hpp"
#include "runtime/stdlib/io/terminal_key_decoder.hpp"

namespace luma::terminal_detail {

namespace {

// ═══════════════════════════════════════════════════════════
// Byte-level reading helpers
// ═══════════════════════════════════════════════════════════

[[nodiscard]] int read_byte() {
    unsigned char c{0};

    const ssize_t n = read(STDIN_FILENO, &c, 1);

    if (n <= 0) {
        return -1;
    }

    return c;
}

[[nodiscard]] int read_byte_timeout() {
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    const auto ms = escape_timeout_ms.load(std::memory_order_relaxed);

    timeval tv{};
    tv.tv_sec = static_cast<long>(ms / 1000);
    tv.tv_usec = static_cast<long>((ms % 1000) * 1000);

    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
        return read_byte();
    }

    return -1;
}

[[nodiscard]] std::string read_key_posix(const SourceLocation& loc, bool mouse_mode_active) {
    const int c = read_byte();

    if (c == -1) {
        throw RuntimeError{"Terminal.read_key: read failed", loc,
                           "the terminal input stream is closed or unavailable"};
    }

    return decode_key(c, read_byte_timeout, mouse_mode_active);
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Key reading — POSIX
// ═══════════════════════════════════════════════════════════

[[nodiscard]] InputResult read_input_posix(std::int64_t timeout_ms, const SourceLocation& loc,
                                           bool mouse_mode_active) {
    if (timeout_ms >= 0) {
        fd_set fds;

        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        timeval tv{};
        tv.tv_sec = static_cast<long>(timeout_ms / 1000);
        tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);

        const int sel = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);

        if (sel <= 0) {
            return {"", true};
        }
    }

    return {read_key_posix(loc, mouse_mode_active), false};
}

[[nodiscard]] bool read_cpr_response(int& row, int& col) {
    // Read until we find ESC [
    int ch = read_byte_timeout();

    if (ch != 27) {
        return false;
    }

    ch = read_byte_timeout();

    if (ch != '[') {
        return false;
    }

    std::string num{};

    ch = read_byte_timeout();

    while (ch >= '0' && ch <= '9') {
        num += static_cast<char>(ch);

        ch = read_byte_timeout();
    }

    if (ch != ';' || num.empty()) {
        return false;
    }

    try {
        row = std::stoi(num);
    } catch (const std::exception&) {
        return false;
    }

    num.clear();

    ch = read_byte_timeout();

    while (ch >= '0' && ch <= '9') {
        num += static_cast<char>(ch);

        ch = read_byte_timeout();
    }

    if (ch != 'R' || num.empty()) {
        return false;
    }

    try {
        col = std::stoi(num);
    } catch (const std::exception&) {
        return false;
    }

    return true;
}

InputResult read_input(std::int64_t timeout_ms, const SourceLocation& loc, bool mouse_mode_active) {
    return read_input_posix(timeout_ms, loc, mouse_mode_active);
}

} // namespace luma::terminal_detail
