#ifndef LUMA_AST_VISITOR_HPP
#define LUMA_AST_VISITOR_HPP

// ─────────────────────────────────────────────────────────────────────────────
// AST Visitor Helpers
// ─────────────────────────────────────────────────────────────────────────────
//
// @file  ast_visitor.hpp
// @brief Convenience umbrella that pairs the canonical AST dispatch
//        functions with the shared `overloaded` overload-set combiner.
//
// Include this single header to visit an AST node with an inline set of
// lambdas — one per node kind — instead of defining a named visitor struct:
//
//   dispatch_expression(expr, overloaded{
//       [](const LiteralExpression& e) { /* handle literal */ },
//       [](const BinaryExpression& e)  { /* handle binary  */ },
//       [](const auto&)                { /* default         */ }
//   });
//
// The dispatch_* functions live in ast_dispatch.hpp (they switch on the
// node's kind); `overloaded` lives in common/overloaded.hpp.  This header
// only bundles the two so callers need a single include.
// ─────────────────────────────────────────────────────────────────────────────

#include "analysis/ast/ast_dispatch.hpp"
#include "common/overloaded.hpp"

#endif // LUMA_AST_VISITOR_HPP
