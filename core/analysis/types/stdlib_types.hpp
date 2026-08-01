#pragma once

#include <string>

#include "analysis/ast/declaration.hpp"
#include "common/string_hash.hpp"

namespace luma {

// ─────────────────────── Stdlib Type Registry ───────────────────────
//
// Provides synthetic RecordDeclaration and ChoiceDeclaration AST nodes
// for types that are built into the standard library.  These types are
// owned by a static storage and their pointers remain valid for the
// entire program lifetime.
//
// Both the type checker and the interpreter call into this module to
// register the same type definitions, so that Luma programmers can:
//   - use the type names in annotations (e.g. Http.Response)
//   - create record literals (e.g. Http.Response { ... })
//   - pattern-match on choice variants (e.g. Log.Level.Information)

// Returns all synthetic record declarations keyed by their qualified
// name (e.g. "DateTime.TimeParts", "Http.Response").
[[nodiscard]] const StringMap<const RecordDeclaration*>& stdlib_record_types();

// Returns all synthetic choice declarations keyed by their qualified
// name (e.g. "Log.Level").
[[nodiscard]] const StringMap<const ChoiceDeclaration*>& stdlib_choice_types();

} // namespace luma
