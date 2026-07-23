#ifndef LUMA_STDLIB_STDLIB_REGISTRY_HPP
#define LUMA_STDLIB_STDLIB_REGISTRY_HPP

#include <string_view>
#include <unordered_map>

#include "analysis/types/stdlib_types.hpp"
#include "runtime/interpreter/environment.hpp"
#include "runtime/stdlib/collections/array_module.hpp"
#include "runtime/stdlib/collections/binarytree_module.hpp"
#include "runtime/stdlib/collections/dictionary_module.hpp"
#include "runtime/stdlib/collections/graph_module.hpp"
#include "runtime/stdlib/collections/hashset_module.hpp"
#include "runtime/stdlib/collections/keyvaluestore_module.hpp"
#include "runtime/stdlib/collections/linkedlist_module.hpp"
#include "runtime/stdlib/collections/queue_module.hpp"
#include "runtime/stdlib/collections/set_module.hpp"
#include "runtime/stdlib/collections/stack_module.hpp"
#include "runtime/stdlib/common/core_builtins_module.hpp"
#include "runtime/stdlib/common/lazy_registry.hpp"
#include "runtime/stdlib/concurrency/channel_module.hpp"
#include "runtime/stdlib/concurrency/task_module.hpp"
#include "runtime/stdlib/io/console_module.hpp"
#include "runtime/stdlib/io/filesystem_module.hpp"
#include "runtime/stdlib/io/graphicalui_module.hpp"
#include "runtime/stdlib/io/http_module.hpp"
#include "runtime/stdlib/io/socket_module.hpp"
#include "runtime/stdlib/io/terminal_module.hpp"
#include "runtime/stdlib/math/calculus_module.hpp"
#include "runtime/stdlib/math/decimal_module.hpp"
#include "runtime/stdlib/math/linearalgebra_module.hpp"
#include "runtime/stdlib/math/math_module.hpp"
#include "runtime/stdlib/system/compression_module.hpp"
#include "runtime/stdlib/system/datetime_module.hpp"
#include "runtime/stdlib/system/encoder_module.hpp"
#include "runtime/stdlib/system/hash_module.hpp"
#include "runtime/stdlib/system/log_module.hpp"
#include "runtime/stdlib/system/process_module.hpp"
#include "runtime/stdlib/system/random_module.hpp"
#include "runtime/stdlib/system/resource_module.hpp"
#include "runtime/stdlib/text/csv_module.hpp"
#include "runtime/stdlib/text/json_module.hpp"
#include "runtime/stdlib/text/regularexpression_module.hpp"
#include "runtime/stdlib/text/string_module.hpp"
#include "runtime/stdlib/text/xml_module.hpp"
#include "runtime/stdlib/types/converter_module.hpp"
#include "runtime/stdlib/types/optional_module.hpp"
#include "runtime/stdlib/types/reference_module.hpp"
#include "runtime/stdlib/types/result_module.hpp"
#include "stdlib/stdlib_catalog.hpp"

