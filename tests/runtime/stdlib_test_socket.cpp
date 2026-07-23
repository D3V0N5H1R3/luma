// Standard library tests: Socket.

#include <string>

#include "stdlib_test_helpers.hpp"

static void test_socket_connect_invalid_host() {
    // Connecting to invalid host should return a fail result.
    ASSERT_EVAL_FAILURE("Socket.connect(\"invalid.host.that.does.not.exist.example\", 80)");
}

static void test_socket_connect_invalid_port() {
    // Port out of range should return a fail result.
    ASSERT_EVAL_FAILURE("Socket.connect(\"127.0.0.1\", 99999)");
}

static void test_socket_is_connected_non_socket_throws() {
    ASSERT_THROWS(eval("Socket.is_connected(42)"));
}

static void test_socket_listen_and_close() {
    const auto v = eval("result<socket> r = Socket.listen(\"127.0.0.1\", 0)\n"
                        "Result.is_success(r)\n");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_socket_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Socket.connect"));
    ASSERT_TRUE(env->has("Socket.listen"));
    ASSERT_TRUE(env->has("Socket.accept"));
    ASSERT_TRUE(env->has("Socket.send"));
    ASSERT_TRUE(env->has("Socket.receive"));
    ASSERT_TRUE(env->has("Socket.close"));
    ASSERT_TRUE(env->has("Socket.set_timeout"));
    ASSERT_TRUE(env->has("Socket.is_connected"));
    ASSERT_TRUE(env->has("Socket.local_address"));
    ASSERT_TRUE(env->has("Socket.local_address_parts"));
    ASSERT_TRUE(env->has("Socket.remote_address"));
    ASSERT_TRUE(env->has("Socket.remote_address_parts"));
    ASSERT_TRUE(env->has("Socket.udp_create"));
    ASSERT_TRUE(env->has("Socket.udp_bind"));
    ASSERT_TRUE(env->has("Socket.udp_send"));
    ASSERT_TRUE(env->has("Socket.udp_receive"));
}

static void test_socket_type_of() {
    const auto v = eval("Socket.udp_create()");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->display_type_name(), "socket");
}

static void test_socket_udp_create() {
    const auto v = eval("result<socket> r = Socket.udp_create()\n"
                        "Result.is_success(r)\n");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

// ─── Socket: positive loopback behaviour ───
//
// These exercise the deterministic, network-free loopback (127.0.0.1) paths.
// Per the test philosophy (no external network), loopback is permitted: it
// stays on the local kernel stack and never leaves the machine.

static void test_socket_udp_bind() {
    // Binding a fresh UDP socket to an ephemeral loopback port succeeds.
    ASSERT_EVAL_BOOL("socket s = Result.unwrap(Socket.udp_create())\n"
                     "Socket.udp_bind(s, \"127.0.0.1\", 0)\n",
                     true);
}

static void test_socket_set_timeout_returns_true() {
    // set_timeout reports success(true) for a non-negative timeout.
    ASSERT_EVAL_BOOL("socket s = Result.unwrap(Socket.udp_create())\n"
                     "Socket.set_timeout(s, 1000)\n",
                     true);
}

static void test_socket_local_address_after_listen() {
    // A listening socket reports a concrete loopback host and a non-zero
    // ephemeral port (the kernel substitutes a real port for the requested 0).
    const auto v = eval("socket srv = Result.unwrap(Socket.listen(\"127.0.0.1\", 0))\n"
                        "Result.unwrap(Socket.local_address(srv))\n");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().rfind("127.0.0.1:", 0) == 0);
    ASSERT_TRUE(v.as_string() != "127.0.0.1:0");
}

static void test_socket_local_address_parts_after_listen() {
    // local_address_parts returns a structured Socket.Address record: the host
    // field is the loopback address and the port field is the kernel-assigned
    // non-zero ephemeral port (no "host:port" string parsing needed).
    const auto v = eval("socket srv = Result.unwrap(Socket.listen(\"127.0.0.1\", 0))\n"
                        "Result.unwrap(Socket.local_address_parts(srv))\n");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Address"});

    const auto* host = v.as_record()->find_field("host");
    const auto* port = v.as_record()->find_field("port");
    ASSERT_TRUE(host != nullptr);
    ASSERT_TRUE(port != nullptr);
    ASSERT_TRUE(host->is_string());
    ASSERT_EQ(host->as_string(), std::string{"127.0.0.1"});
    ASSERT_TRUE(port->is_integer());
    ASSERT_TRUE(port->as_integer() > 0);
}

