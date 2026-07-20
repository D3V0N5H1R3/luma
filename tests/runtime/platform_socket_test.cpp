// Unit tests for shared/protocol/platform_socket.hpp — the lean socket
// primitives the LSP/DAP transports use to paper over the Winsock vs POSIX
// differences.  WinsockGuard, sockopt_ptr, close_socket and the invalid_socket
// sentinel are otherwise only reached indirectly (through the DAP TCP
// transport), so they are pinned directly here.

#include <type_traits>

#include "protocol/platform_socket.hpp"
#include "test_framework.hpp"

using namespace luma::protocol;

// ═══════════════════════════════════════════════════════════
// sockopt_ptr — correct cast type and address preservation
// ═══════════════════════════════════════════════════════════

static void test_sockopt_ptr_mutable_overload() {
    int value = 1;

    // The mutable overload yields char* (what setsockopt expects on Windows and
    // accepts on POSIX) without changing the pointed-to address.
    static_assert(std::is_same_v<decltype(sockopt_ptr(&value)), char*>);
    char* const casted = sockopt_ptr(&value);
    ASSERT_TRUE(static_cast<void*>(casted) == static_cast<void*>(&value));
}

static void test_sockopt_ptr_const_overload() {
    const int value = 1;

    static_assert(std::is_same_v<decltype(sockopt_ptr(&value)), const char*>);
    const char* const casted = sockopt_ptr(&value);
    ASSERT_TRUE(static_cast<const void*>(casted) == static_cast<const void*>(&value));
}

// ═══════════════════════════════════════════════════════════
// WinsockGuard — initialises the socket subsystem
// ═══════════════════════════════════════════════════════════

// Constructing the guard must make socket creation succeed.  On Windows that
// proves WSAStartup ran (::socket fails without it); on POSIX the guard is a
// no-op but socket creation still works, so the assertion is meaningful on both.
static void test_winsock_guard_enables_socket_creation() {
    const WinsockGuard guard;

    const socket_handle sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_TRUE(sock != invalid_socket);

    close_socket(sock);
}

// Nested guards must be safe: WSAStartup/WSACleanup are reference counted on
// Windows, so the inner guard's teardown must not disable Winsock while the
// outer guard is still alive.
static void test_winsock_guard_nesting_keeps_subsystem_live() {
    const WinsockGuard outer;

    {
        const WinsockGuard inner;
        const socket_handle sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ASSERT_TRUE(sock != invalid_socket);
        close_socket(sock);
    }

    // The inner guard has been destroyed; the outer guard must keep Winsock
    // initialised so a further socket can still be created.
    const socket_handle sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_TRUE(sock != invalid_socket);
    close_socket(sock);
}

// ─── main ───

int main() {
    using namespace luma::test;
    print_suite_header("platform_socket");

    RUN(test_sockopt_ptr_mutable_overload);
    RUN(test_sockopt_ptr_const_overload);
    RUN(test_winsock_guard_enables_socket_creation);
    RUN(test_winsock_guard_nesting_keeps_subsystem_live);

    return SUMMARY();
}
