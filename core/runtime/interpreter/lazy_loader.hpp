#ifndef LUMA_INTERPRETER_LAZY_LOADER_HPP
#define LUMA_INTERPRETER_LAZY_LOADER_HPP

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "runtime/interpreter/value_fwd.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

// On-demand module-registration policy.
//
// Single responsibility: hold the lazy-loading callback and, given a qualified
// "Module.name", extract the module prefix and invoke the callback.  Decoupled
// from scope storage so the loading policy can evolve independently of the
// Environment's binding machinery.
//
// Extracted from Environment per the TODO(refactor) note.
class LazyLoader {
public:
    // Callback invoked with a module prefix (e.g. "Queue") to register that
    // module's functions on demand.  Returns true if the module was loaded.
    using ModuleFactory = std::function<bool(const std::string& module_prefix, const EnvPtr& env)>;

    // Install the lazy-loading callback.
    void set_loader(ModuleFactory loader) {
        loader_ = std::move(loader);
    }

    // If `name` looks like "Module.function", extract the module prefix and
    // invoke the loader to register it on `env`.  Returns true if a module was
    // loaded (the caller should then retry the lookup).
    [[nodiscard]] bool try_load(std::string_view name, const EnvPtr& env) const {
        if (!loader_) {
            return false;
        }

        const auto split = split_module(name);
        if (!split) {
            return false;
        }

        return loader_(std::string{split->first}, env);
    }

private:
    ModuleFactory loader_;
};

} // namespace luma

#endif // LUMA_INTERPRETER_LAZY_LOADER_HPP
