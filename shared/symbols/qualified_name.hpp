#ifndef LUMA_SYMBOLS_QUALIFIED_NAME_HPP
#define LUMA_SYMBOLS_QUALIFIED_NAME_HPP

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace luma::symbols {

// ─── Free functions ─────────────────────────────────────────

// Build a qualified "Namespace.member" name; an empty namespace yields the bare
// member.
[[nodiscard]] inline std::string make_qualified(std::string_view ns, std::string_view member) {
    if (ns.empty()) {
        return std::string{member};
    }

    std::string result;
    result.reserve(ns.size() + 1 + member.size());
    result += ns;
    result += '.';
    result += member;

    return result;
}

// Check whether a name contains a dot (i.e. is qualified).
[[nodiscard]] inline bool is_qualified_name(std::string_view name) noexcept {
    return name.find('.') != std::string_view::npos;
}

// Non-allocating accessors mirroring make_qualified — the inverse "split"
// direction. They return views into `name`, so the caller must keep the
// backing storage alive for as long as the result is used.
//
// Two split points exist on purpose, and every name says which one it uses:
//   • FIRST dot — the module head ("A" of "A.B.C"): qualified_module,
//     split_module.  Use it to ask "which top-level namespace is this in?".
//   • LAST dot  — the leaf member ("C" of "A.B.C"): qualified_member,
//     split_member, QualifiedName.  Use it to separate a member from its
//     (possibly nested) owner.

// The module head: the text before the FIRST dot.  Returns the whole name
// when unqualified (no dot).
//   "Math.floor" → "Math"   "A.B.C" → "A"   "floor" → "floor"
[[nodiscard]] inline std::string_view qualified_module(std::string_view name) noexcept {
    const auto pos = name.find('.');
    return pos == std::string_view::npos ? name : name.substr(0, pos);
}

// The leaf member: the text after the LAST dot.  Returns the whole name
// when unqualified (no dot).
//   "Math.floor" → "floor"  "A.B.C" → "C"   "floor" → "floor"
[[nodiscard]] inline std::string_view qualified_member(std::string_view name) noexcept {
    const auto pos = name.rfind('.');
    return pos == std::string_view::npos ? name : name.substr(pos + 1);
}

// Split at the FIRST dot into {head, remainder}.  Returns std::nullopt when
// the name is unqualified, letting a caller guard and destructure in one step:
//   if (const auto split = split_module(name)) { use split->first, split->second; }
//   "Math.floor" → {"Math","floor"}   "A.B.C" → {"A","B.C"}   "floor" → nullopt
[[nodiscard]] inline std::optional<std::pair<std::string_view, std::string_view>>
split_module(std::string_view name) noexcept {
    const auto pos = name.find('.');

    if (pos == std::string_view::npos) {
        return std::nullopt;
    }

    return std::pair{name.substr(0, pos), name.substr(pos + 1)};
}

// Split at the LAST dot into {owner, member}.  Returns std::nullopt when the
// name is unqualified.
//   "Math.floor" → {"Math","floor"}   "A.B.C" → {"A.B","C"}   "floor" → nullopt
[[nodiscard]] inline std::optional<std::pair<std::string_view, std::string_view>>
split_member(std::string_view name) noexcept {
    const auto pos = name.rfind('.');

    if (pos == std::string_view::npos) {
        return std::nullopt;
    }

    return std::pair{name.substr(0, pos), name.substr(pos + 1)};
}

// ═══════════════════════════════════════════════════════════════════
// QualifiedName — "Namespace.member" name representation
// ═══════════════════════════════════════════════════════════════════
//
// A qualified name in Luma is "Namespace.member" (e.g. "Math.floor",
// "DateTime.TimeParts").  This struct is the owning form of split_member():
// it splits at the LAST dot — used where a caller needs both halves as
// std::string.  For non-allocating views, prefer the free functions above.

struct QualifiedName {
    std::string namespace_part; // e.g. "Math", empty for unqualified
    std::string member_part;    // e.g. "floor"

    // Parse a dotted string into a QualifiedName.
    // "Math.floor"  → { "Math", "floor" }
    // "floor"       → { "", "floor" }
    // "A.B.C"       → { "A.B", "C" } (last dot is the split point)
    [[nodiscard]] static QualifiedName parse(std::string_view name) {
        if (const auto split = split_member(name)) {
            return {std::string{split->first}, std::string{split->second}};
        }

        return {{}, std::string{name}};
    }
};

} // namespace luma::symbols

#endif // LUMA_SYMBOLS_QUALIFIED_NAME_HPP
