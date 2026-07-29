#include "runtime/stdlib/concurrency/task_module.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/concurrency/cancellation_token.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/concurrency/concurrency_constants.hpp"

namespace luma {

namespace {

[[nodiscard]] std::shared_ptr<TaskValue> expect_task(const Value& v, std::string_view name,
                                                     const SourceLocation& loc) {
    if (!v.is_task()) {
        throw RuntimeError{
            std::format("{}: expected a task, got '{}'", name, v.display_type_name()), loc,
            "pass a task created with spawn or Task functions"};
    }

    return v.as_task();
}

// Await a task and translate the outcome into a result<T> Value, centralising
// the cancellation/error handling shared by the scalar combinators (race,
// timeout, map, flat_map, any).  `produce` performs the await and returns a
// fully-formed result Value; a CancelledException becomes a failure carrying
// `cancelled_detail`, and any other std::exception becomes a failure built from
// its message.  Kept Task-specific (rather than reusing apply_with_error_handling)
// so the "…was cancelled" wording survives instead of collapsing into e.what().
template <typename Produce>
[[nodiscard]] Value await_to_result(std::string_view cancelled_detail, Produce produce) {
    try {
        return produce();
    } catch (const CancelledException&) {
        return make_failure_value(std::string{cancelled_detail});
    } catch (const std::exception& e) {
        return failure_from_exception(e);
    }
}

// Await a task whose value is collected into a results array, applying the same
// cancellation/error translation as await_to_result.  `collect` performs the
// await and appends to the array; on failure `on_failure` runs first (typically
// cancel_remaining_tasks) before the failure Value is produced.  Returns
// std::nullopt on success, or the failure Value the caller should return.
template <typename Collect, typename OnFailure>
[[nodiscard]] std::optional<Value> collect_awaited(std::string_view cancelled_detail,
                                                   Collect collect, OnFailure on_failure) {
    try {
        collect();
        return std::nullopt;
    } catch (const CancelledException&) {
        on_failure();
        return make_failure_value(std::string{cancelled_detail});
    } catch (const std::exception& e) {
        on_failure();
        return failure_from_exception(e);
    }
}

// Cancel all tasks in the array starting from index `start`.
void cancel_remaining_tasks(std::span<const Value> elements, std::size_t start) {
    for (std::size_t j = start; j < elements.size(); ++j) {
        if (elements[j].is_task() && elements[j].as_task()->token) {
            elements[j].as_task()->token->cancel();
        }
    }
}

// Cancel all tasks in the array except the specified one.
void cancel_other_tasks(std::span<const Value> elements, const std::shared_ptr<TaskValue>& except) {
    for (const auto& elem : elements) {
        if (elem.is_task() && elem.as_task() != except && elem.as_task()->token) {
            elem.as_task()->token->cancel();
        }
    }
}

// Build a task whose shared state is already resolved with `value`.  Mirrors
// the promise/shared_future machinery used by the `spawn` opcode
// (vm_dispatch_concurrency.cpp) but performs no real async work; the future is
// ready immediately.  The task carries no cancellation token (fire-and-forget).
[[nodiscard]] Value make_completed_task(Value value) {
    std::promise<Value> promise;
    promise.set_value(std::move(value));

    return Value{std::make_shared<TaskValue>(promise.get_future().share(), nullptr)};
}

// Build a task whose shared state is already resolved with a failure carrying
// `message`.  Awaiting it (via future.get()) re-throws the RuntimeError, which
// the scalar combinators translate into a failure result exactly as they do for
// a spawned task that throws.
[[nodiscard]] Value make_failed_task(std::string message) {
    std::promise<Value> promise;
    promise.set_exception(std::make_exception_ptr(RuntimeError{std::move(message)}));

    return Value{std::make_shared<TaskValue>(promise.get_future().share(), nullptr)};
}

} // namespace

void register_task_ns(const EnvPtr& env) {
    ModuleBuilder{"Task", env} // Task.all(array<task<T>> tasks) -> result<array<T>>
        // Await all tasks and return their results in order.
        .func("all", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& arr = expect_array(args[0], "Task.all", loc);

            auto results = std::make_shared<ArrayValue>();

            for (std::size_t i{0}; i < arr->elements->size(); ++i) {
                const auto& elem = (*arr->elements)[i];

                if (!elem.is_task()) {
                    // Cancel remaining tasks before reporting the type error.
                    cancel_remaining_tasks(*arr->elements, i + 1);

                    throw RuntimeError{"Task.all: array element is not a task", loc,
                                       "pass an array containing only task values"};
                }

                const auto& task = elem.as_task();

                if (auto failure = collect_awaited(
                        error_msg("Task", "all", "a task was cancelled"),
                        [&] { results->elements->push_back(task->future.get()); },
                        [&] { cancel_remaining_tasks(*arr->elements, i + 1); })) {
                    return std::move(*failure);
                }
            }

            return make_success_value(Value{std::move(results)});
        })
        // Task.race(array<task<T>> tasks) -> result<T>
        // Return the result of the first task to complete.
        .func("race", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& arr = expect_array(args[0], "Task.race", loc);

            if (auto failure =
                    check_non_empty_elements(*arr->elements, "Task", "race", "task",
                                             [](const Value& elem) { return elem.is_task(); })) {
                return std::move(*failure);
            }

            // Poll tasks until one is ready, with exponential backoff
            // to avoid burning CPU while waiting.
            BackoffTimer backoff;

            while (true) {
                for (const auto& elem : *arr->elements) {
                    const auto& task = elem.as_task();

                    // Use zero-wait to check readiness without blocking,
                    // so all tasks are polled fairly each cycle.
                    if (task->future.wait_for(std::chrono::milliseconds{0}) ==
                        std::future_status::ready) {
                        // Cancel remaining (losing) tasks.
                        cancel_other_tasks(*arr->elements, task);

                        return await_to_result(
                            error_msg("Task", "race", "winning task was cancelled"),
                            [&] { return make_success_value(task->future.get()); });
                    }
                }

                // Sleep between polling cycles to avoid burning CPU.
                backoff.sleep();
            }
        })
        // Task.timeout(task<T> t, integer milliseconds) -> result<T>
        // Await a task with a timeout.  Returns success(value) on success or
        // failure("timeout") if the task does not complete in time.
        .func("timeout", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& task = expect_task(args[0], "Task.timeout", loc);

            const auto ms = expect_integer(args[1], "Task.timeout", loc);
            const auto status = task->future.wait_for(std::chrono::milliseconds{ms});

            if (status == std::future_status::ready) {
                return await_to_result(error_msg("Task", "timeout", "task was cancelled"),
                                       [&] { return make_success_value(task->future.get()); });
            }

            // Cancel the task on timeout if it has a cancellation token.
            if (task->token) {
                task->token->cancel();
            }

            return make_failure_value(error_msg("Task", "timeout", "timeout"));
        })
        // Task.is_done(task t) -> result<boolean>
        .func("is_done", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& task = expect_task(args[0], "Task.is_done", loc);

            return make_success_value(Value{task->future.wait_for(std::chrono::milliseconds{0}) ==
                                            std::future_status::ready});
        })
        // Task.delay(integer milliseconds) -> null
        // Sleep for the specified number of milliseconds.
        .func("delay", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto ms = expect_integer(args[0], "Task.delay", loc);

            if (ms < 0) {
                throw RuntimeError{"Task.delay: milliseconds must be non-negative", loc,
                                   "use 0 or a positive number of milliseconds"};
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{ms});

            return NullValue{};
        })
        // Task.map(task t, function f) -> result<U>
        // Await t, apply f to its value, and return the transformed result.
        .func("map", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& task = expect_task(args[0], "Task.map", loc);

            expect_callable(args[1], "Task.map", loc);

            // Await the task and apply the function synchronously.
            return await_to_result(error_msg("Task", "map", "task was cancelled"), [&] {
                auto val = task->future.get();
                std::vector<Value> fn_args{std::move(val)};

                return make_success_value(invoke_callable(args[1], fn_args, loc));
            });
        })
        // Task.flat_map(task t, function f) -> result<U>
        // Apply f to the result of t (f must return a task), then await it.
        .func("flat_map", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& task = expect_task(args[0], "Task.flat_map", loc);

            expect_callable(args[1], "Task.flat_map", loc);

            return await_to_result(error_msg("Task", "flat_map", "task was cancelled"), [&] {
                auto val = task->future.get();
                std::vector<Value> fn_args{std::move(val)};

                const auto inner = invoke_callable(args[1], fn_args, loc);

                if (!inner.is_task()) {
                    return make_failure_value(
                        error_msg("Task", "flat_map",
                                  std::format("function must return a task, got '{}'",
                                              inner.display_type_name())));
                }

                return make_success_value(inner.as_task()->future.get());
            });
        })
        // Task.map_n(array<task<T>> tasks, fn(T) -> U) -> result<array<U>>
        // Await all tasks and apply f to each result.
        .func("map_n", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& arr = expect_array(args[0], "Task.map_n", loc);

            expect_callable(args[1], "Task.map_n", loc);

            auto results = std::make_shared<ArrayValue>();

            for (std::size_t i{0}; i < arr->elements->size(); ++i) {
                const auto& elem = (*arr->elements)[i];

                if (!elem.is_task()) {
                    cancel_remaining_tasks(*arr->elements, i + 1);

                    return make_failure_value(
                        error_msg("Task", "map_n", "array element is not a task"));
                }

                const auto& task = elem.as_task();

                if (auto failure = collect_awaited(
                        error_msg("Task", "map_n", "a task was cancelled"),
                        [&] {
                            auto val = task->future.get();
                            std::vector<Value> fn_args{std::move(val)};

                            results->elements->push_back(invoke_callable(args[1], fn_args, loc));
                        },
                        [&] { cancel_remaining_tasks(*arr->elements, i + 1); })) {
                    return std::move(*failure);
                }
            }

            return make_success_value(Value{std::move(results)});
        })
        // Task.retry(integer max_attempts, func() -> result<T>) -> result<T>
        // Retry f up to max_attempts times. f takes no arguments and
        // returns a result. Retries on failure, returns on success.
        .func("retry", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto max_attempts = expect_integer(args[0], "Task.retry", loc);

            expect_callable(args[1], "Task.retry", loc);

            if (max_attempts <= 0) {
                throw RuntimeError{"Task.retry: max_attempts must be > 0", loc,
                                   "pass a positive integer for the number of retries"};
            }

            Value last_result{NullValue{}};

            for (std::int64_t i{0}; i < max_attempts; ++i) {
                std::vector<Value> fn_args{};

                last_result = invoke_callable(args[1], fn_args, loc);

                if (last_result.is_result() && last_result.as_result()->is_success) {
                    return last_result;
                }
            }

            // All attempts failed -- return the last result (a fail result).
            // Wrap a bare non-result return as a fail result for consistency.
            if (!last_result.is_result()) {
                return make_failure_value(error_msg("Task", "retry", "all attempts failed"));
            }

            return last_result;
        })
        // Task.cancel(task t) -> boolean
        // Request cooperative cancellation of a task.  Returns true if the
        // task had a cancellation token (i.e. was spawned inside a
        // task_scope), false otherwise (fire-and-forget tasks cannot be
        // cancelled).
        .func("cancel", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& task = expect_task(args[0], "Task.cancel", loc);

            if (task->token) {
                task->token->cancel();

                return Value{true};
            }

            return Value{false};
        })
        // Task.is_cancelled(task t) -> boolean
        // Check whether a task has been cancelled.
        .func("is_cancelled", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& task = expect_task(args[0], "Task.is_cancelled", loc);

            if (task->token) {
                return Value{task->token->is_cancelled()};
            }

            return Value{false};
        })
        // Task.any(tasks: array<task<T>>) -> result<T>
        // Returns the first successful result, ignoring failures.
        // If all tasks fail, returns the last failure message.
        .func("any", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& arr = expect_array(args[0], "Task.any", loc);

            if (auto failure =
                    check_non_empty_elements(*arr->elements, "Task", "any", "task",
                                             [](const Value& elem) { return elem.is_task(); })) {
                return std::move(*failure);
            }

            // Track which tasks are done.
            std::vector<bool> done(arr->elements->size(), false);
            std::string last_error;
            std::size_t done_count = 0;

            BackoffTimer backoff;

            while (done_count < arr->elements->size()) {
                for (std::size_t i = 0; i < arr->elements->size(); ++i) {
                    if (done[i]) {
                        continue;
                    }

                    const auto& task = (*arr->elements)[i].as_task();

                    if (task->future.wait_for(std::chrono::milliseconds{0}) ==
                        std::future_status::ready) {
                        done[i] = true;
                        ++done_count;

                        // Translate the completed task once: keep the winning
                        // value, or record why it failed for the aggregate error.
                        const auto outcome = await_to_result("task was cancelled", [&] {
                            return make_success_value(task->future.get());
                        });

                        if (outcome.as_result()->is_success) {
                            // Cancel remaining tasks.
                            cancel_other_tasks(*arr->elements, task);

                            return outcome;
                        }

                        last_error = outcome.as_result()->owned_inner->as_string();
                    }
                }

                if (done_count < arr->elements->size()) {
                    backoff.sleep();
                }
            }

            return make_failure_value(error_msg(
                "Task", "any", std::format("all tasks failed (last error: {})", last_error)));
        })
        // Task.sequence(tasks: array<task<T>>) -> result<array<T>>
        // Awaits each task in order, collecting results. Cancels remaining on failure.
        .func("sequence", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& arr = expect_array(args[0], "Task.sequence", loc);
            auto results = std::make_shared<ArrayValue>();

            for (std::size_t i = 0; i < arr->elements->size(); ++i) {
                const auto& elem = (*arr->elements)[i];

                if (!elem.is_task()) {
                    return make_failure_value(
                        error_msg("Task", "sequence", "array element is not a task"));
                }

                const auto& task = elem.as_task();

                if (auto failure = collect_awaited(
                        error_msg("Task", "sequence", "a task was cancelled"),
                        [&] { results->elements->push_back(task->future.get()); },
                        [&] { cancel_remaining_tasks(*arr->elements, i + 1); })) {
                    return std::move(*failure);
                }
            }

            return make_success_value(Value{std::move(results)});
        })
        // Task.all_settled(array<task<T>> tasks) -> array<result<T>>
        // Await EVERY task and return one result per task in original order.
        // Fail-slow complement to Task.all: never fails as a whole -- each
        // element carries that task's own success or failure.
        .func("all_settled", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& arr = expect_array(args[0], "Task.all_settled", loc);

            auto results = std::make_shared<ArrayValue>();

            for (std::size_t i{0}; i < arr->elements->size(); ++i) {
                const auto& elem = (*arr->elements)[i];

                if (!elem.is_task()) {
                    throw RuntimeError{"Task.all_settled: array element is not a task", loc,
                                       "pass an array containing only task values"};
                }

                const auto& task = elem.as_task();

                // Translate this task's outcome into a result<T> and keep it;
                // a failure never aborts the loop (unlike Task.all).
                results->elements->push_back(await_to_result(
                    error_msg("Task", "all_settled", "a task was cancelled"),
                    [&] { return make_success_value(task->future.get()); }));
            }

            return Value{std::move(results)};
        })
        // Task.retry_with_backoff(integer n, integer base_delay_ms,
        //                         func() -> result<T>) -> result<T>
        // Retry f up to n times, sleeping between attempts with exponentially
        // increasing delays.  Backoff schedule (delay BEFORE attempt k, 0-based):
        // attempt 0 runs immediately, then base_delay_ms, 2*base, 4*base, ... so
        // the sleep before attempt k is base_delay_ms * 2^(k-1).  No sleep after
        // the final attempt.  Returns on first success, or the last failure.
        .func("retry_with_backoff", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "Task.retry_with_backoff", loc);
            const auto base_delay_ms = expect_integer(args[1], "Task.retry_with_backoff", loc);

            expect_callable(args[2], "Task.retry_with_backoff", loc);

            if (n <= 0) {
                throw RuntimeError{"Task.retry_with_backoff: n must be > 0", loc,
                                   "pass a positive integer for the number of attempts"};
            }

            if (base_delay_ms < 0) {
                throw RuntimeError{"Task.retry_with_backoff: base_delay_ms must be non-negative", loc,
                                   "use 0 or a positive number of milliseconds"};
            }

            Value last_result{NullValue{}};

            for (std::int64_t i{0}; i < n; ++i) {
                std::vector<Value> fn_args{};

                last_result = invoke_callable(args[2], fn_args, loc);

                if (last_result.is_result() && last_result.as_result()->is_success) {
                    return last_result;
                }

                // Sleep with exponential backoff before the next attempt; skip
                // after the final one.  Cap the shift to avoid undefined
                // behaviour / absurd sleeps on large attempt counts.
                if (i + 1 < n) {
                    const auto shift = std::min<std::int64_t>(i, 62);
                    const auto delay_ms = base_delay_ms * (static_cast<std::int64_t>(1) << shift);

                    std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms});
                }
            }

            // All attempts failed -- return the last result (a fail result).
            // Wrap a bare non-result return as a fail result for consistency.
            if (!last_result.is_result()) {
                return make_failure_value(
                    error_msg("Task", "retry_with_backoff", "all attempts failed"));
            }

            return last_result;
        })
        // Task.completed(T value) -> task<T>
        // A task already completed with `value` (like Promise.resolve); no real
        // async work is performed.
        .func("completed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            return make_completed_task(args[0]);
        })
        // Task.failed(string message) -> task<T>
        // A task already failed with `message` (like Promise.reject).
        .func("failed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& message = expect_string(args[0], "Task.failed", loc);

            return make_failed_task(message);
        })
        // Task.timeout_or(task<T> t, integer ms, T default) -> T
        // The task's value if it completes within ms, else `default`.
        // Equivalent to Task.timeout(t, ms) followed by unwrap_or(default):
        // a timeout OR a task failure both fall back to `default`.
        .func("timeout_or", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& task = expect_task(args[0], "Task.timeout_or", loc);

            const auto ms = expect_integer(args[1], "Task.timeout_or", loc);
            const auto status = task->future.wait_for(std::chrono::milliseconds{ms});

            if (status == std::future_status::ready) {
                // Completed in time: return its value, or fall back to default
                // if the task failed or was cancelled (mirrors unwrap_or).
                try {
                    return task->future.get();
                } catch (const std::exception&) {
                    return args[2];
                }
            }

            // Cancel the task on timeout if it has a cancellation token.
            if (task->token) {
                task->token->cancel();
            }

            return args[2];
        });
}
} // namespace luma
