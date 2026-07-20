#pragma once

#include <cstddef>
#include <vector>

namespace luma {

// Tracks active loops within a single compiler scope.
//
// Each time the compiler enters a loop it pushes a LoopInfo entry via
// push(); when the loop body completes, end_loop() patches all break
// jumps and pops the entry.  During break/continue the context provides
// accessors for the current loop's start offset and break list.
class LoopContext {
public:
    // Push a new loop onto the stack.
    void push(std::size_t start_offset, int scope_depth, std::size_t try_depth) {
        loops_.push_back({start_offset, scope_depth, {}, try_depth});
    }

    // Record a break jump offset for later patching.
    void add_break(std::size_t offset) {
        loops_.back().breaks.push_back(offset);
    }

    // Patch all break jumps using the provided callback, then pop the loop.
    // Templatised to avoid std::function heap allocation overhead — all
    // call sites pass small lambdas from the same translation unit.
    template <typename PatchFn> void end_loop(PatchFn&& patch_fn) {
        for (auto offset : loops_.back().breaks) {
            patch_fn(offset);
        }
        loops_.pop_back();
    }

    // Start offset of the current (innermost) loop — target for continue.
    [[nodiscard]] std::size_t current_start() const {
        return loops_.back().start;
    }

    // Scope depth at loop entry — used to determine how many locals to pop.
    [[nodiscard]] int current_scope_depth() const {
        return loops_.back().scope_depth;
    }

    // Exception context depth when the current loop was entered.
    [[nodiscard]] std::size_t current_try_depth_at_entry() const {
        return loops_.back().try_depth_at_entry;
    }

    // True when at least one loop is active.
    [[nodiscard]] bool is_active() const {
        return !loops_.empty();
    }

private:
    struct LoopInfo {
        std::size_t start{0};
        int scope_depth{0};
        std::vector<std::size_t> breaks;
        std::size_t try_depth_at_entry{0};
    };

    std::vector<LoopInfo> loops_;
};

} // namespace luma
