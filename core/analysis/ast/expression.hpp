#ifndef LUMA_AST_EXPRESSION_HPP
#define LUMA_AST_EXPRESSION_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "analysis/ast/ast_fwd.hpp"
#include "analysis/ast/match_pattern.hpp"
#include "analysis/ast/type_annotation.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"
#include "common/enum_name.hpp"

namespace luma {

struct Parameter {
    TypeAnnotation type;
    std::string name;
    ExpressionPtr default_value;
    bool is_mutable{false};
};

// Named argument in a call: name: expr.
struct NamedArgument {
    std::string name;
    ExpressionPtr value;
};

// ─────────────────────── Expressions ───────────────────────

enum class ExpressionKind {
    ArrayLiteral,
    Await,
    Binary,
    Call,
    DictionaryLiteral,
    Downcast,
    ErrorPipe,
    Failure,
    FieldAccess,
    If,
    IndexAccess,
    Is,
    Lambda,
    Literal,
    Match,
    Pipe,
    Range,
    RecordCreation,
    RecordWith,
    Some,
    Spawn,
    StringInterpolation,
    Success,
    TaskScope,
    TupleLiteral,
    Unary,
    Variable
};

[[nodiscard]] constexpr std::string_view to_string(ExpressionKind kind) noexcept {
    constexpr std::string_view k_names[] = {"ArrayLiteral",
                                            "Await",
                                            "Binary",
                                            "Call",
                                            "DictionaryLiteral",
                                            "Downcast",
                                            "ErrorPipe",
                                            "Failure",
                                            "FieldAccess",
                                            "If",
                                            "IndexAccess",
                                            "Is",
                                            "Lambda",
                                            "Literal",
                                            "Match",
                                            "Pipe",
                                            "Range",
                                            "RecordCreation",
                                            "RecordWith",
                                            "Some",
                                            "Spawn",
                                            "StringInterpolation",
                                            "Success",
                                            "TaskScope",
                                            "TupleLiteral",
                                            "Unary",
                                            "Variable"};

    static_assert(std::size(k_names) == static_cast<std::size_t>(ExpressionKind::Variable) + 1,
                  "ExpressionKind name table is out of sync with the enum");
    return enum_name(kind, k_names);
}

struct Expression {
    explicit Expression(ExpressionKind kind, SourceLocation loc) : kind{kind}, location{loc} {}

    Expression(const Expression&) = delete;
    Expression(Expression&&) noexcept = default;

    Expression& operator=(const Expression&) = delete;
    Expression& operator=(Expression&&) noexcept = default;

    virtual ~Expression() noexcept = default;

    ExpressionKind kind;
    SourceLocation location;
};

/// Attempts to narrow a base Expression reference to a concrete node type `T`.
/// Returns a pointer to the derived node when `node.kind` equals `T::static_kind`,
/// or nullptr otherwise.  Pairing the kind check with its downcast target means
/// the two cannot silently drift apart, unlike a hand-written
/// `if (node.kind != X) return; static_cast<const XExpression&>(node)` guard.
/// Complements the literal helpers below for the general downcast case.
template <typename T> [[nodiscard]] inline const T* try_cast(const Expression& node) noexcept {
    if (node.kind != T::static_kind) {
        return nullptr;
    }
    return static_cast<const T*>(&node);
}

struct LiteralExpression : Expression {
    static constexpr ExpressionKind static_kind = ExpressionKind::Literal;

    enum class LiteralType {
        Boolean,
        Integer,
        None,
        Number,
        String
    };

    /// Variant holding the actual literal value.
    /// Order matches LiteralType enum: Boolean(bool), Integer(int64), None(monostate),
    /// Number(double), String(string).
    using Value = std::variant<bool, std::int64_t, std::monostate, double, std::string>;

    explicit LiteralExpression(SourceLocation loc, bool boolean_value)
        : Expression{ExpressionKind::Literal, loc}, value{boolean_value} {}

    explicit LiteralExpression(SourceLocation loc, std::int64_t integer_value)
        : Expression{ExpressionKind::Literal, loc}, value{integer_value} {}

