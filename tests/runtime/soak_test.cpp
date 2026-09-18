// Long-uptime soak and reference-cycle leak validation.
//
// These tests validate the memory behaviour that matters for long-running
// Luma processes:
//
//   * Acyclic reference<T> use is fully reclaimed by reference counting, so a
//     workload that repeatedly creates and drops references reaches a steady
//     state (no growth).
//   * A genuine reference<T> cycle is the one construct that leaks under pure
//     reference counting, and the ReferenceLeakTracker detects it.
//   * Breaking the cycle (the documented mitigation — reassign the cell) lets
//     the cells be reclaimed, so the tests leave no real leak behind and stay
//     green under AddressSanitizer/LeakSanitizer.
//
// The assertions are driven by the deterministic ReferenceLeakTracker counter
// rather than by process RSS, so they are reliable in CI.  Running this binary
// under the asan/lsan build preset additionally proves the acyclic paths free
// all memory.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "runtime/interpreter/reference_leak_tracker.hpp"
#include "runtime/interpreter/value.hpp"
#include "shared_eval.hpp"
#include "test_framework.hpp"

using namespace luma;
using namespace luma::test;

namespace {

// ═══════════════════════════════════════════════════════════
// Reference-cell accounting (C++ level, no interpreter)
// ═══════════════════════════════════════════════════════════

void test_acyclic_reference_is_reclaimed() {
    const auto baseline = ReferenceLeakTracker::live_cells();

    {
        auto cell = std::make_shared<ReferenceValue>(Value{std::int64_t{42}});
        ASSERT_EQ(ReferenceLeakTracker::live_cells(), baseline + 1);
    }

    // The cell had no incoming references other than the local handle, so
    // dropping it reclaims the reference immediately.
    ASSERT_EQ(ReferenceLeakTracker::live_cells(), baseline);
}

void test_reference_cycle_leaks_then_break_reclaims() {
    const auto baseline = ReferenceLeakTracker::live_cells();

    std::weak_ptr<ReferenceValue> observer;

    {
        auto cell = std::make_shared<ReferenceValue>(Value{NullValue{}});
        observer = cell;

        // Build a cycle: the cell's contents are an array that holds the cell.
        //   cell -> array -> Value(reference to cell) -> cell
        auto array = std::make_shared<ArrayValue>();
        array->elements->push_back(Value{cell});
        cell->set(Value{array});

        // Both local handles (cell, array) go out of scope at the closing brace.
    }

    // If the graph were acyclic, dropping the local handles would have freed the
    // cell.  The cycle keeps it alive: the reference is leaked.
    ASSERT_FALSE(observer.expired());
    ASSERT_EQ(ReferenceLeakTracker::live_cells(), baseline + 1);

    // Apply the documented mitigation: reassign the cell so it no longer points
    // back into the structure that references it, breaking the cycle.
    if (auto cell = observer.lock()) {
        cell->set(Value{NullValue{}});
    }

    // With the cycle broken and the temporary handle gone, the cell is
    // reclaimed and the tracker returns to its baseline — no real leak remains.
    ASSERT_TRUE(observer.expired());
    ASSERT_EQ(ReferenceLeakTracker::live_cells(), baseline);
}

void test_total_created_is_monotonic() {
    const auto before = ReferenceLeakTracker::total_created();

    {
        auto cell = std::make_shared<ReferenceValue>(Value{std::int64_t{1}});
        (void)cell;
    }

    ASSERT_GE(ReferenceLeakTracker::total_created(), before + 1);
}

void test_reporting_toggle() {
    ReferenceLeakTracker::enable_reporting(true);
    ASSERT_TRUE(ReferenceLeakTracker::reporting_enabled());

    ReferenceLeakTracker::enable_reporting(false);
    ASSERT_FALSE(ReferenceLeakTracker::reporting_enabled());
}

// ═══════════════════════════════════════════════════════════
// Full-pipeline soak: interpreter reference paths do not leak
// ═══════════════════════════════════════════════════════════

// Repeatedly run a reference-heavy program through the whole pipeline and
// confirm the live reference-cell count returns to its baseline every time —
// i.e. no reference leaks accumulate over a long-running workload.
void test_reference_workload_reaches_steady_state() {
    const auto baseline = ReferenceLeakTracker::live_cells();

    constexpr int iterations = 200;
    for (int i = 0; i < iterations; ++i) {
        {
            // Acyclic reference use: create a counter cell, mutate it, read it.
            const auto result = eval(R"(
                mutable reference<number> counter = Reference.new(0.0)
                Reference.set(counter, 1.0)
                Reference.update(counter, (number n) -> n + 1.0)
                Reference.get(counter)
            )");
            (void)result;
        }

        // After each run's value is dropped, every reference the program made
        // must have been reclaimed.
        ASSERT_EQ(ReferenceLeakTracker::live_cells(), baseline);
    }
}

// A broader allocation soak: build and discard large collections many times.
// There is no leak vector for value-semantic collections, so this simply
// proves the pipeline stays stable over a long run (and, under LSan, frees
// everything).
void test_allocation_soak_is_stable() {
    const auto baseline = ReferenceLeakTracker::live_cells();

    constexpr int iterations = 100;
    for (int i = 0; i < iterations; ++i) {
        const auto result = eval(R"(
            array<integer> xs = Array.range(0, 500) |> Result.unwrap()
            array<integer> doubled = Array.map(xs, (integer x) -> x * 2) ?? []
            Array.length(doubled)
        )");
        (void)result;
    }

    ASSERT_EQ(ReferenceLeakTracker::live_cells(), baseline);
}

} // namespace

int main() {
    RUN(test_acyclic_reference_is_reclaimed);
    RUN(test_reference_cycle_leaks_then_break_reclaims);
    RUN(test_total_created_is_monotonic);
    RUN(test_reporting_toggle);
    RUN(test_reference_workload_reaches_steady_state);
    RUN(test_allocation_soak_is_stable);

    return SUMMARY();
}
