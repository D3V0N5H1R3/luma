#include "variable_inspector.hpp"

#include <cstdint>
#include <format>
#include <optional>
#include <string>

#include "common/narrow_int.hpp"
#include "custom_visualizer.hpp"
#include "dap_types.hpp"
#include "debug_session.hpp"
#include "debugger_messages.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"

namespace luma::dap {

// Returns true if the frame index is within the valid range for the given introspector.
[[nodiscard]] static bool is_valid_frame_index(int frame_index,
                                               const VMIntrospector& intro) noexcept {
    return frame_index >= 0 && static_cast<std::size_t>(frame_index) < intro.frame_count();
}

[[nodiscard]] ValueClassification classify_value(const Value& val) noexcept {
    if (val.is_array()) {
        return {ValueKind::Array, ChildVariableKind::Indexed};
    }
    if (val.is_dictionary()) {
        return {ValueKind::Dictionary, ChildVariableKind::Named};
    }
    if (val.is_tuple()) {
        return {ValueKind::Tuple, ChildVariableKind::Indexed};
    }
    if (val.is_record()) {
        return {ValueKind::Record, ChildVariableKind::Named};
    }
    if (val.is_choice()) {
        return {ValueKind::Choice, ChildVariableKind::Named};
    }
    if (val.is_result()) {
        return {ValueKind::Result, ChildVariableKind::Named};
    }
    if (val.is_queue()) {
        return {ValueKind::Queue, ChildVariableKind::Indexed};
    }
    if (val.is_stack()) {
        return {ValueKind::Stack, ChildVariableKind::Indexed};
    }
    if (val.is_set()) {
        return {ValueKind::Set, ChildVariableKind::Indexed};
    }
    if (val.is_key_value_store()) {
        return {ValueKind::KeyValueStore, ChildVariableKind::Named};
    }
    if (val.is_xml()) {
        return {ValueKind::Xml, ChildVariableKind::Named};
    }
    if (val.is_range()) {
        return {ValueKind::Range, ChildVariableKind::Named};
    }
    if (val.is_reference()) {
        return {ValueKind::Reference, ChildVariableKind::Named};
    }
    return {ValueKind::Scalar, ChildVariableKind::None};
}

[[nodiscard]] bool is_structured(const Value& val) noexcept {
    return classify_value(val).is_structured();
}

Variable make_base_variable(const std::string& name, const Value& value) {
    Variable var;
    var.name = name;
    var.value = value.to_string();
    var.type = value.display_type_name();
    return var;
}

VariableCounts count_child_variables(const Value& val) {
    const auto classification = classify_value(val);

    VariableCounts counts;
    if (classification.child_kind == ChildVariableKind::None) {
        return counts;
    }

    int child_count = 0;

    switch (classification.kind) {
        case ValueKind::Array:
            child_count = clamp_to_int(val.as_array()->elements->size());
            break;
        case ValueKind::Dictionary:
            child_count = clamp_to_int(val.as_dictionary()->entries.size());
            break;
        case ValueKind::Tuple:
            child_count = clamp_to_int(val.as_tuple()->elements.size());
            break;
        case ValueKind::Record:
            child_count = clamp_to_int(val.as_record()->fields.size());
            break;
        case ValueKind::Choice:
            child_count = 1 + clamp_to_int(val.as_choice()->fields.size());
            break;
        case ValueKind::Result:
            child_count = val.as_result()->owned_inner ? 2 : 1;
            break;
        case ValueKind::Queue:
            child_count = clamp_to_int(val.as_queue()->elements.size());
            break;
        case ValueKind::Stack:
            child_count = clamp_to_int(val.as_stack()->elements.size());
            break;
        case ValueKind::Set:
            child_count = clamp_to_int(val.as_set()->elements.size());
            break;
        case ValueKind::KeyValueStore:
            child_count = clamp_to_int(val.as_key_value_store()->size());
            break;
        case ValueKind::Xml: {
            // node_type + tag/content leaves, plus every attribute and child node.
            const auto xml = val.as_xml();
            child_count =
                2 + clamp_to_int(xml->attributes.size()) + clamp_to_int(xml->children.size());
            break;
        }
        case ValueKind::Range:
            child_count = 3;
            break;
        case ValueKind::Reference:
            child_count = 1;
            break;
        case ValueKind::Scalar:
            break;
    }

    if (classification.child_kind == ChildVariableKind::Named) {
        counts.named = child_count;
    } else {
        counts.indexed = child_count;
    }

    return counts;
}

VariableInspector::VariableInspector(EventCallback event_cb)
    : event_callback_(std::move(event_cb)), value_expander_(*this) {}

// ─── Reference lifecycle ───

int VariableInspector::alloc_ref(VariableRefEntry entry) const {
    const std::scoped_lock lock(ref_mutex_);

    if (ref_registry_.next_id() >= k_max_variable_references) {
        // Hard cap: full reset to prevent ID overflow.
        ref_registry_.clear_and_reset();
        frame_mappings_.clear_and_reset();
    } else if (ref_registry_.size() + frame_mappings_.size() >= purge_entry_threshold_) {
        // Entry count exceeded threshold — evict stale entries.
        purge_stale_entries();
    }

    return ref_registry_.allocate(std::move(entry));
}

void VariableInspector::invalidate_refs() const {
    {
        const std::scoped_lock lock(ref_mutex_);
        ref_registry_.advance_generation();
        frame_mappings_.advance_generation();

        // Periodically purge stale entries to bound memory usage.
        const bool generation_due =
            ref_registry_.generations_since_purge() >= purge_generation_interval_ ||
            frame_mappings_.generations_since_purge() >= purge_generation_interval_;

        if (generation_due) {
            purge_stale_entries();
        }
    }

    // Notify the client that cached variable/stack data is stale.
    if (event_callback_) {
        JsonValue::ObjectType body;
        JsonValue::ArrayType areas;
        areas.emplace_back(std::string("variables"));
        areas.emplace_back(std::string("stacks"));
        body["areas"] = JsonValue(std::move(areas));
        event_callback_(std::string{kEventInvalidated}, JsonValue(std::move(body)));
    }
}

void VariableInspector::purge_stale_entries() const {
    ref_registry_.purge_stale();
    frame_mappings_.purge_stale();
}

int VariableInspector::reference_count() const {
    const std::scoped_lock lock(ref_mutex_);
    return ref_registry_.size() + frame_mappings_.size();
}

void VariableInspector::set_purge_entry_threshold(int threshold) {
    purge_entry_threshold_ = threshold;
}

void VariableInspector::set_purge_generation_interval(int interval) {
    purge_generation_interval_ = interval;
}

// ─── Frame registration ───

int VariableInspector::register_frame(int thread_id, int frame_index, VM* vm) const {
    const std::scoped_lock lock(ref_mutex_);
    return frame_mappings_.allocate(
        FrameMapping{.thread_id = thread_id, .frame_index = frame_index, .vm = vm});
}

std::optional<FrameMapping> VariableInspector::resolve_frame(int frame_id) const {
    const std::scoped_lock lock(ref_mutex_);
    return frame_mappings_.lookup(frame_id);
}

VariableInspector::LockedFrame
VariableInspector::lock_frame_and_vm(int frame_id, const ThreadResolver& resolver) const {
    auto mapping = resolve_frame(frame_id);
    const int actual_index = mapping ? mapping->frame_index : frame_id;

    if (!mapping) {
        return LockedFrame{
            .state = nullptr, .lock = std::nullopt, .vm = nullptr, .actual_index = actual_index};
    }

    // resolve_frame briefly held ref_mutex_ above and released it; the thread
    // resolver (which acquires thread_states_mutex_) also runs before we take
    // ThreadState::mutex, so this never nests those locks under it.
    auto state = resolver(mapping->thread_id);

    if (!state) {
        return LockedFrame{
            .state = nullptr, .lock = std::nullopt, .vm = nullptr, .actual_index = actual_index};
    }

    // Acquire the ThreadState lock and KEEP it held: the caller dereferences the
    // VM through a VMIntrospector, and releasing here would reopen the window in
    // which a self-exiting task nulls state->vm and destroys the VM mid-read.
    // Use the ordered lock so debug builds validate DAP lock-ordering (leaf:
    // PerThread) instead of taking the raw mutex untracked.
    OrderedUniqueLock<DapLockId> lock(state->mutex, DapLockId::PerThread);
    VM* vm = state->vm; // nullptr if the thread already exited.

    // Treat a running (non-paused) thread as unresolved, exactly like an exited
    // one: only a STOPPED thread has a stable stack/locals to inspect.  A
    // free-running task VM concurrently mutates its frames and value stack, so
    // dereferencing it through the returned VMIntrospector would race the
    // execution thread and can crash the adapter (SIGSEGV).  is_paused is
    // guarded by the ThreadState lock acquired above.
    if (vm == nullptr || !state->is_paused) {
        // Unresolved: return nullopt and let the local `lock` release
        // state->mutex as it destructs on scope exit.
        return LockedFrame{
            .state = nullptr, .lock = std::nullopt, .vm = nullptr, .actual_index = actual_index};
    }

    return LockedFrame{
        .state = std::move(state), .lock = std::move(lock), .vm = vm, .actual_index = actual_index};
}

// ─── Variable building ───

Variable VariableInspector::make_variable(const std::string& name, const Value& val,
                                          bool is_mutable, int depth) const {
    Variable var = make_base_variable(name, val);
    var.is_mutable = is_mutable;

    // Apply custom visualizer if a matching rule exists.  The display template
    // replaces the value verbatim; placeholder expansion is not supported.
    if (custom_visualizer_ != nullptr && custom_visualizer_->has_rules()) {
        auto rule = custom_visualizer_->find_rule(var.type);

        if (rule.has_value() && !rule->display_template.empty()) {
            var.value = rule->display_template;
        }
    }

    if (is_structured(val) && depth < max_expansion_depth_) {
        var.variables_reference =
            alloc_ref(ValueRef{.value = std::make_shared<Value>(val), .depth = depth});

        const auto counts = count_child_variables(val);
        var.named_variables = counts.named;
        var.indexed_variables = counts.indexed;
    }

    return var;
}

// ─── Inspection ───

Scope VariableInspector::make_scope(std::string name, ScopeType scope_type, int frame_id,
                                    bool expensive, std::string presentation_hint) const {
    Scope scope;
    scope.name = std::move(name);
    scope.variables_reference = alloc_ref(ScopeRef{.frame_id = frame_id, .scope_type = scope_type});
    scope.expensive = expensive;
    scope.presentation_hint = std::move(presentation_hint);
    return scope;
}

std::vector<Scope> VariableInspector::get_scopes(int frame_id,
                                                 const ThreadResolver& resolver) const {
    std::vector<Scope> result;

    auto frame = lock_frame_and_vm(frame_id, resolver);

    result.push_back(make_scope("Local", ScopeType::Local, frame_id, false, "locals"));

    // Add Closure scope if the frame has captured upvalues.
    if (frame.vm != nullptr) {
        const VMIntrospector intro(*frame.vm);

        if (is_valid_frame_index(frame.actual_index, intro)) {
            if (intro.has_upvalues(static_cast<std::size_t>(frame.actual_index))) {
                result.push_back(
                    make_scope("Closure", ScopeType::Closure, frame_id, false, "locals"));
            }
        }
    }

    result.push_back(make_scope("Global", ScopeType::Global, frame_id, true, "globals"));

    return result;
}

std::vector<Variable> VariableInspector::get_variables(int reference, int start, int count,
                                                       const std::string& filter,
                                                       const ThreadResolver& resolver) const {
    VariableRefEntry entry_copy;

    {
        const std::scoped_lock lock(ref_mutex_);

        auto entry = ref_registry_.lookup(reference);

        if (!entry) {
            return {};
        }

        entry_copy = *entry;
    }

    std::vector<Variable> result;

    if (std::holds_alternative<ScopeRef>(entry_copy)) {
        if (filter == "indexed") {
            return {};
        }

        result = get_scope_variables(std::get<ScopeRef>(entry_copy), resolver);
    } else if (std::holds_alternative<ValueRef>(entry_copy)) {
        const auto& value_ref = std::get<ValueRef>(entry_copy);
        result = value_expander_.get_value_variables(*value_ref.value, value_ref.depth, start,
                                                     count, filter);
    }

    return result;
}

std::vector<Variable> VariableInspector::get_scope_variables(const ScopeRef& scope_ref,
                                                             const ThreadResolver& resolver) const {
    auto frame = lock_frame_and_vm(scope_ref.frame_id, resolver);

    if (frame.vm == nullptr) {
        return {};
    }

    const VMIntrospector intro(*frame.vm);

    if (!is_valid_frame_index(frame.actual_index, intro)) {
        return {};
    }

    return (this->*scope_descriptor(scope_ref.scope_type).list)(intro, frame.vm,
                                                                frame.actual_index);
}

std::vector<Variable> VariableInspector::list_local_variables(const VMIntrospector& intro,
                                                              VM* /*vm*/, int frame_index) const {
    std::vector<Variable> result;

    auto locals = intro.locals(static_cast<std::size_t>(frame_index));

    for (const auto& local : locals) {
        auto var = make_variable(local.name, local.value, local.is_mutable);
        var.evaluate_name = local.name;
        result.push_back(std::move(var));
    }

    return result;
}

std::vector<Variable> VariableInspector::list_global_variables(const VMIntrospector& /*intro*/,
                                                               VM* vm, int /*frame_index*/) const {
    std::vector<Variable> result;

    auto global_env = vm->global_env();

    if (global_env) {
        global_env->for_each_binding([&](const std::string& name, const Value& val) {
            if (val.is_function() || val.is_native_function()) {
                return;
            }

            const auto* binding = global_env->find_binding(name);
            const bool mutable_var = binding && binding->is_mutable;
            result.push_back(make_variable(name, val, mutable_var));
        });
    }

    return result;
}

std::vector<Variable> VariableInspector::list_closure_variables(const VMIntrospector& intro,
                                                                VM* /*vm*/, int frame_index) const {
    std::vector<Variable> result;

    auto upvalues = intro.upvalues(static_cast<std::size_t>(frame_index));

    for (const auto& uv : upvalues) {
        result.push_back(make_variable(uv.name, uv.value, uv.is_mutable));
    }

    return result;
}

std::pair<int, int> VariableInspector::get_variable_counts(int reference,
                                                           const ThreadResolver& resolver) const {
    int named_count = 0;
    int indexed_count = 0;

    VariableRefEntry entry_copy;

    {
        const std::scoped_lock lock(ref_mutex_);
        auto entry = ref_registry_.lookup(reference);

        if (!entry) {
            return {0, 0};
        }

        entry_copy = *entry;
    }

    if (std::holds_alternative<ScopeRef>(entry_copy)) {
        named_count = count_scope_variables(std::get<ScopeRef>(entry_copy), resolver);
    } else if (std::holds_alternative<ValueRef>(entry_copy)) {
        const auto& value_ref = std::get<ValueRef>(entry_copy);
        const auto counts = count_child_variables(*value_ref.value);
        named_count = counts.named;
        indexed_count = counts.indexed;
    }

    return {named_count, indexed_count};
}

// ─── Modification ───

// ─── Scalar parsing helpers ───
// Shared by parse_value (untyped) and parse_value_typed (type-directed) so the
// boolean/integer/floating-point/string parsing rules live in exactly one place
// and a fix to any one of them applies to both entry points.

[[nodiscard]] static std::optional<Value> try_parse_bool(const std::string& str) {
    if (str == "true") {
        return Value{true};
    }

    if (str == "false") {
        return Value{false};
    }

    return std::nullopt;
}

// Parses str as a base-10 integer, requiring the whole string to be consumed.
[[nodiscard]] static std::optional<Value> try_parse_int(const std::string& str) {
    try {
        std::size_t pos = 0;
        const auto parsed = std::stoll(str, &pos);

        if (pos == str.size()) {
            return Value{static_cast<std::int64_t>(parsed)};
        }
    } catch (...) {
        // Not representable as an integer.
        (void)0;
    }

    return std::nullopt;
}

// Parses str as a floating-point number, requiring the whole string consumed.
[[nodiscard]] static std::optional<Value> try_parse_double(const std::string& str) {
    try {
        std::size_t pos = 0;
        const auto parsed = std::stod(str, &pos);

        if (pos == str.size()) {
            return Value{parsed};
        }
    } catch (...) {
        // Not representable as a number.
        (void)0;
    }

    return std::nullopt;
}

// Interprets str as a string value, stripping one layer of matching surrounding
// double quotes so a client may send either `hello` or `"hello"`.
[[nodiscard]] static Value parse_string_value(const std::string& str) {
    if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
        return Value{str.substr(1, str.size() - 2)};
    }

