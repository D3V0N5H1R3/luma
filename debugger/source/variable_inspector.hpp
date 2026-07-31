#ifndef LUMA_DAP_VARIABLE_INSPECTOR_HPP
#define LUMA_DAP_VARIABLE_INSPECTOR_HPP

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "dap_callback_types.hpp"
#include "dap_session_types.hpp"
#include "dap_types.hpp"
#include "debugger_config.hpp"
#include "value_expander.hpp"
#include "variable_reference_registry.hpp"

namespace luma {
class VM;
class Value;
class VMIntrospector;
struct FrameLocation;
struct LocalVariable;
struct UpvalueVariable;
} // namespace luma

namespace luma::dap {

class CustomVisualizer;

// ═══════════════════════════════════════════════════════════
// Null-safety contract:
// - Public methods: validate inputs and return std::nullopt / empty for null/invalid args
// - Private methods: assert preconditions (caller must ensure non-null)
// ═══════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════
// VariableInspector — DAP variable/scope/frame inspection.
//
// Responsibilities (after VariableReferenceRegistry extraction):
//   1. Reference allocation & generation-based invalidation.
//   2. Frame mapping registration (frame_id → thread + VM*).
//   3. Scope enumeration and variable expansion (get_scopes,
//      get_variables, make_variable).
//   4. Variable mutation (set_variable).
//   5. Expression completions (get_completions).
//   6. Optional custom visualizer integration.
//
// Registry storage is delegated to VariableReferenceRegistry<T>.
// This class owns the mutex, the two registry instances, and
// all higher-level inspection logic that operates on them.
//
// Reference lifecycle uses a generation counter: all refs are
// invalidated when execution resumes, replacing the old
// "evict bottom 50% at 100k" heuristic.
// ═══════════════════════════════════════════════════════════

// ─── Scope types ───
enum class ScopeType : int {
    Local = 1,
    Global = 2,
    Closure = 3,
};

// ─── Frame mapping ───
struct FrameMapping {
    int thread_id{0};
    int frame_index{0};
    VM* vm{nullptr}; // Non-owning — valid while thread is paused.
};

// ─── Variable reference entries ───
struct ScopeRef {
    int frame_id;
    ScopeType scope_type;
};

struct ValueRef {
    std::shared_ptr<Value> value; // Owned copy — safe after resume.
    int depth{0};                 // Nesting depth — expansion stops at max_expansion_depth.
};

using VariableRefEntry = std::variant<ScopeRef, ValueRef>;

// ─── Value classification ───
// Single source of truth for how the debugger treats each runtime Value kind
// when expanding it in the variables view.  is_structured(),
// count_child_variables(), and ValueExpander::get_value_variables() all derive
// their behaviour from classify_value(), so the structured-kind set and the
// named/indexed child discipline cannot drift apart.

// Expandable runtime Value kinds, plus Scalar for everything with no children.
// Adding a new structured kind starts here.
enum class ValueKind {
    Scalar,
    Array,
    Dictionary,
    Tuple,
    Record,
    Choice,
    Result,
    Queue,
    Stack,
    Set,
    BinaryTree,
    KeyValueStore,
    Xml,
    Range,
    Reference,
};

// How a kind's children are reported to the DAP client: as named entries
// (object-like), indexed elements (array-like), or none (scalar).
enum class ChildVariableKind {
    None,
    Named,
    Indexed
};

struct ValueClassification {
    ValueKind kind{ValueKind::Scalar};
    ChildVariableKind child_kind{ChildVariableKind::None};

