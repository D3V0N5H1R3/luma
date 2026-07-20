#ifndef LUMA_RUNTIME_STDLIB_LAZY_REGISTRY_HPP
#define LUMA_RUNTIME_STDLIB_LAZY_REGISTRY_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Lazy Per-Module Standard Library Loading
// ─────────────────────────────────────────────────────────────────────────────
// Delays initialisation of stdlib modules until first use.  Each module
// registers a factory function (its register_*_ns() call) that is invoked
// on demand when the Environment encounters a lookup for a function in
// that module's namespace.
//
// Benefits:
//   - Startup time reduced from O(N) modules to O(1) + per-module cost.
//   - Memory usage reduced for programs that only use a few modules.
//   - Allows large stdlib to grow without penalising simple programs.
//
// Integration:
//   The lazy registry installs itself as the Environment's lazy-loading
//   callback via `Environment::set_lazy_loader()`.  When a lookup for
//   e.g. "Queue.map" fails, the Environment extracts the "Queue" prefix
//   and calls back into the registry, which triggers `register_queue_ns()`.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/string_hash.hpp"
#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

// Thrown when a stdlib module cannot be loaded — e.g. circular dependency
// between modules during lazy initialisation.  Carries the module name so
// that callers can produce targeted diagnostics.
class ModuleLoadingError : public std::runtime_error {
public:
    explicit ModuleLoadingError(const std::string& module_name, const std::string& reason)
        : std::runtime_error{reason}, module_name_{module_name} {}

    [[nodiscard]] const std::string& module_name() const noexcept {
        return module_name_;
    }

private:
    std::string module_name_;
};

// Factory type: registers a module's functions into an environment.
using ModuleFactory = std::function<void(const EnvPtr&)>;

// ─── Registry ───
//
// ── Thread-safety ──
//
// LazyRegistry is internally synchronized with a std::mutex.  Individual
// operations (register_module, try_load, has_module, etc.) are thread-safe.
// However, the typical usage pattern is single-threaded: all module
// registration happens during interpreter startup, and lazy loading is
// triggered from the main VM thread during compilation/execution.
//
// The mutex primarily guards against concurrent access when multiple
// interpreter instances share a registry, or during load_all() calls
// from REPL introspection.  If concurrent module loading with high
// read contention is added in the future, consider upgrading to
// std::shared_mutex (read-heavy workload).

class LazyRegistry {
public:
    LazyRegistry() = default;

    // Register a module factory.  The factory is NOT called until the
    // module is first accessed at runtime.
    void register_module(std::string module_prefix, ModuleFactory factory);

    // Try to load a module by prefix.  Returns true if the module was
    // loaded (or was already loaded).  Returns false if no factory is
    // registered for the prefix.  Thread-safe.
    bool try_load(const std::string& module_prefix, const EnvPtr& env);

    // Check if a module is registered (but possibly not yet loaded).
    [[nodiscard]] bool has_module(const std::string& module_prefix) const;

    // Check if a module has been loaded (factory has been called).
    [[nodiscard]] bool is_loaded(const std::string& module_prefix) const;

    // Force-load all modules (e.g., for REPL introspection or testing).
    void load_all(const EnvPtr& env);

    // Get list of registered module prefixes.
    [[nodiscard]] std::vector<std::string> module_names() const;

    // Unregister all modules (for testing).
    void clear();

    // Statistics.
    [[nodiscard]] std::size_t registered_count() const;
    [[nodiscard]] std::size_t loaded_count() const;

    // Install this registry as the lazy loader for the given environment.
    void install(const EnvPtr& env);

private:
    struct ModuleEntry {
        ModuleFactory factory;
        bool loaded{false};
        bool loading{false}; // Guard against circular loading.
    };

    // Run a module's factory with loading-flag bookkeeping.
    void execute_factory(ModuleEntry& entry, const EnvPtr& env);

    mutable std::mutex mutex_;
    StringMap<ModuleEntry> modules_;
};

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_LAZY_REGISTRY_HPP