    explicit LiteralExpression(SourceLocation loc)
        : Expression{ExpressionKind::Literal, loc}, value{std::monostate{}} {}

    explicit LiteralExpression(SourceLocation loc, double number_value)
        : Expression{ExpressionKind::Literal, loc}, value{number_value} {}

    explicit LiteralExpression(SourceLocation loc, std::string string_value)
        : Expression{ExpressionKind::Literal, loc}, value{std::move(string_value)} {}

    Value value;

    // ── Typed accessors ──

    /// Returns the LiteralType discriminator.
    /// Variant index order matches the LiteralType enum values.
    [[nodiscard]] LiteralType literal_type() const noexcept {
        return static_cast<LiteralType>(value.index());
    }

    [[nodiscard]] std::int64_t integer_value() const {
        return std::get<std::int64_t>(value);
    }

    [[nodiscard]] double number_value() const {
        return std::get<double>(value);
    }

    [[nodiscard]] bool boolean_value() const {
        return std::get<bool>(value);
    }

    [[nodiscard]] const std::string& string_value() const {
        return std::get<std::string>(value);
    }
};

/// Returns true if the expression is an integer literal.
[[nodiscard]] inline bool is_integer_literal(const Expression& expr) noexcept {
    if (expr.kind != ExpressionKind::Literal) {
        return false;
    }
    return static_cast<const LiteralExpression&>(expr).literal_type() ==
           LiteralExpression::LiteralType::Integer;
}

/// Returns the integer value of a literal expression, or std::nullopt if not an integer literal.
[[nodiscard]] inline std::optional<std::int64_t>
get_integer_value(const Expression& expr) noexcept {
    if (expr.kind != ExpressionKind::Literal) {
        return std::nullopt;
    }
    const auto& lit = static_cast<const LiteralExpression&>(expr);
    if (lit.literal_type() == LiteralExpression::LiteralType::Integer) {
        return lit.integer_value();
    }
    return std::nullopt;
}

/// Returns true if the expression is a number (float) literal.
[[nodiscard]] inline bool is_number_literal(const Expression& expr) noexcept {
    if (expr.kind != ExpressionKind::Literal) {
        return false;
    }
    return static_cast<const LiteralExpression&>(expr).literal_type() ==
           LiteralExpression::LiteralType::Number;
}

/// Returns true if the expression is a boolean literal.
[[nodiscard]] inline bool is_boolean_literal(const Expression& expr) noexcept {
    if (expr.kind != ExpressionKind::Literal) {
        return false;
    }
    return static_cast<const LiteralExpression&>(expr).literal_type() ==
           LiteralExpression::LiteralType::Boolean;
}

struct VariableExpression : Expression {
    explicit VariableExpression(SourceLocation loc, std::string name)
        : Expression{ExpressionKind::Variable, loc}, name{std::move(name)} {}

    std::string name;
};

struct BinaryExpression : Expression {
    static constexpr ExpressionKind static_kind = ExpressionKind::Binary;

    explicit BinaryExpression(SourceLocation loc, ExpressionPtr left, TokenType op,
                              ExpressionPtr right)
        : Expression{ExpressionKind::Binary, loc},
          left{std::move(left)},
          op{op},
          right{std::move(right)} {}

    BinaryExpression(const BinaryExpression&) = delete;
    BinaryExpression(BinaryExpression&&) noexcept = default;

    BinaryExpression& operator=(const BinaryExpression&) = delete;
    BinaryExpression& operator=(BinaryExpression&&) noexcept = default;

    // Custom destructor tears the left spine down iteratively — see
    // release_linear_chain below.
    ~BinaryExpression() noexcept;

    ExpressionPtr left;
    TokenType op;
    ExpressionPtr right;
};

struct UnaryExpression : Expression {
    static constexpr ExpressionKind static_kind = ExpressionKind::Unary;

    explicit UnaryExpression(SourceLocation loc, TokenType op, ExpressionPtr operand)
        : Expression{ExpressionKind::Unary, loc}, op{op}, operand{std::move(operand)} {}

