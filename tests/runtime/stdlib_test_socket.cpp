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
}

static void test_socket_type_of() {
    const auto v = eval("Socket.udp_create()");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->display_type_name(), "socket");
}

// ─── Socket.IpAddress: pure IP-literal parsing ───────────────────────────────

static void test_socket_parse_ipv4() {
    const auto v = eval(R"(Socket.parse_ip("192.168.0.1"))");
    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;
    ASSERT_TRUE(inner.is_choice());
    ASSERT_EQ(inner.as_choice()->type_name, std::string{"IpAddress"});
    ASSERT_EQ(inner.as_choice()->variant, std::string{"Version4"});
    ASSERT_EQ(inner.as_choice()->fields.at(0).as_string(), "192.168.0.1");

    // Leading zeros are canonicalised away.
    const auto c = eval(R"(Socket.ip_to_string(Result.unwrap(Socket.parse_ip("010.0.0.005"))))");
    ASSERT_EQ(c.as_string(), "10.0.0.5");
}

static void test_socket_parse_ipv6() {
    const auto v = eval(R"(Socket.parse_ip("2001:DB8::1"))");
    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;
    ASSERT_TRUE(inner.is_choice());
    ASSERT_EQ(inner.as_choice()->variant, std::string{"Version6"});
    // Rendered lowercased.
    ASSERT_EQ(inner.as_choice()->fields.at(0).as_string(), "2001:db8::1");

    // "::" alone (all zeros) is valid.
    ASSERT_RESULT_SUCCESS(eval(R"(Socket.parse_ip("::"))"));
    // Embedded IPv4 tail.
    ASSERT_RESULT_SUCCESS(eval(R"(Socket.parse_ip("::ffff:192.168.0.1"))"));
}

static void test_socket_parse_ip_invalid_fails() {
    ASSERT_EVAL_FAILURE(R"(Socket.parse_ip("256.0.0.1"))");
    ASSERT_EVAL_FAILURE(R"(Socket.parse_ip("1.2.3"))");
    ASSERT_EVAL_FAILURE(R"(Socket.parse_ip("1.2.3.4.5"))");
    ASSERT_EVAL_FAILURE(R"(Socket.parse_ip("gggg::1"))");
    ASSERT_EVAL_FAILURE(R"(Socket.parse_ip("1::2::3"))");
    ASSERT_EVAL_FAILURE(R"(Socket.parse_ip("hello"))");
    // An embedded IPv4 is only legal as the address tail, never before "::".
    ASSERT_EVAL_FAILURE(R"(Socket.parse_ip("1.2.3.4::"))");
    ASSERT_EVAL_FAILURE(R"(Socket.parse_ip("1.2.3.4::1"))");
}

static void test_socket_ip_to_string_rejects_non_choice() {
    ASSERT_THROWS(eval("Socket.ip_to_string(42)"));
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

// ─── Socket: typed transport errors (the Socket.Error choice) ────────────────
//
// The *_typed companions surface a transport failure as a Socket.Error choice
// instead of an opaque string, so a program can branch on the category.  These
// exercise the deterministic, network-free paths (a closed handle, a refused
// loopback port, and an unresolvable host), leaving the remaining variants
// (Timeout / AddressInUse / ConnectionReset) to the exhaustive Luma match test —
// they are not reliably triggerable through the public API on every platform
// (e.g. listen() always sets SO_REUSEADDR, so a double-bind does not fault).

// Reads the Socket.Error variant name from a *_typed failure result, asserting
// the failure carries a typed Socket.Error choice rather than a string message.
[[nodiscard]] static std::string socket_error_variant_of(const luma::Value& v) {
    const auto& inner = v.as_result()->owned_inner;
    if (!inner->is_choice()) {
        return "<not-a-choice>";
    }
    return inner->as_choice()->type_name + "." + inner->as_choice()->variant;
}

static void test_socket_typed_registered() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Socket.connect_typed"));
    ASSERT_TRUE(env->has("Socket.listen_typed"));
    ASSERT_TRUE(env->has("Socket.send_typed"));
    ASSERT_TRUE(env->has("Socket.receive_typed"));
}

static void test_socket_send_typed_on_closed_is_not_connected() {
    const auto v = eval("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.close(s)\n"
                        "Socket.send_typed(s, \"data\")\n");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(socket_error_variant_of(v), "Error.NotConnected");
}

static void test_socket_receive_typed_on_closed_is_not_connected() {
    const auto v = eval("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.close(s)\n"
                        "Socket.receive_typed(s, 1024)\n");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(socket_error_variant_of(v), "Error.NotConnected");
}

static void test_socket_receive_typed_non_positive_max_bytes_is_other() {
    const auto v = eval("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.receive_typed(s, 0)\n");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(socket_error_variant_of(v), "Error.Other");
}

