#ifndef LUMA_AST_TYPE_ANNOTATION_HPP
#define LUMA_AST_TYPE_ANNOTATION_HPP

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace luma {

// ─────────────────────── Type Annotation ───────────────────────

// TypeAnnotation: Syntactic type expression from source code (AST level).
// Represents the type as written by the programmer before any resolution or
// validation.  Compare with TypeInfo (type_info.hpp) which represents the
// resolved, validated type after analysis.

/// Discriminates the three mutually exclusive shapes of a TypeAnnotation.
enum class TypeAnnotationKind {
    Plain,    ///< Named (optionally generic) type, e.g. "integer", "array<string>".
    Tuple,    ///< Tuple type, e.g. (integer, string).
    Function, ///< Function type, e.g. function(integer) -> string.
};

/// Represents a user-written type annotation in source code.
///
/// Uses a std::variant internally to represent three mutually exclusive shapes:
///   1. **Plain / generic type** — name + generic type parameters
///   2. **Tuple type** — element types
///   3. **Function type** — parameter types + return type
///
/// Orthogonal ownership qualifiers (`is_unique`, `is_borrow`) may combine
/// with any shape.
///
/// Construction: use `make_plain()`, `make_tuple()`, `make_function()` factory
/// methods, or the convenience constructors.
struct TypeAnnotation {
    // ── Internal shape data types ──

    struct PlainData {
        std::string name;
        std::vector<TypeAnnotation> type_params;
    };

    struct TupleData {
        std::vector<TypeAnnotation> element_types;
    };

    struct FunctionData {
        std::vector<TypeAnnotation> param_types;
        std::unique_ptr<TypeAnnotation> return_type;

        FunctionData() = default;

        FunctionData(std::vector<TypeAnnotation> params, std::unique_ptr<TypeAnnotation> ret)
            : param_types{std::move(params)}, return_type{std::move(ret)} {}

        FunctionData(FunctionData&&) noexcept = default;

        FunctionData(const FunctionData& other)
            : param_types{other.param_types},
              return_type{other.return_type ? std::make_unique<TypeAnnotation>(*other.return_type)
                                            : nullptr} {}

        FunctionData& operator=(FunctionData&&) noexcept = default;

        FunctionData& operator=(const FunctionData& other) {
            if (this != &other) {
                param_types = other.param_types;
                return_type = other.return_type
                                  ? std::make_unique<TypeAnnotation>(*other.return_type)
                                  : nullptr;
            }
            return *this;
        }

        ~FunctionData() noexcept = default;
    };

    using Shape = std::variant<PlainData, TupleData, FunctionData>;

    // ── Data members ──

    Shape shape;
    bool is_unique{false};
    bool is_borrow{false};

    // ── Constructors ──

    TypeAnnotation() : shape{PlainData{}} {}

    explicit TypeAnnotation(std::string name) : shape{PlainData{std::move(name), {}}} {}

    TypeAnnotation(const TypeAnnotation&) = default;
    TypeAnnotation(TypeAnnotation&&) noexcept = default;
    TypeAnnotation& operator=(const TypeAnnotation&) = default;
    TypeAnnotation& operator=(TypeAnnotation&&) noexcept = default;
    ~TypeAnnotation() noexcept = default;

    // ── Shape queries ──

    /// Returns the discriminated kind of this type annotation.
    [[nodiscard]] TypeAnnotationKind kind() const noexcept {
        if (std::holds_alternative<TupleData>(shape)) {
            return TypeAnnotationKind::Tuple;
        }
        if (std::holds_alternative<FunctionData>(shape)) {
            return TypeAnnotationKind::Function;
        }
        return TypeAnnotationKind::Plain;
    }

    /// Returns true if this is a plain or generic type (neither tuple nor function).
    [[nodiscard]] bool is_plain() const noexcept {
        return std::holds_alternative<PlainData>(shape);
    }

    /// Whether this annotation represents a tuple type.
    [[nodiscard]] bool is_tuple() const noexcept {
        return std::holds_alternative<TupleData>(shape);
    }

    /// Whether this annotation represents a function type.
    [[nodiscard]] bool is_func() const noexcept {
        return std::holds_alternative<FunctionData>(shape);
    }

    // ── Name and type_params accessors ──
    // These work across shapes for backward compatibility:
    //   - Plain: name = type name, type_params = generic args
    //   - Function: name = "function", type_params = param types
    //   - Tuple: name = "", type_params = empty

    /// Returns the type name. Empty for tuple types, "function" for function types.
    [[nodiscard]] const std::string& name() const noexcept {
        static const std::string k_function = "function";
        static const std::string k_empty;
        if (const auto* p = std::get_if<PlainData>(&shape)) {
            return p->name;
        }
        if (std::holds_alternative<FunctionData>(shape)) {
            return k_function;
        }
        return k_empty;
    }

    /// Returns mutable reference to the type name. Only valid for plain types.
    [[nodiscard]] std::string& name() {
        return std::get<PlainData>(shape).name;
    }

    /// Returns type parameters (plain: generic args, function: param types).
    [[nodiscard]] const std::vector<TypeAnnotation>& type_params() const noexcept {
        static const std::vector<TypeAnnotation> k_empty;
        if (const auto* p = std::get_if<PlainData>(&shape)) {
            return p->type_params;
        }
        if (const auto* f = std::get_if<FunctionData>(&shape)) {
            return f->param_types;
        }
        return k_empty;
    }

    /// Returns mutable type parameters (plain: generic args, function: param types).
    [[nodiscard]] std::vector<TypeAnnotation>& type_params() {
        if (auto* p = std::get_if<PlainData>(&shape)) {
            return p->type_params;
        }
        return std::get<FunctionData>(shape).param_types;
    }

    // ── Convenience type checks ──

    [[nodiscard]] bool is_number_type() const noexcept {
        return name() == "number";
    }

    [[nodiscard]] bool is_integer_type() const noexcept {
        return name() == "integer";
    }

    [[nodiscard]] bool is_string_type() const noexcept {
        return name() == "string";
    }

    [[nodiscard]] bool is_boolean_type() const noexcept {
        return name() == "boolean";
    }

    // ── Tuple type accessors ──

    /// Returns the tuple element types.  Asserts that this is a tuple type.
    [[nodiscard]] const std::vector<TypeAnnotation>& tuple_elements() const {
        assert(is_tuple() && "tuple_elements() called on non-tuple type");
        return std::get<TupleData>(shape).element_types;
    }

    /// Mutable access to the tuple element types.  Asserts that this is a tuple type.
    [[nodiscard]] std::vector<TypeAnnotation>& tuple_elements() {
        assert(is_tuple() && "tuple_elements() called on non-tuple type");
        return std::get<TupleData>(shape).element_types;
    }

    // ── Function type accessors ──

    /// Returns the function return type.  Asserts that this is a function type
    /// with a non-null return type.
    [[nodiscard]] const TypeAnnotation& return_type_ref() const {
        assert(is_func() && "return_type_ref() called on non-function type");
        const auto& f = std::get<FunctionData>(shape);
        assert(f.return_type && "function type has no return type");
        return *f.return_type;
    }

    /// Returns the function parameter types. Asserts that this is a function type.
    [[nodiscard]] const std::vector<TypeAnnotation>& function_params() const {
        assert(is_func() && "function_params() called on non-function type");
        return std::get<FunctionData>(shape).param_types;
    }

    /// Backward-compatible access to return type pointer (null for non-function types).
    [[nodiscard]] const TypeAnnotation* return_type_ptr() const noexcept {
        if (const auto* f = std::get_if<FunctionData>(&shape)) {
            return f->return_type.get();
        }
        return nullptr;
    }

    // ── Factory methods ──

    /// Construct a plain or generic type (e.g. "integer", "array<string>").
    [[nodiscard]] static TypeAnnotation make_plain(std::string name,
                                                   std::vector<TypeAnnotation> params = {}) {
        TypeAnnotation ann;
        ann.shape = PlainData{std::move(name), std::move(params)};
        return ann;
    }

    /// Construct a tuple type (e.g. (integer, string)).
    [[nodiscard]] static TypeAnnotation make_tuple(std::vector<TypeAnnotation> elements) {
        TypeAnnotation ann;
        ann.shape = TupleData{std::move(elements)};
        return ann;
    }

    /// Construct a function type (e.g. function(integer) -> string).
    [[nodiscard]] static TypeAnnotation make_function(std::vector<TypeAnnotation> params,
                                                      TypeAnnotation ret) {
        TypeAnnotation ann;
        ann.shape =
            FunctionData{std::move(params), std::make_unique<TypeAnnotation>(std::move(ret))};
        return ann;
    }
};

} // namespace luma

#endif // LUMA_AST_TYPE_ANNOTATION_HPP
