// Standard library tests: Reference.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "stdlib_test_helpers.hpp"

using luma::ReferenceValue;
using luma::SourceLocation;
using luma::Value;
using luma::test::eval;

namespace {

// Construct a reference cell holding an integer directly, mirroring what
// Reference.new(initial) produces.  Used by the thread-safety tests, which
// drive the real registered native functions from worker threads.
[[nodiscard]] Value make_int_reference(std::int64_t initial) {
    return Value{std::make_shared<ReferenceValue>(Value{initial})};
}

// Invoke a registered native stdlib function value with the given arguments.
[[nodiscard]] Value call_native(const Value& fn, const std::vector<Value>& args) {
    return fn.as_native_function()->function(args, SourceLocation{});
}

// Start `body` on a std::jthread. An exception escaping a thread function would
// call std::terminate, so any exception `body` throws (including a failed
// assertion) is stored in `failure` for the test thread to rethrow after the
// join. `failure` must outlive the returned thread.
template <typename Body>
[[nodiscard]] std::jthread start_worker(std::exception_ptr& failure, Body body) {
    return std::jthread{[&failure, body = std::move(body)]() {
        try {
            body();
        } catch (...) {
            failure = std::current_exception();
        }
    }};
}

// Rethrow the first exception a worker captured, if any. Call only after every
// worker has been joined.
template <std::size_t N>
void rethrow_first_failure(const std::array<std::exception_ptr, N>& failures) {
    const auto failed = std::ranges::find_if(
        failures, [](const std::exception_ptr& failure) { return failure != nullptr; });

    if (failed != failures.end()) {
        std::rethrow_exception(*failed);
    }
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

// (source, expected error message) for each operation given a non-reference
// where a reference cell is required.
constexpr std::array<std::pair<std::string_view, std::string_view>, 6> wrong_type_cases{{
    {"Reference.get(42)", "Reference.get: expected reference, got 'integer'"},
    {"Reference.set(42, 1)", "Reference.set: expected reference, got 'integer'"},
    {"Reference.update(42, (integer x) -> x)",
     "Reference.update: expected reference, got 'integer'"},
    {"Reference.swap(42, Reference.new(1))", "Reference.swap: expected reference, got 'integer'"},
    {"Reference.swap(Reference.new(1), 42)", "Reference.swap: expected reference, got 'integer'"},
    {"Reference.inspect(42)", "Reference.inspect: expected reference, got 'integer'"},
}};

static void test_reference_wrong_type_throws(std::string_view source, std::string_view message) {
    ASSERT_EVAL_THROWS(std::string{source}, message);
}

static void test_reference_update_non_callable_throws() {
    // The second argument must be callable; a non-callable must be rejected
    // after the reference is validated.
    ASSERT_EVAL_THROWS("Reference.update(Reference.new(1), 42)", "cannot call 'integer'");
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
    std::array<std::exception_ptr, thread_count> failures{};

    {
        std::vector<std::jthread> workers;
        workers.reserve(thread_count);

        for (int t = 0; t < thread_count; ++t) {
            const auto tag = static_cast<std::int64_t>(t);

            workers.push_back(start_worker(
                failures.at(static_cast<std::size_t>(t)),
                [&set_fn, &get_fn, &observed_torn, ref, tag]() {
                    for (int i = 0; i < iterations; ++i) {
                        if (!call_native(set_fn, {ref, Value{tag}}).is_null()) {
                            throw std::runtime_error{"Reference.set did not return none"};
                        }

                        const auto seen = call_native(get_fn, {ref}).as_integer();

                        if (seen < 0 || seen >= thread_count) {
                            observed_torn.store(true);
                        }
                    }
                }));
        }
    } // The jthreads join here.

    rethrow_first_failure(failures);
    ASSERT_FALSE(observed_torn.load());

    const auto final_value = call_native(get_fn, {ref}).as_integer();

    ASSERT_GE(final_value, 0);
    ASSERT_LT(final_value, thread_count);
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

    const auto swap_repeatedly = [&swap_fn](const Value& first, const Value& second) {
        return [&swap_fn, first, second]() {
            for (int i = 0; i < iterations; ++i) {
                if (!call_native(swap_fn, {first, second}).is_null()) {
                    throw std::runtime_error{"Reference.swap did not return none"};
                }
            }
        };
    };

    std::array<std::exception_ptr, 2> failures{};

    {
        const std::jthread forward{start_worker(failures[0], swap_repeatedly(a, b))};
        const std::jthread backward{start_worker(failures[1], swap_repeatedly(b, a))};
    } // The jthreads join here.

    rethrow_first_failure(failures);

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
    RUN_PARAMETERIZED(test_reference_wrong_type_throws, wrong_type_cases);
    RUN(test_reference_update_non_callable_throws);
    RUN(test_reference_update_error_preserves_value);
    RUN(test_reference_concurrent_access_is_thread_safe);
    RUN(test_reference_swap_avoids_deadlock_under_contention);
    return SUMMARY();
}
