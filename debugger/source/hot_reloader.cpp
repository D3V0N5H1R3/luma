#include "hot_reloader.hpp"

#include <algorithm>

#include "diagnostic_log.hpp"

namespace luma::dap {

HotReloader::HotReloader(ReloadCallback callback, std::unique_ptr<IFilesystemMonitor> fs_monitor,
                         ErrorCallback error_cb)
    : callback_(std::move(callback)),
      error_callback_(std::move(error_cb)),
      fs_monitor_(fs_monitor ? std::move(fs_monitor) : std::make_unique<StdFilesystemMonitor>()) {}

WatchResult HotReloader::watch(const std::filesystem::path& source_path) {
    std::error_code ec;
    const auto canonical = fs_monitor_->canonical(source_path, ec);

    if (ec) {
        report_or_log(error_callback_, "hot_reloader: failed to canonicalize path: " +
                                           source_path.string() + " (" + ec.message() + ")");
        return WatchResult::PathError;
    }

    const std::scoped_lock lock(mutex_);

    // Avoid duplicate watches for the same canonical path.
    const auto it = std::ranges::find_if(
        watched_files_, [&canonical](const WatchedFile& w) { return w.path == canonical; });

    if (it != watched_files_.end()) {
        return WatchResult::AlreadyWatched;
    }

    const auto mtime = fs_monitor_->last_write_time(canonical, ec);

    if (ec) {
        report_or_log(error_callback_, "hot_reloader: failed to stat file: " + canonical.string() +
                                           " (" + ec.message() + ")");
        return WatchResult::StatError;
    }

    watched_files_.push_back({.path = canonical, .last_modified = mtime});
    return WatchResult::Success;
}

int HotReloader::check_for_changes() {
    const std::scoped_lock lock(mutex_);

    const auto now = std::chrono::steady_clock::now();

    if (now - last_check_time_ < min_check_interval) {
        return 0;
    }

    int changed_count = 0;

    for (auto& entry : watched_files_) {
        std::error_code ec;
        const auto current_mtime = fs_monitor_->last_write_time(entry.path, ec);

        if (ec) {
            // Intentionally log-and-skip: check_for_changes() is called on a
            // polling timer, so transient filesystem errors (deleted files,
            // permission changes) should not abort the entire poll cycle.
            // Callers observe the reduced changed_count but do not need to
            // handle individual per-file errors.
            report_or_log(error_callback_, "hot_reloader: failed to read last write time: " +
                                               entry.path.string() + " (" + ec.message() + ")");
            continue;
        }

        if (current_mtime != entry.last_modified) {
            entry.last_modified = current_mtime;
            ++changed_count;

            if (callback_) {
                callback_(entry.path);
            }
        }
    }

    last_check_time_ = std::chrono::steady_clock::now();

    return changed_count;
}

void HotReloader::clear() {
    const std::scoped_lock lock(mutex_);
    watched_files_.clear();
}

} // namespace luma::dap
