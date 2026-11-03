#include "runtime/vm/vm_exception_handler.hpp"

#include <cstdint>
#include <vector>

#include "common/byte_utils.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/opcode_metadata.hpp"
#include "runtime/compiler/verifier.hpp"

namespace luma {

namespace {

// Control-flow state reaching an instruction during the cleanup-handler walk.
// `depth` counts the values above the handler's base stack depth, so the
// handler's own error value sits at depth 1.  `previous` is the instruction
// that fell through to `offset` (Op::Jump for a jump edge).
struct HandlerWalkState {
    std::size_t offset;
    int depth;
    Op previous;
};

// Values a ForIterStep / ForIterStepKV does not push when the iterator is
// exhausted, so the JumpIfFalse after it reaches the loop exit with fewer
// values (mirrors the bytecode verifier's stack-depth walk).
[[nodiscard]] int values_missing_when_exhausted(Op op) noexcept {
    if (op == Op::ForIterStep) {
        return 1;
    }

    if (op == Op::ForIterStepKV) {
        return 2;
    }

    return 0;
}

// Per-instruction visit budget: compiled code reaches an instruction with one
// stack depth (two at a loop exit), so this only bounds crafted bytecode.
constexpr std::size_t k_handler_walk_visits_per_byte = 4;

} // namespace

bool is_cleanup_handler(const std::vector<std::uint8_t>& code, std::size_t catch_offset) {
    std::vector<int> visited_depth(code.size(), -1);
    std::vector<HandlerWalkState> pending{
        {.offset = catch_offset, .depth = 1, .previous = Op::Jump}};
    const std::size_t step_budget = (code.size() * k_handler_walk_visits_per_byte) + 1;
    std::size_t steps = 0;

    while (!pending.empty()) {
        const auto [offset, depth, previous] = pending.back();
        pending.pop_back();

        // A path that popped the handler's error value has left the handler.
        if (depth < 1 || offset >= code.size() || visited_depth[offset] == depth) {
            continue;
        }

        if (code[offset] > static_cast<std::uint8_t>(Op::EndModule) ||
            offset + instruction_size(code, offset) > code.size()) {
            continue; // Invalid or truncated instruction: not a handler path.
        }

        if (++steps > step_budget) {
            return false;
        }

        visited_depth[offset] = depth;

        const auto op = static_cast<Op>(code[offset]);

        if (op == Op::Rethrow) {
            // Re-raising the handler's own error value (nothing above it)
            // marks a cleanup handler; a Rethrow at any other depth belongs to
            // an enclosing finally that this path reached.
            if (depth == 1) {
                return true;
            }

            continue;
        }

        const auto effect = instruction_stack_effect(code, offset);
        const int after = depth - effect.pops + effect.pushes;
        const auto next = offset + instruction_size(code, offset);

        if (depth - effect.pops < 1) {
            continue; // Consumes the error value: the handler ends here.
        }

        switch (op) {
            case Op::Return:
            case Op::GetLocalReturn:
            case Op::TailCall:
            case Op::EndModule:
                break;

            case Op::Jump:
                pending.push_back({.offset = Chunk::resolve_forward_jump_target(
                                       offset, read_u32_be(&code[offset + 1])),
                                   .depth = after,
                                   .previous = Op::Jump});
                break;

            case Op::Loop: {
                const auto back = static_cast<std::size_t>(read_u32_be(&code[offset + 1]));

                if (back <= next) {
                    pending.push_back({.offset = Chunk::resolve_loop_jump_target(offset, back),
                                       .depth = after,
                                       .previous = Op::Jump});
                }

                break;
            }

            case Op::JumpIfFalse:
            case Op::JumpIfTrue:
            case Op::NullCoalesce: {
                const int missing =
                    op == Op::JumpIfFalse ? values_missing_when_exhausted(previous) : 0;

                pending.push_back({.offset = next, .depth = after, .previous = op});
                pending.push_back({.offset = Chunk::resolve_forward_jump_target(
                                       offset, read_u32_be(&code[offset + 1])),
                                   .depth = after - missing,
                                   .previous = Op::Jump});
                break;
            }

            default:
                // Includes TryCatch: only its normal (fall-through) edge is
                // followed — another handler's code is entered only by an
                // exception.
                pending.push_back({.offset = next, .depth = after, .previous = op});
                break;
        }
    }

    return false;
}

void VMExceptionHandler::push(ExceptionHandler handler) {
    manager_.push_handler(handler);
}

ExceptionHandler VMExceptionHandler::pop() {
    return manager_.pop_handler();
}

void VMExceptionHandler::discard() {
    manager_.pop_handler_discard();
}

bool VMExceptionHandler::has_handler_for(std::size_t base_depth) const {
    return manager_.has_handler_for(base_depth);
}

const ExceptionHandler& VMExceptionHandler::current() const {
    return manager_.current();
}

bool VMExceptionHandler::empty() const noexcept {
    return manager_.empty();
}

std::size_t VMExceptionHandler::size() const noexcept {
    return manager_.size();
}

void VMExceptionHandler::clear() noexcept {
    manager_.clear();
}

} // namespace luma
