#include "lsp_scope_stack.hpp"

#include <unordered_set>

#include "lsp_string_utils.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// ScopeLevel — method implementations
// ═══════════════════════════════════════════════════════════

std::optional<ScopeSymbol> ScopeLevel::find(const std::string& name) const {
    switch (kind_) {
        case ScopeKind::Block:
            return find_in_block(name);
        case ScopeKind::Function:
            return find_in_function(name);
        case ScopeKind::Module:
            return find_in_module(name);
    }
    return std::nullopt;
}

std::vector<ScopeSymbol> ScopeLevel::collect_all() const {
    switch (kind_) {
        case ScopeKind::Block:
            return collect_block();
        case ScopeKind::Function:
            return collect_function();
        case ScopeKind::Module:
            return collect_module();
    }
    return {};
}

// ─── Block scope ───

std::optional<ScopeSymbol> ScopeLevel::find_in_block(const std::string& name) const {
    if (!enclosing_function_.has_value()) {
        return std::nullopt;
    }
    auto fn_it = result_.semantic.locals.scoped_locals.find(*enclosing_function_);
    if (fn_it == result_.semantic.locals.scoped_locals.end()) {
        return std::nullopt;
    }
    auto sl_it = fn_it->second.find(name);
    if (sl_it == fn_it->second.end()) {
        return std::nullopt;
    }
    for (const auto& entry : sl_it->second) {
        if (line_ >= entry.scope_start_line && line_ <= entry.scope_end_line) {
            const bool is_mut = is_mutable_symbol(result_, name, enclosing_function_);
            return ScopeSymbol{.name = name,
                               .type_name = entry.type_string,
                               .origin = ScopeKind::Block,
                               .is_mutable = is_mut,
                               .is_parameter = false};
        }
    }
    return std::nullopt;
}

std::vector<ScopeSymbol> ScopeLevel::collect_block() const {
    std::vector<ScopeSymbol> symbols;
    if (!enclosing_function_.has_value()) {
        return symbols;
    }
    auto fn_it = result_.semantic.locals.scoped_locals.find(*enclosing_function_);
    if (fn_it == result_.semantic.locals.scoped_locals.end()) {
        return symbols;
    }
    for (const auto& [var_name, entries] : fn_it->second) {
        for (const auto& entry : entries) {
            if (line_ >= entry.scope_start_line && line_ <= entry.scope_end_line) {
                const bool is_mut = is_mutable_symbol(result_, var_name, enclosing_function_);
                symbols.push_back(ScopeSymbol{.name = var_name,
                                              .type_name = entry.type_string,
                                              .origin = ScopeKind::Block,
                                              .is_mutable = is_mut,
                                              .is_parameter = false});
                break; // only one entry per var can be in scope at a time
            }
        }
    }
    return symbols;
}

// ─── Function scope ───

std::optional<ScopeSymbol> ScopeLevel::find_in_function(const std::string& name) const {
    if (!enclosing_function_.has_value()) {
        return std::nullopt;
    }
    // Check parameters first.
    auto fn_it = result_.semantic.symbols.user_functions.find(*enclosing_function_);
    if (fn_it != result_.semantic.symbols.user_functions.end()) {
        for (const auto& p : fn_it->second.parameters) {
            if (p.name == name) {
                return ScopeSymbol{.name = name,
                                   .type_name = p.type_string,
                                   .origin = ScopeKind::Function,
                                   .is_mutable = false,
                                   .is_parameter = true};
            }
        }
    }
    // Check function-level locals.
    auto fl_it = result_.semantic.locals.function_locals.find(*enclosing_function_);
    if (fl_it != result_.semantic.locals.function_locals.end()) {
        auto vl_it = fl_it->second.find(name);
        if (vl_it != fl_it->second.end()) {
            const bool is_mut = is_mutable_symbol(result_, name, enclosing_function_);
            return ScopeSymbol{.name = name,
                               .type_name = vl_it->second,
                               .origin = ScopeKind::Function,
                               .is_mutable = is_mut,
                               .is_parameter = false};
        }
    }
    return std::nullopt;
}

std::vector<ScopeSymbol> ScopeLevel::collect_function() const {
    std::vector<ScopeSymbol> symbols;
    if (!enclosing_function_.has_value()) {
        return symbols;
    }
    // Parameters.
    auto fn_it = result_.semantic.symbols.user_functions.find(*enclosing_function_);
    if (fn_it != result_.semantic.symbols.user_functions.end()) {
        for (const auto& p : fn_it->second.parameters) {
            symbols.push_back(ScopeSymbol{.name = p.name,
                                          .type_name = p.type_string,
                                          .origin = ScopeKind::Function,
                                          .is_mutable = false,
                                          .is_parameter = true});
        }
    }
    // Function-level locals.
    auto fl_it = result_.semantic.locals.function_locals.find(*enclosing_function_);
    if (fl_it != result_.semantic.locals.function_locals.end()) {
        for (const auto& [name, type_str] : fl_it->second) {
            const bool is_mut = is_mutable_symbol(result_, name, enclosing_function_);
            symbols.push_back(ScopeSymbol{.name = name,
                                          .type_name = type_str,
                                          .origin = ScopeKind::Function,
                                          .is_mutable = is_mut,
                                          .is_parameter = false});
        }
    }
    return symbols;
}

