#include "time_travel.hpp"

#include <algorithm>

#include "runtime/vm/vm.hpp"

namespace luma::dap {

// ─── TimeTravelRecorder ───

TimeTravelRecorder::TimeTravelRecorder(TimeTravelConfig config) : config_{config} {}

void TimeTravelRecorder::on_line(const VM& vm, int file_id, int line, std::size_t frame_depth) {
    const std::scoped_lock lock(mutex_);
    instruction_count_++;
    lines_since_snapshot_++;

    if (!config_.enabled) {
        return;
    }

    if (lines_since_snapshot_ >= config_.snapshot_interval) {
        take_snapshot_unlocked(vm, file_id, line, frame_depth);
        lines_since_snapshot_ = 0;
    }
}

void TimeTravelRecorder::take_snapshot(const VM& vm, int file_id, int line,
                                       std::size_t frame_depth) {
    const std::scoped_lock lock(mutex_);
    take_snapshot_unlocked(vm, file_id, line, frame_depth);
}

void TimeTravelRecorder::take_snapshot_unlocked(const VM& vm, int file_id, int line,
                                                std::size_t frame_depth) {
    VMSnapshot snap;
    snap.instruction_count = instruction_count_;
    snap.file_id = file_id;
    snap.line = line;
    snap.frame_depth = frame_depth;

    // Capture VM state — full copy by design (see COW analysis in header).
    snap.stack = std::vector<Value>(vm.stack().begin(), vm.stack().end());
    snap.frames = vm.frames();

    // Metadata.
    if (!snap.frames.empty()) {
        const auto& top_frame = snap.frames.back();
        if (top_frame.function != nullptr) {
            snap.function_name = top_frame.function->name;
        }
    }

    // Evict snapshots to make room for the new one.
    auto size = estimate_snapshot_size(snap);
    int evictions = 0;
    while (memory_used_ + size > config_.max_memory_bytes && !snapshots_.empty() &&
           evictions < config::time_travel::k_max_evictions_per_snapshot) {
        evict_oldest();
        ++evictions;
    }
    while (snapshots_.size() >= config_.max_snapshots && !snapshots_.empty()) {
        evict_oldest();
    }

    memory_used_ += size;
    snapshots_.push_back(std::move(snap));
    current_index_ = snapshots_.size() - 1;
}

const VMSnapshot* TimeTravelRecorder::snapshot_before(std::uint64_t target_ic) const {
    const std::scoped_lock lock(mutex_);
    const VMSnapshot* best = nullptr;
    for (const auto& snap : snapshots_) {
        if (snap.instruction_count <= target_ic) {
            best = &snap;
        } else {
            break; // Snapshots are ordered by instruction count.
        }
    }
    return best;
}

const VMSnapshot* TimeTravelRecorder::snapshot_at(std::size_t index) const {
    const std::scoped_lock lock(mutex_);
    if (index < snapshots_.size()) {
        return &snapshots_[index];
    }
    return nullptr;
}

const VMSnapshot* TimeTravelRecorder::latest() const {
    const std::scoped_lock lock(mutex_);
    if (snapshots_.empty()) {
        return nullptr;
    }
    return &snapshots_.back();
}

std::optional<VMSnapshot> TimeTravelRecorder::step_back(std::size_t steps) const {
    const std::scoped_lock lock(mutex_);
    if (snapshots_.empty()) {
        return std::nullopt;
    }

    if (steps > current_index_) {
        return snapshots_.front();
    }

    return snapshots_[current_index_ - steps];
}

std::vector<const VMSnapshot*> TimeTravelRecorder::snapshots_at_line(int file_id, int line) const {
    const std::scoped_lock lock(mutex_);
    std::vector<const VMSnapshot*> result;
    for (const auto& snap : snapshots_) {
        if (snap.file_id == file_id && snap.line == line) {
            result.push_back(&snap);
        }
    }
    return result;
}

void TimeTravelRecorder::clear() {
    const std::scoped_lock lock(mutex_);
    snapshots_.clear();
    memory_used_ = 0;
    instruction_count_ = 0;
    lines_since_snapshot_ = 0;
    current_index_ = 0;
}

void TimeTravelRecorder::evict_oldest() {
    if (snapshots_.empty()) {
        return;
    }
    memory_used_ -= estimate_snapshot_size(snapshots_.front());
    snapshots_.pop_front();
    if (current_index_ > 0) {
        current_index_--;
    }
}

std::size_t TimeTravelRecorder::estimate_snapshot_size(const VMSnapshot& snap) const {
    std::size_t size = sizeof(VMSnapshot);
    size += snap.stack.size() * sizeof(Value);
    size += snap.frames.size() * sizeof(CallFrame);
    size += snap.function_name.capacity();
    size += snap.source_line_text.capacity();
    return size;
}

// ─── ReplayEngine ───

void ReplayEngine::restore_snapshot(VM& vm, const VMSnapshot& snapshot) {
    // Restore the VM's value stack from the snapshot.
    vm.restore_stack(snapshot.stack);

    // Frame and IP restoration is intentionally not performed: the VM does
    // not expose frame/IP restoration APIs, and resuming execution from a
    // restored past state (true reverse execution) is out of scope.  Stack-
    // only restoration is sufficient for inspecting earlier values at a
    // snapshot point but not for resuming execution from that point.
}

} // namespace luma::dap
