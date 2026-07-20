// VMStackAPI seam tests.
//
// Proves the central goal of the VMStackAPI refactor: bytecode-handler logic
// that depends only on the VMStackAPI interface can be exercised against a
// lightweight mock — no full VM instance required.  The free functions
// `handler_dup` / `handler_swap` below have the exact shape dispatch handlers
// take once migrated onto the seam (see TODO(refactor/V1) in vm.hpp), and the
// tests drive them through a stub implementation of the interface.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_stack.hpp"
#include "runtime/vm/vm_stack_api.hpp"
#include "test_framework.hpp"

using namespace luma;

namespace {

// ─── A minimal, VM-free implementation of the seam ───
//
// Backs the value stack with a std::vector and the operand readers with a flat
// byte buffer.  This is all a unit test needs to drive a handler.
class MockStackAPI final : public VMStackAPI {
public:
    std::vector<Value> stack;
    std::vector<std::uint8_t> bytecode;
    std::size_t ip{0};
    CallFrame frame{};

    void push(Value value) override {
        stack.push_back(std::move(value));
    }

    [[nodiscard]] Value pop() override {
        Value top = std::move(stack.back());
        stack.pop_back();
        return top;
    }

    [[nodiscard]] Value& peek(std::size_t distance = 0) override {
        return stack[stack.size() - 1 - distance];
    }

    [[nodiscard]] const CallFrame& current_frame() const override {
        return frame;
    }

    [[nodiscard]] TaskScope* current_task_scope() const noexcept override {
        return nullptr;
    }

    [[nodiscard]] std::uint8_t read_byte() override {
        return bytecode[ip++];
    }

    [[nodiscard]] std::uint16_t read_u16() override {
        const auto hi = static_cast<std::uint16_t>(bytecode[ip++]);
        const auto lo = static_cast<std::uint16_t>(bytecode[ip++]);
        return static_cast<std::uint16_t>((hi << 8) | lo);
    }

    [[nodiscard]] std::uint32_t read_u32() override {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value = (value << 8) | bytecode[ip++];
        }
        return value;
    }

    [[noreturn]] void runtime_error(std::string_view message,
                                    std::string_view /*hint*/ = {}) const override {
        throw std::runtime_error(std::string{message});
    }
};

// ─── Example handlers written against the seam (the migration shape) ───

// Op::Dup — duplicate the top of stack.
void handler_dup(VMStackAPI& s) {
    s.push(s.peek(0));
}

// Op::Swap — exchange the top two stack slots.
void handler_swap(VMStackAPI& s) {
    auto top = s.pop();
    auto below = s.pop();
    s.push(std::move(top));
    s.push(std::move(below));
}

// ─── Compile-time contract checks ───

static_assert(std::is_abstract_v<VMStackAPI>, "VMStackAPI must be a pure interface");
static_assert(std::is_base_of_v<VMStackAPI, VM>, "VM must implement the VMStackAPI seam");

} // namespace

// ─── Stack primitives through the mock ───

static void test_mock_push_pop_peek() {
    MockStackAPI s;
    ValueHash hash;

    s.push(Value{10});
    s.push(Value{20});

    ASSERT_EQ(s.stack.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(hash(s.peek(0)), hash(Value{20}));
    ASSERT_EQ(hash(s.peek(1)), hash(Value{10}));

    ASSERT_EQ(hash(s.pop()), hash(Value{20}));
    ASSERT_EQ(hash(s.pop()), hash(Value{10}));
    ASSERT_TRUE(s.stack.empty());
}

// ─── Handlers exercised with no VM present ───

static void test_handler_dup_against_mock() {
    MockStackAPI s;
    ValueHash hash;

    s.push(Value{7});
    handler_dup(s);

    ASSERT_EQ(s.stack.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(hash(s.pop()), hash(Value{7}));
    ASSERT_EQ(hash(s.pop()), hash(Value{7}));
}

static void test_handler_swap_against_mock() {
    MockStackAPI s;
    ValueHash hash;

    s.push(Value{1});
    s.push(Value{2});
    handler_swap(s);

    // After swap the original lower value is on top.
    ASSERT_EQ(hash(s.pop()), hash(Value{1}));
    ASSERT_EQ(hash(s.pop()), hash(Value{2}));
}

// ─── Operand readers ───

static void test_operand_readers() {
    MockStackAPI s;
    s.bytecode = {0xAB, 0x12, 0x34, 0x00, 0x00, 0x10, 0x00};

    ASSERT_EQ(static_cast<int>(s.read_byte()), 0xAB);
    ASSERT_EQ(static_cast<int>(s.read_u16()), 0x1234);
    ASSERT_EQ(static_cast<std::uint32_t>(s.read_u32()), static_cast<std::uint32_t>(0x00001000));
}

// ─── Context accessors ───

static void test_current_frame_and_scope() {
    MockStackAPI s;
    s.frame.slot_offset = 5;

    ASSERT_EQ(s.current_frame().slot_offset, static_cast<std::size_t>(5));
    ASSERT_TRUE(s.current_task_scope() == nullptr);
}

// ─── Error reporting ───

static void test_runtime_error_throws() {
    MockStackAPI s;
    ASSERT_THROWS(s.runtime_error("boom"));
}

static void test_handler_can_signal_error() {
    // A handler that pops an operand and rejects it via the seam's error path.
    const auto guard = [](VMStackAPI& s) {
        (void)s.pop();
        s.runtime_error("rejected");
    };

    MockStackAPI s;
    s.push(Value{1});
    ASSERT_THROWS(guard(s));
}

int main() {
    RUN(test_mock_push_pop_peek);
    RUN(test_handler_dup_against_mock);
    RUN(test_handler_swap_against_mock);
    RUN(test_operand_readers);
    RUN(test_current_frame_and_scope);
    RUN(test_runtime_error_throws);
    RUN(test_handler_can_signal_error);
    return SUMMARY();
}