    TokenType op;
    ExpressionPtr operand;
};

struct CallExpression : Expression {
    explicit CallExpression(SourceLocation loc, ExpressionPtr callee,
                            std::vector<ExpressionPtr> arguments,
                            std::vector<NamedArgument> named_arguments,
                            std::vector<TypeAnnotation> type_arguments = {})
        : Expression{ExpressionKind::Call, loc},
          callee{std::move(callee)},
          arguments{std::move(arguments)},
          named_arguments{std::move(named_arguments)},
          type_arguments{std::move(type_arguments)} {}

    ExpressionPtr callee;
    std::vector<ExpressionPtr> arguments;
    std::vector<NamedArgument> named_arguments;
    std::vector<TypeAnnotation> type_arguments;
};

struct FieldAccessExpression : Expression {
    explicit FieldAccessExpression(SourceLocation loc, ExpressionPtr object, std::string field_name,
                                   bool is_optional = false)
        : Expression{ExpressionKind::FieldAccess, loc},
          object{std::move(object)},
          field_name{std::move(field_name)},
          is_optional{is_optional} {}

    ExpressionPtr object;
    std::string field_name;
    bool is_optional{false};
};

struct IndexAccessExpression : Expression {
    explicit IndexAccessExpression(SourceLocation loc, ExpressionPtr object, ExpressionPtr index,
                                   bool is_optional = false)
        : Expression{ExpressionKind::IndexAccess, loc},
          object{std::move(object)},
          index{std::move(index)},
          is_optional{is_optional} {}

    ExpressionPtr object;
    ExpressionPtr index;
    bool is_optional{false};
};

/// Policy for how root_variable_of() walks an access chain down to its root.
enum class ChainTraversal {
    /// Descend through field accesses only; stop at an index access.  Used
    /// where indexed-element mutation is permitted — e.g. 'a[i].x = …' mutates
    /// the stored element rather than the 'a' binding, so the chain must not be
    /// resolved past the '[i]'.
    FieldsOnly,
    /// Descend through both field and index accesses to reach the binding —
    /// e.g. 'a[i]', 'p.arr[i]', and 'a[i][j]' all resolve to their root variable.
    FieldsAndIndices
};

/// Returns the expression as a VariableExpression, or nullptr if it is not one.
[[nodiscard]] inline const VariableExpression* as_variable(const Expression& expr) noexcept {
    if (expr.kind == ExpressionKind::Variable) {
        return static_cast<const VariableExpression*>(&expr);
    }
    return nullptr;
}

/// Walks a field-access / index-access chain down to the root VariableExpression.
///
/// Returns nullptr if the chain does not bottom out at a variable (for example,
/// it is rooted in a call such as 'get_point().x'), or if traversal is
/// FieldsOnly and an index access is encountered before a variable.
[[nodiscard]] inline const VariableExpression* root_variable_of(const Expression& expr,
                                                                ChainTraversal traversal) noexcept {
    const Expression* current = &expr;
    while (current != nullptr) {
        switch (current->kind) {
            case ExpressionKind::Variable:
                return static_cast<const VariableExpression*>(current);
            case ExpressionKind::FieldAccess:
                current = static_cast<const FieldAccessExpression&>(*current).object.get();
                break;
            case ExpressionKind::IndexAccess:
                if (traversal == ChainTraversal::FieldsOnly) {
                    return nullptr;
                }
                current = static_cast<const IndexAccessExpression&>(*current).object.get();
                break;
            default:
                return nullptr;
        }
    }
    return nullptr;
}

struct LambdaExpression : Expression {
    /// A lambda body — either a block of statements or a single expression.
    using Body = std::variant<std::vector<StatementPtr>, ExpressionPtr>;

    explicit LambdaExpression(SourceLocation loc, std::vector<Parameter> parameters,
                              std::optional<TypeAnnotation> return_type)
        : Expression{ExpressionKind::Lambda, loc},
          parameters{std::move(parameters)},
          return_type{std::move(return_type)},
          body{std::vector<StatementPtr>{}} {}

