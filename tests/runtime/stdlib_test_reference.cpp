// Standard library tests: Reference.

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "stdlib_test_helpers.hpp"

namespace {

// Construct a reference cell holding an integer directly, mirroring what
// Reference.new(initial) produces.  Used by the thread-safety tests, which
// drive the real registered native functions from worker threads.
Value make_int_reference(std::int64_t initial) {
    return Value{std::make_shared<ReferenceValue>(Value{initial})};
}

// Invoke a registered native stdlib function value with the given arguments.
Value call_native(const Value& fn, std::vector<Value> args) {
    return fn.as_native_function()->function(args, SourceLocation{});
}

} // namespace

static void test_reference_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Reference.new"));
    ASSERT_TRUE(env->has("Reference.get"));
    ASSERT_TRUE(env->has("Reference.set"));
    ASSERT_TRUE(env->has("Reference.update"));
    ASSERT_TRUE(env->has("Reference.swap"));
    ASSERT_TRUE(env->has("Reference.inspect"));
}

static void test_reference_new_get() {
    const auto result = eval("Reference.new(42) |> Reference.get()");

    ASSERT_EQ(result.as_integer(), 42);
}

static void test_reference_set() {
    const auto result = eval("reference<integer> r = Reference.new(1)\n"
                             "Reference.set(r, 99)\n"
                             "Reference.get(r)\n");

    ASSERT_EQ(result.as_integer(), 99);
}

static void test_reference_shared_identity() {
    // Core feature: reference cells share identity across closure capture.
    const auto result = eval("reference<integer> r = Reference.new(0)\n"
                             "function() -> none increment = () -> {\n"
                             "    Reference.set(r, Reference.get(r) + 1)\n"
                             "}\n"
                             "increment()\n"
                             "increment()\n"
                             "increment()\n"
                             "Reference.get(r)\n");

    ASSERT_EQ(result.as_integer(), 3);
}

static void test_reference_swap() {
    const auto result = eval("reference<integer> a = Reference.new(1)\n"
                             "reference<integer> b = Reference.new(2)\n"
                             "Reference.swap(a, b)\n"
                             "Reference.get(a)\n");

    ASSERT_EQ(result.as_integer(), 2);
}

static void test_reference_to_string() {
    const auto result = eval(R"(Reference.new("hello") |> Reference.inspect())");

    ASSERT_EQ(result.as_string(), "ref(hello)");
}

static void test_reference_type_name() {
    const auto result = eval(R"(type_of(Reference.new(42)))");

    ASSERT_EQ(result.as_string(), "reference");
}

static void test_reference_update() {
    const auto result = eval("reference<integer> r = Reference.new(10)\n"
                             "Reference.update(r, (integer x) -> x + 5)\n"
                             "Reference.get(r)\n");

    ASSERT_EQ(result.as_integer(), 15);
}

// ─── Return values: set / update / swap yield none ───

static void test_reference_set_returns_none() {
    ASSERT_TRUE(eval("Reference.set(Reference.new(1), 2)").is_null());
}

static void test_reference_update_returns_none() {
    ASSERT_TRUE(eval("Reference.update(Reference.new(1), (integer x) -> x + 1)").is_null());
}

static void test_reference_swap_returns_none() {
    ASSERT_TRUE(eval("Reference.swap(Reference.new(1), Reference.new(2))").is_null());
}

// ─── Additional positive behaviours ───

static void test_reference_update_is_reentrant() {
    // Reference.update holds the cell's lock across the callback, so reading the
    // SAME cell inside the callback only works because the mutex is recursive.
    const auto result = eval("reference<integer> r = Reference.new(5)\n"
                             "Reference.update(r, (integer current) -> "
                             "Reference.get(r) + current)\n"
                             "Reference.get(r)\n");

    ASSERT_EQ(result.as_integer(), 10);
}

static void test_reference_nested_cell() {
    // A reference can hold another reference; mutating the shared inner cell is
    // visible through every alias.
    const auto result = eval("reference<integer> inner = Reference.new(1)\n"
                             "reference<reference<integer>> outer = Reference.new(inner)\n"
                             "Reference.set(Reference.get(outer), 42)\n"
                             "Reference.get(inner)\n");

    ASSERT_EQ(result.as_integer(), 42);
}

static void test_reference_holds_array() {
    const auto result = eval("reference<array<integer>> r = Reference.new([1, 2, 3])\n"
                             "Reference.set(r, [1, 2, 3, 4, 5])\n"
                             "Array.length(Reference.get(r))\n");

    ASSERT_EQ(result.as_integer(), 5);
}

static void test_reference_swap_updates_both_cells() {
    const auto result = eval("reference<integer> a = Reference.new(1)\n"
                             "reference<integer> b = Reference.new(2)\n"
                             "Reference.swap(a, b)\n"
                             "Reference.get(a) * 10 + Reference.get(b)\n");

    ASSERT_EQ(result.as_integer(), 21);
}

static void test_reference_inspect_boolean() {
    ASSERT_EQ(eval("Reference.inspect(Reference.new(true))").as_string(), "ref(true)");
}

static void test_reference_inspect_integer() {
    ASSERT_EQ(eval("Reference.inspect(Reference.new(42))").as_string(), "ref(42)");
}

static void test_reference_inspect_array() {
    ASSERT_EQ(eval("Reference.inspect(Reference.new([1, 2, 3]))").as_string(), "ref([1, 2, 3])");
}

