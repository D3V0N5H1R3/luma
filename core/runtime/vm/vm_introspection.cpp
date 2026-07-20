#include "runtime/vm/vm_introspection.hpp"

#include <format>

#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"

namespace luma {

namespace {

// Mirror of get_upvalue_cell() in vm_dispatch_control_flow.cpp.  A mutable
// captured variable lives in the closure's heap cell (upvalue_cells); an
// immutable capture is stored inline in the upvalues array.  Returns the cell
// backing the upvalue at `index`, or nullptr when the capture is inline.
[[nodiscard]] Value* upvalue_cell(const FunctionValue& closure, std::size_t index) {
    if (index < closure.upvalue_cells.size() && closure.upvalue_cells[index]) {
        return closure.upvalue_cells[index].get();
    }
    return nullptr;
}

} // namespace

VMIntrospector::VMIntrospector(const VM& vm) : vm_(vm) {}

std::size_t VMIntrospector::frame_count() const {
    return vm_.frames().size();
}

std::size_t VMIntrospector::depth() const {
    return vm_.frames().size();
}

FrameLocation VMIntrospector::frame_location(std::size_t frame_index) const {
    const auto& frames = vm_.frames();

    if (frame_index >= frames.size()) {
        return {};
    }

    const auto& cf = frames[frame_index];

    if (cf.function == nullptr) {
        return {};
    }

    auto offset = static_cast<std::size_t>(cf.ip - cf.function->chunk().code.data());
    auto loc = cf.function->chunk().location_at(offset > 0 ? offset - 1 : 0);

    return FrameLocation{
        .function_name = cf.function->name,
        .file_id = loc.file_id,
        .line = loc.line,
        .column = loc.column,
    };
}

FrameLocation VMIntrospector::current_location() const {
    const auto& frames = vm_.frames();

    if (frames.empty()) {
        return {};
    }

    return frame_location(frames.size() - 1);
}

std::vector<FrameLocation> VMIntrospector::stack_trace() const {
    const auto& frames = vm_.frames();
    const auto frame_count = frames.size();
    std::vector<FrameLocation> result;
    result.reserve(frame_count);

    for (int i = static_cast<int>(frame_count) - 1; i >= 0; --i) {
        result.push_back(frame_location(static_cast<std::size_t>(i)));
    }

    return result;
}

VMIntrospector::SlotRange VMIntrospector::slot_range(std::size_t frame_index) const {
    const auto& frames = vm_.frames();
    const auto stack = vm_.stack();

    if (frame_index >= frames.size()) {
        return {};
    }

    const std::size_t start = frames[frame_index].slot_offset;
    std::size_t end = stack.size();

    if (frame_index + 1 < frames.size()) {
        end = frames[frame_index + 1].slot_offset;
    }

    return {.start = start, .end = end};
}

std::vector<LocalVariable> VMIntrospector::locals(std::size_t frame_index) const {
    const auto& frames = vm_.frames();
    const auto stack = vm_.stack();

    if (frame_index >= frames.size() || (frames[frame_index].function == nullptr)) {
        return {};
    }

    const auto& cf = frames[frame_index];
    const auto& local_names = cf.function->debug_info.local_names;
    const auto& local_mutable = cf.function->debug_info.local_mutable;
    auto [slot_start, slot_end] = slot_range(frame_index);
    const std::size_t num_slots = slot_end > slot_start ? slot_end - slot_start : 0;

    std::vector<LocalVariable> result;

    for (std::size_t i = 0; i < num_slots && (slot_start + i) < stack.size(); ++i) {
        std::string name;

        if (i < local_names.size()) {
            name = local_names[i];
        }

        if (name.empty() || name == "_") {
            continue;
        }

        const bool mutable_var = (i < local_mutable.size()) && local_mutable[i];
        result.push_back(LocalVariable{
            .name = std::move(name), .value = stack[slot_start + i], .is_mutable = mutable_var});
    }

    return result;
}

std::optional<LocalVariable> VMIntrospector::find_local(std::size_t frame_index,
                                                        const std::string& name) const {
    const auto& frames = vm_.frames();
    const auto stack = vm_.stack();

    if (frame_index >= frames.size() || (frames[frame_index].function == nullptr)) {
        return std::nullopt;
    }

    const auto& cf = frames[frame_index];
    const auto& local_names = cf.function->debug_info.local_names;
    const auto& local_mutable = cf.function->debug_info.local_mutable;
    const std::size_t slot_start = cf.slot_offset;

    for (std::size_t i = 0; i < local_names.size(); ++i) {
        if (local_names[i] == name) {
            const std::size_t slot = slot_start + i;

            if (slot < stack.size()) {
                const bool mutable_var = (i < local_mutable.size()) && local_mutable[i];
                return LocalVariable{.name = name, .value = stack[slot], .is_mutable = mutable_var};
            }

            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::vector<std::string> VMIntrospector::resolve_upvalue_names(std::size_t frame_index) const {
    const auto& frames = vm_.frames();

    if (frame_index >= frames.size()) {
        return {};
    }

    const auto& cf = frames[frame_index];

    if ((cf.function == nullptr) || cf.function->upvalues.empty()) {
        return {};
    }

    const auto& descriptors = cf.function->upvalues;
    std::vector<std::string> names;

    for (int pi = static_cast<int>(frame_index) - 1; pi >= 0 && names.empty(); --pi) {
        const auto& parent = frames[static_cast<std::size_t>(pi)];

        if (parent.function != nullptr) {
            for (std::size_t i = 0; i < descriptors.size(); ++i) {
                if (descriptors[i].is_local &&
                    descriptors[i].index < parent.function->debug_info.local_names.size()) {
                    names.push_back(parent.function->debug_info.local_names[descriptors[i].index]);
                } else {
                    names.push_back(std::format("[upvalue {}]", i));
                }
            }
        }
    }

    return names;
}

std::vector<bool> VMIntrospector::resolve_upvalue_mutability(std::size_t frame_index) const {
    const auto& frames = vm_.frames();

    if (frame_index >= frames.size()) {
        return {};
    }

    const auto& cf = frames[frame_index];

    if ((cf.function == nullptr) || cf.function->upvalues.empty()) {
        return {};
    }

    const auto& descriptors = cf.function->upvalues;
    std::vector<bool> result(descriptors.size(), false);

    for (int pi = static_cast<int>(frame_index) - 1; pi >= 0; --pi) {
        const auto& parent = frames[static_cast<std::size_t>(pi)];

        if (parent.function != nullptr) {
            for (std::size_t i = 0; i < descriptors.size(); ++i) {
                if (descriptors[i].is_local &&
                    descriptors[i].index < parent.function->debug_info.local_mutable.size()) {
                    result[i] = parent.function->debug_info.local_mutable[descriptors[i].index];
                }
            }

            break;
        }
    }

    return result;
}

bool VMIntrospector::has_upvalues(std::size_t frame_index) const {
    const auto& frames = vm_.frames();

    if (frame_index >= frames.size()) {
        return false;
    }

    const auto& cf = frames[frame_index];
    return (cf.closure != nullptr) && !cf.closure->upvalues.empty();
}

std::vector<UpvalueVariable> VMIntrospector::upvalues(std::size_t frame_index) const {
    const auto& frames = vm_.frames();

    if (frame_index >= frames.size()) {
        return {};
    }

    const auto& cf = frames[frame_index];

    if ((cf.closure == nullptr) || cf.closure->upvalues.empty()) {
        return {};
    }

    const auto& uv_values = cf.closure->upvalues;
    auto names = resolve_upvalue_names(frame_index);
    auto mutability = resolve_upvalue_mutability(frame_index);

    std::vector<UpvalueVariable> result;
    result.reserve(uv_values.size());

    for (std::size_t i = 0; i < uv_values.size(); ++i) {
        std::string name = (i < names.size()) ? names[i] : std::format("[upvalue {}]", i);
        const bool is_mutable = (i < mutability.size()) && mutability[i];

        // Mutable captures live in a heap cell; read through it when present so
        // the debugger shows the live value instead of the unused inline slot.
        const Value* cell = upvalue_cell(*cf.closure, i);
        const Value& value = (cell != nullptr) ? *cell : uv_values[i];

        result.push_back(
            UpvalueVariable{.name = std::move(name), .value = value, .is_mutable = is_mutable});
    }

    return result;
}

bool VMIntrospector::set_local(VM& vm, std::size_t frame_index, const std::string& name,
                               const Value& new_value) {
    const auto& frames = vm.frames();

    if (frame_index >= frames.size() || (frames[frame_index].function == nullptr)) {
        return false;
    }

    const auto& cf = frames[frame_index];
    const auto& local_names = cf.function->debug_info.local_names;
    const auto& local_mutable = cf.function->debug_info.local_mutable;
    const std::size_t slot_start = cf.slot_offset;
    auto stack = vm.stack_mut();

    for (std::size_t i = 0; i < local_names.size(); ++i) {
        if (local_names[i] == name) {
            if (i < local_mutable.size() && !local_mutable[i]) {
                return false; // Immutable.
            }

            const std::size_t slot = slot_start + i;

            if (slot < stack.size()) {
                stack[slot] = new_value;
                return true;
            }

            return false;
        }
    }

    return false;
}

bool VMIntrospector::set_upvalue(VM& vm, std::size_t frame_index, const std::string& name,
                                 const Value& new_value) {
    const auto& frames = vm.frames();

    if (frame_index >= frames.size()) {
        return false;
    }

    const auto& cf = frames[frame_index];

    if (cf.closure == nullptr) {
        return false;
    }

    auto& uv_values = cf.closure->upvalues;
    const VMIntrospector intro(vm);
    auto names = intro.resolve_upvalue_names(frame_index);
    auto mutability = intro.resolve_upvalue_mutability(frame_index);

    for (std::size_t i = 0; i < uv_values.size(); ++i) {
        const std::string uv_name = (i < names.size()) ? names[i] : std::format("[upvalue {}]", i);

        if (uv_name == name) {
            const bool is_mutable = (i < mutability.size()) && mutability[i];

            if (!is_mutable) {
                return false; // Immutable.
            }

            // Mutable captures live in a shared heap cell; write through it so
            // the running program observes the edit.  Fall back to the inline
            // array only when no cell backs this upvalue.
            if (Value* cell = upvalue_cell(*cf.closure, i)) {
                *cell = new_value;
            } else {
                uv_values[i] = new_value;
            }

            return true;
        }
    }

    return false;
}

} // namespace luma