    std::vector<Parameter> parameters;
    std::optional<TypeAnnotation> return_type;
    Body body;

    /// Returns true if the lambda has an expression body (=> expr).
    [[nodiscard]] bool is_expression_body() const noexcept {
        return std::holds_alternative<ExpressionPtr>(body);
    }

    /// Returns the expression body, or nullptr if this is a block body.
    [[nodiscard]] const Expression* expression_body() const noexcept {
        if (const auto* p = std::get_if<ExpressionPtr>(&body)) {
            return p->get();
        }
        return nullptr;
    }

    /// Returns the statement body (empty if this is an expression body).
    [[nodiscard]] const std::vector<StatementPtr>& statements() const noexcept {
        static const std::vector<StatementPtr> empty;
        if (const auto* p = std::get_if<std::vector<StatementPtr>>(&body)) {
            return *p;
        }
        return empty;
    }

    /// Returns the mutable statement body. Asserts that this is a block body.
    [[nodiscard]] std::vector<StatementPtr>& statements() {
        assert(!is_expression_body() && "statements() requires a block-body lambda");
        return std::get<std::vector<StatementPtr>>(body);
    }
};

struct IfExpression : Expression {
    /// A branch body — either a block of statements or a single expression.
    using Branch = std::variant<std::vector<StatementPtr>, ExpressionPtr>;

    explicit IfExpression(SourceLocation loc, ExpressionPtr condition)
        : Expression{ExpressionKind::If, loc},
          condition{std::move(condition)},
          then_branch{std::vector<StatementPtr>{}},
          else_branch{std::vector<StatementPtr>{}} {}

    ExpressionPtr condition;
    Branch then_branch;
    Branch else_branch;

    /// Returns the then-branch expression, or nullptr if it's a block body.
    [[nodiscard]] const Expression* then_expr() const noexcept {
        if (const auto* p = std::get_if<ExpressionPtr>(&then_branch)) {
            return p->get();
        }
        return nullptr;
    }

    /// Returns the else-branch expression, or nullptr if it's a block body.
    [[nodiscard]] const Expression* else_expr() const noexcept {
        if (const auto* p = std::get_if<ExpressionPtr>(&else_branch)) {
            return p->get();
        }
        return nullptr;
    }

    /// Returns the then-branch statement body (empty if expression branch).
    [[nodiscard]] const std::vector<StatementPtr>& then_body() const {
        static const std::vector<StatementPtr> empty;
        if (const auto* p = std::get_if<std::vector<StatementPtr>>(&then_branch)) {
            return *p;
        }
        return empty;
    }

    /// Returns the mutable then-branch statement body.
    [[nodiscard]] std::vector<StatementPtr>& then_body_mut() {
        return std::get<std::vector<StatementPtr>>(then_branch);
    }

    /// Returns the else-branch statement body (empty if expression branch).
    [[nodiscard]] const std::vector<StatementPtr>& else_body() const {
        static const std::vector<StatementPtr> empty;
        if (const auto* p = std::get_if<std::vector<StatementPtr>>(&else_branch)) {
            return *p;
        }
        return empty;
    }

    /// Returns the mutable else-branch statement body.
    [[nodiscard]] std::vector<StatementPtr>& else_body_mut() {
        return std::get<std::vector<StatementPtr>>(else_branch);
    }
};

struct MatchExpression : Expression {
    explicit MatchExpression(SourceLocation loc, ExpressionPtr subject)
        : Expression{ExpressionKind::Match, loc}, subject{std::move(subject)} {}

    ExpressionPtr subject;
    std::vector<MatchArm> arms;
};

struct PipeExpression : Expression {
    explicit PipeExpression(SourceLocation loc, ExpressionPtr left, ExpressionPtr right)
        : Expression{ExpressionKind::Pipe, loc}, left{std::move(left)}, right{std::move(right)} {}

    PipeExpression(const PipeExpression&) = delete;
    PipeExpression(PipeExpression&&) noexcept = default;

