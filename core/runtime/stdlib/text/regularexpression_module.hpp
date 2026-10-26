#ifndef LUMA_STDLIB_REGULAREXPRESSION_MODULE_HPP
#define LUMA_STDLIB_REGULAREXPRESSION_MODULE_HPP

#include <string_view>

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

// ReDoS heuristic behind every RegularExpression entry point: returns true when
// `pattern` contains a quantified construct that risks catastrophic
// backtracking -- either nested quantifiers (e.g. `(a+)+`, `((a+))+`,
// `(?:a+)+`) or ambiguous alternation under repetition (e.g. `(a|aa)+`,
// `(.|a)+`, `(a|)+`).  Disjoint alternation such as `(cat|dog)+` is left alone.
// It is a hand-written walk over an
// untrusted pattern string — a trust boundary that must never crash, read out
// of bounds, or exhaust memory on arbitrary bytes — so it is exposed here for
// direct unit and fuzz testing (see fuzz/fuzz_regex.cpp).  The walk is pure:
// equal inputs always yield equal results, and a pattern with no '(' group can
// never be flagged.
// `case_insensitive` mirrors the icase flag the pattern will be compiled
// with: literal first characters are then compared ASCII-case-folded, so
// `(a|A)+` is flagged under icase.  Named-group headers (`(?<n>`, `(?P<n>`)
// are skipped, and \x / \u / \c escapes and backreferences count as wildcard
// first tokens.
[[nodiscard]] bool has_dangerous_quantifier_nesting(std::string_view pattern,
                                                    bool case_insensitive = false);

void register_regularexpression_ns(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_REGULAREXPRESSION_MODULE_HPP
