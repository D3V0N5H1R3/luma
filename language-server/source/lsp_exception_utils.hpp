#ifndef LUMA_LSP_EXCEPTION_UTILS_HPP
#define LUMA_LSP_EXCEPTION_UTILS_HPP

#include <exception>
#include <string>

namespace luma::lsp {

// Format the message from the current in-flight exception.
// Call this only inside a catch(...) block where std::current_exception()
// is valid.  Returns "unknown error" for non-std::exception types.
[[nodiscard]] inline std::string format_current_exception() {
    try {
        std::rethrow_exception(std::current_exception());
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown error";
    }
}

} // namespace luma::lsp

#endif // LUMA_LSP_EXCEPTION_UTILS_HPP