static void test_socket_is_connected_reflects_lifetime() {
    // is_connected is true for a fresh socket and false once it is closed.
    const auto fresh = eval("socket s = Result.unwrap(Socket.udp_create())\n"
                            "Socket.is_connected(s)\n");

    ASSERT_TRUE(fresh.is_bool());
    ASSERT_TRUE(fresh.as_bool());

    const auto closed = eval("socket s = Result.unwrap(Socket.udp_create())\n"
                             "Socket.close(s)\n"
                             "Socket.is_connected(s)\n");

    ASSERT_TRUE(closed.is_bool());
    ASSERT_FALSE(closed.as_bool());
}

static void test_socket_close_is_idempotent() {
    // Closing an already-closed socket must not throw and leaves it invalid.
    const auto v = eval("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.close(s)\n"
                        "Socket.close(s)\n"
                        "Socket.is_connected(s)\n");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

static void test_socket_udp_send_receive_roundtrip() {
    // Two bound UDP sockets exchange a datagram over loopback; the payload
    // arrives intact in the returned UdpPacket record. Wrapped in a function so
    // that `pkt` is a local — field access on a top-level REPL global would be
    // miscompiled as a qualified module lookup.
    const auto v = eval(
        "function string udp_echo() {\n"
        "    socket sender = Result.unwrap(Socket.udp_create())\n"
        "    result<boolean> _sb = Socket.udp_bind(sender, \"127.0.0.1\", 0)\n"
        "    socket receiver = Result.unwrap(Socket.udp_create())\n"
        "    result<boolean> _rb = Socket.udp_bind(receiver, \"127.0.0.1\", 0)\n"
        "    array<string> parts = String.split(Result.unwrap(Socket.local_address(receiver)), "
        "\":\")\n"
        "    integer port = Result.unwrap(Converter.to_integer(parts[1]))\n"
        "    result<boolean> _t = Socket.set_timeout(receiver, 2000)\n"
        "    result<integer> _s = Socket.udp_send(sender, \"ping\", parts[0], port)\n"
        "    Socket.UdpPacket pkt = Result.unwrap(Socket.udp_receive(receiver, 1024))\n"
        "    string data = pkt.data\n"
        "    Socket.close(sender)\n"
        "    Socket.close(receiver)\n"
        "    return data\n"
        "}\n"
        "udp_echo()\n");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "ping");
}

static void test_socket_tcp_send_receive_roundtrip() {
    // Single-threaded TCP loopback exchange. connect() completes the handshake
    // in-kernel and queues the peer, so accept() returns it without a second
    // thread; a string sent by the client is received intact on the connection.
    const auto v =
        eval("socket server = Result.unwrap(Socket.listen(\"127.0.0.1\", 0))\n"
             "array<string> parts = String.split(Result.unwrap(Socket.local_address(server)), "
             "\":\")\n"
             "integer port = Result.unwrap(Converter.to_integer(parts[1]))\n"
             "socket client = Result.unwrap(Socket.connect(\"127.0.0.1\", port))\n"
             "socket conn = Result.unwrap(Socket.accept(server))\n"
             "result<boolean> _t = Socket.set_timeout(conn, 2000)\n"
             "result<integer> _s = Socket.send(client, \"hello\")\n"
             "string got = Result.unwrap(Socket.receive(conn, 1024))\n"
             "Socket.close(client)\n"
             "Socket.close(conn)\n"
             "Socket.close(server)\n"
             "got\n");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello");
}