    return Value{str};
}

Value VariableInspector::parse_value(const std::string& str) {
    if (auto parsed = try_parse_bool(str)) {
        return *parsed;
    }

    if (auto parsed = try_parse_int(str)) {
        return *parsed;
    }

    if (auto parsed = try_parse_double(str)) {
        return *parsed;
    }

    return parse_string_value(str);
}

std::optional<Value> VariableInspector::parse_value_typed(const std::string& str,
                                                          const Value& current) {
    if (current.is_bool()) {
        return try_parse_bool(str);
    }

    if (current.is_integer()) {
        return try_parse_int(str);
    }

    if (current.is_number()) {
        return try_parse_double(str);
    }

    if (current.is_string()) {
        // Any text is a valid string; strip matching surrounding quotes so a
        // client can send either `hello` or `"hello"`.
        return parse_string_value(str);
    }

    // Complex or null types cannot be reconstructed from a plain DAP string
    // edit; reject rather than silently overwriting with a primitive.
    return std::nullopt;
}

Variable VariableInspector::set_variable(int variables_reference, const std::string& name,
                                         const std::string& value,
                                         const ThreadResolver& resolver) const {
    Variable result;
    result.name = name;
    result.value = value;
    result.type = "unknown";

    VariableRefEntry entry_copy;

    {
        const std::scoped_lock lock(ref_mutex_);

        auto entry = ref_registry_.lookup(variables_reference);

        if (!entry) {
            result.value = std::string{messages::variable::stale_reference};
            result.type = "error";
            return result;
        }

        entry_copy = *entry;
    }

    auto new_val = parse_value(value);

    if (std::holds_alternative<ScopeRef>(entry_copy)) {
        const auto& scope_ref = std::get<ScopeRef>(entry_copy);
        auto frame = lock_frame_and_vm(scope_ref.frame_id, resolver);

        if (frame.vm == nullptr) {
            return result;
        }

        // Respect the target variable's declared type: coerce the incoming
        // string to the current value's type, or reject a type-changing edit
        // with an error, rather than letting parse_value infer a (possibly
        // different) type from the input string. When the current value cannot
        // be read (unknown target type), fall back to the untyped parse so the
        // scope setter still reports the existing not-found/immutable errors.
        if (auto current = read_scope_value(scope_ref, frame.vm, frame.actual_index, name)) {
            auto coerced = parse_value_typed(value, *current);

            if (!coerced) {
                result.value = std::format(messages::variable::type_mismatch_format, value,
                                           current->display_type_name());
                result.type = "error";
                return result;
            }

            new_val = std::move(*coerced);
        }

        return (this->*scope_descriptor(scope_ref.scope_type).set)(frame.vm, frame.actual_index,
                                                                   name, new_val);
    }

    result.value = std::format(messages::variable::not_found_format, name);
    result.type = "error";
    return result;
}

