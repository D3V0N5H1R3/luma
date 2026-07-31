#ifndef LUMA_AST_MATCH_PATTERN_HPP
#define LUMA_AST_MATCH_PATTERN_HPP

// Match-pattern AST machinery shared by MatchExpression and MatchStatement.
// Extracted from expression.hpp so the pattern sub-system (its Kind enum,
// variant payload, and read-only accessors) forms one cohesive unit that
// both a statement and an expression can reference.

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "analysis/ast/ast_fwd.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

/// Shared pattern payload, `Kind` discriminator, and read-only accessors for
/// match constructs.
///
/// Both `MatchArm` and `MatchArm::AlternativePattern` derive from this so the
/// variant payload and its read-only accessors are defined exactly once.
struct MatchPattern {
    enum class Kind {
        BooleanCase,
        ChoiceCase,
        Comparison,
        Else,
        FailureResult,
        IntegerCase,
        IntegerRangeCase,
        NoneCase,
        SomeCase,
        StringCase,
        SuccessResult,
        VariantCase,
        RecordCase
    };

    // ── Pattern data types (variant alternatives) ──
    // Order matches Kind enum so variant index == Kind value.

    struct BooleanPatternData {
        bool value{false};
    };

    struct ChoicePatternData {
        std::string enum_type;
        std::string enum_variant;
        std::vector<std::string> choice_bindings;
    };

    struct ComparisonPatternData {
        TokenType comparison_op{TokenType::EqualsEquals};
        ExpressionPtr comparison_value;
    };

    struct ElsePatternData {};

    struct FailurePatternData {
        std::string binding_name;
    };

    struct IntegerPatternData {
        std::int64_t value{0};
    };

    /// A `case lo..hi` / `case lo..=hi` integer range pattern.  Bounds are
    /// compile-time integer-literal constants; `inclusive` selects `..=` (closed)
    /// vs `..` (half-open).  Compiled to the same bounds test as `value in lo..hi`.
    struct RangePatternData {
        std::int64_t lo{0};
        std::int64_t hi{0};
        bool inclusive{false};
        SourceLocation location{};
    };

    struct NonePatternData {};

    struct SomePatternData {
        std::string binding_name;
    };

    struct StringPatternData {
        std::string value;
    };

    struct SuccessPatternData {
        std::string binding_name;
    };

    struct VariantPatternData {
        std::string enum_type;
        std::string enum_variant;
    };

    /// A `case Type { field, ... }` record-destructuring pattern.  Binds each
    /// listed field to a same-named local; a subset of fields may be listed.
    struct RecordPatternData {
        std::string record_type;
        std::vector<std::string> field_bindings;
    };

    using PatternData =
        std::variant<BooleanPatternData, ChoicePatternData, ComparisonPatternData, ElsePatternData,
                     FailurePatternData, IntegerPatternData, RangePatternData, NonePatternData,
                     SomePatternData, StringPatternData, SuccessPatternData, VariantPatternData,
                     RecordPatternData>;

    // ── Pattern data ──
    PatternData pattern{ComparisonPatternData{}};

    /// Returns the Kind discriminator. Variant index matches Kind enum.
    [[nodiscard]] Kind kind() const noexcept {
        return static_cast<Kind>(pattern.index());
    }

    // ── Read-only field accessors ──
    // Return safe defaults for the wrong variant type to preserve backward
    // compatibility with code that relied on default-constructed fields.

    [[nodiscard]] TokenType comparison_op() const noexcept {
        if (const auto* p = std::get_if<ComparisonPatternData>(&pattern)) {
            return p->comparison_op;
        }
        return TokenType::EqualsEquals;
    }

    [[nodiscard]] const ExpressionPtr& comparison_value() const noexcept {
        if (const auto* p = std::get_if<ComparisonPatternData>(&pattern)) {
            return p->comparison_value;
        }
        static const ExpressionPtr null_expr{};
        return null_expr;
    }

    [[nodiscard]] const std::string& enum_type() const noexcept {
        if (const auto* p = std::get_if<ChoicePatternData>(&pattern)) {
            return p->enum_type;
        }
        if (const auto* p = std::get_if<VariantPatternData>(&pattern)) {
            return p->enum_type;
        }
        static const std::string empty;
        return empty;
    }

    [[nodiscard]] const std::string& enum_variant() const noexcept {
        if (const auto* p = std::get_if<ChoicePatternData>(&pattern)) {
            return p->enum_variant;
        }
        if (const auto* p = std::get_if<VariantPatternData>(&pattern)) {
            return p->enum_variant;
        }
        static const std::string empty;
        return empty;
    }

    [[nodiscard]] const std::string& string_value() const noexcept {
        if (const auto* p = std::get_if<StringPatternData>(&pattern)) {
            return p->value;
        }
        static const std::string empty;
        return empty;
    }

    [[nodiscard]] bool boolean_value() const noexcept {
        if (const auto* p = std::get_if<BooleanPatternData>(&pattern)) {
            return p->value;
        }
        return false;
    }

    [[nodiscard]] std::int64_t integer_value() const noexcept {
        if (const auto* p = std::get_if<IntegerPatternData>(&pattern)) {
            return p->value;
        }
        return 0;
    }

    [[nodiscard]] std::int64_t range_lo() const noexcept {
        if (const auto* p = std::get_if<RangePatternData>(&pattern)) {
            return p->lo;
        }
        return 0;
    }

    [[nodiscard]] std::int64_t range_hi() const noexcept {
        if (const auto* p = std::get_if<RangePatternData>(&pattern)) {
            return p->hi;
        }
        return 0;
    }

    [[nodiscard]] bool range_inclusive() const noexcept {
        if (const auto* p = std::get_if<RangePatternData>(&pattern)) {
            return p->inclusive;
        }
        return false;
    }

