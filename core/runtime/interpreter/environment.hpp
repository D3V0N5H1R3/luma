#ifndef LUMA_INTERPRETER_ENVIRONMENT_HPP
#define LUMA_INTERPRETER_ENVIRONMENT_HPP

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "analysis/source/source_location.hpp"
#include "common/string_hash.hpp"
#include "runtime/interpreter/sandbox_policy.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

struct Binding {
    Value value{};
    bool is_mutable{false};
    bool is_from_namespace{false}; // true when registered as a namespace bare name
    // Bumped by the VM on every write to the binding (SetGlobal, the in-place
    // write UniqueGlobal announces); `k_unversioned` after a write that
    // bypassed the VM.  Lets a task global snapshot reuse the copy of a
    // binding unchanged since the previous snapshot (see BindingCopyCache).
    std::uint64_t write_version{0};

    static constexpr std::uint64_t k_unversioned{~std::uint64_t{0}};
};

// Deep copies of global bindings kept across successive task snapshots
// (Environment::copy_bindings).  A binding whose write_version still matches
// its cached copy is shared by the new snapshot instead of being deep-copied
// again, so re-snapshotting after a global write costs one deep copy per
// binding actually written, not one per global.  Sharing a copy between
// snapshots is safe for the same reason sharing one snapshot between sibling
// tasks is: tasks never write their globals, and copy-on-write clones a value
// before any in-place mutation while another holder still references it.
struct BindingCopyCache {
    struct Entry {
        std::uint64_t write_version{0};
        Value copy;
    };

    StringMap<Entry> entries;
};

// Represents a lexical scope in the interpreter's runtime.  Each
// Environment holds a map of variable bindings (name → value,
// mutability) and an optional pointer to an enclosing parent scope,
// forming a chain that mirrors the nesting of blocks, functions, and
// closures in a Luma program.  Variable lookup walks up the chain
// until a match is found, giving inner scopes access to outer
// definitions while allowing shadowing at any level.
//
// ─── Performance note ───────────────────────────────────────────
// The parent-chain walk in get() / set() / has() is O(depth), but
// this is NOT a hot-path concern in compiled (VM) execution:
//
//   • Local variables are resolved at compile time to stack-slot
//     indices and accessed via GetLocal / SetLocal opcodes — O(1)
//     direct stack indexing, no Environment involved.
//
//   • Captured variables (closures) are resolved to upvalue slots
//     and stored directly on FunctionValue — accessed via
//     GetUpvalue / SetUpvalue opcodes, also O(1).
//
//   • Global variables go through GetGlobal / SetGlobal which use
//     the VM's inline global_cache_ (StringMap<Binding*>).  The
//     Environment chain is walked only on the first access to each
//     unique global name; subsequent accesses are O(1) cache hits.
//
// The chain-walking methods are therefore exercised primarily during
// REPL evaluation, stdlib registration, and sandbox checks — none of
// which are tight-loop hot paths.  Adding a lookup cache here would
// duplicate the VM's global_cache_ without measurable benefit.
//
// deep_copy() is O(n) across all ancestor scopes with a visited-set
// guard against cycles.  It is used only for concurrency isolation
// (task spawning, channel sends) where correctness requires a full
// independent copy.  The visited set is an unordered_set<const
// Environment*> with reserve(16) to avoid rehashing for the typical
// case of shallow scope chains — acceptable because deep_copy() is
// rare and never called in inner loops.
// ────────────────────────────────────────────────────────────────
[[nodiscard]] inline RuntimeError make_undefined_variable_error(std::string_view name,
                                                                const SourceLocation& loc) {
    return RuntimeError{
        std::format("undefined variable '{}'", name), loc,
        "check the spelling or ensure the variable is declared in an accessible scope"};
}

