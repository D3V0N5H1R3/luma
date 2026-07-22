#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif

#include "dap_server.hpp"
#include "dap_tcp_transport.hpp"
#include "dap_transport.hpp"

int main(int argc, char* argv[]) try { // NOLINT(bugprone-exception-escape)
#ifndef _WIN32
    // luma_dap speaks the Debug Adapter Protocol over stdout.  When the editor
    // (or a test harness) closes the connection, the next write to the broken
    // pipe delivers SIGPIPE, whose default disposition terminates the process
    // before the transport's write can return an error.  Ignore it so a broken
    // pipe instead surfaces as a ConnectionClosed exception and the adapter
    // shuts down cleanly (exit 0) rather than being killed by the signal.
    ::signal(SIGPIPE, SIG_IGN);
#endif

    // Parse --port flag for TCP mode.
    std::uint16_t tcp_port = 0;
    std::string auth_token;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            try {
                const int port_value = std::stoi(argv[i + 1]);

                if (port_value < 0 || port_value > 65535) {
                    std::cerr << "error: port must be between 0 and 65535\n";
                    return 1;
                }

                tcp_port = static_cast<std::uint16_t>(port_value);
            } catch (const std::exception&) {
                std::cerr << "error: invalid port number '" << argv[i + 1] << "'\n";
                return 1;
            }

            ++i;
        } else if (std::strcmp(argv[i], "--auth-token") == 0 && i + 1 < argc) {
            auth_token = argv[i + 1];
            ++i;
        }
    }

    if (tcp_port > 0) {
        // TCP mode — listen on the specified port for a single client.
        luma::dap::TcpTransport tcp_transport(tcp_port);

        if (!tcp_transport.accept_client()) {
            std::cerr << "error: failed to accept client connection\n";
            return 1;
        }

        // TcpTransport now inherits from Transport, so it can be
        // passed directly to DapServer.
        luma::dap::DapServer server(tcp_transport);

        if (!auth_token.empty()) {
            server.enable_auth(std::move(auth_token));
        }

        return server.run();
    }

    // Default: stdio mode.
    // Set stdin/stdout to binary mode on Windows to prevent \r\n
    // translation that would corrupt Content-Length framing.
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    luma::dap::Transport transport;
    luma::dap::DapServer server(transport);

    if (!auth_token.empty()) {
        server.enable_auth(std::move(auth_token));
    }

    return server.run();
} catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
} catch (...) {
    return 1;
}
