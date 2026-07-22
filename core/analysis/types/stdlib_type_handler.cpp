#include "analysis/types/stdlib_type_handler.hpp"

#include <string>
#include <vector>

#include "stdlib/stdlib_catalog.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// Known stdlib namespaces (derived from the stdlib catalog)
// ═══════════════════════════════════════════════════════════

namespace {

// Lazily-initialised on first use so the set is built in a catchable context
// rather than during unsequenced static initialisation.
[[nodiscard]] const StringSet& stdlib_namespaces() {
    static const StringSet set = [] {
        StringSet namespace_names;
        for (const auto& [qualified_name, _] : stdlib::catalog()) {
            if (const auto split = split_module(qualified_name)) {
                namespace_names.insert(std::string{split->first});
            }
        }
        return namespace_names;
    }();
    return set;
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Public interface
// ═══════════════════════════════════════════════════════════

void StdlibTypeHandler::initialize() {
    functions_.clear();

    init_signatures();
    init_arities();
    init_param_types();
}

bool StdlibTypeHandler::is_stdlib_namespace(std::string_view name) const {
    return stdlib_namespaces().contains(name);
}

const StringSet& StdlibTypeHandler::namespaces() const {
    return stdlib_namespaces();
}

bool StdlibTypeHandler::has_function(std::string_view name) const {
    return functions_.contains(name);
}

const TypeInfo* StdlibTypeHandler::get_return_type(std::string_view name) const {
    const auto it = functions_.find(name);
    if (it != functions_.end()) {
        return &it->second.return_type;
    }
    return nullptr;
}

const StdlibTypeHandler::ArityInfo* StdlibTypeHandler::get_arity(std::string_view name) const {
    const auto it = functions_.find(name);
    if (it == functions_.end()) {
        return nullptr;
    }
    const auto& arity = it->second.arity;
    if (arity.has_value()) {
        return &*arity;
    }
    return nullptr;
}

const std::vector<TypeInfo>* StdlibTypeHandler::get_param_types(std::string_view name) const {
    const auto it = functions_.find(name);
    if (it != functions_.end() && !it->second.param_types.empty()) {
        return &it->second.param_types;
    }
    return nullptr;
}

StringMap<TypeInfo> StdlibTypeHandler::build_signature_map() const {
    StringMap<TypeInfo> result;
    result.reserve(functions_.size());
    for (const auto& [name, info] : functions_) {
        result[name] = info.return_type;
    }
    return result;
}

} // namespace luma