// ─── Scope-specific mutation helpers ───

Variable VariableInspector::build_set_result(const std::string& name, bool success,
                                             const Value& new_val, std::string_view error_message) {
    Variable result;
    result.name = name;

    if (success) {
        result.value = new_val.to_string();
        result.type = new_val.display_type_name();
    } else {
        result.value = std::string{error_message};
        result.type = "error";
    }

    return result;
}

Variable VariableInspector::set_local_variable(VM* vm, int frame_index, const std::string& name,
                                               const Value& new_val) const {
    const bool success =
        VMIntrospector::set_local(*vm, static_cast<std::size_t>(frame_index), name, new_val);
    return build_set_result(name, success, new_val, messages::variable::immutable_variable);
}

Variable VariableInspector::set_global_variable(VM* vm, int /*frame_index*/,
                                                const std::string& name,
                                                const Value& new_val) const {
    auto global_env = vm->global_env();

    if (!global_env) {
        return build_set_result(name, false, new_val,
                                std::format(messages::variable::not_found_format, name));
    }

    auto* binding = global_env->find_binding(name);

    if (binding == nullptr) {
        return build_set_result(name, false, new_val,
                                std::format(messages::variable::not_found_format, name));
    }

    if (!binding->is_mutable) {
        return build_set_result(name, false, new_val, messages::variable::immutable_variable);
    }

    binding->value = new_val;
    return build_set_result(name, true, new_val, "");
}

