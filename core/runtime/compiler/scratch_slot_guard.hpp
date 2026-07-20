#ifndef LUMA_COMPILER_SCRATCH_SLOT_GUARD_HPP
#define LUMA_COMPILER_SCRATCH_SLOT_GUARD_HPP

#include <cstddef>

#include "analysis/source/source_location.hpp"

namespace luma {

// RAII guard that reserves placeholder local ("scratch") slots on construction
// and releases exactly the same count on destruction. Mirrors ScopeDepthGuard:
// pairing the reserve/release calls means the count is stated once and the two
// can never drift or be forgotten on an early exit.
//
// `Api` is any type exposing reserve_scratch_slots(count, loc) and
// release_scratch_slots(count) — either the Compiler's controlled access object
// or the ICompilationBackend interface held by the helper compilers.
//
// Use it only for balanced single-scope reserve/release pairs. Sites that
// reserve one slot per loop iteration and bulk-release at the end, or that
// interleave unequal counts across pipe stages, have a different lifetime shape
// and must keep their explicit reserve_scratch_slots/release_scratch_slots calls.
template <typename Api> class ScratchSlotGuard {
public:
    ScratchSlotGuard(Api& api, std::size_t count, SourceLocation loc = {})
        : api_{api}, count_{count} {
        api_.reserve_scratch_slots(count, loc);
    }

    ~ScratchSlotGuard() {
        api_.release_scratch_slots(count_);
    }

    ScratchSlotGuard(const ScratchSlotGuard&) = delete;
    ScratchSlotGuard& operator=(const ScratchSlotGuard&) = delete;
    ScratchSlotGuard(ScratchSlotGuard&&) = delete;
    ScratchSlotGuard& operator=(ScratchSlotGuard&&) = delete;

private:
    Api& api_;
    std::size_t count_;
};

} // namespace luma

#endif // LUMA_COMPILER_SCRATCH_SLOT_GUARD_HPP
