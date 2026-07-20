#ifndef LUMA_DAP_DIAGNOSTIC_LOG_HPP
#define LUMA_DAP_DIAGNOSTIC_LOG_HPP

#include <iostream>
#include <string>
#include <string_view>

namespace luma::dap {

// Report a non-fatal diagnostic through an optional callback, falling back
// to stderr when no callback is installed.  Centralises the
// "callback-or-stderr" idiom shared by components that surface diagnostics
// out of band (e.g. HotReloader, CustomVisualizer).
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
        std::cerr << stderr_prefix << msg << '\n';
    }
}

} // namespace luma::dap

#endif // LUMA_DAP_DIAGNOSTIC_LOG_HPP