static void test_socket_tcp_remote_address_matches_server() {
    // After a loopback connect/accept, the client's remote address must equal
    // the server's local (listening) address.
    const auto v = eval("socket server = Result.unwrap(Socket.listen(\"127.0.0.1\", 0))\n"
                        "string saddr = Result.unwrap(Socket.local_address(server))\n"
                        "array<string> parts = String.split(saddr, \":\")\n"
                        "integer port = Result.unwrap(Converter.to_integer(parts[1]))\n"
                        "socket client = Result.unwrap(Socket.connect(\"127.0.0.1\", port))\n"
                        "socket conn = Result.unwrap(Socket.accept(server))\n"
                        "string remote = Result.unwrap(Socket.remote_address(client))\n"
                        "Socket.close(client)\n"
                        "Socket.close(conn)\n"
                        "Socket.close(server)\n"
                        "remote == saddr\n");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

// ─── Socket: type errors (thrown RuntimeError, not failure result) ───

static void test_socket_operations_reject_non_socket() {
    // Every socket-consuming function rejects a non-socket first argument by
    // throwing, rather than silently producing a failure result.
    ASSERT_THROWS(eval("Socket.send(42, \"x\")"));
    ASSERT_THROWS(eval("Socket.receive(42, 10)"));
    ASSERT_THROWS(eval("Socket.accept(42)"));
    ASSERT_THROWS(eval("Socket.close(42)"));
    ASSERT_THROWS(eval("Socket.set_timeout(42, 10)"));
    ASSERT_THROWS(eval("Socket.local_address(42)"));
    ASSERT_THROWS(eval("Socket.remote_address(42)"));
    ASSERT_THROWS(eval("Socket.udp_bind(42, \"127.0.0.1\", 0)"));
    ASSERT_THROWS(eval("Socket.udp_send(42, \"d\", \"127.0.0.1\", 80)"));
    ASSERT_THROWS(eval("Socket.udp_receive(42, 10)"));
}

static void test_socket_connect_rejects_non_string_host() {
    ASSERT_THROWS(eval("Socket.connect(123, 80)"));
}

static void test_socket_connect_rejects_non_integer_port() {
    ASSERT_THROWS(eval("Socket.connect(\"127.0.0.1\", \"80\")"));
}

static void test_socket_listen_rejects_non_string_host() {
    ASSERT_THROWS(eval("Socket.listen(123, 80)"));
}

static void test_socket_udp_send_rejects_non_string_data() {
    ASSERT_THROWS(eval("socket s = Result.unwrap(Socket.udp_create())\n"
                       "Socket.udp_send(s, 123, \"127.0.0.1\", 80)\n"));
}

// ─── Socket: failure results (recoverable, returned as failure) ───

static void test_socket_send_on_closed_fails() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.close(s)\n"
                        "Socket.send(s, \"data\")\n");
}

static void test_socket_receive_on_closed_fails() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.close(s)\n"
                        "Socket.receive(s, 1024)\n");
}

static void test_socket_receive_rejects_non_positive_max_bytes() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.receive(s, 0)\n");
}

static void test_socket_udp_receive_rejects_non_positive_max_bytes() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.udp_receive(s, -1)\n");
}

static void test_socket_set_timeout_rejects_negative() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.set_timeout(s, -1)\n");
}

static void test_socket_accept_on_non_server_fails() {
    // A UDP socket is valid but not a listening server, so accept must fail.
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.accept(s)\n");
}

static void test_socket_remote_address_on_unconnected_fails() {
    // getpeername fails on an unconnected socket, surfaced as a failure result.
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.remote_address(s)\n");
}

static void test_socket_remote_address_parts_on_unconnected_fails() {
    // The structured variant fails the same way on an unconnected socket.
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.remote_address_parts(s)\n");
}

static void test_socket_listen_rejects_invalid_port() {
    ASSERT_EVAL_FAILURE("Socket.listen(\"127.0.0.1\", 99999)");
}

static void test_socket_udp_send_rejects_invalid_port() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.udp_send(s, \"d\", \"127.0.0.1\", 99999)\n");
}

static void test_socket_udp_bind_rejects_invalid_port() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.udp_bind(s, \"127.0.0.1\", 99999)\n");
}

int main() {
    RUN(test_socket_accept_on_non_server_fails);
    RUN(test_socket_close_is_idempotent);
    RUN(test_socket_connect_invalid_host);
    RUN(test_socket_connect_invalid_port);
    RUN(test_socket_connect_rejects_non_integer_port);
    RUN(test_socket_connect_rejects_non_string_host);
    RUN(test_socket_is_connected_non_socket_throws);
    RUN(test_socket_is_connected_reflects_lifetime);
    RUN(test_socket_listen_and_close);
    RUN(test_socket_listen_rejects_invalid_port);
    RUN(test_socket_listen_rejects_non_string_host);
    RUN(test_socket_local_address_after_listen);
    RUN(test_socket_local_address_parts_after_listen);
    RUN(test_socket_module);
    RUN(test_socket_operations_reject_non_socket);
    RUN(test_socket_receive_on_closed_fails);
    RUN(test_socket_receive_rejects_non_positive_max_bytes);
    RUN(test_socket_remote_address_on_unconnected_fails);
    RUN(test_socket_remote_address_parts_on_unconnected_fails);
    RUN(test_socket_send_on_closed_fails);
    RUN(test_socket_set_timeout_rejects_negative);
    RUN(test_socket_set_timeout_returns_true);
    RUN(test_socket_tcp_remote_address_matches_server);
    RUN(test_socket_tcp_send_receive_roundtrip);
    RUN(test_socket_type_of);
    RUN(test_socket_udp_bind);
    RUN(test_socket_udp_bind_rejects_invalid_port);
    RUN(test_socket_udp_create);
    RUN(test_socket_udp_receive_rejects_non_positive_max_bytes);
    RUN(test_socket_udp_send_receive_roundtrip);
    RUN(test_socket_udp_send_rejects_invalid_port);
    RUN(test_socket_udp_send_rejects_non_string_data);

    return SUMMARY();
}
