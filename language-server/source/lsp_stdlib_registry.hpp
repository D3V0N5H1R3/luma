#ifndef LUMA_LSP_STDLIB_REGISTRY_HPP
#define LUMA_LSP_STDLIB_REGISTRY_HPP

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "common/string_hash.hpp"
#include "lsp_optional_ref.hpp"

namespace luma::lsp {

// Information about a single stdlib function or constant.
struct StdlibFunction {
    std::string name;             // e.g. "floor"
    std::string return_type;      // e.g. "result<integer>"
    std::string params_signature; // e.g. "(value: number)" or empty
    bool is_constant{false};      // true for Math.pi etc.
};

// ═══════════════════════════════════════════════════════════
// StdlibRegistry — stdlib metadata for hover, completion,
// and signature help.
//
// Initialised once on first access.  All accessors return
// const references and are safe to call from any thread
// after init() has returned.
// ═══════════════════════════════════════════════════════════

class StdlibRegistry {
public:
    // Optional callback for diagnostic/log messages during init.
    using LogCallback = std::function<void(const std::string&)>;

    // Populate the registry from TypeChecker and the shared catalog.
    // Thread-safe: uses std::call_once internally.
    // If no log callback is provided, warnings are silently discarded.
    void init(const LogCallback& log = {});

    // Module name → sorted list of functions in that module.
    [[nodiscard]] const StringMap<std::vector<StdlibFunction>>& modules() const {
        return modules_;
    }

    // O(1) lookup: "Module.function" → optional reference.
    [[nodiscard]] optional_ref<const StdlibFunction>
    find_function(const std::string& qualified_name) const {
        auto it = function_index_.find(qualified_name);
        if (it != function_index_.end()) {
            return *it->second;
        }
        return {};
    }

    // Check if a module name exists.
    [[nodiscard]] bool has_module(const std::string& name) const {
        return modules_.contains(name);
    }

    // Find a module's function list.
    [[nodiscard]] optional_ref<const std::vector<StdlibFunction>>
    find_module(const std::string& name) const {
        auto it = modules_.find(name);
        if (it != modules_.end()) {
            return it->second;
        }
        return {};
    }

    // Sorted list of all module names.
    [[nodiscard]] const std::vector<std::string>& module_names() const {
        return module_names_;
    }

private:
    std::once_flag init_flag_;
    StringMap<std::vector<StdlibFunction>> modules_;
    StringMap<const StdlibFunction*> function_index_;
    std::vector<std::string> module_names_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_STDLIB_REGISTRY_HPP
