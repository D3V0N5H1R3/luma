#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "fuzz_harness.hpp"
#include "runtime/stdlib/text/regularexpression_module.hpp"

// LibFuzzer entry point for the RegularExpression module's ReDoS heuristic
// (luma::has_dangerous_quantifier_nesting, declared in
// core/runtime/stdlib/text/regularexpression_module.hpp).
//
// has_dangerous_quantifier_nesting is the hand-written guard that every
// RegularExpression entry point (matches, find, find_all, replace, replace_all,
// split and is_valid) runs over an untrusted pattern before handing it to
// std::regex. It walks the pattern with a per-open-parenthesis stack to flag
// nested quantifiers that risk catastrophic backtracking (e.g. (a+)+, ((a+))+,
// (?:a+)+), skipping character classes [...] and group-modifier syntax
// ((?: (?= (?! (?<= (?<!) and honouring backslash escapes. Because it processes
// arbitrary bytes — regex patterns reach a Luma program as string literals, file
// contents or network payloads — it must never crash, read out of bounds or
// exhaust memory. std::regex itself is a standard-library component fuzzed
// upstream and is intentionally out of scope; this target pins down the
// first-party pre-filter that sits in front of it, mirroring fuzz_process and
// fuzz_keyvaluestore which drive other stdlib trust boundaries directly.
//
// Three oracles run on top of the never-crash contract:
//   1. Idempotence: the walk is pure, so a second call on the same bytes must
//      return the same verdict. A divergence signals state leaking across calls
//      (e.g. a stray static) or a non-deterministic read past the buffer.
//   2. Group invariant: the algorithm only ever raises its flag when a closing
//      ')' is followed by a quantifier, so a pattern containing no '(' byte at
//      all can never be dangerous. Flagging such a pattern means the walk strayed.
//   3. Known answers: a fixed table of representative safe and dangerous patterns
//      pins the heuristic's contract — the nested-quantifier shapes it must catch
//      and the single-quantifier, escaped and character-class shapes it must not —
//      against accidental change, mirroring fuzz_hash's CRC-32 anchors. It is
//      input-independent, so it is verified once in LLVMFuzzerInitialize rather
//      than on every iteration.
namespace {

struct KnownAnswer {
    std::string_view pattern;
    bool dangerous;
};

// Representative anchors. Dangerous: a group that both contains a quantifier and
// is itself quantified. Safe: single quantifiers, quantified groups with no inner
// quantifier, a quantifier inside a character class, and escaped parentheses.
constexpr KnownAnswer k_known_answers[] = {
    {"", false},     {"abc", false},          {"a+", false},     {"a{2,3}", false},
    {"(a)+", false}, {"(a+)", false},         {"[a+]+", false},  {"\\(a+\\)+", false},
    {"(a+)+", true}, {"(a+)+b", true},        {"((a+))+", true}, {"(?:a+)+", true},
    {"(a*)*", true}, {"(a{2,3}){2,3}", true},
};

// Verify the input-independent known-answer table.  Run once at startup.
void check_known_answers() {
    for (const auto& [pattern, expected] : k_known_answers) {
        if (luma::has_dangerous_quantifier_nesting(pattern) != expected) {
            luma::fuzz::trap(); // heuristic contract drifted from the anchors.
        }
    }
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    check_known_answers();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            using luma::has_dangerous_quantifier_nesting;

            // ── Never-crash: arbitrary bytes are classified. ──
            const bool dangerous = has_dangerous_quantifier_nesting(input);
            luma::fuzz::do_not_optimize(dangerous);

            // ── Oracle 1: the walk is pure, so it is idempotent. ──
            if (has_dangerous_quantifier_nesting(input) != dangerous) {
                luma::fuzz::trap(); // verdict changed between two identical calls.
            }

            // ── Oracle 2: no '(' group means no possible nested quantifier. ──
            if (dangerous && input.find('(') == std::string::npos) {
                luma::fuzz::trap(); // flagged a pattern that has no group at all.
            }

            // Oracle 3 (fixed known answers) is input-independent and verified
            // once in LLVMFuzzerInitialize.
        });
}