Variable VariableInspector::set_closure_variable(VM* vm, int frame_index, const std::string& name,
                                                 const Value& new_val) const {
    const bool success =
        VMIntrospector::set_upvalue(*vm, static_cast<std::size_t>(frame_index), name, new_val);
    return build_set_result(name, success, new_val, messages::variable::immutable_variable);
}

std::optional<Value> VariableInspector::read_scope_value(const ScopeRef& scope_ref, VM* vm,
                                                         int frame_index,
                                                         const std::string& name) const {
    const VMIntrospector intro(*vm);

    return (this->*scope_descriptor(scope_ref.scope_type).read)(intro, vm, frame_index, name);
}

std::optional<Value> VariableInspector::read_local_value(const VMIntrospector& intro, VM* /*vm*/,
                                                         int frame_index,
                                                         const std::string& name) const {
    auto local = intro.find_local(static_cast<std::size_t>(frame_index), name);
    return local ? std::optional<Value>{local->value} : std::nullopt;
}

std::optional<Value> VariableInspector::read_closure_value(const VMIntrospector& intro, VM* /*vm*/,
                                                           int frame_index,
                                                           const std::string& name) const {
    for (const auto& upvalue : intro.upvalues(static_cast<std::size_t>(frame_index))) {
        if (upvalue.name == name) {
            return upvalue.value;
        }
    }

    return std::nullopt;
}

