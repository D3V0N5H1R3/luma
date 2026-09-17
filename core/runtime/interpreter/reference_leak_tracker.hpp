#ifndef LUMA_INTERPRETER_REFERENCE_LEAK_TRACKER_HPP
#define LUMA_INTERPRETER_REFERENCE_LEAK_TRACKER_HPP

#include <cstddef>

namespace luma {

// Process-global diagnostics for reference cells (the `reference<T>` type).
//
// Why this exists
// ───────────────
// Luma's value graph is otherwise acyclic: primitives and compound values have
// value semantics (operations return new copies) and closures deep-copy their
// captures, so ordinary values cannot form cycles.  The one exception is
// `reference<T>` — the only mutable, shared, aliasing cell — whose contents can
// be reassigned (via Reference.set / update / swap) to a structure that
// transitively contains the reference itself.  That forms a std::shared_ptr
// cycle, and because heap values are reclaimed purely by reference counting
// (there is no cycle collector), the cell then leaks: it stays alive after it
// has become unreachable from any Luma root.
//
// For short-lived scripts this is harmless (the OS reclaims everything at
// exit), but a long-running process that repeatedly builds and drops such
// cycles leaks steadily.  This tracker makes that leak observable: it counts
// live reference cells so long-running programs and the soak tests can detect
// and diagnose reference-cycle leaks, and prove that acyclic reference use is
// reclaimed as expected.
//
// Design
// ──────
// The counter is maintained unconditionally — two relaxed atomic operations per
// Reference.new and per cell destruction.  Reference cells are not on any hot
// path (they are created only by Reference.new, never by copying or
// deep_copy), so the cost is negligible and a shutdown-time snapshot is always
// accurate.  Only *reporting* is opt-in, via enable_reporting() or the
// LUMA_DIAGNOSE_REFERENCE_CYCLES environment variable.
//
// This is deliberately a detector, not a collector: it never frees anything, so
// it cannot cause a use-after-free.  Programs break a cycle the same way they
// build one — by reassigning the cell (for example `Reference.set(r, none)`)
// before dropping the last handle to it.
class ReferenceLeakTracker {
public:
    // Number of reference cells currently alive process-wide.
    [[nodiscard]] static std::size_t live_cells() noexcept;

    // Total reference cells ever created (monotonic; for diagnostics/tests).
    [[nodiscard]] static std::size_t total_created() noexcept;

    // Enable or disable shutdown reporting programmatically.  Overrides the
    // environment variable for the remainder of the process.
    static void enable_reporting(bool enabled) noexcept;

    // True if reporting is enabled, either programmatically or via the
    // LUMA_DIAGNOSE_REFERENCE_CYCLES environment variable (any value other
    // than "0"/"false"/empty).  The environment value is resolved once and
    // cached.
    [[nodiscard]] static bool reporting_enabled() noexcept;

    // If reporting is enabled and live_cells() > 0, write a one-line leak
    // summary to std::cerr.  Returns the number of leaked cells reported (0
    // when reporting is disabled or nothing leaked).
    //
    // Call this only after the interpreter and its environment have been
    // destroyed, so that any remaining reference cells are genuine leaks.
    static std::size_t report_if_enabled();
};

// Construction/destruction hooks invoked from ReferenceValue.  Declared as free
// functions so the widely-included value header need not depend on the tracker.
void note_reference_constructed() noexcept;
void note_reference_destroyed() noexcept;

} // namespace luma

#endif // LUMA_INTERPRETER_REFERENCE_LEAK_TRACKER_HPP
