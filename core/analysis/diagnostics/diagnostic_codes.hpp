#pragma once

#include <optional>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostic Codes — Severity, category, source, and stable code vocabulary
// ─────────────────────────────────────────────────────────────────────────────
// This header holds the pure "vocabulary" of the diagnostic system: the
// Severity / DiagnosticCategory / DiagnosticSource enums, the numeric code
// ranges, the DiagnosticCode enum (which grows with every new diagnostic), and
// the small constexpr helpers that map between them.
//
// It carries no data types or behaviour and depends on nothing but the
// standard library, so translation units that only need to name a code or a
// category can include it directly without pulling in the diagnostic value
// types or the builder.  diagnostic.hpp re-includes this header for source
// compatibility.
namespace luma {

// Severity levels for diagnostics.
enum class Severity {
    Error,
    Warning,
    Hint,
    Info
};

// Category matches the pipeline phase that produced the diagnostic.
enum class DiagnosticCategory {
    Compile,
    Runtime,
    Syntax,
    Type,
    Warning
};

// Convert DiagnosticCategory to a human-readable name.
[[nodiscard]] constexpr std::string_view category_name(DiagnosticCategory category) noexcept {
    switch (category) {
        case DiagnosticCategory::Compile:
            return "CompileError";
        case DiagnosticCategory::Runtime:
            return "RuntimeError";
        case DiagnosticCategory::Syntax:
            return "SyntaxError";
        case DiagnosticCategory::Type:
            return "TypeError";
        case DiagnosticCategory::Warning:
            return "Warning";
    }

    return "Error";
}

// Identifies the compilation phase that produced a diagnostic.
enum class DiagnosticSource {
    Unknown,
    Syntax,  // Parser
    Name,    // Resolver
    Type,    // Type checker
    Lint,    // Linter
    Compile, // Compiler
    Verify,  // Bytecode verifier
    Runtime, // VM / stdlib runtime error converted to diagnostic
};

// ─── Diagnostic Code Ranges ───
//
// Each diagnostic code falls into a numbered range that encodes its category.
// The ranges are deliberately non-contiguous to leave room for future growth
// and to make the category immediately visible from the raw code value.
//
//   Range         Category          Display prefix
//   ──────────    ────────────────  ──────────────
//   0001–0999    Type errors       E0xxx
//   1001–1999    Syntax errors     E1xxx
//   2001–2999    Compile errors    E2xxx
//   4001–4999    Runtime errors    E4xxx
//   5001–5999    Lint warnings     W0xxx
//
// The constants below mark the first code in each range.
inline constexpr int k_type_error_min = 1;
inline constexpr int k_syntax_error_min = 1001;
inline constexpr int k_compile_error_min = 2001;
inline constexpr int k_runtime_error_min = 4001;
inline constexpr int k_lint_warning_min = 5001;

// One past the last code in each range (exclusive upper bound).
inline constexpr int k_type_error_max = 1000;
inline constexpr int k_syntax_error_max = 2000;
inline constexpr int k_compile_error_max = 3000;
inline constexpr int k_runtime_error_max = 5000;
inline constexpr int k_lint_warning_max = 6000;

// Categorise a diagnostic code by its numeric range.
// Returns the DiagnosticCategory for the given code, or std::nullopt
// if the code falls outside any known range.
[[nodiscard]] constexpr std::optional<DiagnosticCategory> category_of(int code) noexcept {
    if (code >= k_lint_warning_min && code < k_lint_warning_max) {
        return DiagnosticCategory::Warning;
    }
    if (code >= k_runtime_error_min && code < k_runtime_error_max) {
        return DiagnosticCategory::Runtime;
    }
    if (code >= k_compile_error_min && code < k_compile_error_max) {
        return DiagnosticCategory::Compile;
    }
    if (code >= k_syntax_error_min && code < k_syntax_error_max) {
        return DiagnosticCategory::Syntax;
    }
    if (code >= k_type_error_min && code < k_type_error_max) {
        return DiagnosticCategory::Type;
    }

    return std::nullopt;
}

// Get the display prefix for a category ("E" for errors, "W" for warnings).
[[nodiscard]] constexpr std::string_view category_prefix(DiagnosticCategory category) noexcept {
    return (category == DiagnosticCategory::Warning) ? "W" : "E";
}

// Get the display number for a diagnostic code within its category.
// Error codes use the raw numeric value (e.g. code 1001 → "E1001").
// Warning codes are rebased to a 1-based display index (e.g. code 5001 → "W0001").
[[nodiscard]] constexpr int display_number(DiagnosticCategory category, int code) noexcept {
    if (category == DiagnosticCategory::Warning) {
        return code - k_lint_warning_min + 1;
    }

    return code;
}

// Stable error/warning codes for documentation and suppression.
// Codes are grouped by category:
//   E0xxx — Type errors
//   E1xxx — Syntax errors
//   E2xxx — Compile errors
//   E4xxx — Runtime errors
//   W0xxx — Lint warnings
enum class DiagnosticCode {
    None = 0,

    // ─── Type errors (E0xxx) ───
    TypeMismatch = 1,         // E0001
    WrongArgCount = 2,        // E0002
    UndefinedVariable = 3,    // E0003
    UndefinedField = 4,       // E0004
    NotCallable = 5,          // E0005
    ImmutableAssignment = 6,  // E0006
    MissingReturnType = 7,    // E0007
    MissingMain = 8,          // E0008
    StdlibArgCount = 9,       // E0009 — wrong arity for stdlib function
    UndefinedFunction = 10,   // E0010
    InvalidOperand = 11,      // E0011
    DivisionByZero = 12,      // E0012
    IncompatibleTypes = 13,   // E0013
    MissingTypeArg = 14,      // E0014
    DuplicateDefinition = 15, // E0015
    CircularDependency = 16,  // E0016
    InvalidCast = 17,         // E0017
    IntegerOverflow = 18,     // E0018
    InvalidRepeatCount = 19,  // E0019

    // ─── Syntax errors (E1xxx) ───
    UnexpectedToken = 1001,    // E1001
    UnterminatedString = 1002, // E1002
    InvalidNumber = 1003,      // E1003

    // ─── Compile errors (E2xxx) ───
    CompileError = 2001, // E2001

    // ─── Runtime errors (E4xxx) ───
    RecursionLimit = 4001,     // E4001
    ResourceLimit = 4002,      // E4002
    SecurityViolation = 4003,  // E4003
    IncludeFailed = 4004,      // E4004
    OwnershipViolation = 4005, // E4005
    BorrowViolation = 4006,    // E4006
    InvalidIndex = 4007,       // E4007
    ShiftOutOfRange = 4008,    // E4008

    // ─── Lint warnings (W0xxx) ───
    UnusedVariable = 5001,        // W0001
    UnusedFunction = 5002,        // W0002
    UnusedParameter = 5003,       // W0003
    MutableNeverMutated = 5004,   // W0004
    SelfAssignment = 5005,        // W0005
    UnreachableCode = 5006,       // W0006
    EmptyBody = 5007,             // W0007
    AlwaysTrueFalse = 5008,       // W0008
    FloatingPointEquality = 5009, // W0009
    DiscardedResult = 5010,       // W0010
    RedundantElse = 5011,         // W0011
    ShadowedVariable = 5012,      // W0012
    EmptyCatch = 5013,            // W0013
    DivisionByLiteralZero = 5014, // W0014
    DoubleNegation = 5015,        // W0015
    UnusedInclude = 5016,         // W0016
};

} // namespace luma