static void test_socket_connect_typed_refused_is_connection_refused() {
    // Listen on an ephemeral loopback port, capture it, then close the listener
    // so nothing is accepting; connecting to that port must be refused.
    const auto v = eval("socket srv = Result.unwrap(Socket.listen(\"127.0.0.1\", 0))\n"
                        "array<string> parts = String.split(Result.unwrap(Socket.local_address("
                        "srv)), \":\")\n"
                        "integer port = Result.unwrap(Converter.to_integer(parts[1]))\n"
                        "Socket.close(srv)\n"
                        "Socket.connect_typed(\"127.0.0.1\", port)\n");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(socket_error_variant_of(v), "Error.ConnectionRefused");
}

static void test_socket_connect_typed_invalid_host_is_host_unreachable() {
    // An unresolvable host fails before any connect attempt; classified as
    // HostUnreachable (name resolution could not locate the target).
    const auto v = eval("Socket.connect_typed(\"invalid.host.that.does.not.exist.example\", 80)\n");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(socket_error_variant_of(v), "Error.HostUnreachable");
}

static void test_socket_connect_typed_invalid_port_is_other() {
    const auto v = eval("Socket.connect_typed(\"127.0.0.1\", 99999)\n");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(socket_error_variant_of(v), "Error.Other");
}

// ─── Socket: send_all / receive_all / receive_line / bytes / connect_timeout ─

static void test_socket_extended_registered() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Socket.send_all"));
    ASSERT_TRUE(env->has("Socket.send_all_typed"));
    ASSERT_TRUE(env->has("Socket.receive_all"));
    ASSERT_TRUE(env->has("Socket.receive_line"));
    ASSERT_TRUE(env->has("Socket.connect_timeout"));
    ASSERT_TRUE(env->has("Socket.connect_timeout_typed"));
    ASSERT_TRUE(env->has("Socket.send_bytes"));
    ASSERT_TRUE(env->has("Socket.receive_bytes"));
}

static void test_socket_send_bytes_rejects_out_of_range() {
    // A byte value outside 0-255 fails before any data is sent.
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.send_bytes(s, [1, 2, 999])\n");
}

static void test_socket_send_bytes_rejects_negative() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.send_bytes(s, [-1])\n");
}

static void test_socket_send_all_on_closed_fails() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.close(s)\n"
                        "Socket.send_all(s, \"data\")\n");
}

static void test_socket_send_all_typed_on_closed_is_not_connected() {
    const auto v = eval("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.close(s)\n"
                        "Socket.send_all_typed(s, \"data\")\n");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(socket_error_variant_of(v), "Error.NotConnected");
}

static void test_socket_receive_bytes_rejects_non_positive_max() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.receive_bytes(s, 0)\n");
}

static void test_socket_receive_all_on_closed_fails() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.close(s)\n"
                        "Socket.receive_all(s)\n");
}

static void test_socket_receive_line_on_closed_fails() {
    ASSERT_EVAL_FAILURE("socket s = Result.unwrap(Socket.udp_create())\n"
                        "Socket.close(s)\n"
                        "Socket.receive_line(s)\n");
}

static void test_socket_operations_reject_non_socket_extended() {
    ASSERT_THROWS(eval("Socket.send_all(42, \"x\")"));
    ASSERT_THROWS(eval("Socket.send_bytes(42, [1])"));
    ASSERT_THROWS(eval("Socket.receive_all(42)"));
    ASSERT_THROWS(eval("Socket.receive_line(42)"));
    ASSERT_THROWS(eval("Socket.receive_bytes(42, 10)"));
}

static void test_socket_connect_timeout_invalid_port() {
    ASSERT_EVAL_FAILURE("Socket.connect_timeout(\"127.0.0.1\", 99999, 1000)");
}

static void test_socket_connect_timeout_negative_timeout() {
    ASSERT_EVAL_FAILURE("Socket.connect_timeout(\"127.0.0.1\", 80, -1)");
}

static void test_socket_tcp_send_all_receive_all_roundtrip() {
    // The client sends a payload with send_all, then closes; the server drains
    // the whole stream with receive_all until the peer's orderly shutdown.
    const auto v =
        eval("socket server = Result.unwrap(Socket.listen(\"127.0.0.1\", 0))\n"
             "array<string> parts = String.split(Result.unwrap(Socket.local_address(server)), "
             "\":\")\n"
             "integer port = Result.unwrap(Converter.to_integer(parts[1]))\n"
             "socket client = Result.unwrap(Socket.connect(\"127.0.0.1\", port))\n"
             "socket conn = Result.unwrap(Socket.accept(server))\n"
             "result<boolean> _t = Socket.set_timeout(conn, 2000)\n"
             "result<boolean> _s = Socket.send_all(client, \"hello world\")\n"
             "Socket.close(client)\n"
             "string got = Result.unwrap(Socket.receive_all(conn))\n"
             "Socket.close(conn)\n"
             "Socket.close(server)\n"
             "got\n");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello world");
}