class Environment {
public:
    // ── Responsibilities ────────────────────────────────────────────────
    // Environment combines several related concerns that are small enough
    // to share a single class today but could be extracted if the class
    // grows further:
    //
    //   1. Scope chain management — maintains an optional parent_ pointer
    //      and walks the chain for lookups, assignments, and prefix scans.
    //
    //   2. Value binding — define(), get(), set(), define_or_assign(),
    //      has(), has_local(), find_binding(), and is_binding_mutable()
    //      map variable names to Binding{value, is_mutable} entries.
    //
    //   3. Sandbox enforcement — set_sandbox_blocked() records a set of
    //      module prefixes that raise RuntimeError on access (--box mode).
    //      verify_sandbox_access() walks up to the root environment to check.
    //
    //   4. Deep copy for task isolation — deep_copy() clones the entire
    //      ancestor chain so that spawned tasks and channel sends receive
    //      fully independent value snapshots.
    //
    // Environment owns only scope chaining and value bindings; it delegates
    // sandbox enforcement to SandboxPolicy (sandbox_policy.hpp).  Each
    // Environment holds a value-typed policy, but only the root's is
    // populated; the scope-chain walk locates it from any child scope.
    // ────────────────────────────────────────────────────────────────────
    explicit Environment(EnvPtr parent = nullptr) : parent_{std::move(parent)} {}

    [[nodiscard]] static EnvPtr create(EnvPtr parent = nullptr) {
        return std::make_shared<Environment>(std::move(parent));
    }

    // Mark a set of module prefixes as blocked by sandbox mode.
    // Only call this on the global (root) environment.
    void set_sandbox_blocked(StringSet prefixes, StringSet functions = {}) {
        assert(parent_ == nullptr &&
               "set_sandbox_blocked() must only be called on root environment");
        sandbox_policy_.set_blocked(std::move(prefixes), std::move(functions));
    }

    // Why `name` is blocked by sandbox mode anywhere up the scope chain (the
    // blocked sets live on the root environment), or nullopt if it is not.
    [[nodiscard]] std::optional<std::string> sandbox_denial_hint(std::string_view name) const {
        if (auto hint = sandbox_policy_.denial_hint(name)) {
            return hint;
        }

        return parent_ ? parent_->sandbox_denial_hint(name) : std::nullopt;
    }

    // Deep-copy this environment and the entire ancestor chain.
    //
    // ── What is copied ──
    // Only the single scope chain rooted at `this` is copied — the clone
    // mirrors the same parent-child nesting, but every Binding in every
    // scope is independently copied via Value::deep_copy().  Compound
    // value types (arrays, dicts, records, etc.) are deep-copied so the
    // clone shares no mutable heap state with the original.
    //
    // ── Algorithm ──
    // Walks the parent chain recursively (deep_copy_impl), building new
    // Environment nodes in bottom-up order (parent first, child after).
    // A visited set (unordered_set<const Environment*>) detects cycles in
    // pathological scope chains and converts them to a parent-less root,
    // preventing infinite recursion.
    //
    // ── Complexity ──
    // O(n · m) where n is the total number of Binding entries across all
    // ancestor scopes and m is the average deep-copy cost per Value.
    // The visited set is reserved for 16 entries to avoid rehashing for
    // typical shallow scope chains.
    //
    // ── Stack depth risk ──
    // Recursion depth equals the length of the parent chain.  For normal
    // Luma programs this is small (< 50 frames for the deepest realistic
    // nesting), but pathological REPL sessions that build extremely deep
    // scope chains could overflow the C++ call stack.  This is considered
    // an acceptable trade-off: deep_copy() is called only at task-spawn
    // and channel-send boundaries, never in inner loops.
    [[nodiscard]] EnvPtr deep_copy() const {
        std::unordered_set<const Environment*> visited;
        visited.reserve(16);

        return deep_copy_impl(visited);
    }