std::optional<Value> VariableInspector::read_global_value(const VMIntrospector& /*intro*/, VM* vm,
                                                          int /*frame_index*/,
                                                          const std::string& name) const {
    auto global_env = vm->global_env();

    if (!global_env) {
        return std::nullopt;
    }

    const auto* binding = global_env->find_binding(name);
    return binding ? std::optional<Value>{binding->value} : std::nullopt;
}

// ─── Scope counting helper ───

int VariableInspector::count_scope_variables(const ScopeRef& scope_ref,
                                             const ThreadResolver& resolver) const {
    auto frame = lock_frame_and_vm(scope_ref.frame_id, resolver);

    if (frame.vm == nullptr) {
        return 0;
    }

    const VMIntrospector intro(*frame.vm);

    if (!is_valid_frame_index(frame.actual_index, intro)) {
        return 0;
    }

    return (this->*scope_descriptor(scope_ref.scope_type).count)(intro, frame.vm,
                                                                 frame.actual_index);
}

int VariableInspector::count_local_variables(const VMIntrospector& intro, VM* /*vm*/,
                                             int frame_index) const {
    auto locals = intro.locals(static_cast<std::size_t>(frame_index));
    return clamp_to_int(locals.size());
}

int VariableInspector::count_global_variables(const VMIntrospector& /*intro*/, VM* vm,
                                              int /*frame_index*/) const {
    int count = 0;
    auto global_env = vm->global_env();

    if (global_env) {
        global_env->for_each_binding([&](const std::string& /*name*/, const Value& val) {
            if (!val.is_function() && !val.is_native_function()) {
                ++count;
            }
        });
    }

    return count;
}

