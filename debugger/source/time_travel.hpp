#ifndef LUMA_DAP_TIME_TRAVEL_HPP
#define LUMA_DAP_TIME_TRAVEL_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Time-Travel / Reverse Debugging
// ─────────────────────────────────────────────────────────────────────────────
// Records VM execution state at periodic intervals, enabling reverse stepping
// and state inspection at any point in the program's history.
//
// Design:
//   - A ring buffer of VM state snapshots, taken at configurable intervals.
//   - Each snapshot captures the value stack and call frames, plus light
//     metadata (instruction count, source position, function name).
//   - Reverse stepping navigates to a recorded snapshot and restores its
//     value stack so earlier values can be inspected.
//   - Bounded memory: older snapshots are evicted when the buffer is full.
//
// Copy-on-write analysis (D24):
//   Snapshots deep-copy the VM stack (vector<Value>) and call frames
//   (vector<CallFrame>) on every capture.  A copy-on-write optimisation
//   using shared_ptr was evaluated and deemed unnecessary because:
//
//   1. Data per snapshot is small.  CallFrame is 32 bytes (pointer + IP +
//      slot offset).  For a teaching language, typical stack depths are
//      10–200 elements and frame depths are 1–20.  A snapshot therefore
//      costs roughly 2–15 KB — far below any threshold where COW would
//      pay for the shared_ptr overhead (atomic reference counting, heap
//      allocation for the control block, indirection on every access).
//
//   2. Snapshots are immutable after creation.  They are never modified —
//      restore_snapshot copies FROM the snapshot TO the VM, leaving the
//      snapshot untouched.  The "write" leg of COW never triggers, so the
//      mechanism would add overhead with no benefit.
//
//   3. Consecutive stacks rarely share content.  Between two snapshot
//      intervals the VM typically pushes/pops several values, so sharing
//      the underlying buffer between adjacent snapshots would require an
//      equality comparison that is as expensive as the copy itself.
//
//   4. The existing memory budget (64 MB default, configurable) and ring-
//      buffer eviction already bound total memory.  At ~10 KB per snapshot,
//      the budget allows ~6 500 snapshots before eviction begins — more
//      than sufficient for interactive reverse debugging.
//
//   If profiling reveals snapshot creation as a bottleneck in a future
//   workload (e.g., very deep recursive programs with large stacks), the
//   recommended optimisation is incremental/delta snapshots rather than
//   COW — store only the changed portion of the stack relative to the
//   previous snapshot.
//
// For a teaching language, this enables students to understand program
// execution by stepping backwards through their code.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "debugger_config.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {
struct CallFrame;
class VM;
} // namespace luma

namespace luma::dap {

// A snapshot of the VM state at a point in time.
struct VMSnapshot {
    // Execution position.
    std::uint64_t instruction_count{0}; // Total instructions executed at this point.
    int file_id{0};
    int line{0};
    std::size_t frame_depth{0};

    // Captured state.
    std::vector<Value> stack;
    std::vector<CallFrame> frames;

    // Lightweight metadata for display.
    std::string function_name;
    std::string source_line_text;
};

// Configuration for the time-travel recorder.
struct TimeTravelConfig {
    // Take a snapshot every N source lines executed.
    std::size_t snapshot_interval{config::time_travel::k_default_snapshot_interval};

    // Maximum number of snapshots to retain (ring buffer size).
    std::size_t max_snapshots{config::time_travel::k_default_max_snapshots};

    // Maximum total memory budget for snapshots (bytes).
    // When exceeded, oldest snapshots are evicted.
    std::size_t max_memory_bytes{config::time_travel::k_default_max_memory_bytes};

    // Whether to record at all (can be toggled at runtime).
    bool enabled{true};
};

// The time-travel recorder, attached to a VM during debugging.
class TimeTravelRecorder {
public:
    explicit TimeTravelRecorder(TimeTravelConfig config = {});
    ~TimeTravelRecorder() = default;

    TimeTravelRecorder(const TimeTravelRecorder&) = delete;
    TimeTravelRecorder& operator=(const TimeTravelRecorder&) = delete;

    // Called by the debug hook on every source line.
    // Decides whether to take a snapshot based on the interval.
    void on_line(const VM& vm, int file_id, int line, std::size_t frame_depth);

    // Force a snapshot right now (e.g., on breakpoint hit).
    void take_snapshot(const VM& vm, int file_id, int line, std::size_t frame_depth);

    // ─── Reverse Navigation ───

    // Get the snapshot closest to (but not after) the given instruction count.
    [[nodiscard]] const VMSnapshot* snapshot_before(std::uint64_t instruction_count) const;

    // Get the snapshot at a specific index (0 = oldest retained).
    [[nodiscard]] const VMSnapshot* snapshot_at(std::size_t index) const;

    // Get the most recent snapshot.
    [[nodiscard]] const VMSnapshot* latest() const;

    // Get a COPY of the snapshot N steps before the current position, or
    // std::nullopt when no snapshot has been recorded.  Returning by value
    // (copied while mutex_ is held) is deliberate: a raw pointer into the deque
    // would dangle if a concurrent take_snapshot() evicted the front entry after
    // the caller released mutex_ but before it read the snapshot.
    [[nodiscard]] std::optional<VMSnapshot> step_back(std::size_t steps = 1) const;

    // Find all snapshots at a given line in a given file.
    [[nodiscard]] std::vector<const VMSnapshot*> snapshots_at_line(int file_id, int line) const;

    // ─── State ───

    [[nodiscard]] std::size_t snapshot_count() const noexcept {
        return snapshots_.size();
    }

    [[nodiscard]] std::size_t memory_used() const noexcept {
        return memory_used_;
    }

    [[nodiscard]] std::uint64_t total_instructions() const noexcept {
        return instruction_count_;
    }

    [[nodiscard]] const TimeTravelConfig& config() const noexcept {
        return config_;
    }

    // Update configuration at runtime.
    void set_enabled(bool enabled) {
        config_.enabled = enabled;
    }

    void set_interval(std::size_t interval) {
        config_.snapshot_interval = interval;
    }

    // Clear all recorded history.
    void clear();

private:
    void take_snapshot_unlocked(const VM& vm, int file_id, int line, std::size_t frame_depth);
    void evict_oldest();
    [[nodiscard]] std::size_t estimate_snapshot_size(const VMSnapshot& snap) const;

    // Leaf-level lock — never held while acquiring any other mutex.
    mutable std::mutex mutex_;
    TimeTravelConfig config_;               // GUARDED_BY(mutex_)
    std::deque<VMSnapshot> snapshots_;      // GUARDED_BY(mutex_)
    std::uint64_t instruction_count_{0};    // GUARDED_BY(mutex_)
    std::uint64_t lines_since_snapshot_{0}; // GUARDED_BY(mutex_)
    std::size_t memory_used_{0};            // GUARDED_BY(mutex_)
    std::size_t current_index_{0};          // GUARDED_BY(mutex_)
};

// ─── Replay Engine ───
// Restores recorded VM snapshots for inspection.

class ReplayEngine {
public:
    // Restore VM state from a snapshot for inspection.
    //
    // Only the value stack is restored; call frames and the instruction
    // pointer are left unchanged because the VM does not expose frame/IP
    // restoration.  This is sufficient for inspecting earlier values at a
    // snapshot point but not for resuming execution from it.
    static void restore_snapshot(VM& vm, const VMSnapshot& snapshot);
};

} // namespace luma::dap

#endif // LUMA_DAP_TIME_TRAVEL_HPP