    // Copy only this scope's bindings whose mutability equals
    // `mutable_bindings` into a new environment chained to `parent`.  The
    // sandbox policy is carried over.  Used to build a spawned task's global
    // snapshot in two layers: the immutable bindings (the stdlib — large but
    // never reassigned) form a shared base layer, and the mutable user globals
    // a top layer rebuilt after a global write.  Each value is deep-copied
    // only when `cache` holds no copy of it at its current write_version.
    [[nodiscard]] EnvPtr copy_bindings(EnvPtr parent, bool mutable_bindings,
                                       BindingCopyCache& cache) const {
        auto copy = std::make_shared<Environment>(std::move(parent));
        copy->sandbox_policy_ = sandbox_policy_;

        for (const auto& [name, binding] : bindings_) {
            if (binding.is_mutable != mutable_bindings) {
                continue;
            }

            auto [it, inserted] = cache.entries.try_emplace(name);
            auto& entry = it->second;

            if (inserted || entry.write_version != binding.write_version ||
                binding.write_version == Binding::k_unversioned) {
                entry = {.write_version = binding.write_version, .copy = binding.value.deep_copy()};
            }

            copy->bindings_[name] = Binding{entry.copy, binding.is_mutable,
                                            binding.is_from_namespace, binding.write_version};
        }

        return copy;
    }

    void define(std::string_view name, Value value, bool is_mutable, const SourceLocation& loc = {},
                bool is_from_namespace = false) {
        if (bindings_.contains(name)) {
            throw RuntimeError{std::format("variable '{}' is already defined in this scope", name),
                               loc};
        }

        bindings_[std::string{name}] =
            Binding{std::move(value), is_mutable, is_from_namespace, /*write_version=*/0};
    }

    [[nodiscard]] Value get(std::string_view name, const SourceLocation& loc) {
        const auto it = bindings_.find(name);

        if (it != bindings_.end()) {
            return it->second.value;
        }

        if (parent_) {
            return parent_->get(name, loc);
        }

        // Check if the name belongs to a module blocked by sandbox mode.
        verify_sandbox_access(name, loc);

        throw make_undefined_variable_error(name, loc);
    }

    void set(std::string_view name, Value value, const SourceLocation& loc) {
        auto it = bindings_.find(name);

        if (it != bindings_.end()) {
            if (!it->second.is_mutable) {
                throw RuntimeError{std::format("cannot assign to immutable variable '{}'", name),
                                   loc,
                                   "declare the variable with 'mutable' to allow reassignment"};
            }

            it->second.value = std::move(value);
            it->second.write_version = Binding::k_unversioned; // Written outside the VM.

            return;
        }

        if (parent_) {
            parent_->set(name, std::move(value), loc);

            return;
        }

        throw make_undefined_variable_error(name, loc);
    }

    // Define a new variable or update an existing one.
    // Used by the VM's SetGlobal opcode which handles both initial
    // declarations and subsequent assignments.
    void define_or_assign(std::string_view name, Value value, bool is_mutable,
                          const SourceLocation& loc = {}) {
        auto it = bindings_.find(name);

        if (it != bindings_.end()) {
            if (!it->second.is_mutable) {
                throw RuntimeError{std::format("cannot assign to immutable variable '{}'", name),
                                   loc,
                                   "declare the variable with 'mutable' to allow reassignment"};
            }

            it->second.value = std::move(value);
            it->second.write_version = Binding::k_unversioned; // Caller may re-stamp it.

            return;
        }

        bindings_[std::string{name}] =
            Binding{std::move(value), is_mutable, false, /*write_version=*/0};
    }

    [[nodiscard]] bool has(std::string_view name) const {
        if (bindings_.contains(name)) {
            return true;
        }

        if (parent_) {
            return parent_->has(name);
        }

        return false;
    }

    // Check only this scope's bindings, without walking the parent chain.
    [[nodiscard]] bool has_local(std::string_view name) const {
        return bindings_.contains(name);
    }

    // Number of bindings in this scope only (does not walk the parent chain).
    [[nodiscard]] std::size_t local_binding_count() const noexcept {
        return bindings_.size();
    }