int VariableInspector::count_closure_variables(const VMIntrospector& intro, VM* /*vm*/,
                                               int frame_index) const {
    auto upvalues = intro.upvalues(static_cast<std::size_t>(frame_index));
    return clamp_to_int(upvalues.size());
}

// ─── Scope dispatch table ───

const VariableInspector::ScopeDescriptor&
VariableInspector::scope_descriptor(ScopeType scope_type) {
    static const ScopeDescriptor local{
        .list = &VariableInspector::list_local_variables,
        .count = &VariableInspector::count_local_variables,
        .read = &VariableInspector::read_local_value,
        .set = &VariableInspector::set_local_variable,
    };
    static const ScopeDescriptor global{
        .list = &VariableInspector::list_global_variables,
        .count = &VariableInspector::count_global_variables,
        .read = &VariableInspector::read_global_value,
        .set = &VariableInspector::set_global_variable,
    };
    static const ScopeDescriptor closure{
        .list = &VariableInspector::list_closure_variables,
        .count = &VariableInspector::count_closure_variables,
        .read = &VariableInspector::read_closure_value,
        .set = &VariableInspector::set_closure_variable,
    };

    switch (scope_type) {
        case ScopeType::Local:
            return local;
        case ScopeType::Global:
            return global;
        case ScopeType::Closure:
            return closure;
    }

    // Unreachable: ScopeType is a closed enum and every case returns above.
    return local;
}