    // A value is structured (expandable) when it is not a scalar.
    [[nodiscard]] bool is_structured() const noexcept {
        return kind != ValueKind::Scalar;
    }
};

// Classify a runtime Value into its expansion kind and child discipline.
[[nodiscard]] ValueClassification classify_value(const Value& val) noexcept;

// Returns true if a Value is a structured type that can be expanded.
[[nodiscard]] bool is_structured(const Value& val) noexcept;

// Build a Variable with the display fields (name, value, type) common to
// every Variable constructed from a runtime Value.  Callers populate the
// structure-specific fields (variables_reference, child counts, is_mutable).
[[nodiscard]] Variable make_base_variable(const std::string& name, const Value& value);

// Counts of named vs indexed child variables for a structured Value.
struct VariableCounts {
    int named{0};
    int indexed{0};
};

// Count the named and indexed children of a structured Value.
[[nodiscard]] VariableCounts count_child_variables(const Value& val);

class VariableInspector {
public:
    // Callback to resolve a thread_id → ThreadState (and thus VM*).
    using ThreadResolver = std::function<std::shared_ptr<ThreadState>(int thread_id)>;

    explicit VariableInspector(EventCallback event_cb = {});

    // Set an optional custom visualizer for value formatting.
    // The pointed-to object must outlive this inspector.
    void set_custom_visualizer(const CustomVisualizer* viz) {
        custom_visualizer_ = viz;
    }

    // Configure the maximum number of registry entries before stale entries are purged.
    void set_purge_entry_threshold(int threshold);

    // Configure how many generation advances between automatic purges.
    void set_purge_generation_interval(int interval);

    // ─── Reference lifecycle ───

    // Allocate a new variablesReference ID.
    [[nodiscard]] int alloc_ref(VariableRefEntry entry) const;

    // Invalidate all references (called when execution resumes).
    // Uses generation counter — O(1) instead of clearing the map.
    void invalidate_refs() const;

    // Total number of live variable reference entries currently tracked.
    [[nodiscard]] int reference_count() const;

    // ─── Frame registration ───

    // Register a frame mapping (called during get_stack_trace).
    [[nodiscard]] int register_frame(int thread_id, int frame_index, VM* vm) const;

    // Resolve a frame_id to its mapping.
    [[nodiscard]] std::optional<FrameMapping> resolve_frame(int frame_id) const;

    // ─── Inspection ───

    // Build scopes for a frame.
    [[nodiscard]] std::vector<Scope> get_scopes(int frame_id, const ThreadResolver& resolver) const;

    // Expand variables for a reference.
    [[nodiscard]] std::vector<Variable> get_variables(int reference, int start, int count,
                                                      const std::string& filter,
                                                      const ThreadResolver& resolver) const;

    // Get the total count of named and indexed variables for a reference.
    // Returns a pair of (named_count, indexed_count).
    [[nodiscard]] std::pair<int, int> get_variable_counts(int reference,
                                                          const ThreadResolver& resolver) const;

    // Build a Variable representation for a Value.
    [[nodiscard]] Variable make_variable(const std::string& name, const Value& val,
                                         bool is_mutable = false, int depth = 0) const;

    // ─── Modification ───

    // Parse a string representation into a Value (for setVariable).
    [[nodiscard]] static Value parse_value(const std::string& str);

    // Parse a string edit into a Value that matches `current`'s type, so a
    // setVariable request respects the target variable's declared type instead
    // of inferring a (possibly different) type from the input string. Returns
    // nullopt when the input cannot be represented in the target type, letting
    // the caller reject a type-changing edit with a DAP error. String targets
    // always succeed (any text is a valid string); complex and null targets are
    // rejected because they cannot be reconstructed from a plain string.
    [[nodiscard]] static std::optional<Value> parse_value_typed(const std::string& str,
                                                                const Value& current);

    // Set a variable's value.  Returns the updated Variable or an error.
    [[nodiscard]] Variable set_variable(int variables_reference, const std::string& name,
                                        const std::string& value,
                                        const ThreadResolver& resolver) const;

    // ─── Completions ───

