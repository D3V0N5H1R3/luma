#include "analysis/diagnostics/diagnostic_builders.hpp"

#include <array>
#include <format>
#include <string>
#include <string_view>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/types/type_info.hpp"

namespace luma::diag_builders {

// ─── Type Mismatch Hint ────────────────────────────────────────────────────

std::string type_mismatch_hint(const TypeInfo& expected, const TypeInfo& actual) {
    struct HintEntry {
        TypeInfo::Kind expected;
        TypeInfo::Kind actual;
        std::string_view hint;
    };

    static constexpr std::array k_type_hints = {
        HintEntry{.expected = TypeInfo::Kind::Number,
                  .actual = TypeInfo::Kind::String,
                  .hint = "use Converter.to_number() to convert string to number"},
        HintEntry{.expected = TypeInfo::Kind::String,
                  .actual = TypeInfo::Kind::Number,
                  .hint = "use Converter.to_string() to convert to string"},
        HintEntry{.expected = TypeInfo::Kind::String,
                  .actual = TypeInfo::Kind::Integer,
                  .hint = "use Converter.to_string() to convert to string"},
        HintEntry{.expected = TypeInfo::Kind::Integer,
                  .actual = TypeInfo::Kind::String,
                  .hint = "use Converter.to_integer() to convert string to integer"},
        HintEntry{.expected = TypeInfo::Kind::Boolean,
                  .actual = TypeInfo::Kind::String,
                  .hint = "use Converter.to_boolean() to convert string to boolean"},
        HintEntry{.expected = TypeInfo::Kind::Number,
                  .actual = TypeInfo::Kind::Integer,
                  .hint = "integer can be widened to number with Converter.to_number()"},
        HintEntry{.expected = TypeInfo::Kind::Integer,
                  .actual = TypeInfo::Kind::Number,
                  .hint =
                      "use Converter.to_integer() to truncate — this may lose the fractional part"},
        HintEntry{.expected = TypeInfo::Kind::String,
                  .actual = TypeInfo::Kind::Boolean,
                  .hint = "use Converter.to_string() to convert boolean to string"},
        HintEntry{.expected = TypeInfo::Kind::Integer,
                  .actual = TypeInfo::Kind::Boolean,
                  .hint = "boolean cannot be implicitly converted to integer — "
                          "use 'if value { 1 } else { 0 }' instead"},
        HintEntry{.expected = TypeInfo::Kind::Boolean,
                  .actual = TypeInfo::Kind::Integer,
                  .hint = "integer cannot be implicitly converted to boolean — "
                          "use 'value != 0' instead"},
    };

    for (const auto& entry : k_type_hints) {
        if (expected.kind == entry.expected && actual.kind == entry.actual) {
            return std::string{entry.hint};
        }
    }

    if (actual.kind == TypeInfo::Kind::Result && expected.kind != TypeInfo::Kind::Result) {
        return "handle the result with 'match', 'Result.unwrap()', or '?'";
    }

    if (actual.kind == TypeInfo::Kind::Optional && expected.kind != TypeInfo::Kind::Optional) {
        return "unwrap the optional with 'match', '?\?', or 'Optional.unwrap()'";
    }

    if (expected.kind == TypeInfo::Kind::Optional && actual.kind != TypeInfo::Kind::Optional &&
        actual.kind != TypeInfo::Kind::None) {
        return "wrap with Optional.some() or change the expected type";
    }

    if (expected.kind == TypeInfo::Kind::Array && actual.kind == TypeInfo::Kind::Array &&
        !expected.inner_types.empty() && !actual.inner_types.empty()) {
        return std::format("array element type mismatch: expected '{}', got '{}'",
                           expected.inner_types[0].to_string(), actual.inner_types[0].to_string());
    }

    return {};
}

// ─── Auto Hint for Diagnostic Code ─────────────────────────────────────────

std::string_view auto_hint_for_code(DiagnosticCode code) noexcept {
    switch (code) {
        case DiagnosticCode::TypeMismatch:
        case DiagnosticCode::IncompatibleTypes:
        case DiagnosticCode::InvalidCast:
            return "Use Converter.to_integer() or Converter.to_string() to convert between types.";
        case DiagnosticCode::UndefinedVariable:
        case DiagnosticCode::UndefinedFunction:
            return "Check spelling or add the required include.";
        case DiagnosticCode::MissingReturnType:
            return "Add the return type before the function name, e.g. 'function void name()'.";
        default:
            return {};
    }
}

} // namespace luma::diag_builders
