#ifndef LUMA_DAP_EXCEPTION_BREAKPOINT_SETTINGS_HPP
#define LUMA_DAP_EXCEPTION_BREAKPOINT_SETTINGS_HPP

#include <atomic>
#include <string>
#include <vector>

#include "dap_types.hpp"

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// ExceptionBreakpointSettings — atomic flags for exception
// filter breakpoints ("caught" / "uncaught").
//
// These are std::atomic<bool> so the VM's hot-path can query
// them without acquiring the breakpoint mutex.
// ═══════════════════════════════════════════════════════════

struct ExceptionBreakpointSettings {
    std::atomic<bool> break_on_caught{false};
    std::atomic<bool> break_on_uncaught{false};

    void set_exception_breakpoints(const std::vector<std::string>& filters) {
        break_on_caught.store(false, std::memory_order_release);
        break_on_uncaught.store(false, std::memory_order_release);

        for (const auto& filter : filters) {
            if (filter == kFilterCaught) {
                break_on_caught.store(true, std::memory_order_release);
            } else if (filter == kFilterUncaught) {
                break_on_uncaught.store(true, std::memory_order_release);
            }
        }
    }
};

// Memory ordering rationale:
// - Stores use memory_order_release so that the filter state published by
//   the protocol thread is visible to the VM's exception hook.
// - Loads (in on_exception) use memory_order_acquire to pair with the
//   release stores, ensuring the VM sees the most recent filter settings.

} // namespace luma::dap

#endif // LUMA_DAP_EXCEPTION_BREAKPOINT_SETTINGS_HPP
