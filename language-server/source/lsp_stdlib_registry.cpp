#include "lsp_stdlib_registry.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/types/type_checker.hpp"
#include "stdlib/stdlib_catalog.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::lsp {

namespace {

using ModuleMap = std::unordered_map<std::string, std::vector<StdlibFunction>>;
using FunctionIndex = std::unordered_map<std::string, const StdlibFunction*>;

// Build per-module function lists from stdlib type signatures, reading the
// human-readable parameter list for each function from the shared catalog.
void populate_modules(const StringMap<TypeInfo>& signatures, const stdlib::CatalogMap& cat,
                      const StringSet& constants, ModuleMap& modules) {
    for (const auto& [qualified_name, type_info] : signatures) {
        // Split "Module.function" into module and function name.
        const auto split = split_module(qualified_name);
        if (!split) {
            continue;
        }

        const std::string module_name{split->first};
        const std::string func_name{split->second};

        std::string params_sig;
        if (const auto it = cat.find(qualified_name); it != cat.end()) {
            params_sig = it->second.params;
        }

        modules[module_name].push_back(StdlibFunction{
            .name = func_name,
            .return_type = type_info.to_string(),
            .params_signature = std::move(params_sig),
            .is_constant = constants.contains(qualified_name),
        });
    }
}

// Sort each module's functions alphabetically and build the O(1) lookup index.
void sort_and_index_modules(ModuleMap& modules, FunctionIndex& function_index) {
    for (auto& [module, funcs] : modules) {
        std::ranges::sort(funcs, [](const StdlibFunction& a, const StdlibFunction& b) {
            return a.name < b.name;
        });

        for (const auto& func : funcs) {
            function_index[module + "." + func.name] = &func;
        }
    }
}

// Build the sorted list of module names used for keyword completions.
void build_sorted_module_names(const ModuleMap& modules, std::vector<std::string>& module_names) {
    module_names.reserve(modules.size());

    for (const auto& [module, _] : modules) {
        module_names.push_back(module);
    }

    std::ranges::sort(module_names);
}

// Warn about catalog entries with no matching stdlib signature, then log a
// one-line summary of what was loaded.
void log_registry_summary(const StringMap<TypeInfo>& signatures, const stdlib::CatalogMap& cat,
                          const StringSet& constants, std::size_t module_count,
                          const StdlibRegistry::LogCallback& log) {
    if (!log) {
        return;
    }

    std::vector<std::string> missing_entries;
    for (const auto& [qualified_name, _] : signatures) {
        if (!cat.contains(qualified_name) && !constants.contains(qualified_name)) {
            missing_entries.push_back(qualified_name);
        }
    }

    if (!missing_entries.empty()) {
        log(std::format("[luma-lsp] WARNING: {} stdlib functions have no catalog entry:",
                        missing_entries.size()));
        for (const auto& name : missing_entries) {
            log(std::format("  - {}", name));
        }
    }

    log(std::format("[luma-lsp] Loaded {} stdlib modules with {} total functions", module_count,
                    signatures.size()));
}

} // namespace

void StdlibRegistry::init(const LogCallback& log) {
    std::call_once(init_flag_, [this, &log] {
        // Create a TypeChecker and run it on an empty program to populate
        // the stdlib signature registry.
        TypeChecker checker;
        const Program empty_program;

        (void)checker.check(empty_program, false);

        // Read signatures, constants, and parameter lists from the shared catalog.
        const auto& signatures = checker.stdlib_signatures();
        const auto& cat = stdlib::catalog();
        const auto& constants = stdlib::constants();

        populate_modules(signatures, cat, constants, modules_);
        sort_and_index_modules(modules_, function_index_);
        build_sorted_module_names(modules_, module_names_);
        log_registry_summary(signatures, cat, constants, modules_.size(), log);
    });
}

} // namespace luma::lsp
