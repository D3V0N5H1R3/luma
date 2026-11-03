#ifndef LUMA_DAP_DIAGNOSTIC_LOG_HPP
#define LUMA_DAP_DIAGNOSTIC_LOG_HPP

#include <cstdio>
#include <string>
#include <string_view>

namespace luma::dap {

// Write one line of the adapter's own diagnostics to the process's stderr.
//
// Deliberately bypasses std::cerr: while a program runs, std::cerr is
// redirected into DAP output events attributed to the debuggee (see
// DebugExecutionEngine::execute_with_stream_redirect), and the adapter's
// messages must not appear as program output.  Writing through the C stream
// also avoids racing the execution thread's std::cerr.rdbuf() swaps.
inline void write_adapter_diagnostic(std::string_view message) {
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

// Report a non-fatal diagnostic through an optional callback, falling back
// to the adapter's stderr when no callback is installed.  Centralises the
// "callback-or-stderr" idiom shared by components that surface diagnostics
// out of band (e.g. BreakpointManager).
//
// The callback may take either `const std::string&` or `std::string_view`;
// `msg` binds to both.  `stderr_prefix` is written ahead of the message only
// on the stderr fallback path.
template <typename Callback>
void report_or_log(const Callback& callback, const std::string& msg,
                   std::string_view stderr_prefix = {}) {
    if (callback) {
        callback(msg);
    } else {
        write_adapter_diagnostic(std::string{stderr_prefix} + msg);
    }
}

} // namespace luma::dap

#endif // LUMA_DAP_DIAGNOSTIC_LOG_HPP
