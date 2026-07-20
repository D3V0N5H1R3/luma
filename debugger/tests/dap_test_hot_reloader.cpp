// DAP hot-reloader tests — watch registration, change detection, and poll
// throttling exercised deterministically through the IFilesystemMonitor seam.
//
// A mock monitor replaces std::filesystem so timestamps and error conditions
// are fully controlled: no real files are touched and no wall-clock sleeps are
// needed for the detection paths.  The only real-time dependency is the 500 ms
// poll throttle in check_for_changes(), which keys off steady_clock; the tests
// that need a fresh HotReloader per case exploit the fact that the first poll
// after construction always fires (last_check_time_ starts at the clock epoch).

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "debugger_config.hpp"
#include "hot_reloader.hpp"
#include "i_filesystem_monitor.hpp"
#include "test_framework.hpp"

using namespace luma::dap;

namespace {

using FileTime = std::filesystem::file_time_type;

// Slightly longer than the reloader's internal poll throttle, so a poll issued
// after sleeping this long is guaranteed to run rather than being suppressed.
constexpr auto min_poll_interval =
    config::hot_reload::k_min_check_interval + std::chrono::milliseconds(50);

// Fabricate a distinct file_time_type from a whole-second offset so tests can
// express "modified" vs "unmodified" without touching the real clock.
[[nodiscard]] FileTime seconds(int s) {
    return FileTime{} + std::chrono::seconds(s);
}

// Deterministic IFilesystemMonitor double.  canonical() is identity (the input
// path is treated as already canonical) unless fail_canonical is set; stat
// timestamps come from an explicit map unless fail_stat is set.
class MockFilesystemMonitor final : public IFilesystemMonitor {
public:
    bool fail_canonical{false};
    bool fail_stat{false};
    std::map<std::filesystem::path, FileTime> mtimes;

    [[nodiscard]] std::filesystem::path canonical(const std::filesystem::path& p,
                                                  std::error_code& ec) const override {
        if (fail_canonical) {
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return {};
        }
        ec.clear();
        return p;
    }

    [[nodiscard]] FileTime last_write_time(const std::filesystem::path& p,
                                           std::error_code& ec) const override {
        if (fail_stat) {
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return {};
        }
        ec.clear();
        if (const auto it = mtimes.find(p); it != mtimes.end()) {
            return it->second;
        }
        return {};
    }
};

// Bundles a HotReloader with the raw mock pointer (kept live by the reloader)
// and the captured reload/error notifications.
struct ReloaderFixture {
    std::vector<std::filesystem::path> reloaded;
    std::vector<std::string> errors;
    MockFilesystemMonitor* mock{nullptr};
    std::unique_ptr<HotReloader> reloader;

