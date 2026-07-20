#ifndef LUMA_INTERPRETER_SANDBOX_POLICY_HPP
#define LUMA_INTERPRETER_SANDBOX_POLICY_HPP

#include <format>
#include <string_view>
#include <utility>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/string_hash.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

// Sandbox (--box) module-blocking policy.
//
// Single responsibility: own the set of sandbox-blocked module prefixes and
// answer/raise on access decisions.  The scope-chain walk that locates the
// root policy remains in Environment; this type owns only the blocked set,
// the block decision, and the standard access-denied diagnostic.
//
// Extracted from Environment per the TODO(refactor) note: decoupling the
// sandbox policy from scope storage keeps each concern independently testable.
class SandboxPolicy {
public:
    // Replace the set of blocked module prefixes (e.g. "FileSystem", "Process").
    void set_blocked(StringSet prefixes) {
        blocked_ = std::move(prefixes);
    }

    // True if `name`'s module prefix (the text before the first '.') is blocked.
    [[nodiscard]] bool is_blocked(std::string_view name) const {
        if (blocked_.empty()) {
            return false;
        }

        const auto split = split_module(name);
        return split && blocked_.contains(split->first);
    }

    // Throw a RuntimeError if `name` is sandbox-blocked; otherwise no-op.
    void verify_access(std::string_view name, const SourceLocation& loc) const {
        if (!is_blocked(name)) {
            return;
        }

        const auto module = qualified_module(name);

        throw RuntimeError{std::format("'{}' is not available in sandbox mode (--box)", name), loc,
                           std::format("the {} module is disabled in sandbox mode to "
                                       "prevent access to system resources",
                                       module)};
    }

private:
    StringSet blocked_;
};

} // namespace luma

#endif // LUMA_INTERPRETER_SANDBOX_POLICY_HPP