    PipeExpression& operator=(const PipeExpression&) = delete;
    PipeExpression& operator=(PipeExpression&&) noexcept = default;

    // Custom destructor tears the left spine down iteratively — see
    // release_linear_chain below.
    ~PipeExpression() noexcept;

    ExpressionPtr left;
    ExpressionPtr right;
};

// left !> right — unwrap success(v) from left and pipe v into right;
// short-circuit to failure(msg) if left is a failure result.
struct ErrorPipeExpression : Expression {
    explicit ErrorPipeExpression(SourceLocation loc, ExpressionPtr left, ExpressionPtr right)
        : Expression{ExpressionKind::ErrorPipe, loc},
          left{std::move(left)},
          right{std::move(right)} {}

    ErrorPipeExpression(const ErrorPipeExpression&) = delete;
    ErrorPipeExpression(ErrorPipeExpression&&) noexcept = default;

    ErrorPipeExpression& operator=(const ErrorPipeExpression&) = delete;
    ErrorPipeExpression& operator=(ErrorPipeExpression&&) noexcept = default;

    // Custom destructor tears the left spine down iteratively — see
    // release_linear_chain below.
    ~ErrorPipeExpression() noexcept;

    ExpressionPtr left;
    ExpressionPtr right;
};

namespace detail {

// Detaches and returns the deep (left-associative) child of a linear expression
// node, or nullptr when `node` is not one. Binary, pipe, and error-pipe chains
// are the only node kinds a parser builds iteratively, so they are the only ones
// whose left spine can grow past the recursive-descent depth guard.
[[nodiscard]] inline ExpressionPtr detach_linear_child(Expression& node) noexcept {
    switch (node.kind) {
        case ExpressionKind::Binary:
            return std::move(static_cast<BinaryExpression&>(node).left);
        case ExpressionKind::Pipe:
            return std::move(static_cast<PipeExpression&>(node).left);
        case ExpressionKind::ErrorPipe:
            return std::move(static_cast<ErrorPipeExpression&>(node).left);
        default:
            return nullptr;
    }
}

// Iteratively releases a left-associative chain of expression nodes. A long
// operator or pipe chain (e.g. `a + b + c + ...` with thousands of terms) forms
// a deeply left-leaning tree whose naive recursive destructor would overflow the
// native stack. Walking the spine into a local and destroying one node at a time
// keeps teardown at O(1) stack depth; each detached node's remaining (shallow)
// children are freed by its own destructor.
inline void release_linear_chain(ExpressionPtr head) noexcept {
    while (head) {
        ExpressionPtr next = detach_linear_child(*head);
        head.reset();
        head = std::move(next);
    }
}

} // namespace detail

inline BinaryExpression::~BinaryExpression() noexcept {
    detail::release_linear_chain(std::move(left));
}

inline PipeExpression::~PipeExpression() noexcept {
    detail::release_linear_chain(std::move(left));
}

inline ErrorPipeExpression::~ErrorPipeExpression() noexcept {
    detail::release_linear_chain(std::move(left));
}

struct RecordFieldInit {
    std::string name;
    ExpressionPtr value;
};

struct RecordCreationExpression : Expression {
    explicit RecordCreationExpression(SourceLocation loc, std::string type_name,
                                      std::vector<TypeAnnotation> type_args,
                                      std::vector<RecordFieldInit> fields)
        : Expression{ExpressionKind::RecordCreation, loc},
          type_name{std::move(type_name)},
          type_args{std::move(type_args)},
          fields{std::move(fields)} {}

    std::string type_name;
    std::vector<TypeAnnotation> type_args; // explicit generic type arguments, e.g. Box<integer>
    std::vector<RecordFieldInit> fields;
};

struct RecordWithExpression : Expression {
    explicit RecordWithExpression(SourceLocation loc, ExpressionPtr base,
                                  std::vector<RecordFieldInit> overrides)
        : Expression{ExpressionKind::RecordWith, loc},
          base{std::move(base)},
          overrides{std::move(overrides)} {}

