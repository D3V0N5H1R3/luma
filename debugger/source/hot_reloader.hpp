#ifndef LUMA_DAP_HOT_RELOADER_HPP
#define LUMA_DAP_HOT_RELOADER_HPP

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "debugger_config.hpp"
#include "i_filesystem_monitor.hpp"

namespace luma::dap {

// Outcome of a watch() call — enables callers to distinguish between
// success, duplicate watches, and filesystem errors.
enum class WatchResult {
    Success,        // File is now being watched.
    AlreadyWatched, // File was already being watched (no-op).
    PathError,      // Failed to canonicalize the source path.
    StatError,      // Failed to read the file's last-modified time.
};

// Watches source files and triggers a callback when changes are detected.
// Uses filesystem last-modified timestamps for polling-based change detection.
//
// NOTE: The Luma VM does not support replacing bytecode mid-execution.
// When a source file changes, the hot reloader notifies the debugger which
// sends DAP output and invalidated events so the editor can refresh.
// A full restart (via luma/hotReload) is required to run the new code.
//
// Thread safety: all public methods acquire mutex_.  The ReloadCallback and
// ErrorCallback are invoked while the mutex is held, so callbacks must not
// call back into HotReloader (non-reentrant).
class HotReloader {
public:
    using ReloadCallback = std::function<void(const std::filesystem::path& changed_file)>;
    using ErrorCallback = std::function<void(const std::string& message)>;

    explicit HotReloader(ReloadCallback callback,
                         std::unique_ptr<IFilesystemMonitor> fs_monitor = nullptr,
                         ErrorCallback error_cb = nullptr);

    ~HotReloader() {
        // File system watches are automatically cleaned up by OS.
        // No explicit resource cleanup needed beyond RAII members.
    }

    // Register a source file to watch. Records its last-modified time.
    // Returns the outcome so callers can decide whether to log, retry, or abort.
    [[nodiscard]] WatchResult watch(const std::filesystem::path& source_path);

    // Check all watched files for changes. Calls the callback for each changed file.
    // Returns the number of files that changed.
    [[nodiscard]] int check_for_changes();

    // Remove all watches.
    void clear();

private:
    struct WatchedFile {
        std::filesystem::path path;
        std::filesystem::file_time_type last_modified;
    };

    // Minimum interval between filesystem polls.  500 ms balances
    // responsiveness with CPU overhead — lower values cause noticeable
    // load on large projects, while higher values delay reload feedback.
    static constexpr auto min_check_interval = config::hot_reload::k_min_check_interval;

    // Leaf-level lock — never held while acquiring any other mutex.
    mutable std::mutex mutex_;
    std::vector<WatchedFile> watched_files_;                // GUARDED_BY(mutex_)
    ReloadCallback callback_;                               // GUARDED_BY(mutex_)
    ErrorCallback error_callback_;                          // GUARDED_BY(mutex_)
    std::chrono::steady_clock::time_point last_check_time_; // GUARDED_BY(mutex_)
    std::unique_ptr<IFilesystemMonitor> fs_monitor_;        // GUARDED_BY(mutex_)
};

} // namespace luma::dap

#endif // LUMA_DAP_HOT_RELOADER_HPP
