// ─────────────────────────────────────────────────────────────────────────────
// VMGlobalCache — Inline cache for global variable lookups.
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Cache Binding* pointers keyed by global variable name so
// that GetGlobal/SetGlobal opcodes avoid walking the Environment chain on
// every access.
//
// Contract: Environment::find_binding() returns pointers that remain valid
// across subsequent define_or_assign() calls. This is guaranteed because
// Environment uses std::unordered_map internally, which provides pointer
// stability for existing elements on insertion.
//
// Globals are never erased from the Environment, so cached Binding* pointers
// remain valid for the lifetime of the VM.
//
// Usage:
//   VMGlobalCache cache;
//   Binding* b = cache.lookup("x", *global_env);   // populates on miss
//   cache.insert_or_update("y", new_binding);       // after a new definition
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_RUNTIME_VM_VM_GLOBAL_CACHE_HPP
#define LUMA_RUNTIME_VM_VM_GLOBAL_CACHE_HPP

#include <string>
#include <string_view>

#include "common/string_hash.hpp"
#include "runtime/interpreter/environment.hpp"

namespace luma {

// Inline cache mapping global variable names to Binding pointers.
class VMGlobalCache {
public:
    // Look up a binding by name, populating the cache on a miss.
    // Returns nullptr when the variable does not exist in the environment.
    [[nodiscard]] Binding* lookup(std::string_view name, Environment& env) {
        // Use heterogeneous find() first (no allocation on cache hit).
        auto it = cache_.find(name);
        if (it != cache_.end()) {
            return it->second;
        }

        // Cache miss — allocate key string and populate from environment.
        auto [jt, _] = cache_.try_emplace(std::string{name}, env.find_binding(name));
        return jt->second;
    }

    // Insert or update a cache entry.  Used after a new global binding is
    // created (SetGlobal on a previously-undefined name) to keep the cache
    // consistent with the environment without an extra lookup.
    void insert_or_update(std::string_view name, Binding* binding) {
        auto it = cache_.find(name);
        if (it != cache_.end()) {
            it->second = binding;
        } else {
            cache_.try_emplace(std::string{name}, binding);
        }
    }

private:
    StringMap<Binding*> cache_;
};

} // namespace luma

#endif // LUMA_RUNTIME_VM_VM_GLOBAL_CACHE_HPP
