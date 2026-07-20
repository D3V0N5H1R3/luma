#ifndef LUMA_INCLUDE_INCLUDE_RESOLVER_DETAIL_HPP
#define LUMA_INCLUDE_INCLUDE_RESOLVER_DETAIL_HPP

// Internal header for InclusionStack and InclusionGuard.
// Exposed for unit testing only — do not include from production code
// outside of include_resolver.cpp.

#include <cstddef>

namespace luma {

// ── InclusionStack ──────────────────────────────────────────────────────────
//
// Tracks the current nesting depth of the include chain so the resolver can
// enforce a maximum depth and guard against runaway recursion.
//
// This is deliberately just a counter, not a set of active paths: circular
// and self includes cannot produce infinite recursion because the resolver
// consults SourceManager's include-once registry (is_loaded) before entering
// any file, so no path can be entered — and therefore pushed here — twice.
// A membership set would never report a positive, so none is kept.

class InclusionStack {
public:
    void push() {
        ++depth_;
    }

    void pop() {
        --depth_;
    }

    [[nodiscard]] std::size_t depth() const {
        return depth_;
    }

private:
    std::size_t depth_{0};
};

// RAII guard that pushes onto an InclusionStack on construction and pops on
// destruction, ensuring balanced push/pop even on early returns or exceptions.
class InclusionGuard {
public:
    explicit InclusionGuard(InclusionStack& stack) : stack_{stack} {
        stack_.push();
    }

    ~InclusionGuard() {
        stack_.pop();
    }

    InclusionGuard(const InclusionGuard&) = delete;
    InclusionGuard& operator=(const InclusionGuard&) = delete;
    InclusionGuard(InclusionGuard&&) = delete;
    InclusionGuard& operator=(InclusionGuard&&) = delete;

private:
    InclusionStack& stack_;
};

} // namespace luma

#endif // LUMA_INCLUDE_INCLUDE_RESOLVER_DETAIL_HPP
