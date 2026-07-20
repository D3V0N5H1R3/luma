#include <cstdio>
#include <memory>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "lsp_server.hpp"
#include "lsp_transport.hpp"

int main() {
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
