#include "runtime/stdlib/system/resource_module.hpp"

#include <iostream>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

// Call cleanup_fn with resource and log (but do not rethrow) any exception.
// Used to guarantee cleanup runs even when the body throws.
void safe_cleanup(const Value& cleanup_fn, const Value& resource, std::string_view label,
                  const SourceLocation& loc) {
    std::vector<Value> cleanup_args{resource};

    try {
        (void)invoke_callable(cleanup_fn, cleanup_args, loc);
    } catch (const std::exception& e) {
        std::cerr << label << " cleanup function failed: " << e.what() << "\n";
    } catch (...) {
        std::cerr << label << " cleanup function failed with unknown error\n";
    }
}

// Invoke body_fn(resource) and always run cleanup_fn(resource) afterwards.
// If the body throws, cleanup is attempted through safe_cleanup (whose own
// failure is logged, not rethrown) and the original error propagates. If the
// body succeeds, cleanup runs directly so any error it raises reaches the
// caller. `label` names the calling function for the swallowed-cleanup log.
[[nodiscard]] Value run_with_cleanup(const Value& resource, const Value& body_fn,
                                     const Value& cleanup_fn, std::string_view label,
                                     const SourceLocation& loc) {
    std::vector<Value> body_args{resource};
    Value body_result;

    try {
        body_result = invoke_callable(body_fn, body_args, loc);
    } catch (...) {
        safe_cleanup(cleanup_fn, resource, label, loc);

        throw;
    }

    std::vector<Value> cleanup_args{resource};

    (void)invoke_callable(cleanup_fn, cleanup_args, loc);

    return body_result;
}

} // namespace

void register_resource_ns(const EnvPtr& env) {
    ModuleBuilder{"Resource", env}
        .func("with", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& resource = args[0];
            const auto& body_fn = args[1];
            const auto& cleanup_fn = args[2];

            expect_callable(body_fn, "Resource.with (body)", loc);
            expect_callable(cleanup_fn, "Resource.with (cleanup)", loc);

            return run_with_cleanup(resource, body_fn, cleanup_fn, "Resource.with:", loc);
        })
        .func("using", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& acquire_fn = args[0];
            const auto& body_fn = args[1];
            const auto& release_fn = args[2];

            expect_callable(acquire_fn, "Resource.using (acquire)", loc);
            expect_callable(body_fn, "Resource.using (body)", loc);
            expect_callable(release_fn, "Resource.using (release)", loc);

            // Acquire — if this throws, no cleanup is needed.
            std::vector<Value> no_args;
            const auto resource = invoke_callable(acquire_fn, no_args, loc);

            return run_with_cleanup(resource, body_fn, release_fn, "Resource.using:", loc);
        })
        .func("with_all", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& resources = expect_array(args[0], "Resource.with_all (resources)", loc);
            const auto& body_fn = args[1];
            const auto& cleanup_fn = args[2];

            expect_callable(body_fn, "Resource.with_all (body)", loc);
            expect_callable(cleanup_fn, "Resource.with_all (cleanup)", loc);

            // Clean up every resource in reverse acquisition order. Each cleanup
            // goes through safe_cleanup so a failing cleanup is swallowed and the
            // remaining resources are still released.
            const auto cleanup_all = [&]() {
                const auto& elements = *resources->elements;

                for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
                    safe_cleanup(cleanup_fn, *it, "Resource.with_all:", loc);
                }
            };

            std::vector<Value> body_args{args[0]};
            Value body_result;

            try {
                body_result = invoke_callable(body_fn, body_args, loc);
            } catch (...) {
                cleanup_all();

                throw;
            }

            cleanup_all();

            return body_result;
        });
}

} // namespace luma
