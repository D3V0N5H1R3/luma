#ifndef LUMA_STDLIB_WINSOCK_INIT_HPP
#define LUMA_STDLIB_WINSOCK_INIT_HPP

// RAII guard for Winsock initialisation.  Shared by socket_module and
// http_module to ensure WSAStartup is called before any socket operation.
// On non-Windows platforms, ensure_winsock() is a no-op.

#ifdef _WIN32

#include <stdexcept>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>

namespace luma {

class WinsockInit {
public:
    static void ensure() {
        static const WinsockInit instance;
    }

    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;
    WinsockInit(WinsockInit&&) = delete;
    WinsockInit& operator=(WinsockInit&&) = delete;

private:
    WinsockInit() {
        WSADATA wsa_data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);

        if (result != 0) {
            // Uses std::runtime_error instead of RuntimeError because this
            // runs during static initialisation (before the Luma runtime is
            // ready), so RuntimeError's SourceLocation would be meaningless.
            throw std::runtime_error{"WSAStartup failed with error code " + std::to_string(result)};
        }
    }

    ~WinsockInit() noexcept {
        WSACleanup();
    }
};

inline void ensure_winsock() {
    WinsockInit::ensure();
}

} // namespace luma

#else

namespace luma {

inline void ensure_winsock() {
    // No-op on non-Windows platforms.
}

} // namespace luma

#endif // _WIN32

#endif // LUMA_STDLIB_WINSOCK_INIT_HPP
