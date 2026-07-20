#ifndef LUMA_COMMON_DEPTH_GUARD_HPP
#define LUMA_COMMON_DEPTH_GUARD_HPP

#include <cassert>
#include <format>
#include <stdexcept>
#include <string_view>

namespace luma {

// RAII guard for tracking recursive nesting depth.
// Increments the depth counter on construction (throwing if the limit is
// exceeded) and decrements it on destruction.  Eliminates manual
// increment/check/decrement patterns in recursive parsers.
//
// Stores a reference to the depth counter directly, avoiding the
// std::function heap allocation that a ScopeGuard<std::function> would incur.
class DepthGuard {
public:
    explicit DepthGuard(int& depth, int max_depth, std::string_view context) : depth_{depth} {
        assert(max_depth > 0 && "max_depth must be positive");
        // Check BEFORE incrementing so that the counter is not left dirty if
        // the constructor throws (a thrown constructor never runs its destructor).
        if (depth_ >= max_depth) {
            throw std::runtime_error(
                std::format("{}: maximum nesting depth {} exceeded", context, max_depth));
        }
        ++depth_;
    }

    ~DepthGuard() {
        --depth_;
    }

    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;

    // Move operations are deleted because moving would leave the source
    // object's depth_ reference in an inconsistent state — the destructor
    // of the moved-from object would decrement a counter it never incremented.
    DepthGuard(DepthGuard&&) = delete;
    DepthGuard& operator=(DepthGuard&&) = delete;

private:
    int& depth_;
};

} // namespace luma

#endif // LUMA_COMMON_DEPTH_GUARD_HPP
