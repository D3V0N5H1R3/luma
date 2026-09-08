#include <cstdio>
#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif

#include "dap_server.hpp"
#include "dap_transport.hpp"

int main() try { // NOLINT(bugprone-exception-escape)
#ifndef _WIN32
    // luma_dap speaks the Debug Adapter Protocol over stdout.  When the editor
    // (or a test harness) closes the connection, the next write to the broken
    // pipe delivers SIGPIPE, whose default disposition terminates the process
    // before the transport's write can return an error.  Ignore it so a broken
    // pipe instead surfaces as a ConnectionClosed exception and the adapter
    // shuts down cleanly (exit 0) rather than being killed by the signal.
    ::signal(SIGPIPE, SIG_IGN);
#endif

    // Set stdin/stdout to binary mode on Windows to prevent \r\n
    // translation that would corrupt Content-Length framing.
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    luma::dap::Transport transport;
    luma::dap::DapServer server(transport);

    return server.run();
} catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
} catch (...) {
    return 1;
}