static void test_socket_tcp_receive_line_roundtrip() {
    // receive_line returns bytes up to and including the first newline.
    const auto v =
        eval("socket server = Result.unwrap(Socket.listen(\"127.0.0.1\", 0))\n"
             "array<string> parts = String.split(Result.unwrap(Socket.local_address(server)), "
             "\":\")\n"
             "integer port = Result.unwrap(Converter.to_integer(parts[1]))\n"
             "socket client = Result.unwrap(Socket.connect(\"127.0.0.1\", port))\n"
             "socket conn = Result.unwrap(Socket.accept(server))\n"
             "result<boolean> _t = Socket.set_timeout(conn, 2000)\n"
             "result<integer> _s = Socket.send(client, \"line1\\nline2\\n\")\n"
             "string got = Result.unwrap(Socket.receive_line(conn))\n"
             "Socket.close(client)\n"
             "Socket.close(conn)\n"
             "Socket.close(server)\n"
             "got\n");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "line1\n");
}

static void test_socket_tcp_send_bytes_receive_bytes_roundtrip() {
    // send_bytes writes raw bytes; receive_bytes reads them back as integers.
    const auto v =
        eval("function integer byte_echo() {\n"
             "    socket server = Result.unwrap(Socket.listen(\"127.0.0.1\", 0))\n"
             "    array<string> parts = String.split(Result.unwrap(Socket.local_address(server)), "
             "\":\")\n"
             "    integer port = Result.unwrap(Converter.to_integer(parts[1]))\n"
             "    socket client = Result.unwrap(Socket.connect(\"127.0.0.1\", port))\n"
             "    socket conn = Result.unwrap(Socket.accept(server))\n"
             "    result<boolean> _t = Socket.set_timeout(conn, 2000)\n"
             "    result<integer> _s = Socket.send_bytes(client, [10, 20, 30])\n"
             "    array<integer> got = Result.unwrap(Socket.receive_bytes(conn, 1024))\n"
             "    Socket.close(client)\n"
             "    Socket.close(conn)\n"
             "    Socket.close(server)\n"
             "    return got[0] + got[1] + got[2]\n"
             "}\n"
             "byte_echo()\n");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 60);
}

static void test_socket_connect_timeout_roundtrip() {
    // connect_timeout completes a loopback handshake within the deadline.
    const auto v = eval("socket server = Result.unwrap(Socket.listen(\"127.0.0.1\", 0))\n"
                        "array<string> parts = String.split(Result.unwrap(Socket.local_address("
                        "server)), \":\")\n"
                        "integer port = Result.unwrap(Converter.to_integer(parts[1]))\n"
                        "result<socket> r = Socket.connect_timeout(\"127.0.0.1\", port, 2000)\n"
                        "Socket.close(server)\n"
                        "Result.is_success(r)\n");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
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
    RUN(test_socket_parse_ipv4);
    RUN(test_socket_parse_ipv6);
    RUN(test_socket_parse_ip_invalid_fails);
    RUN(test_socket_ip_to_string_rejects_non_choice);
    RUN(test_socket_udp_bind);
    RUN(test_socket_udp_bind_rejects_invalid_port);
    RUN(test_socket_udp_create);
    RUN(test_socket_udp_send_rejects_invalid_port);
    RUN(test_socket_udp_send_rejects_non_string_data);

    RUN(test_socket_typed_registered);
    RUN(test_socket_send_typed_on_closed_is_not_connected);
    RUN(test_socket_receive_typed_on_closed_is_not_connected);
    RUN(test_socket_receive_typed_non_positive_max_bytes_is_other);
    RUN(test_socket_connect_typed_refused_is_connection_refused);
    RUN(test_socket_connect_typed_invalid_host_is_host_unreachable);
    RUN(test_socket_connect_typed_invalid_port_is_other);

    RUN(test_socket_extended_registered);
    RUN(test_socket_send_bytes_rejects_out_of_range);
    RUN(test_socket_send_bytes_rejects_negative);
    RUN(test_socket_send_all_on_closed_fails);
    RUN(test_socket_send_all_typed_on_closed_is_not_connected);
    RUN(test_socket_receive_bytes_rejects_non_positive_max);
    RUN(test_socket_receive_all_on_closed_fails);
    RUN(test_socket_receive_line_on_closed_fails);
    RUN(test_socket_operations_reject_non_socket_extended);
    RUN(test_socket_connect_timeout_invalid_port);
    RUN(test_socket_connect_timeout_negative_timeout);
    RUN(test_socket_tcp_send_all_receive_all_roundtrip);
    RUN(test_socket_tcp_receive_line_roundtrip);
    RUN(test_socket_tcp_send_bytes_receive_bytes_roundtrip);
    RUN(test_socket_connect_timeout_roundtrip);

    return SUMMARY();
}
