#include "vm_debug_adapter.hpp"

#include "runtime/vm/vm.hpp"

namespace luma::dap {

VMDebugAdapter::VMDebugAdapter(VM& vm) : vm_(vm) {}

// ─── IVMControl ───

void VMDebugAdapter::request_pause_check() {
    vm_.request_pause_check();
}

void VMDebugAdapter::execute(const std::vector<CompiledFunction>& functions,
                             const CompiledFunction& top_level) {
    vm_.execute(functions, top_level);
}

void VMDebugAdapter::set_debug_callbacks(DebugCallbacks callbacks) {
    vm_.set_debug_callbacks(std::move(callbacks));
}

// ─── IVMIntrospection ───

VMIntrospector VMDebugAdapter::introspector() const {
    return VMIntrospector(vm_);
}

std::size_t VMDebugAdapter::frame_count() const {
    return introspector().frame_count();
}

FrameLocation VMDebugAdapter::frame_location(std::size_t frame_index) const {
    return introspector().frame_location(frame_index);
}

std::vector<FrameLocation> VMDebugAdapter::stack_trace() const {
    return introspector().stack_trace();
}

std::vector<LocalVariable> VMDebugAdapter::locals(std::size_t frame_index) const {
    return introspector().locals(frame_index);
}

std::optional<LocalVariable> VMDebugAdapter::find_local(std::size_t frame_index,
                                                        const std::string& name) const {
    return introspector().find_local(frame_index, name);
}

std::vector<UpvalueVariable> VMDebugAdapter::upvalues(std::size_t frame_index) const {
    return introspector().upvalues(frame_index);
}

bool VMDebugAdapter::has_upvalues(std::size_t frame_index) const {
    return introspector().has_upvalues(frame_index);
}

FrameLocation VMDebugAdapter::current_location() const {
    return introspector().current_location();
}

std::size_t VMDebugAdapter::depth() const {
    return introspector().depth();
}

} // namespace luma::dap
