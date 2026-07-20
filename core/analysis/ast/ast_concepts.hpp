#ifndef LUMA_AST_CONCEPTS_HPP
#define LUMA_AST_CONCEPTS_HPP

// ─────────────────────────────────────────────────────────────────────────────
// AST Concepts — C++20 Concept Constraints for AST-Related Templates
// ─────────────────────────────────────────────────────────────────────────────
//
// @file  ast_concepts.hpp
// @brief C++20 concepts that constrain template parameters operating on AST
//        nodes.  These provide compile-time validation and improved error
//        messages when a type does not satisfy the structural requirements
//        of an AST node, visitor, or dispatcher.
//
// Concepts defined:
//   - HasSourceLocation   — type has a SourceLocation `location` member
//   - AstNode             — base structural concept for all AST nodes
//   - ExpressionNode      — derived from Expression with ExpressionKind
//   - StatementNode       — derived from Statement with StatementKind
//   - DeclarationNode     — derived from Declaration with DeclarationKind
//   - VisitableNode       — AstNode whose kind enum has a known underlying type
//   - TypeAnnotatedNode   — AstNode that carries a TypeAnnotation `.type` member
//
// The visitor/handler concepts (ExpressionVisitor, StatementVisitor,
// DeclarationVisitor) are defined in ast_dispatch.hpp alongside the
// dispatch functions they constrain.
// ─────────────────────────────────────────────────────────────────────────────

#include <concepts>
#include <type_traits>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/ast/type_annotation.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

// ─────────────────────── Structural Concepts ───────────────────────

/// A type that carries source location information.
template <typename T>
concept HasSourceLocation = requires(const T& node) {
    { node.location } -> std::convertible_to<SourceLocation>;
};

/// Base concept for all AST nodes: must have a kind discriminator and
/// a source location.  The kind member is used by dispatch functions
/// to route to the correct concrete type.
template <typename T>
concept AstNode = HasSourceLocation<T> && requires(const T& node) { node.kind; };

/// An expression AST node: derived from Expression and identified by
/// ExpressionKind.
template <typename T>
concept ExpressionNode = AstNode<T> && std::derived_from<T, Expression> &&
                         std::is_same_v<decltype(T::kind), ExpressionKind>;

/// A statement AST node: derived from Statement and identified by
/// StatementKind.
template <typename T>
concept StatementNode = AstNode<T> && std::derived_from<T, Statement> &&
                        std::is_same_v<decltype(T::kind), StatementKind>;

/// A declaration AST node: derived from Declaration and identified by
/// DeclarationKind.
template <typename T>
concept DeclarationNode = AstNode<T> && std::derived_from<T, Declaration> &&
                          std::is_same_v<decltype(T::kind), DeclarationKind>;

// ─────────────────────── Derived Concepts ───────────────────────

/// An AST node that can be visited (has a kind enum with a known
/// underlying type, suitable for switch-based dispatch).
template <typename T>
concept VisitableNode =
    AstNode<T> && requires { typename std::underlying_type_t<decltype(T::kind)>; };

/// A node that carries a syntactic type annotation.
/// Applies to nodes such as VariableDeclStatement, whose `.type` member holds
/// the type the user wrote in source.
template <typename T>
concept TypeAnnotatedNode = AstNode<T> && requires(const T& node) {
    { node.type } -> std::convertible_to<const TypeAnnotation&>;
};

} // namespace luma

#endif // LUMA_AST_CONCEPTS_HPP
