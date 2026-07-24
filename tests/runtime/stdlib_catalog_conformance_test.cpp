// Catalog conformance test — verifies that every function in the stdlib
// catalog is actually registered at runtime and that the declared arity
// matches the runtime arity where testable.

#include <iostream>
#include <set>
#include <string>

#include "runtime/interpreter/environment.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "shared_eval.hpp"
#include "stdlib/stdlib_catalog.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Tests ───

static void test_all_catalog_entries_are_registered() {
    // Create a fully-registered environment.
    const auto env = luma::test::make_std_env();

    int missing_count = 0;

    for (const auto& [name, spec] : stdlib::catalog()) {
        if (!env->has(name)) {
            std::cout << "  MISSING: " << name << "\n";
            ++missing_count;
        }
    }

    ASSERT_EQ(missing_count, 0);
}

static void test_all_registered_functions_are_in_catalog() {
    // Create a fully-registered environment.
    const auto env = luma::test::make_std_env();

    const auto& cat = stdlib::catalog();

    // Stdlib choice variants are registered as globals but are not catalog
    // function entries: unit variants (e.g. "Log.Level.Info") as field-less
    // choice values, and payload-bearing variants (e.g. "Log.Output.File",
    // "Json.Value.JsonString") as native constructor functions.  Collect every
    // variant's fully-qualified name so both forms are skipped by name rather
    // than relying on the value kind (which no longer distinguishes the
    // constructor form from an ordinary stdlib function).
    std::set<std::string> choice_variant_names;
    for (const auto& [qualified, ch] : stdlib_choice_types()) {
        for (const auto& variant : ch->variants) {
            choice_variant_names.insert(qualified + "." + variant.name);
        }
    }

    int uncataloged_count = 0;

    // Collect all bindings that look like stdlib functions (contain a dot).
    env->for_each_binding([&](const std::string& name, const Value& value) {
        if (name.find('.') == std::string::npos) {
            return; // Skip non-namespaced builtins (print, assert, etc.)
        }

        if (!cat.contains(name)) {
            // Skip choice variants (unit or payload constructor) by name, and
            // still skip any other choice-valued global defensively.
            if (value.is_choice() || choice_variant_names.contains(name)) {
                return;
            }

            std::cout << "  UNCATALOGED: " << name << "\n";
            ++uncataloged_count;
        }
    });

    ASSERT_EQ(uncataloged_count, 0);
}

static void test_catalog_constants_are_not_callable() {
    const auto env = luma::test::make_std_env();

    const auto& consts = stdlib::constants();

    for (const auto& name : consts) {
        if (!env->has(name)) {
            continue; // Some constants may be conditionally compiled.
        }

        const auto val = env->get(name, {});

        ASSERT_FALSE(val.is_native_function());
    }
}

static void test_catalog_functions_are_callable() {
    const auto env = luma::test::make_std_env();

    const auto& cat = stdlib::catalog();
    int non_callable_count = 0;

    for (const auto& [name, spec] : cat) {
        if (spec.is_constant) {
            continue;
        }

        if (!env->has(name)) {
            continue; // Conditionally compiled.
        }

        const auto val = env->get(name, {});

        if (!val.is_native_function()) {
            std::cout << "  NOT CALLABLE: " << name << "\n";
            ++non_callable_count;
        }
    }

    ASSERT_EQ(non_callable_count, 0);
}

static void test_sandbox_blocked_modules_are_correct() {
    const auto& blocked = stdlib::sandbox_blocked_modules();

    // These modules should be blocked in sandbox mode.
    ASSERT_TRUE(blocked.contains("Console"));
    ASSERT_TRUE(blocked.contains("FileSystem"));
    ASSERT_TRUE(blocked.contains("Process"));
    ASSERT_TRUE(blocked.contains("Socket"));
    ASSERT_TRUE(blocked.contains("Http"));
    ASSERT_TRUE(blocked.contains("Csv"));
    ASSERT_TRUE(blocked.contains("Xml"));
    ASSERT_TRUE(blocked.contains("KeyValueStore"));

    // These modules should NOT be blocked.
    ASSERT_FALSE(blocked.contains("Math"));
    ASSERT_FALSE(blocked.contains("String"));
    ASSERT_FALSE(blocked.contains("Array"));
    ASSERT_FALSE(blocked.contains("Result"));
}

static void test_capability_flags_are_set() {
    const auto& cat = stdlib::catalog();

    // Spot-check capability assignments.
    using C = stdlib::Capability;

    auto check = [&](const std::string& name, C expected) {
        auto it = cat.find(name);
        ASSERT_TRUE(it != cat.end());
        ASSERT_EQ(static_cast<uint16_t>(it->second.capabilities), static_cast<uint16_t>(expected));
    };

    check("Console.prompt", C::Console);
    check("FileSystem.read_file", C::FileSystem);
    check("Process.run", C::Process);
    check("Socket.connect", C::Network);
    check("Http.get", C::Network);
    check("Math.absolute", C::None);
    check("String.length", C::None);
    check("Array.map", C::None);
}

static void test_result_error_code_accessor() {
    // Verify that Result.error_code and Result.source_function exist.
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Result.error_code"));
    ASSERT_TRUE(env->has("Result.source_function"));
}

static void test_catalog_arity_is_enforced() {
    const auto env = luma::test::make_std_env();

    const auto& cat = stdlib::catalog();
    int failures = 0;

    for (const auto& [name, spec] : cat) {
        if (spec.is_constant) {
            continue;
        }

        if (!env->has(name)) {
            continue;
        }

        // Skip functions that accept 0 args — they would succeed or fail
        // for non-arity reasons.
        if (spec.get_min_arity() == 0) {
            continue;
        }

        const auto val = env->get(name, {});

        if (!val.is_native_function()) {
            continue;
        }

        // Call with zero args — should get an arity error.
        try {
            std::vector<Value> empty_args;
            val.as_native_function()->function(empty_args, SourceLocation{});

            // If we get here, the function accepted 0 args despite claiming
            // it needs more.  That's an arity enforcement failure.
            std::cout << "  ARITY NOT ENFORCED: " << name << " (declared " << spec.arity
                      << " args, accepted 0)\n";
            ++failures;
        } catch (const RuntimeError&) { // NOLINT(bugprone-empty-catch)
            // Expected — arity is enforced.
        } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
            // Other exceptions are also OK — they may be from trying to
            // access the missing arguments.
        }
    }

    ASSERT_EQ(failures, 0);
}

// ─── Main ───

int main() {
    RUN(test_all_catalog_entries_are_registered);
    RUN(test_all_registered_functions_are_in_catalog);
    RUN(test_catalog_constants_are_not_callable);
    RUN(test_catalog_functions_are_callable);
    RUN(test_sandbox_blocked_modules_are_correct);
    RUN(test_capability_flags_are_set);
    RUN(test_result_error_code_accessor);
    RUN(test_catalog_arity_is_enforced);

    return SUMMARY();
}