// ─── Module scope ───

std::optional<ScopeSymbol> ScopeLevel::find_in_module(const std::string& name) const {
    // Top-level definitions (constants, variables).
    if (auto def = result_.find_definition(name)) {
        return ScopeSymbol{.name = name,
                           .type_name = def->type_string,
                           .origin = ScopeKind::Module,
                           .is_mutable = def->is_mutable,
                           .is_parameter = false};
    }
    // User functions (as callable symbols).
    auto fn_it = result_.semantic.symbols.user_functions.find(name);
    if (fn_it != result_.semantic.symbols.user_functions.end()) {
        return ScopeSymbol{.name = name,
                           .type_name = fn_it->second.return_type,
                           .origin = ScopeKind::Module,
                           .is_mutable = false,
                           .is_parameter = false};
    }
    // Document-level local variable types (globals outside any function).
    auto lv_it = result_.semantic.locals.local_variable_types.find(name);
    if (lv_it != result_.semantic.locals.local_variable_types.end()) {
        const bool is_mut = is_mutable_symbol(result_, name);
        return ScopeSymbol{.name = name,
                           .type_name = lv_it->second,
                           .origin = ScopeKind::Module,
                           .is_mutable = is_mut,
                           .is_parameter = false};
    }
    return std::nullopt;
}

std::vector<ScopeSymbol> ScopeLevel::collect_module() const {
    std::vector<ScopeSymbol> symbols;
    // Top-level definitions.
    symbols.reserve(result_.semantic.symbols.definitions.size());
    for (const auto& [name, def] : result_.semantic.symbols.definitions) {
        symbols.push_back(ScopeSymbol{.name = name,
                                      .type_name = def.type_string,
                                      .origin = ScopeKind::Module,
                                      .is_mutable = def.is_mutable,
                                      .is_parameter = false});
    }
    // User functions (top-level only, skip namespace-qualified).
    for (const auto& [name, info] : result_.semantic.symbols.user_functions) {
        if (!util::is_qualified_name(name)) {
            symbols.push_back(ScopeSymbol{.name = name,
                                          .type_name = info.return_type,
                                          .origin = ScopeKind::Module,
                                          .is_mutable = false,
                                          .is_parameter = false});
        }
    }
    // Document-level locals not already in definitions.
    for (const auto& [name, type_str] : result_.semantic.locals.local_variable_types) {
        if (!result_.semantic.symbols.definitions.contains(name) &&
            !result_.semantic.symbols.user_functions.contains(name)) {
            const bool is_mut = is_mutable_symbol(result_, name);
            symbols.push_back(ScopeSymbol{.name = name,
                                          .type_name = type_str,
                                          .origin = ScopeKind::Module,
                                          .is_mutable = is_mut,
                                          .is_parameter = false});
        }
    }
    return symbols;
}

// ═══════════════════════════════════════════════════════════
// ScopeStack — method implementations
// ═══════════════════════════════════════════════════════════

ScopeStack::ScopeStack(const AnalysisResult& result, int line_1based)
    : result_{result},
      line_{line_1based},
      enclosing_function_{find_enclosing_function(result, line_1based)} {
    build_levels();
}

std::optional<ScopeSymbol> ScopeStack::find_symbol(const std::string& name) const {
    for (const auto& level : levels_) {
        auto sym = level.find(name);
        if (sym.has_value()) {
            return sym;
        }
    }
    return std::nullopt;
}

std::vector<ScopeSymbol> ScopeStack::collect_visible_symbols() const {
    // Complexity is O(total_symbols) across all levels; the seen set prevents
    // duplicates from shadowed names without introducing quadratic behaviour.
    std::vector<ScopeSymbol> result;
    result.reserve(64);
    std::unordered_set<std::string> seen;
    seen.reserve(64);
    for (const auto& level : levels_) {
        for (auto& sym : level.collect_all()) {
            if (!seen.contains(sym.name)) {
                seen.insert(sym.name);
                result.push_back(std::move(sym));
            }
        }
    }
    return result;
}

void ScopeStack::build_levels() {
    if (enclosing_function_.has_value()) {
        // Inside a function: block → function → module.
        levels_.emplace_back(ScopeKind::Block, result_, line_, enclosing_function_);
        levels_.emplace_back(ScopeKind::Function, result_, line_, enclosing_function_);
    }
    // Module scope is always present.
    levels_.emplace_back(ScopeKind::Module, result_, line_, enclosing_function_);
}

} // namespace luma::lsp
