#ifndef LUMA_INTERPRETER_SANDBOX_POLICY_HPP
#define LUMA_INTERPRETER_SANDBOX_POLICY_HPP

#include <format>
#include <optional>
#include <string>
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
    // Replace the blocked sets: whole module prefixes (e.g. "FileSystem",
    // "Process") and individual functions withheld from otherwise available
    // modules (e.g. "Hash.sha256_file").
    void set_blocked(StringSet prefixes, StringSet functions = {}) {
        blocked_ = std::move(prefixes);
        blocked_functions_ = std::move(functions);
    }

    // True if `name` is a blocked function, or its module prefix (the text
    // before the first '.') is blocked.
    [[nodiscard]] bool is_blocked(std::string_view name) const {
        return denial_hint(name).has_value();
    }

    // The hint explaining why `name` is blocked, or nullopt if it is not.
    [[nodiscard]] std::optional<std::string> denial_hint(std::string_view name) const {
        if (blocked_functions_.contains(name)) {
            return std::format("{} reads or writes files, which sandbox mode disables; the "
                               "rest of the {} module stays available",
                               name, symbols::qualified_module(name));
        }

        const auto split = symbols::split_module(name);

        if (split && blocked_.contains(split->first)) {
            return std::format("the {} module is disabled in sandbox mode to "
                               "prevent access to system resources",
                               split->first);
        }

        return std::nullopt;
    }

    // Throw a RuntimeError if `name` is sandbox-blocked; otherwise no-op.
    void verify_access(std::string_view name, const SourceLocation& loc) const {
        if (auto hint = denial_hint(name)) {
            throw RuntimeError{access_denied_message(name), loc, std::move(*hint)};
        }
    }

    [[nodiscard]] static std::string access_denied_message(std::string_view name) {
        return std::format("'{}' is not available in sandbox mode (--box)", name);
    }

private:
    StringSet blocked_;
    StringSet blocked_functions_;
};

} // namespace luma

#endif // LUMA_INTERPRETER_SANDBOX_POLICY_HPP