    [[nodiscard]] std::vector<std::pair<std::string, std::string>>
    get_completions(int frame_id, const std::string& text, const ThreadResolver& resolver) const;

private:
    // Result of resolving a frame_id to a VM* and actual frame index, WITH the
    // owning ThreadState::mutex held for the lifetime of this object.  Callers
    // must dereference `vm` (build a VMIntrospector, read locals/upvalues/
    // globals, set variables) only while this object is alive: holding the lock
    // closes the use-after-free window where a concurrently self-exiting task
    // nulls state->vm and destroys the VM mid-inspection (the debugger does not
    // stop-the-world on every thread).  `state` keeps the ThreadState alive so
    // the held lock can never dangle.
    struct LockedFrame {
        std::shared_ptr<ThreadState> state; // keeps the locked state alive
        // Held while this object lives. std::optional because the lock has no
        // empty state of its own (it locks in its constructor); an unresolved
        // frame carries std::nullopt. Ordered so debug builds validate DAP
        // lock-ordering (leaf: PerThread).
        std::optional<OrderedUniqueLock<DapLockId>> lock;
        VM* vm{nullptr}; // nullptr if unresolved or exited
        int actual_index{-1};
    };

    // Resolve a frame_id to its target VM and actual frame index, returning the
    // ThreadState::mutex already held.  A null `vm` means the frame could not be
    // resolved (no mapping, no thread, or the thread already exited); in that
    // case no lock is held.
    [[nodiscard]] LockedFrame lock_frame_and_vm(int frame_id, const ThreadResolver& resolver) const;

    // Build one DAP Scope: allocate its variablesReference for
    // (frame_id, scope_type) and set the display fields.  Factors out the three
    // near-identical scope constructions in get_scopes().
    [[nodiscard]] Scope make_scope(std::string name, ScopeType scope_type, int frame_id,
                                   bool expensive, std::string presentation_hint) const;

    // Handle the ScopeRef branch of get_variables.
    // NOTE: VMIntrospector is constructed locally per call because get_scopes,
    // get_scope_variables, and count_scope_variables operate on different
    // call paths and the introspector is lightweight (non-allocating view).
    [[nodiscard]] std::vector<Variable> get_scope_variables(const ScopeRef& scope_ref,
                                                            const ThreadResolver& resolver) const;

    // Per-type value expansion (get_value_variables and the get_*_variables
    // family) lives in the ValueExpander component (value_expander_ below).

    // Build a Variable result for a set operation: success with updated value, or error.
    [[nodiscard]] static Variable build_set_result(const std::string& name, bool success,
                                                   const Value& new_val,
                                                   std::string_view error_message);

    // Scope-specific mutation helpers for set_variable().  All share the
    // dispatch-table signature (see ScopeDescriptor); set_global_variable
    // ignores frame_index because globals are not frame-scoped.
    [[nodiscard]] Variable set_local_variable(VM* vm, int frame_index, const std::string& name,
                                              const Value& new_val) const;
    [[nodiscard]] Variable set_global_variable(VM* vm, int frame_index, const std::string& name,
                                               const Value& new_val) const;
    [[nodiscard]] Variable set_closure_variable(VM* vm, int frame_index, const std::string& name,
                                                const Value& new_val) const;

    // ─── Scope dispatch table ───
    // The Local/Global/Closure scopes each support the same four operations
    // (list, count, read, set).  Rather than repeat a parallel `switch
    // (scope_type)` for every operation, each scope kind is described once by a
    // ScopeDescriptor of member-function pointers, and every operation indexes
    // scope_descriptor() to find its handler.  Adding or renaming a scope kind
    // then touches one table plus the -Wswitch-checked accessor, not four
    // scattered switches.  Follows the vm_dispatch_table.cpp precedent.
    struct ScopeDescriptor {
        std::vector<Variable> (VariableInspector::*list)(const VMIntrospector&, VM*, int) const;
        int (VariableInspector::*count)(const VMIntrospector&, VM*, int) const;
        std::optional<Value> (VariableInspector::*read)(const VMIntrospector&, VM*, int,
                                                        const std::string&) const;
        Variable (VariableInspector::*set)(VM*, int, const std::string&, const Value&) const;
    };

    // Look up the descriptor for a scope kind.  Single source of truth for the
    // scope-kind set; the -Wswitch check flags any newly added ScopeType.
    [[nodiscard]] static const ScopeDescriptor& scope_descriptor(ScopeType scope_type);