    [[nodiscard]] const SourceLocation& range_location() const noexcept {
        if (const auto* p = std::get_if<RangePatternData>(&pattern)) {
            return p->location;
        }
        static const SourceLocation empty{};
        return empty;
    }

    [[nodiscard]] const std::string& binding_name() const noexcept {
        if (const auto* p = std::get_if<SuccessPatternData>(&pattern)) {
            return p->binding_name;
        }
        if (const auto* p = std::get_if<FailurePatternData>(&pattern)) {
            return p->binding_name;
        }
        if (const auto* p = std::get_if<SomePatternData>(&pattern)) {
            return p->binding_name;
        }
        static const std::string empty;
        return empty;
    }

    [[nodiscard]] bool has_choice_bindings() const noexcept {
        if (const auto* p = std::get_if<ChoicePatternData>(&pattern)) {
            return !p->choice_bindings.empty();
        }
        return false;
    }

    [[nodiscard]] const std::vector<std::string>& choice_bindings() const noexcept {
        if (const auto* p = std::get_if<ChoicePatternData>(&pattern)) {
            return p->choice_bindings;
        }
        static const std::vector<std::string> empty;
        return empty;
    }

    [[nodiscard]] const std::string& record_type() const noexcept {
        if (const auto* p = std::get_if<RecordPatternData>(&pattern)) {
            return p->record_type;
        }
        static const std::string empty;
        return empty;
    }

    [[nodiscard]] bool has_record_bindings() const noexcept {
        return std::holds_alternative<RecordPatternData>(pattern);
    }

    [[nodiscard]] const std::vector<std::string>& record_field_bindings() const noexcept {
        if (const auto* p = std::get_if<RecordPatternData>(&pattern)) {
            return p->field_bindings;
        }
        static const std::vector<std::string> empty;
        return empty;
    }
};

/// Represents one arm of a `match` expression.
///
/// Inherits the pattern payload, `Kind` discriminator, and read-only accessors
/// from MatchPattern.  The `Kind` enum is preserved for switch-based dispatch.
///
/// Fields shared across all kinds:
///   - `alternatives`  — additional `case A | B` patterns.
///   - `guard`         — optional `when <condition>` expression.
///   - `body` / `body_expr` — the arm's body (statement list or expression).
struct MatchArm : MatchPattern {
    /// An alternative pattern for the `case A | B` syntax.
    /// Shares MatchPattern's payload and read-only accessors.
    struct AlternativePattern : MatchPattern {};

    // ── Shared across all kinds ──
    std::vector<AlternativePattern> alternatives;
    ExpressionPtr guard;
    std::vector<StatementPtr> body;
    ExpressionPtr body_expr;

    /// Returns true if this arm binds a variable (SuccessResult, FailureResult, SomeCase).
    [[nodiscard]] bool has_binding() const noexcept {
        return std::holds_alternative<SuccessPatternData>(pattern) ||
               std::holds_alternative<FailurePatternData>(pattern) ||
               std::holds_alternative<SomePatternData>(pattern);
    }

    // ── Mutable / arm-only field accessors ──
    // Re-expose the inherited read-only overloads whose names are shared with
    // the mutable accessors below; otherwise name hiding would shadow them.
    using MatchPattern::binding_name;
    using MatchPattern::choice_bindings;
    using MatchPattern::comparison_value;
    using MatchPattern::enum_type;
    using MatchPattern::enum_variant;

    [[nodiscard]] ExpressionPtr& comparison_value() {
        return std::get<ComparisonPatternData>(pattern).comparison_value;
    }

    [[nodiscard]] std::string& binding_name() {
        if (auto* p = std::get_if<SuccessPatternData>(&pattern)) {
            return p->binding_name;
        }
        if (auto* p = std::get_if<FailurePatternData>(&pattern)) {
            return p->binding_name;
        }
        return std::get<SomePatternData>(pattern).binding_name;
    }

    [[nodiscard]] std::string& enum_type() {
        if (auto* p = std::get_if<ChoicePatternData>(&pattern)) {
            return p->enum_type;
        }
        return std::get<VariantPatternData>(pattern).enum_type;
    }

    [[nodiscard]] std::string& enum_variant() {
        if (auto* p = std::get_if<ChoicePatternData>(&pattern)) {
            return p->enum_variant;
        }
        return std::get<VariantPatternData>(pattern).enum_variant;
    }

    [[nodiscard]] std::vector<std::string>& choice_bindings() {
        return std::get<ChoicePatternData>(pattern).choice_bindings;
    }

    // ── Safe ref accessors ──

    [[nodiscard]] const Expression& comparison_value_ref() const noexcept {
        const auto& cv = comparison_value();
        assert(cv && "comparison_value_ref() called on arm without comparison value");
        return *cv;
    }

    [[nodiscard]] const std::string& binding_name_ref() const noexcept {
        const auto& bn = binding_name();
        assert(!bn.empty() && "binding_name_ref() called on arm without binding");
        return bn;
    }

    [[nodiscard]] const std::string& enum_type_ref() const noexcept {
        const auto& et = enum_type();
        assert(!et.empty() && "enum_type_ref() called on arm without enum type");
        return et;
    }

    [[nodiscard]] const std::string& enum_variant_ref() const noexcept {
        const auto& ev = enum_variant();
        assert(!ev.empty() && "enum_variant_ref() called on arm without enum variant");
        return ev;
    }

    [[nodiscard]] bool has_guard() const noexcept {
        return guard != nullptr;
    }

    [[nodiscard]] const Expression& guard_ref() const noexcept {
        assert(guard && "guard_ref() called on arm without guard");
        return *guard;
    }
};

} // namespace luma

#endif // LUMA_AST_MATCH_PATTERN_HPP
