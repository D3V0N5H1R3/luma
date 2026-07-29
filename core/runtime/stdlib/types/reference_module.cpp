#include "runtime/stdlib/types/reference_module.hpp"

#include <format>
#include <mutex>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

void register_reference_ns(const EnvPtr& env) {
    ModuleBuilder builder{"Reference", env};

    // Reference.new(value) -> reference<T>
    // Creates a new mutable reference cell containing the given value.
    builder.func("new", 1).raw_body([](std::span<const Value> args, SourceLocation) -> Value {
        return Value{std::make_shared<ReferenceValue>(args[0])};
    });

    // Reference.get(ref) -> T
    // Returns the current value stored in the reference cell.
    // Delegates to ReferenceValue::get(), which acquires the cell's lock to
    // prevent torn reads.
    builder.func("get", 1).extract_body(
        expect_reference,
        [](const auto& ref, const Args&, SourceLocation) -> Value { return ref->get(); });

    // Reference.set(ref, value) -> none
    // Updates the value stored in the reference cell.
    // Delegates to ReferenceValue::set(), which acquires the cell's lock to
    // prevent torn writes.
    builder.func("set", 2).extract_body(
        expect_reference, [](const auto& ref, const Args& args, SourceLocation) -> Value {
            ref->set(args[1]);

            return Value{NullValue{}};
        });

    // Reference.update(ref, fn) -> none
    // Applies a function to the current value and stores the result.
    // The read-modify-write is performed under the reference's lock.
    builder.func("update", 2)
        .extract_body(expect_reference,
                      [](const auto& ref, const Args& args, SourceLocation loc) -> Value {
                          // Hold the lock for the entire read-modify-write to ensure atomicity.
                          const std::scoped_lock lock{ref->mutex};

                          auto current = *ref->value;
                          auto call_args = std::vector<Value>{std::move(current)};
                          auto new_value = invoke_callable(args[1], call_args, loc);

                          *ref->value = std::move(new_value);

                          return Value{NullValue{}};
                      });

    // Reference.update_and_get(ref, fn) -> T
    // Applies fn to the current value, stores the result, and returns it.
    builder.func("update_and_get", 2)
        .extract_body(expect_reference,
                      [](const auto& ref, const Args& args, SourceLocation loc) -> Value {
                          const std::scoped_lock lock{ref->mutex};

                          auto current = *ref->value;
                          auto call_args = std::vector<Value>{std::move(current)};
                          auto new_value = invoke_callable(args[1], call_args, loc);

                          *ref->value = new_value;

                          return new_value;
                      });

    // Reference.get_and_update(ref, fn) -> T
    // Applies fn to the current value and stores the result, returning the
    // value the cell held before the update.
    builder.func("get_and_update", 2)
        .extract_body(expect_reference,
                      [](const auto& ref, const Args& args, SourceLocation loc) -> Value {
                          const std::scoped_lock lock{ref->mutex};

                          auto old_value = *ref->value;
                          auto call_args = std::vector<Value>{old_value};
                          auto new_value = invoke_callable(args[1], call_args, loc);

                          *ref->value = std::move(new_value);

                          return old_value;
                      });

    // Reference.get_and_set(ref, value) -> T
    // Stores a new value and returns the value the cell held before.
    builder.func("get_and_set", 2)
        .extract_body(expect_reference,
                      [](const auto& ref, const Args& args, SourceLocation) -> Value {
                          const std::scoped_lock lock{ref->mutex};

                          auto old_value = *ref->value;
                          *ref->value = args[1];

                          return old_value;
                      });

    // Reference.equals(a, b) -> boolean
    // Whether the two cells currently hold equal values (structural equality).
    builder.func("equals", 2).raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
        const auto& ref_a = expect_reference(args[0], "Reference.equals", loc);
        const auto& ref_b = expect_reference(args[1], "Reference.equals", loc);

        return Value{ref_a->get().equals(ref_b->get())};
    });

    // Reference.same(a, b) -> boolean
    // Whether the two arguments refer to the same cell (identity).
    builder.func("same", 2).raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
        const auto& ref_a = expect_reference(args[0], "Reference.same", loc);
        const auto& ref_b = expect_reference(args[1], "Reference.same", loc);

        return Value{ref_a.get() == ref_b.get()};
    });

    // Reference.swap(ref1, ref2) -> none
    // Swaps the values stored in two reference cells.
    builder.func("swap", 2).raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
        const auto& ref_a = expect_reference(args[0], "Reference.swap", loc);
        const auto& ref_b = expect_reference(args[1], "Reference.swap", loc);

        if (ref_a.get() == ref_b.get()) {
            return Value{NullValue{}};
        }

        // A multi-mutex std::scoped_lock uses a deadlock-avoidance algorithm
        // (as if by std::lock), so the two cells can be locked in any order.
        const std::scoped_lock lock{ref_a->mutex, ref_b->mutex};
        std::swap(*ref_a->value, *ref_b->value);

        return Value{NullValue{}};
    });

    // Reference.inspect(ref) -> string
    // Returns a string representation of the reference cell.
    builder.func("inspect", 1)
        .extract_body(expect_reference, [](const auto& ref, const Args&, SourceLocation) -> Value {
            return Value{std::format("ref({})", ref->get().to_string())};
        });
}

} // namespace luma