namespace luma {

// ─────────────────────────────────────────────────────────────────────────────
// Module Table — single source of truth for stdlib module registration
// ─────────────────────────────────────────────────────────────────────────────
// Each entry maps a module name to its registration function.  Modules
// that require OS access (disabled in sandbox mode) are tagged os_only.
// Modules whose behaviour adapts to the sandbox flag (Compression, Hash,
// Log) are tagged sandbox_aware and carry a sandbox_register_fn instead
// of the regular register_fn.
//
// ─── Method naming convention ────────────────────────────────────────────────
//   register_<module>_ns()  — Top-level registration function for a stdlib
//                              module.  Each module exposes exactly one of
//                              these, called by the module table below.
//
// Loading strategy (RT-22):
//   Two registration paths are provided — eager and lazy.
//
//   Eager (register_all):
//     Every module is registered into the Environment immediately at
//     startup.  Simple and predictable, but incurs a fixed cost
//     proportional to the number of modules, even if the program only
//     uses a few.  Used by the CLI interpreter and REPL.
//
//   Lazy (register_all_lazy):
//     Module factories are stored in a LazyRegistry and loaded on first
//     access via the Environment's lazy-loading callback.  Core builtins
//     (print, assert, type_of) and choice variants are always loaded
//     eagerly because they are needed before any user code runs.  Used
//     by the language server and other contexts where startup latency
//     matters more than steady-state performance.

namespace detail {

/// Module registry entry.
///
/// - `register_fn`:         Called when !sandbox_aware && (!os_only || !sandbox).
/// - `sandbox_register_fn`: Called when sandbox_aware (receives the sandbox flag).
/// - `os_only`:             If true, module is skipped entirely when sandbox=true.
/// - `sandbox_aware`:       If true, uses sandbox_register_fn instead of register_fn.
struct ModuleEntry {
    const char* name;
    void (*register_fn)(const EnvPtr&);
    void (*sandbox_register_fn)(const EnvPtr&, bool);
    bool os_only;
    bool sandbox_aware;
};

// Alphabetical order within each group (safe, sandbox-aware, then os-only).
inline constexpr ModuleEntry kModules[] = {
    // ── Always-available modules ──
    {"Array", register_array_ns, nullptr, false, false},
    {"BinaryTree", register_binarytree_ns, nullptr, false, false},
    {"Calculus", register_calculus_ns, nullptr, false, false},
    {"Channel", register_channel_ns, nullptr, false, false},
    {"Converter", register_converter_ns, nullptr, false, false},
    {"DateTime", register_datetime_ns, nullptr, false, false},
    {"Decimal", register_decimal_ns, nullptr, false, false},
    {"Dictionary", register_dictionary_ns, nullptr, false, false},
    {"Encoder", register_encoder_ns, nullptr, false, false},
    {"Graph", register_graph_ns, nullptr, false, false},
    // ^ Uses define_native directly instead of ModuleBuilder — see graphicalui_module.hpp.
    // Registered under the "GraphicalUi" name: this is the low-level web-view engine
    // that the built-in Solaris prelude (gui_prelude) reconciles onto.
    {"GraphicalUi", register_graphicalui_ns, nullptr, false, false},
    {"HashSet", register_hashset_ns, nullptr, false, false},
    {"Json", register_json_ns, nullptr, false, false},
    {"LinearAlgebra", register_linearalgebra_ns, nullptr, false, false},
    {"LinkedList", register_linkedlist_ns, nullptr, false, false},
    {"Math", register_math_ns, nullptr, false, false},
    {"Optional", register_optional_ns, nullptr, false, false},
    {"Queue", register_queue_ns, nullptr, false, false},
    {"Random", register_random_ns, nullptr, false, false},
    {"Reference", register_reference_ns, nullptr, false, false},
    {"RegularExpression", register_regularexpression_ns, nullptr, false, false},
    {"Resource", register_resource_ns, nullptr, false, false},
    {"Result", register_result_ns, nullptr, false, false},
    {"Set", register_set_ns, nullptr, false, false},
    {"Stack", register_stack_ns, nullptr, false, false},
    {"String", register_string_ns, nullptr, false, false},
    {"Task", register_task_ns, nullptr, false, false},
    {"Terminal", register_terminal_ns, nullptr, false, false},
    // ── Sandbox-aware modules (adapt behaviour based on sandbox flag) ──
    {"Compression", nullptr, register_compression_ns, false, true},
    {"Hash", nullptr, register_hash_ns, false, true},
    {"Log", nullptr, register_log_ns, false, true},
    // ── OS-only modules (disabled in sandbox mode) ──
    {"Console", register_console_ns, nullptr, true, false},
    {"Csv", register_csv_ns, nullptr, true, false},
    {"FileSystem", register_filesystem_ns, nullptr, true, false},
    {"Http", register_http_ns, nullptr, true, false},
    {"KeyValueStore", register_keyvaluestore_ns, nullptr, true, false},
    {"Process", register_process_ns, nullptr, true, false},
    {"Socket", register_socket_ns, nullptr, true, false},
    {"Xml", register_xml_ns, nullptr, true, false},
};

constexpr std::size_t kModuleCount = std::size(kModules);

// O(1) module lookup by name.  Returns a pointer to the matching
// ModuleEntry, or nullptr if the name is not a known stdlib module.
[[nodiscard]] inline const ModuleEntry* find_module(std::string_view name) {
    // Build a static hash map on first call for O(1) lookups.
    // The map is small and never changes, so a static local is safe.
    static const auto module_map = [] {
        std::unordered_map<std::string_view, const ModuleEntry*> map;
        for (const auto& mod : kModules) {
            map[mod.name] = &mod;
        }
        assert(map.size() == kModuleCount && "module registration failed: duplicate name");
        return map;
    }();

    const auto it = module_map.find(name);
    return it != module_map.end() ? it->second : nullptr;
}

// Check if a module name refers to an OS-only module.
[[nodiscard]] inline bool is_os_only_module(std::string_view name) {
    const auto* entry = find_module(name);
    return entry != nullptr && entry->os_only;
}

// Verify that a module name is a recognized stdlib module.
[[nodiscard]] inline bool is_stdlib_module(std::string_view name) {
    return find_module(name) != nullptr;
}

// Register choice variants and sandbox-blocked prefixes.
// Shared by both eager and lazy registration paths.
inline void register_stdlib_postamble(const EnvPtr& env, bool sandbox) {
    // Register stdlib-provided choice variants so they are accessible
    // at runtime (e.g. Log.Level.Info).
    for (const auto& [qualified, ch] : stdlib_choice_types()) {
        for (const auto& variant : ch->variants) {
            const auto full = qualified + "." + variant.name;

            auto cv = std::make_shared<ChoiceValue>();
            cv->type_name = ch->name;
            cv->variant = variant.name;

            env->define(full, Value{std::move(cv)}, false);
        }
    }

    // Tell the environment which module prefixes are blocked so that
    // look-ups produce a clear "not available in sandbox mode" message
    // instead of the generic "undefined variable" error.  The blocked
    // set is derived from the catalog's capability tags — single source
    // of truth for sandbox restrictions.
    if (sandbox) {
        StringSet blocked;
        for (const auto& mod : stdlib::sandbox_blocked_modules()) {
            blocked.insert(mod);
        }
        env->set_sandbox_blocked(std::move(blocked));
    }
}

// Iterate over active modules, calling the appropriate visitor for each.
// Encapsulates the sandbox_aware / os_only filtering logic shared by
// both eager and lazy registration paths.
template <typename SandboxAwareFn, typename NormalFn>
void for_each_active_module(bool sandbox, SandboxAwareFn sa_fn, NormalFn normal_fn) {
    for (const auto& mod : kModules) {
        if (mod.sandbox_aware) {
            sa_fn(mod);
        } else if (!mod.os_only || !sandbox) {
            normal_fn(mod);
        }
    }
}

} // namespace detail

inline void register_all(const EnvPtr& env, bool sandbox = false) {
    register_core(env);

    detail::for_each_active_module(
        sandbox, [&](const detail::ModuleEntry& mod) { mod.sandbox_register_fn(env, sandbox); },
        [&](const detail::ModuleEntry& mod) { mod.register_fn(env); });

    detail::register_stdlib_postamble(env, sandbox);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lazy Registration
// ─────────────────────────────────────────────────────────────────────────────
// Registers module factories into a LazyRegistry instead of loading all
// modules eagerly.  Modules are loaded on first access via Environment's
// lazy-loading callback.  Core builtins (print, assert, type_of) and
// choice variants are always loaded eagerly.

// Populate a LazyRegistry with factories for all stdlib modules.
// The registry should be installed on the Environment via install().
inline void register_all_lazy(LazyRegistry& registry, const EnvPtr& env, bool sandbox = false) {
    // Core builtins are always needed.
    register_core(env);

    detail::for_each_active_module(
        sandbox,
        [&](const detail::ModuleEntry& mod) {
            registry.register_module(mod.name, [fn = mod.sandbox_register_fn,
                                                sandbox](const EnvPtr& e) { fn(e, sandbox); });
        },
        [&](const detail::ModuleEntry& mod) {
            registry.register_module(mod.name, [fn = mod.register_fn](const EnvPtr& e) { fn(e); });
        });

    detail::register_stdlib_postamble(env, sandbox);

    // Install the registry as the lazy loader for the environment.
    registry.install(env);
}

} // namespace luma

#endif // LUMA_STDLIB_STDLIB_REGISTRY_HPP