    // Whether this scope (not its parents) holds any mutable binding.
    [[nodiscard]] bool has_mutable_bindings() const noexcept {
        return std::ranges::any_of(bindings_,
                                   [](const auto& entry) { return entry.second.is_mutable; });
    }

    [[nodiscard]] Binding* find_binding(std::string_view name) {
        auto it = bindings_.find(name);

        if (it != bindings_.end()) {
            return &it->second;
        }

        if (parent_) {
            return parent_->find_binding(name);
        }

        return nullptr;
    }

    [[nodiscard]] const Binding* find_binding(std::string_view name) const {
        const auto it = bindings_.find(name);

        if (it != bindings_.end()) {
            return &it->second;
        }

        if (parent_) {
            return parent_->find_binding(name);
        }

        return nullptr;
    }

    [[nodiscard]] bool is_binding_mutable(std::string_view name) const {
        const auto it = bindings_.find(name);

        if (it != bindings_.end()) {
            return it->second.is_mutable;
        }

        if (parent_) {
            return parent_->is_binding_mutable(name);
        }

        return false;
    }

    [[nodiscard]] EnvPtr parent() const {
        return parent_;
    }

    // Iterate all bindings in this scope only (does not walk parent chain).
    template <typename Fn> void for_each_binding(Fn&& fn) const {
        for (const auto& [name, binding] : bindings_) {
            fn(name, binding.value);
        }
    }

    // Iterate all bindings whose name starts with the given prefix
    // and is strictly longer than the prefix itself (i.e. "Ns." matches
    // "Ns.foo" but not "Ns.").  This is intentional: the prefix is
    // typically "Namespace." and the caller wants member names, not the
    // namespace name itself.
    void
    for_each_with_prefix(std::string_view prefix,
                         const std::function<void(const std::string&, const Value&)>& fn) const {
        StringSet seen;

        for_each_with_prefix(prefix, fn, seen);
    }

private:
    [[nodiscard]] EnvPtr deep_copy_impl(std::unordered_set<const Environment*>& visited) const {
        if (!visited.insert(this).second) {
            // Cycle detected — stop following the parent chain but
            // still return a valid (parent-less) environment so that
            // callers never receive nullptr.
            return std::make_shared<Environment>(nullptr);
        }

        EnvPtr parent_copy = parent_ ? parent_->deep_copy_impl(visited) : nullptr;
        auto copy = std::make_shared<Environment>(std::move(parent_copy));

        // Carry the sandbox policy over so a copy (e.g. a spawned task's global
        // snapshot) still reports blocked modules with the sandbox diagnostic.
        copy->sandbox_policy_ = sandbox_policy_;

        for (const auto& [name, binding] : bindings_) {
            copy->bindings_[name] = Binding{binding.value.deep_copy(), binding.is_mutable,
                                            binding.is_from_namespace, binding.write_version};
        }

        return copy;
    }

    void for_each_with_prefix(std::string_view prefix,
                              const std::function<void(const std::string&, const Value&)>& fn,
                              StringSet& seen) const {
        for (const auto& [name, binding] : bindings_) {
            if (name.size() > prefix.size() && name.starts_with(prefix)) {
                if (seen.insert(name).second) {
                    fn(name, binding.value);
                }
            }
        }

        if (parent_) {
            parent_->for_each_with_prefix(prefix, fn, seen);
        }
    }

    EnvPtr parent_;
    StringMap<Binding> bindings_;
    SandboxPolicy sandbox_policy_;

    void verify_sandbox_access(std::string_view name, const SourceLocation& loc) const {
        // Each scope's policy throws if it blocks the name; walk up to the root
        // policy (which holds the blocked set) so a blocked module is rejected
        // from any child scope.
        sandbox_policy_.verify_access(name, loc);

        if (parent_) {
            parent_->verify_sandbox_access(name, loc);
        }
    }
};

} // namespace luma

#endif // LUMA_INTERPRETER_ENVIRONMENT_HPP
