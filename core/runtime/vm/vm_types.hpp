// ─────────────────────────────────────────────────────────────────────────────
// VM Type Matching Utilities
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Determine whether a runtime Value matches a type pattern
// string.  Used by the VM for IS_TYPE / match-expression dispatch.
//
// Patterns supported:
//   - Simple types:        "integer", "string", "boolean", ...
//   - Tuple types:         "(integer,string)"
//   - Parameterized types: "array<integer>"
//   - Choice/record names: "Some", "MyRecord"
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_RUNTIME_VM_VM_TYPES_HPP
#define LUMA_RUNTIME_VM_VM_TYPES_HPP

#include <optional>
#include <string_view>
#include <vector>

#include "runtime/interpreter/value.hpp"

namespace luma {

// A parsed representation of a type pattern string.
//
// Replaces manual starts_with/ends_with/find parsing in TypeMatcher. Parse
// once, then call matches() or inspect the fields directly.
//
// Lifetime: params and base are string_views into the original pattern passed
// to parse(). A TypePattern must not outlive that string.
struct TypePattern {
    enum class Kind {
        Simple,
        Tuple,
        Parameterized
    };

    Kind kind = Kind::Simple;
    std::string_view base;                // e.g. "integer", "array"
    std::vector<std::string_view> params; // e.g. ["integer"] for array<integer>

    /// Parse a type pattern string into a TypePattern.
    /// Returns nullopt for empty patterns.
    [[nodiscard]] static std::optional<TypePattern> parse(std::string_view pattern);

    /// Return true if type_string (e.g., from Value::display_type_name())
    /// satisfies this pattern. For Simple patterns this includes integer->number
    /// widening. For Parameterized patterns only the base type is checked here;
    /// element-level checks require a Value and are performed in TypeMatcher.
    [[nodiscard]] bool matches(std::string_view type_string) const noexcept;
};

// Stateless helper for runtime type matching against type-pattern strings.
class TypeMatcher {
public:
    [[nodiscard]] static bool matches(const Value& val, std::string_view type_pattern);

private:
    // Depth-tracked core.  Every recursive descent — nested tuple elements and
    // the optional<...> unwrap that recurses on the same value with the inner
    // pattern — funnels back through this overload, which returns a safe "no
    // match" once nesting exceeds ResourceLimits::max_call_depth.  This bounds
    // native stack growth when a crafted .lumc supplies a pathologically nested
    // type-pattern string (source-level types are already capped at parse time).
    [[nodiscard]] static bool matches(const Value& val, std::string_view type_pattern, int depth);
    [[nodiscard]] static bool matches_tuple_type(const Value& val, const TypePattern& pattern,
                                                 int depth);
    [[nodiscard]] static bool matches_parameterized_type(const Value& val,
                                                         const TypePattern& pattern, int depth);
    [[nodiscard]] static bool matches_simple_type(const Value& val, const TypePattern& pattern);
};

} // namespace luma

#endif // LUMA_RUNTIME_VM_VM_TYPES_HPP
