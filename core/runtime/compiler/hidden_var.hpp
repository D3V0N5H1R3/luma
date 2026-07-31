// ─────────────────────────────────────────────────────────────────────────────
// Hidden Variable Names
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Typed enumeration of synthetic local variable names
//                 injected by the compiler for internal bookkeeping (loop
//                 iterators, match subjects, error slots, etc.).
//
// Using a typed enum instead of raw string constants prevents typos,
// enables exhaustive switch checking, and centralises the name mapping.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_HIDDEN_VAR_HPP
#define LUMA_COMPILER_HIDDEN_VAR_HPP

#include <string_view>

namespace luma {

// Synthetic local variable names injected by the compiler.
// Each enumerator maps to a fixed `"__name__"` string that is stored in
// compiled bytecode — the string values must never change without a
// corresponding bytecode version bump.
enum class HiddenVar {
    Iterator,          // for-loop iterator state
    Element,           // for-loop element temporary
    MatchStmtSubject,  // match-statement subject
    MatchSubject,      // match-expression subject
    MatchGuardSubject, // preserved subject for a guarded match-expression arm
    Error,             // try/catch error binding
    TuplePrefix,       // tuple destructuring prefix (appended with index)
    RecordPrefix,      // record destructuring prefix (appended with index)
};

// Map a HiddenVar to its canonical string representation.
[[nodiscard]] constexpr std::string_view to_string(HiddenVar var) noexcept {
    switch (var) {
        case HiddenVar::Iterator:
            return "__iter__";
        case HiddenVar::Element:
            return "__element__";
        case HiddenVar::MatchStmtSubject:
            return "__match_stmt_subject__";
        case HiddenVar::MatchSubject:
            return "__match_subject__";
        case HiddenVar::MatchGuardSubject:
            return "__match_guard_subject__";
        case HiddenVar::Error:
            return "__err__";
        case HiddenVar::TuplePrefix:
            return "__tuple__";
        case HiddenVar::RecordPrefix:
            return "__record__";
    }
    return "";
}

} // namespace luma

#endif // LUMA_COMPILER_HIDDEN_VAR_HPP
