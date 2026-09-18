#include "runtime/interpreter/reference_leak_tracker.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>

#include "common/platform_utils.hpp"

namespace luma {

namespace {

// Live reference cells and the monotonic total ever created.  Maintained
// unconditionally; see the header for why this is cheap and always accurate.
std::atomic<std::size_t> g_live_cells{0};
std::atomic<std::size_t> g_total_created{0};

// Reporting is resolved lazily from the environment on first query, then cached.
enum class ReportingState : std::uint8_t {
    Unresolved, // Not yet consulted the environment.
    Disabled,
    Enabled,
};

std::atomic<ReportingState> g_reporting{ReportingState::Unresolved};

[[nodiscard]] bool resolve_env_reporting() {
    const auto value = safe_getenv("LUMA_DIAGNOSE_REFERENCE_CYCLES");
    if (!value || value->empty()) {
        return false;
    }

    return *value != "0" && *value != "false";
}

} // namespace

void note_reference_constructed() noexcept {
    g_live_cells.fetch_add(1, std::memory_order_relaxed);
    g_total_created.fetch_add(1, std::memory_order_relaxed);
}

void note_reference_destroyed() noexcept {
    g_live_cells.fetch_sub(1, std::memory_order_relaxed);
}

std::size_t ReferenceLeakTracker::live_cells() noexcept {
    return g_live_cells.load(std::memory_order_relaxed);
}

std::size_t ReferenceLeakTracker::total_created() noexcept {
    return g_total_created.load(std::memory_order_relaxed);
}

void ReferenceLeakTracker::enable_reporting(bool enabled) noexcept {
    g_reporting.store(enabled ? ReportingState::Enabled : ReportingState::Disabled,
                      std::memory_order_relaxed);
}

bool ReferenceLeakTracker::reporting_enabled() noexcept {
    auto state = g_reporting.load(std::memory_order_relaxed);

    if (state == ReportingState::Unresolved) {
        // Resolve from the environment and cache.  A concurrent race here is
        // benign: every racer computes the same value and the store is
        // idempotent.
        state = resolve_env_reporting() ? ReportingState::Enabled : ReportingState::Disabled;
        g_reporting.store(state, std::memory_order_relaxed);
    }

    return state == ReportingState::Enabled;
}

std::size_t ReferenceLeakTracker::report_if_enabled() {
    if (!reporting_enabled()) {
        return 0;
    }

    const auto leaked = live_cells();
    if (leaked == 0) {
        return 0;
    }

    std::cerr << "luma: reference-cycle diagnostic: " << leaked
              << " reference cell(s) still alive at shutdown out of " << total_created()
              << " created. This indicates a reference<T> cycle leak. Break such cycles by "
                 "reassigning a cell (for example Reference.set(r, none)) before dropping the "
                 "last handle to it.\n";

    return leaked;
}

} // namespace luma
