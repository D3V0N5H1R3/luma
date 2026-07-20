// ─────────────────────────────────────────────────────────────────────────────
// Symbol Registry
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Centralised store of named symbols for "did you mean?"
//                 suggestions.  Populated during the TypeChecker registration
//                 pass; queried by the TypeChecker's suggestion helpers.
//
// Key Types:
//   - SuggestionCategory: Discriminates functions, variables, types, and modules.
//   - SymbolRegistry: Flat name store with kind tagging and bulk name access.
//
// Design:
//   The registry is reset at the start of each check() call and populated
//   during the registration pass.  It provides a single all_symbol_names()
//   view over all registered names, replacing the previous pattern of
//   iterating records_, choices_, interfaces_, type_aliases_, and functions_
//   separately in the suggestion helpers.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/string_hash.hpp"

namespace luma {

// ── Symbol Lookup Architecture ──
//
// The type checker uses four independent lookup mechanisms; their priority
// ordering and the helpers that wrap them are documented in
// TypeCheckingServices (type_checking_context.hpp) under the
// "Symbol lookup" section.
//
// The SymbolRegistry below handles one cross-cutting slice of symbol
// management: a flat name → kind mapping used exclusively for
// "did you mean?" suggestion candidates.  It is populated during the
// registration pass and queried by suggest_type_name() /
// suggest_variable_name().  It is not a general-purpose lookup table —
// for runtime symbol resolution use TypeCheckingServices::lookup_variable(),
// find_record(), find_choice(), etc.
//
// See: documents/Luma_Software_Architecture.md for design context.

enum class SuggestionCategory {
    Variable,
    Function,
    Type,
    Module,
};

// Centralised registry of named symbols used for "did you mean?" suggestions.
//
// Symbols are registered with a kind tag during the TypeChecker registration
// pass.  The registry is reset at the start of each check() call so it always
// reflects the current program.
class SymbolRegistry {
public:
    // Lightweight view of a registered symbol.
    // name is a view into the registry's internal storage — valid until
    // the next call to reset() or the registry is destroyed.
    struct Entry {
        std::string_view name;
        SuggestionCategory kind;
    };

    // Register a symbol with the given kind.  Duplicate names are allowed
    // (e.g. the same qualified name registered under multiple aliases).
    void register_symbol(std::string_view name, SuggestionCategory kind) {
        const auto idx = symbols_.size();
        symbols_.push_back({std::string{name}, kind});
        // Only record the first occurrence so find() returns the first match.
        index_.try_emplace(std::string{name}, idx);
    }

    // Returns a view of every registered name, regardless of kind.
    // The returned string_views are valid until the next reset() call.
    [[nodiscard]] std::vector<std::string_view> all_symbol_names() const {
        std::vector<std::string_view> names;
        names.reserve(symbols_.size());

        for (const auto& entry : symbols_) {
            names.push_back(entry.name);
        }

        return names;
    }

    // Finds the first registered entry whose name equals name, if any.
    // O(1) lookup via the hash map index.
    [[nodiscard]] std::optional<Entry> find(std::string_view name) const {
        const auto it = index_.find(name);

        if (it != index_.end()) {
            const auto& entry = symbols_[it->second];
            return Entry{entry.name, entry.kind};
        }

        return std::nullopt;
    }

    // Clears all registered symbols.  Call at the start of each check() pass.
    void reset() {
        symbols_.clear();
        index_.clear();
    }

private:
    struct StoredEntry {
        // TODO: Consider using StringInterner (already used by the compiler
        // and variable resolver) for symbol name storage.  Symbol names are
        // frequently repeated (e.g. the same identifier registered under
        // multiple aliases) and interning would deduplicate them while
        // reducing per-entry overhead.  Requires threading an interner
        // through the type-checking phase; worth pursuing when profiling
        // shows symbol name duplication as a measurable cost.
        std::string name;
        SuggestionCategory kind;
    };

    std::vector<StoredEntry> symbols_;

    // Maps symbol name → index of the first occurrence in symbols_.
    // Enables O(1) lookup in find() instead of linear search.
    StringMap<std::size_t> index_;
};

} // namespace luma
