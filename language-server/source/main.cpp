#include <cstdio>
#include <memory>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif

#include "lsp_server.hpp"
#include "lsp_transport.hpp"

int main() {
#ifndef _WIN32
    // The language server speaks LSP over stdout.  When the editor closes the
    // connection, the next write to the broken pipe delivers SIGPIPE, whose
    // default disposition terminates the process before the transport's write
    // can return an error.  Ignore it so a broken pipe instead surfaces as a
    // ConnectionClosed exception and the server shuts down cleanly.
    ::signal(SIGPIPE, SIG_IGN);
#endif

    // Set stdin/stdout to binary mode on Windows to prevent \r\n
    // translation that would corrupt Content-Length framing.
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    auto server = luma::lsp::LspServer::create(
        luma::lsp::LspServerConfig{.transport = std::make_unique<luma::lsp::StdioTransport>()});

    if (!server) {
        return 1;
    }

    return server->run();
}