// ─── Completions ───

std::vector<std::pair<std::string, std::string>>
VariableInspector::get_completions(int frame_id, const std::string& text,
                                   const ThreadResolver& resolver) const {
    std::vector<std::pair<std::string, std::string>> result;

    auto frame = lock_frame_and_vm(frame_id, resolver);

    if (frame.vm == nullptr) {
        return result;
    }

    const VMIntrospector intro(*frame.vm);

    // Collect local names.
    if (is_valid_frame_index(frame.actual_index, intro)) {
        auto locals = intro.locals(static_cast<std::size_t>(frame.actual_index));

        for (const auto& local : locals) {
            if (!local.name.empty() && local.name != "_" && local.name.starts_with(text)) {
                result.emplace_back(local.name, "variable");
            }
        }

        // Include closure upvalue names.
        if (intro.has_upvalues(static_cast<std::size_t>(frame.actual_index))) {
            auto upvalues = intro.upvalues(static_cast<std::size_t>(frame.actual_index));

            for (const auto& uv : upvalues) {
                if (!uv.name.empty() && uv.name.starts_with(text)) {
                    result.emplace_back(uv.name, "variable");
                }
            }
        }
    }

    // Collect global names.
    auto global_env = frame.vm->global_env();

    if (global_env) {
        global_env->for_each_binding([&](const std::string& name, const Value& val) {
            if (name.starts_with(text)) {
                if (val.is_function() || val.is_native_function()) {
                    result.emplace_back(name, "function");
                } else {
                    result.emplace_back(name, "variable");
                }
            }
        });
    }

    return result;
}

} // namespace luma::dap
