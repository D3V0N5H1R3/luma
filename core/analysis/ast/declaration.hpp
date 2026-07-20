#ifndef LUMA_AST_DECLARATION_HPP
#define LUMA_AST_DECLARATION_HPP

#include <cstddef>
#include <string_view>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/source/source_location.hpp"
#include "common/enum_name.hpp"

namespace luma {

// ─────────────────────── Declarations ───────────────────────

enum class DeclarationKind {
    Choice,
    Function,
    Include,
    Interface,
    Namespace,
    Record,
    TypeAlias,
    Use
};

[[nodiscard]] constexpr std::string_view to_string(DeclarationKind kind) noexcept {
    constexpr std::string_view k_names[] = {"Choice",    "Function", "Include",   "Interface",
                                            "Namespace", "Record",   "TypeAlias", "Use"};

    static_assert(std::size(k_names) == static_cast<std::size_t>(DeclarationKind::Use) + 1,
                  "DeclarationKind name table is out of sync with the enum");
    return enum_name(kind, k_names);
}

struct Declaration {
    Declaration(DeclarationKind kind, SourceLocation loc) : kind{kind}, location{loc} {}

    Declaration(const Declaration&) = delete;
    Declaration(Declaration&&) noexcept = default;

    Declaration& operator=(const Declaration&) = delete;
    Declaration& operator=(Declaration&&) noexcept = default;

    virtual ~Declaration() noexcept = default;

    DeclarationKind kind;
    SourceLocation location;

    // True when declared with the 'internal' keyword inside a namespace.
    bool is_internal_to_namespace{false};
};

/// A single generic type parameter, optionally constrained by interface bounds.
/// Example: `T` (unbounded) or `T: Comparable` or `T: Serializable ++ Comparable`.
struct TypeParam {
    explicit TypeParam(std::string name) : name{std::move(name)} {}

    TypeParam(std::string name, std::vector<std::string> bounds)
        : name{std::move(name)}, bounds{std::move(bounds)} {}

    std::string name;
    std::vector<std::string> bounds; // interface names this param must satisfy
};

struct FunctionDeclaration : Declaration {
    explicit FunctionDeclaration(SourceLocation loc, std::string name)
        : Declaration{DeclarationKind::Function, loc}, name{std::move(name)} {}

    std::string name;
    std::vector<TypeParam> type_params; // generic type parameters
    std::vector<Parameter> parameters;
    TypeAnnotation return_type;
    std::vector<StatementPtr> body;
    bool is_main{false};
    bool is_test{false};
};

struct RecordField {
    TypeAnnotation type;
    std::string name;
    ExpressionPtr default_value; // optional — null means "required"
};

struct RecordDeclaration : Declaration {
    explicit RecordDeclaration(SourceLocation loc, std::string name)
        : Declaration{DeclarationKind::Record, loc}, name{std::move(name)} {}

    std::string name;
    std::vector<TypeParam> type_params; // generic type parameters
    std::vector<RecordField> fields;
};

// A single variant of a choice (ADT / tagged union).
struct ChoiceVariant {
    std::string name;
    std::vector<Parameter> fields; // empty for unit variants
};

struct ChoiceDeclaration : Declaration {
    explicit ChoiceDeclaration(SourceLocation loc, std::string name)
        : Declaration{DeclarationKind::Choice, loc}, name{std::move(name)} {}

    std::string name;
    std::vector<TypeParam> type_params; // generic type parameters
    std::vector<ChoiceVariant> variants;
};

struct InterfaceDeclaration : Declaration {
    explicit InterfaceDeclaration(SourceLocation loc, std::string name)
        : Declaration{DeclarationKind::Interface, loc}, name{std::move(name)} {}

    std::string name;
    std::vector<TypeParam> type_params; // generic type parameters
    std::vector<RecordField> fields;
};

struct NamespaceDeclaration : Declaration {
    explicit NamespaceDeclaration(SourceLocation loc, std::string name)
        : Declaration{DeclarationKind::Namespace, loc}, name{std::move(name)} {}

    std::string name;
    std::vector<DeclarationPtr> declarations;
};

struct TypeAliasDeclaration : Declaration {
    explicit TypeAliasDeclaration(SourceLocation loc, std::string name, TypeAnnotation target)
        : Declaration{DeclarationKind::TypeAlias, loc},
          name{std::move(name)},
          target_type{std::move(target)} {}

    std::string name;
    std::vector<TypeParam> type_params; // generic type parameters
    TypeAnnotation target_type;
};

struct IncludeDeclaration : Declaration {
    explicit IncludeDeclaration(SourceLocation loc, std::string path)
        : Declaration{DeclarationKind::Include, loc}, path{std::move(path)} {}

    std::string path;
};

struct UseDeclaration : Declaration {
    explicit UseDeclaration(SourceLocation loc, std::string path)
        : Declaration{DeclarationKind::Use, loc}, namespace_path{std::move(path)} {}

    std::string namespace_path; // e.g., "Geometry" or "Geometry.distance"
};

// ─────────────────────── Program ───────────────────────

struct Program {
    std::vector<DeclarationPtr> declarations;
    std::vector<StatementPtr> statements;
};

} // namespace luma

#endif // LUMA_AST_DECLARATION_HPP