// ─── Negative: wrong-argument-type guards (runtime, unchecked eval) ───

static void test_reference_get_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Reference.get(42)"), "reference");
}

static void test_reference_set_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Reference.set(42, 1)"), "reference");
}

static void test_reference_update_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Reference.update(42, (integer x) -> x)"), "reference");
}

static void test_reference_swap_first_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Reference.swap(42, Reference.new(1))"), "reference");
}

static void test_reference_swap_second_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Reference.swap(Reference.new(1), 42)"), "reference");
}

static void test_reference_inspect_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Reference.inspect(42)"), "reference");
}

static void test_reference_update_non_callable_throws() {
    // The second argument must be callable; a non-callable must be rejected
    // after the reference is validated.
    ASSERT_THROWS(eval("Reference.update(Reference.new(1), 42)"));
}

static void test_reference_update_error_preserves_value() {
    // A throwing update callback must leave the stored value untouched: the
    // write happens only after the callback returns successfully.
    const auto result = eval("reference<integer> r = Reference.new(7)\n"
                             "try {\n"
                             "    Reference.update(r, (integer x) -> {\n"
                             "        assert(false, \"boom\")\n"
                             "        return x\n"
                             "    })\n"
                             "} catch(e) {}\n"
                             "Reference.get(r)\n");

    ASSERT_EQ(result.as_integer(), 7);
}

// ─── Thread safety ───

static void test_reference_concurrent_access_is_thread_safe() {
    // Many threads hammering one cell through Reference.set / Reference.get must
    // never crash, race (under TSan), or expose a torn value.
    const auto env = luma::test::make_std_env();
    const auto set_fn = env->get("Reference.set", SourceLocation{});
    const auto get_fn = env->get("Reference.get", SourceLocation{});

    const auto ref = make_int_reference(0);

    constexpr int thread_count = 8;
    constexpr int iterations = 4000;

    std::atomic<bool> observed_torn{false};
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    for (int t = 0; t < thread_count; ++t) {
        workers.emplace_back([&set_fn, &get_fn, &observed_torn, ref, t]() {
            const auto tag = static_cast<std::int64_t>(t);

            for (int i = 0; i < iterations; ++i) {
                call_native(set_fn, {ref, Value{tag}});

                const auto seen = call_native(get_fn, {ref}).as_integer();
                if (seen < 0 || seen >= thread_count) {
                    observed_torn.store(true);
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    ASSERT_FALSE(observed_torn.load());

    const auto final_value = call_native(get_fn, {ref}).as_integer();
    ASSERT_TRUE(final_value >= 0 && final_value < thread_count);
}

static void test_reference_swap_avoids_deadlock_under_contention() {
    // Two threads swap the same pair of cells in OPPOSITE argument order.
    // Reference.swap locks both cells with a single multi-mutex
    // std::scoped_lock, whose deadlock-avoidance algorithm (as if by
    // std::lock) makes this textbook lock-ordering hazard safe; the test
    // terminating at all proves the two mutexes are never acquired in a
    // conflicting order.
    const auto env = luma::test::make_std_env();
    const auto swap_fn = env->get("Reference.swap", SourceLocation{});

    const auto a = make_int_reference(1);
    const auto b = make_int_reference(2);

    constexpr int iterations = 5000;

    const auto worker = [&swap_fn](Value first, Value second) {
        for (int i = 0; i < iterations; ++i) {
            call_native(swap_fn, {first, second});
        }
    };

    std::thread t1{worker, a, b};
    std::thread t2{worker, b, a};
    t1.join();
    t2.join();

    const auto va = a.as_reference()->get().as_integer();
    const auto vb = b.as_reference()->get().as_integer();

    // Swap only ever exchanges the two stored values, so the pair is conserved
    // regardless of how the operations interleaved.
    ASSERT_EQ(va + vb, 3);
    ASSERT_NE(va, vb);
}

int main() {
    RUN(test_reference_module);
    RUN(test_reference_new_get);
    RUN(test_reference_set);
    RUN(test_reference_shared_identity);
    RUN(test_reference_swap);
    RUN(test_reference_to_string);
    RUN(test_reference_type_name);
    RUN(test_reference_update);
    RUN(test_reference_set_returns_none);
    RUN(test_reference_update_returns_none);
    RUN(test_reference_swap_returns_none);
    RUN(test_reference_update_is_reentrant);
    RUN(test_reference_nested_cell);
    RUN(test_reference_holds_array);
    RUN(test_reference_swap_updates_both_cells);
    RUN(test_reference_inspect_boolean);
    RUN(test_reference_inspect_integer);
    RUN(test_reference_inspect_array);
    RUN(test_reference_get_wrong_type_throws);
    RUN(test_reference_set_wrong_type_throws);
    RUN(test_reference_update_wrong_type_throws);
    RUN(test_reference_swap_first_wrong_type_throws);
    RUN(test_reference_swap_second_wrong_type_throws);
    RUN(test_reference_inspect_wrong_type_throws);
    RUN(test_reference_update_non_callable_throws);
    RUN(test_reference_update_error_preserves_value);
    RUN(test_reference_concurrent_access_is_thread_safe);
    RUN(test_reference_swap_avoids_deadlock_under_contention);
    return SUMMARY();
}