    // Per-scope list handlers: enumerate the scope's variables as DAP Variables.
    [[nodiscard]] std::vector<Variable> list_local_variables(const VMIntrospector& intro, VM* vm,
                                                             int frame_index) const;
    [[nodiscard]] std::vector<Variable> list_global_variables(const VMIntrospector& intro, VM* vm,
                                                              int frame_index) const;
    [[nodiscard]] std::vector<Variable> list_closure_variables(const VMIntrospector& intro, VM* vm,
                                                               int frame_index) const;

    // Per-scope count handlers: count the scope's variables without building
    // Variable objects (so no variable references are allocated).
    [[nodiscard]] int count_local_variables(const VMIntrospector& intro, VM* vm,
                                            int frame_index) const;
    [[nodiscard]] int count_global_variables(const VMIntrospector& intro, VM* vm,
                                             int frame_index) const;
    [[nodiscard]] int count_closure_variables(const VMIntrospector& intro, VM* vm,
                                              int frame_index) const;

    // Per-scope read handlers: read a single named value, or nullopt if absent.
    [[nodiscard]] std::optional<Value> read_local_value(const VMIntrospector& intro, VM* vm,
                                                        int frame_index,
                                                        const std::string& name) const;
    [[nodiscard]] std::optional<Value> read_global_value(const VMIntrospector& intro, VM* vm,
                                                         int frame_index,
                                                         const std::string& name) const;
    [[nodiscard]] std::optional<Value> read_closure_value(const VMIntrospector& intro, VM* vm,
                                                          int frame_index,
                                                          const std::string& name) const;

    // Read the current value of a variable in the given scope, used by
    // set_variable to learn the target's type before coercing an edit. Returns
    // nullopt when the variable is not present in that scope.
    [[nodiscard]] std::optional<Value> read_scope_value(const ScopeRef& scope_ref, VM* vm,
                                                        int frame_index,
                                                        const std::string& name) const;

    // Count the number of named variables in a scope without building Variable objects.
    [[nodiscard]] int count_scope_variables(const ScopeRef& scope_ref,
                                            const ThreadResolver& resolver) const;

    // Remove entries from ref_registry_ and frame_mappings_ whose generation
    // is older than the current generation. Must be called with ref_mutex_ held.
    void purge_stale_entries() const;

    // ─── State ───

    // Maximum nesting depth for structured variable expansion.
    static constexpr int max_expansion_depth_{config::variable::k_max_expansion_depth};

    // Maximum number of variable references before a full reset.
    static constexpr int k_max_variable_references = config::variable::k_max_variable_references;

    EventCallback event_callback_;

    // Optional custom visualizer — non-owning, may be nullptr.
    const CustomVisualizer* custom_visualizer_{nullptr};

    // Variable reference and frame mapping registries.
    //
    // Lock ordering invariant: both ref_registry_ and frame_mappings_ are
    // always accessed under ref_mutex_. Never acquire any other mutex while
    // holding ref_mutex_. This ensures a single consistent lock domain for
    // all registry operations and prevents potential deadlock with callers
    // that hold other locks (e.g., ThreadState::mutex).
    mutable std::mutex ref_mutex_;
    mutable VariableReferenceRegistry<VariableRefEntry> ref_registry_; // GUARDED_BY(ref_mutex_)
    mutable VariableReferenceRegistry<FrameMapping> frame_mappings_;   // GUARDED_BY(ref_mutex_)

    // Purge configuration.
    int purge_entry_threshold_{config::variable::k_default_purge_entry_threshold};
    int purge_generation_interval_{config::variable::k_default_purge_generation_interval};

    // Per-type value expansion, delegating back to make_variable().  Declared
    // last so it is initialised after the members it references through *this.
    ValueExpander value_expander_;
};

} // namespace luma::dap

#endif // LUMA_DAP_VARIABLE_INSPECTOR_HPP
