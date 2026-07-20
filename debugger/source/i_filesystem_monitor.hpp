#ifndef LUMA_DAP_I_FILESYSTEM_MONITOR_HPP
#define LUMA_DAP_I_FILESYSTEM_MONITOR_HPP

#include <filesystem>
#include <system_error>

namespace luma::dap {

// Abstraction over filesystem queries, used by HotReloader to
// canonicalize paths and read last-write timestamps without calling
// std::filesystem directly.  Enables deterministic testing.
class IFilesystemMonitor {
public:
    virtual ~IFilesystemMonitor() = default;

    // Canonicalize a path.  Sets `ec` on failure.
    [[nodiscard]] virtual std::filesystem::path canonical(const std::filesystem::path& p,
                                                          std::error_code& ec) const = 0;

    // Query the last modification time.  Sets `ec` on failure.
    [[nodiscard]] virtual std::filesystem::file_time_type
    last_write_time(const std::filesystem::path& p, std::error_code& ec) const = 0;
};

// Default implementation that delegates directly to std::filesystem.
class StdFilesystemMonitor final : public IFilesystemMonitor {
public:
    [[nodiscard]] std::filesystem::path canonical(const std::filesystem::path& p,
                                                  std::error_code& ec) const override {
        return std::filesystem::canonical(p, ec);
    }

    [[nodiscard]] std::filesystem::file_time_type
    last_write_time(const std::filesystem::path& p, std::error_code& ec) const override {
        return std::filesystem::last_write_time(p, ec);
    }
};

} // namespace luma::dap

#endif // LUMA_DAP_I_FILESYSTEM_MONITOR_HPP
