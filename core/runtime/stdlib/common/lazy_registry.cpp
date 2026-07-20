#include "lazy_registry.hpp"

// ─── ModuleBuilder note ──────────────────────────────────────────────────────
// This file does not use ModuleBuilder and never will.  LazyRegistry is the
// on-demand module loading infrastructure — it holds and invokes the factory
// functions (register_*_ns calls) that register individual stdlib modules.
// It installs itself as the Environment's lazy-loader callback so that a
// module's functions are registered on first access.  There are no
// `define_native` calls here and no Luma-callable functions being defined.
// ─────────────────────────────────────────────────────────────────────────────

#include "runtime/interpreter/environment.hpp"

namespace luma {

void LazyRegistry::register_module(std::string module_prefix, ModuleFactory factory) {
    const std::scoped_lock lock{mutex_};
    modules_[std::move(module_prefix)].factory = std::move(factory);
}

void LazyRegistry::execute_factory(ModuleEntry& entry, const EnvPtr& env) {
    // Contract: a module factory must NOT synchronously trigger loading of
    // another lazy module. try_load() holds mutex_ across this call (so that
    // concurrent first-access from multiple threads is serialised and each
    // module loads exactly once), and mutex_ is a plain, non-recursive
    // std::mutex. A nested try_load() for a *different* module would therefore
    // deadlock, and a nested load of the *same* module is reported as a
    // circular dependency via the `loading` flag below. In practice the stdlib
    // factories (register_*_ns) only call define_native to register their own
    // functions and never load other modules, so this contract holds. If a
    // future factory genuinely needs another module, resolve it lazily from
    // within the registered native function (at call time, outside this lock),
    // not eagerly here.
    entry.loading = true;
    try {
        entry.factory(env);
        entry.loaded = true;
    } catch (...) {
        entry.loading = false;
        throw;
    }
    entry.loading = false;
}

bool LazyRegistry::try_load(const std::string& module_prefix, const EnvPtr& env) {
    const std::scoped_lock lock{mutex_};

    auto it = modules_.find(module_prefix);
    if (it == modules_.end()) {
        return false;
    }

    auto& entry = it->second;

    if (entry.loaded) {
        return true;
    }

    if (entry.loading) {
        throw ModuleLoadingError(module_prefix,
                                 "circular dependency detected while loading stdlib module '" +
                                     module_prefix + "'");
    }

    if (!entry.factory) {
        return false;
    }

    execute_factory(entry, env);

    return true;
}

bool LazyRegistry::has_module(const std::string& module_prefix) const {
    const std::scoped_lock lock{mutex_};
    return modules_.contains(module_prefix);
}

bool LazyRegistry::is_loaded(const std::string& module_prefix) const {
    const std::scoped_lock lock{mutex_};
    auto it = modules_.find(module_prefix);
    return it != modules_.end() && it->second.loaded;
}

void LazyRegistry::load_all(const EnvPtr& env) {
    const std::scoped_lock lock{mutex_};
    for (auto& [name, entry] : modules_) {
        if (!entry.loaded && entry.factory) {
            execute_factory(entry, env);
        }
    }
}

std::vector<std::string> LazyRegistry::module_names() const {
    const std::scoped_lock lock{mutex_};
    std::vector<std::string> names;
    names.reserve(modules_.size());
    for (const auto& [name, _] : modules_) {
        names.push_back(name);
    }
    return names;
}

void LazyRegistry::clear() {
    const std::scoped_lock lock{mutex_};
    modules_.clear();
}

std::size_t LazyRegistry::registered_count() const {
    const std::scoped_lock lock{mutex_};
    return modules_.size();
}

std::size_t LazyRegistry::loaded_count() const {
    const std::scoped_lock lock{mutex_};
    std::size_t count = 0;
    for (const auto& [_, entry] : modules_) {
        if (entry.loaded) {
            count++;
        }
    }
    return count;
}

void LazyRegistry::install(const EnvPtr& env) {
    // Safety: `self` remains valid because LazyRegistry is owned by the
    // Environment that also owns the native functions referencing it.
    // The raw pointer breaks a shared_ptr cycle (Environment → LazyRegistry
    // → native function → Environment).
    auto* self = this;
    env->set_lazy_loader([self](const std::string& module_prefix, const EnvPtr& e) -> bool {
        return self->try_load(module_prefix, e);
    });
}

} // namespace luma