    ExpressionPtr base;
    std::vector<RecordFieldInit> overrides;
};

struct ArrayLiteralExpression : Expression {
    explicit ArrayLiteralExpression(SourceLocation loc, std::vector<ExpressionPtr> elements)
        : Expression{ExpressionKind::ArrayLiteral, loc}, elements{std::move(elements)} {}

    std::vector<ExpressionPtr> elements;
};

struct DictionaryEntry {
    ExpressionPtr key;
    ExpressionPtr value;
};

struct DictionaryLiteralExpression : Expression {
    explicit DictionaryLiteralExpression(SourceLocation loc, std::vector<DictionaryEntry> entries)
        : Expression{ExpressionKind::DictionaryLiteral, loc}, entries{std::move(entries)} {}

    std::vector<DictionaryEntry> entries;
};

struct TupleLiteralExpression : Expression {
    explicit TupleLiteralExpression(SourceLocation loc, std::vector<ExpressionPtr> elements)
        : Expression{ExpressionKind::TupleLiteral, loc}, elements{std::move(elements)} {}

    std::vector<ExpressionPtr> elements;
};

struct StringInterpolationExpression : Expression {
    explicit StringInterpolationExpression(SourceLocation loc)
        : Expression{ExpressionKind::StringInterpolation, loc} {}

    std::vector<std::string> parts;
    std::vector<ExpressionPtr> expressions;
};

struct DowncastExpression : Expression {
    explicit DowncastExpression(SourceLocation loc, TypeAnnotation target_type,
                                ExpressionPtr operand, bool is_trusted = false)
        : Expression{ExpressionKind::Downcast, loc},
          target_type{std::move(target_type)},
          operand{std::move(operand)},
          is_trusted{is_trusted} {}

    TypeAnnotation target_type;
    ExpressionPtr operand;
    bool is_trusted{false};
};

struct IsExpression : Expression {
    explicit IsExpression(SourceLocation loc, TypeAnnotation target_type, ExpressionPtr operand)
        : Expression{ExpressionKind::Is, loc},
          target_type{std::move(target_type)},
          operand{std::move(operand)} {}

    TypeAnnotation target_type;
    ExpressionPtr operand;
};

struct SpawnExpression : Expression {
    explicit SpawnExpression(SourceLocation loc, ExpressionPtr call)
        : Expression{ExpressionKind::Spawn, loc}, call{std::move(call)} {}

    ExpressionPtr call;
};

struct TaskScopeExpression : Expression {
    explicit TaskScopeExpression(SourceLocation loc, std::vector<StatementPtr> body)
        : Expression{ExpressionKind::TaskScope, loc}, body{std::move(body)} {}

    std::vector<StatementPtr> body;
};

struct AwaitExpression : Expression {
    explicit AwaitExpression(SourceLocation loc, ExpressionPtr operand)
        : Expression{ExpressionKind::Await, loc}, operand{std::move(operand)} {}

    ExpressionPtr operand;
};

struct SuccessExpression : Expression {
    explicit SuccessExpression(SourceLocation loc, ExpressionPtr value)
        : Expression{ExpressionKind::Success, loc}, value{std::move(value)} {}

    ExpressionPtr value;
};

struct SomeExpression : Expression {
    explicit SomeExpression(SourceLocation loc, ExpressionPtr value)
        : Expression{ExpressionKind::Some, loc}, value{std::move(value)} {}

    ExpressionPtr value;
};

struct FailureExpression : Expression {
    explicit FailureExpression(SourceLocation loc, ExpressionPtr message)
        : Expression{ExpressionKind::Failure, loc}, message{std::move(message)} {}

    ExpressionPtr message;
};

struct RangeExpression : Expression {
    explicit RangeExpression(SourceLocation loc, ExpressionPtr start, ExpressionPtr end,
                             bool inclusive = false)
        : Expression{ExpressionKind::Range, loc},
          start{std::move(start)},
          end{std::move(end)},
          inclusive{inclusive} {}

    ExpressionPtr start;
    ExpressionPtr end;
    bool inclusive{false};
};

} // namespace luma

#endif // LUMA_AST_EXPRESSION_HPP
