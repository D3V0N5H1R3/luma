// ─────────────────────────────────────────────────────────────────────────────
// TypeScope                                        (TypeScope implementation)
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "analysis/types/type_info.hpp"

namespace luma {

TypeScope::TypeScope(std::shared_ptr<TypeScope> parent) : parent_{std::move(parent)} {}

void TypeScope::define(std::string_view name, TypeInfo type, VariableModifiers modifiers,
                       SourceLocation loc) {
    if (modifiers.is_unique && std::ranges::find(unique_names_, name) == unique_names_.end()) {
        unique_names_.emplace_back(name);
    }

    symbols_[std::string{name}] = SymbolInfo{
        .type = std::move(type),
        .is_mutable = modifiers.is_mutable,
        .is_unique = modifiers.is_unique,
        .is_borrow = modifiers.is_borrow,
        .location = loc,
    };
}

const SymbolInfo* TypeScope::lookup(std::string_view name) const {
    const auto it = symbols_.find(name);

    if (it != symbols_.end()) {
        return &it->second;
    }

    if (parent_) {
        return parent_->lookup(name);
    }

    return nullptr;
}

SymbolInfo* TypeScope::lookup_mut(std::string_view name) {
    auto it = symbols_.find(name);

    if (it != symbols_.end()) {
        return &it->second;
    }

    if (parent_) {
        return parent_->lookup_mut(name);
    }

    return nullptr;
}

void TypeScope::mark_consumed(std::string_view name, bool consumed) {
    auto* sym = lookup_mut(name);

    if ((sym != nullptr) && sym->is_unique) {
        sym->is_consumed = consumed;
    }
}

void TypeScope::mark_read(std::string_view name) {
    auto* sym = lookup_mut(name);

    if (sym != nullptr) {
        sym->is_read = true;
    }
}

void TypeScope::mark_written(std::string_view name) {
    auto* sym = lookup_mut(name);

    if (sym != nullptr) {
        sym->is_written = true;
    }
}

void TypeScope::mark_as_parameter(std::string_view name) {
    auto* sym = lookup_mut(name);

    if (sym != nullptr) {
        sym->is_parameter = true;
    }
}

bool TypeScope::has_local(std::string_view name) const {
    return symbols_.contains(name);
}

std::shared_ptr<TypeScope> TypeScope::parent() const {
    return parent_;
}

std::vector<std::pair<std::string, SymbolInfo>> TypeScope::unconsumed_unique_locals() const {
    std::vector<std::pair<std::string, SymbolInfo>> result;

    for (const auto& [name, sym] : symbols_) {
        if (sym.is_unique && !sym.is_consumed && !name.starts_with('_')) {
            result.emplace_back(name, sym);
        }
    }

    return result;
}

const StringMap<SymbolInfo>& TypeScope::locals() const {
    return symbols_;
}

TypeScope::OwnershipSnapshot TypeScope::snapshot_ownership() const {
    OwnershipSnapshot snap;

    // Innermost scope first, so a shadowing variable claims its name before
    // the shadowed outer one is reached.  Only names defined as unique are
    // visited; one is skipped when any inner scope already visited defines
    // the same name (unique or not), because it is hidden there.
    for (const auto* scope = this; scope != nullptr; scope = scope->parent_.get()) {
        for (const auto& name : scope->unique_names_) {
            const auto it = scope->symbols_.find(name);

            if (it == scope->symbols_.end() || !it->second.is_unique) {
                continue;
            }

            bool is_shadowed{false};

            for (const auto* inner = this; inner != scope; inner = inner->parent_.get()) {
                if (inner->has_local(name)) {
                    is_shadowed = true;
                    break;
                }
            }

            if (!is_shadowed) {
                snap.emplace_back(name, it->second.is_consumed);
            }
        }
    }

    return snap;
}

void TypeScope::restore_ownership(const OwnershipSnapshot& snap) {
    for (const auto& [name, was_consumed] : snap) {
        auto* sym = lookup_mut(name);

        if ((sym != nullptr) && sym->is_unique) {
            sym->is_consumed = was_consumed;
        }
    }
}

TypeScope::OwnershipSnapshot TypeScope::snapshot_local_ownership() const {
    OwnershipSnapshot snap;

    for (const auto& name : unique_names_) {
        if (const auto it = symbols_.find(name); it != symbols_.end() && it->second.is_unique) {
            snap.emplace_back(name, it->second.is_consumed);
        }
    }

    return snap;
}

void TypeScope::restore_local_ownership(const OwnershipSnapshot& snap) {
    for (const auto& [name, was_consumed] : snap) {
        if (const auto it = symbols_.find(name); it != symbols_.end() && it->second.is_unique) {
            it->second.is_consumed = was_consumed;
        }
    }
}

} // namespace luma