    explicit ReloaderFixture() {
        auto owned = std::make_unique<MockFilesystemMonitor>();
        mock = owned.get();
        reloader = std::make_unique<HotReloader>(
            [this](const std::filesystem::path& p) { reloaded.push_back(p); }, std::move(owned),
            [this](const std::string& msg) { errors.push_back(msg); });
    }
};

// ─── watch() outcomes ─────────────────────────────────────────────

void test_watch_success() {
    ReloaderFixture fx;
    fx.mock->mtimes[std::filesystem::path{"a.luma"}] = seconds(10);

    ASSERT_TRUE(fx.reloader->watch("a.luma") == WatchResult::Success);
    ASSERT_TRUE(fx.errors.empty());
}

void test_watch_duplicate_is_reported() {
    ReloaderFixture fx;
    fx.mock->mtimes[std::filesystem::path{"a.luma"}] = seconds(10);

    ASSERT_TRUE(fx.reloader->watch("a.luma") == WatchResult::Success);
    // Same canonical path — second watch is a no-op.
    ASSERT_TRUE(fx.reloader->watch("a.luma") == WatchResult::AlreadyWatched);
}

void test_watch_path_error() {
    ReloaderFixture fx;
    fx.mock->fail_canonical = true;

    ASSERT_TRUE(fx.reloader->watch("bogus.luma") == WatchResult::PathError);
    // The error callback receives a canonicalisation diagnostic.
    ASSERT_EQ(fx.errors.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(fx.errors[0].find("canonicalize") != std::string::npos);
}

void test_watch_stat_error() {
    ReloaderFixture fx;
    // canonical() succeeds, last_write_time() fails.
    fx.mock->fail_stat = true;

    ASSERT_TRUE(fx.reloader->watch("a.luma") == WatchResult::StatError);
    ASSERT_EQ(fx.errors.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(fx.errors[0].find("stat file") != std::string::npos);
}

// ─── check_for_changes() detection ────────────────────────────────

void test_check_detects_modification() {
    ReloaderFixture fx;
    const std::filesystem::path p{"a.luma"};
    fx.mock->mtimes[p] = seconds(10);
    ASSERT_TRUE(fx.reloader->watch(p) == WatchResult::Success);

    // Bump the timestamp, then poll (first poll after construction always fires).
    fx.mock->mtimes[p] = seconds(20);
    ASSERT_EQ(fx.reloader->check_for_changes(), 1);

    // The reload callback fired exactly once with the changed path.
    ASSERT_EQ(fx.reloaded.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(fx.reloaded[0] == p);
}

void test_check_no_change_returns_zero() {
    ReloaderFixture fx;
    const std::filesystem::path p{"a.luma"};
    fx.mock->mtimes[p] = seconds(10);
    ASSERT_TRUE(fx.reloader->watch(p) == WatchResult::Success);

    // Timestamp unchanged — nothing to report.
    ASSERT_EQ(fx.reloader->check_for_changes(), 0);
    ASSERT_TRUE(fx.reloaded.empty());
}

void test_check_counts_multiple_files() {
    ReloaderFixture fx;
    const std::filesystem::path p1{"a.luma"};
    const std::filesystem::path p2{"b.luma"};
    fx.mock->mtimes[p1] = seconds(10);
    fx.mock->mtimes[p2] = seconds(10);
    ASSERT_TRUE(fx.reloader->watch(p1) == WatchResult::Success);
    ASSERT_TRUE(fx.reloader->watch(p2) == WatchResult::Success);

    fx.mock->mtimes[p1] = seconds(20);
    fx.mock->mtimes[p2] = seconds(30);
    ASSERT_EQ(fx.reloader->check_for_changes(), 2);
    ASSERT_EQ(fx.reloaded.size(), static_cast<std::size_t>(2));
}

void test_check_updates_baseline() {
    // After a change is reported, the new timestamp becomes the baseline, so a
    // later poll of that same (now stable) timestamp reports nothing.  A real
    // sleep is needed to clear the 500 ms poll throttle between the two polls.
    ReloaderFixture fx;
    const std::filesystem::path p{"a.luma"};
    fx.mock->mtimes[p] = seconds(10);
    ASSERT_TRUE(fx.reloader->watch(p) == WatchResult::Success);

    // First poll: the change from 10 -> 20 is reported and 20 becomes the baseline.
    fx.mock->mtimes[p] = seconds(20);
    ASSERT_EQ(fx.reloader->check_for_changes(), 1);

    // Wait out the throttle, then poll again with the timestamp unchanged.
    std::this_thread::sleep_for(min_poll_interval);
    ASSERT_EQ(fx.reloader->check_for_changes(), 0);
    ASSERT_EQ(fx.reloaded.size(), static_cast<std::size_t>(1));
}

void test_check_stat_error_skips_file() {
    ReloaderFixture fx;
    const std::filesystem::path p{"a.luma"};
    fx.mock->mtimes[p] = seconds(10);
    ASSERT_TRUE(fx.reloader->watch(p) == WatchResult::Success);

    // A transient stat failure during polling is logged and skipped, not fatal.
    fx.mock->fail_stat = true;
    ASSERT_EQ(fx.reloader->check_for_changes(), 0);
    ASSERT_EQ(fx.errors.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(fx.errors[0].find("last write time") != std::string::npos);
}

// ─── Poll throttling ──────────────────────────────────────────────

void test_poll_is_throttled() {
    ReloaderFixture fx;
    const std::filesystem::path p{"a.luma"};
    fx.mock->mtimes[p] = seconds(10);
    ASSERT_TRUE(fx.reloader->watch(p) == WatchResult::Success);

    // First poll fires and detects the change.
    fx.mock->mtimes[p] = seconds(20);
    ASSERT_EQ(fx.reloader->check_for_changes(), 1);

    // A second change immediately afterwards is suppressed by the 500 ms
    // minimum poll interval, so the second poll returns 0.
    fx.mock->mtimes[p] = seconds(30);
    ASSERT_EQ(fx.reloader->check_for_changes(), 0);
}

// ─── clear() ──────────────────────────────────────────────────────

void test_clear_removes_watches() {
    ReloaderFixture fx;
    fx.mock->mtimes[std::filesystem::path{"a.luma"}] = seconds(10);
    ASSERT_TRUE(fx.reloader->watch("a.luma") == WatchResult::Success);
    ASSERT_TRUE(fx.reloader->watch("a.luma") == WatchResult::AlreadyWatched);

    fx.reloader->clear();

    // After clearing, the same path can be watched fresh again.
    ASSERT_TRUE(fx.reloader->watch("a.luma") == WatchResult::Success);
}

// ─── Default monitor construction ─────────────────────────────────

void test_default_monitor_reports_missing_file() {
    // With no injected monitor, HotReloader falls back to StdFilesystemMonitor.
    // Watching a path that does not exist must surface a PathError rather than
    // crashing — exercises the real std::filesystem canonical() error path.
    std::vector<std::string> errors;
    HotReloader reloader{[](const std::filesystem::path&) {}, nullptr,
                         [&errors](const std::string& msg) {
                             errors.push_back(msg);
                         }};

    const auto result = reloader.watch("this_path_does_not_exist_zzz.luma");
    ASSERT_TRUE(result == WatchResult::PathError);
    ASSERT_FALSE(errors.empty());
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Hot Reloader Tests");

    // watch() outcomes.
    RUN(test_watch_success);
    RUN(test_watch_duplicate_is_reported);
    RUN(test_watch_path_error);
    RUN(test_watch_stat_error);

    // check_for_changes() detection.
    RUN(test_check_detects_modification);
    RUN(test_check_no_change_returns_zero);
    RUN(test_check_counts_multiple_files);
    RUN(test_check_updates_baseline);
    RUN(test_check_stat_error_skips_file);

    // Poll throttling.
    RUN(test_poll_is_throttled);

    // clear().
    RUN(test_clear_removes_watches);

    // Default (real) monitor.
    RUN(test_default_monitor_reports_missing_file);

    return SUMMARY();
}
