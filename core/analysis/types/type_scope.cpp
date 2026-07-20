// ─────────────────────────────────────────────────────────────────────────────
// TypeScope                                        (TypeScope implementation)
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <utility>
#include <vector>

#include "analysis/types/type_info.hpp"

namespace luma {

TypeScope::TypeScope(std::shared_ptr<TypeScope> parent) : parent_{std::move(parent)} {}

void TypeScope::define(std::string_view name, TypeInfo type, VariableModifiers modifiers,
                       SourceLocation loc) {
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

    for (const auto& [name, sym] : symbols_) {
        if (sym.is_unique) {
            snap.emplace_back(name, sym.is_consumed);
        }
    }

    if (parent_) {
        auto parent_snap = parent_->snapshot_ownership();

        snap.insert(snap.end(), parent_snap.begin(), parent_snap.end());
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

} // namespace luma
