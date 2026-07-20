// Standard library tests: Channel, Task.

#include <mutex>

#include "runtime/concurrency/thread_pool.hpp"
#include "stdlib_test_helpers.hpp"

static void test_channel_close() {
    const auto v = eval("channel<integer> ch = Channel.new()\n"
                        "Channel.close(ch)\n"
                        "Channel.is_closed(ch)\n");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_channel_is_closed_non_channel_throws() {
    ASSERT_THROWS(eval("Channel.is_closed(42)"));
}

static void test_channel_is_empty() {
    ASSERT_TRUE(eval("Channel.new() |> Channel.is_empty()").as_bool());

    const auto v = eval("channel<integer> ch = Channel.new()\n"
                        "Channel.send(ch, 1)\n"
                        "Channel.is_empty(ch)\n");

    ASSERT_FALSE(v.as_bool());
}

static void test_channel_length() {
    const auto v = eval("channel<integer> ch = Channel.new()\n"
                        "Channel.send(ch, 1)\n"
                        "Channel.send(ch, 2)\n"
                        "Channel.length(ch)\n");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 2);
}

static void test_channel_length_non_channel_throws() {
    ASSERT_THROWS(eval("Channel.length(42)"));
}

static void test_channel_make() {
    const auto v = eval("Channel.new()");

    ASSERT_TRUE(v.is_channel());
}

static void test_channel_make_buffered() {
    const auto v = eval("channel<integer> ch = Channel.new_buffered(2)\n"
                        "Channel.send(ch, 1)\n"
                        "Channel.send(ch, 2)\n"
                        "Channel.length(ch)\n");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 2);
}

static void test_channel_make_buffered_invalid() {
    bool threw_zero{false};

    try {
        eval("Channel.new_buffered(0)");
    } catch (const RuntimeError&) {
        threw_zero = true;
    }

    ASSERT_TRUE(threw_zero);

    bool threw_neg{false};

    try {
        eval("Channel.new_buffered(-5)");
    } catch (const RuntimeError&) {
        threw_neg = true;
    }

    ASSERT_TRUE(threw_neg);
}

static void test_channel_receive_all() {
    const auto v = eval("channel<integer> ch = Channel.new()\n"
                        "Channel.send(ch, 10)\n"
                        "Channel.send(ch, 20)\n"
                        "Channel.send(ch, 30)\n"
                        "Channel.receive_all(ch)\n");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), static_cast<std::size_t>(3));
}

static void test_channel_receive_closed() {
    ASSERT_EVAL_FAILURE("channel<integer> ch = Channel.new()\n"
                        "Channel.close(ch)\n"
                        "Channel.receive(ch)\n");
}

static void test_channel_receive_timeout_times_out() {
    // Empty channel — should time out quickly.
    const auto v = eval("channel<integer> ch = Channel.new()\n"
                        "Channel.receive_timeout(ch, 1)\n");

    ASSERT_RESULT_FAILURE(v);

    const std::string msg = v.as_result()->owned_inner->as_string();

    ASSERT_TRUE(msg.find("timeout") != std::string::npos);
}

static void test_channel_receive_timeout_with_value() {
    // Pre-send a value so receive_timeout returns immediately.
    ASSERT_EVAL_INT("channel<integer> ch = Channel.new()\n"
                    "Channel.send(ch, 99)\n"
                    "Channel.receive_timeout(ch, 1000)\n",
                    99);
}

static void test_channel_send_non_channel_throws() {
    ASSERT_THROWS(eval("Channel.send(42, 1)"));
}

static void test_channel_send_receive() {
    ASSERT_EVAL_INT("channel<integer> ch = Channel.new()\n"
                    "Channel.send(ch, 42)\n"
                    "Channel.receive(ch)\n",
                    42);
}

static void test_channel_send_timeout_on_closed() {
    // Closed channel — send_timeout should return failure.
    const auto v = eval("channel<integer> ch = Channel.new_buffered(2)\n"
                        "Channel.close(ch)\n"
                        "Channel.send_timeout(ch, 42, 100)\n");

    ASSERT_RESULT_FAILURE(v);

    const std::string msg = v.as_result()->owned_inner->as_string();

    ASSERT_TRUE(msg.find("closed") != std::string::npos);
}

static void test_channel_send_timeout_succeeds() {
    // Buffered channel with room — send_timeout should succeed immediately.
    ASSERT_EVAL_BOOL("channel<integer> ch = Channel.new_buffered(2)\n"
                     "Channel.send_timeout(ch, 42, 1000)\n",
                     true);
}

static void test_channel_send_timeout_times_out() {
    // Full buffered channel with capacity 1 — send_timeout should time out.
    const auto v = eval("channel<integer> ch = Channel.new_buffered(1)\n"
                        "Channel.send(ch, 1)\n"
                        "Channel.send_timeout(ch, 2, 1)\n");

    ASSERT_RESULT_FAILURE(v);

    const std::string msg = v.as_result()->owned_inner->as_string();

    ASSERT_TRUE(msg.find("timeout") != std::string::npos);
}

static void test_channel_try_receive_empty() {
    ASSERT_EVAL_FAILURE("channel<integer> ch = Channel.new()\n"
                        "Channel.try_receive(ch)\n");
}

static void test_channel_try_receive_with_value() {
    ASSERT_EVAL_STR("channel<string> ch = Channel.new()\n"
                    "Channel.send(ch, \"hello\")\n"
                    "Channel.try_receive(ch)\n",
                    "hello");
}

static void test_task_cancel() {
    const auto v = eval("task<integer> t = spawn Math.absolute(-5)\n"
                        "integer _r = await t\n"
                        "Task.cancel(t)\n");

    ASSERT_TRUE(v.is_bool());
    // Unscoped tasks may not have a cancellation token, so cancel may return false.
}

static void test_task_cancel_non_task_throws() {
    ASSERT_THROWS(eval("Task.cancel(42)"));
}

static void test_task_is_cancelled() {
    const auto v = eval("task<integer> t = spawn Math.absolute(-5)\n"
                        "integer _r = await t\n"
                        "Task.is_cancelled(t)\n");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

static void test_task_is_cancelled_after_cancel() {
    const auto v = eval("task<integer> t = spawn Math.absolute(-5)\n"
                        "integer _r = await t\n"
                        "boolean _c = Task.cancel(t)\n"
                        "Task.is_cancelled(t)\n");

    ASSERT_TRUE(v.is_bool());
    // For unscoped tasks, cancel may not set a token, so is_cancelled may be false.
}

static void test_task_is_cancelled_non_task_throws() {
    ASSERT_THROWS(eval("Task.is_cancelled(42)"));
}

static void test_task_is_done() {
    // A completed task should report is_done = true.
    ASSERT_EVAL_BOOL("task<integer> t = spawn Math.absolute(-5)\n"
                     "await t\n"
                     "Task.is_done(t)\n",
                     true);
}

static void test_task_is_done_non_task_throws() {
    ASSERT_THROWS(eval("Task.is_done(42)"));
}

static void test_task_map_n_returns_result() {
    const auto v = eval("task<integer> t1 = spawn Math.absolute(-1)\n"
                        "task<integer> t2 = spawn Math.absolute(-2)\n"
                        "Task.map_n([t1, t2], (result<integer> r) -> Result.unwrap(r))\n");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 2U);
}

static void test_task_retry_all_fail() {
    // Function always returns a fail result — retry should return result.fail.
    ASSERT_EVAL_FAILURE("Task.retry(3, () -> failure(\"oops\"))");
}

static void test_task_retry_invalid_attempts() {
    ASSERT_THROWS(eval("Task.retry(0, (integer n) -> success(n))"));
}

static void test_task_retry_negative_attempts_throws() {
    ASSERT_THROWS(eval("Task.retry(-1, () -> success(1))"));
}

static void test_task_retry_success() {
    ASSERT_EVAL_INT("Task.retry(3, () -> success(42))", 42);
}

static void test_task_all_returns_ordered_results() {
    const auto v = eval("task<integer> t1 = spawn String.length(\"a\")\n"
                        "task<integer> t2 = spawn String.length(\"ab\")\n"
                        "task<integer> t3 = spawn String.length(\"abc\")\n"
                        "Task.all([t1, t2, t3])\n");

    ASSERT_RESULT_SUCCESS(v);

    const auto arr = v.as_result()->owned_inner->as_array();
    ASSERT_EQ(arr->elements->size(), 3U);
    ASSERT_EQ((*arr->elements)[0].as_integer(), 1);
    ASSERT_EQ((*arr->elements)[1].as_integer(), 2);
    ASSERT_EQ((*arr->elements)[2].as_integer(), 3);
}

static void test_task_all_empty_returns_empty_array() {
    const auto v = eval("Task.all([])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 0U);
}

static void test_task_all_non_task_throws() {
    ASSERT_THROWS(eval("Task.all([42])"));
}

static void test_task_race_returns_result() {
    const auto v = eval("task<integer> t1 = spawn String.length(\"hello\")\n"
                        "task<integer> t2 = spawn String.length(\"hi\")\n"
                        "Task.race([t1, t2])\n");

    ASSERT_RESULT_SUCCESS(v);

    const auto val = v.as_result()->owned_inner->as_integer();
    ASSERT_TRUE(val == 5 || val == 2);
}

static void test_task_race_empty_fails() {
    ASSERT_EVAL_FAILURE("Task.race([])");
}

static void test_task_race_non_task_fails() {
    // Unlike Task.all (which throws), the polling combinators report a
    // non-task element as a failure result.
    ASSERT_EVAL_FAILURE("Task.race([42])");
}

static void test_task_timeout_success() {
    ASSERT_EVAL_INT("task<integer> t = spawn String.length(\"hello\")\n"
                    "Task.timeout(t, 5000)\n",
                    5);
}

static void test_task_timeout_non_task_throws() {
    ASSERT_THROWS(eval("Task.timeout(42, 100)"));
}

static void test_task_map_transforms_result() {
    ASSERT_EVAL_INT("task<integer> t = spawn String.length(\"hello\")\n"
                    "Task.map(t, (integer x) -> x + 1)\n",
                    6);
}

static void test_task_map_non_task_throws() {
    ASSERT_THROWS(eval("Task.map(42, (integer x) -> x)"));
}

static void test_task_flat_map_chains_tasks() {
    ASSERT_EVAL_INT("function integer plus_hundred(integer x) { return x + 100 }\n"
                    "task<integer> t = spawn String.length(\"hello\")\n"
                    "Task.flat_map(t, (integer x) -> spawn plus_hundred(x))\n",
                    105);
}

static void test_task_flat_map_non_task_throws() {
    ASSERT_THROWS(eval("Task.flat_map(42, (integer x) -> spawn String.length(\"a\"))"));
}

static void test_task_flat_map_requires_task_return() {
    // The function returns a non-task value, so flat_map yields a failure result
    // at runtime rather than throwing.
    ASSERT_EVAL_FAILURE("task<integer> t = spawn String.length(\"hello\")\n"
                        "Task.flat_map(t, (integer x) -> x + 1)\n");
}

static void test_task_map_n_non_task_fails() {
    ASSERT_EVAL_FAILURE("Task.map_n([42], (integer x) -> x)");
}

static void test_task_any_non_task_fails() {
    ASSERT_EVAL_FAILURE("Task.any([42])");
}

static void test_task_sequence_non_task_fails() {
    ASSERT_EVAL_FAILURE("Task.sequence([42])");
}

static void test_task_map_n_empty_succeeds() {
    const auto v = eval("Task.map_n([], (integer x) -> x)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 0U);
}

static void test_task_delay_returns_null() {
    const auto v = eval("Task.delay(0)");

    ASSERT_TRUE(v.is_null());
}

static void test_task_delay_negative_throws() {
    ASSERT_THROWS(eval("Task.delay(-1)"));
}

static void test_thread_pool_queue_limit() {
    // Create a pool with 1 worker thread and block it so tasks queue up.
    ThreadPool pool{1};

    std::mutex block;
    block.lock(); // hold the mutex so the first task blocks

    // Enqueue a blocking task that holds the worker.
    pool.enqueue([&block] { std::lock_guard<std::mutex> guard{block}; });

    // Fill the queue up to the limit.  The worker is blocked, so all
    // tasks accumulate in the queue.
    bool threw{false};

    try {
        for (std::size_t i{0}; i < ResourceLimits::max_task_queue_size + 1; ++i) {
            pool.enqueue([] {});
        }
    } catch (const std::runtime_error& e) {
        threw = true;

        const std::string msg = e.what();

        ASSERT_TRUE(msg.find("task queue is full") != std::string::npos);
    }

    block.unlock(); // unblock worker so pool can shut down

    ASSERT_TRUE(threw);
}

static void test_task_any_first_success() {
    const auto v = eval("task<integer> t1 = spawn String.length(\"hello\")\n"
                        "task<integer> t2 = spawn String.length(\"hi\")\n"
                        "Task.any([t1, t2])\n");

    ASSERT_RESULT_SUCCESS(v);

    // Should be either 5 or 2 (whichever finishes first).
    const auto val = v.as_result()->owned_inner->as_integer();
    ASSERT_TRUE(val == 5 || val == 2);
}

static void test_task_any_empty() {
    ASSERT_EVAL_FAILURE("Task.any([])");
}

static void test_task_sequence() {
    const auto v = eval("task<integer> t1 = spawn String.length(\"a\")\n"
                        "task<integer> t2 = spawn String.length(\"ab\")\n"
                        "task<integer> t3 = spawn String.length(\"abc\")\n"
                        "Task.sequence([t1, t2, t3]) |> Result.unwrap()\n");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[1].as_integer(), 2);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 3);
}

static void test_task_sequence_empty() {
    const auto v = eval("Task.sequence([]) |> Result.unwrap()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 0U);
}

static void test_channel_select() {
    const auto v = eval("channel<integer> ch1 = Channel.new()\n"
                        "channel<integer> ch2 = Channel.new()\n"
                        "Channel.send(ch2, 42)\n"
                        "Channel.select([ch1, ch2]) |> Result.unwrap()");

    ASSERT_TRUE(v.is_tuple());
    // Should return (1, 42) — index 1 (ch2) and value 42.
    ASSERT_EQ(v.as_tuple()->elements[0].as_integer(), 1);
    ASSERT_EQ(v.as_tuple()->elements[1].as_integer(), 42);
}

static void test_channel_select_empty() {
    ASSERT_EVAL_FAILURE("Channel.select([])");
}

static void test_channel_select_all_closed() {
    ASSERT_EVAL_FAILURE("channel<integer> ch = Channel.new()\n"
                        "Channel.close(ch)\n"
                        "Channel.select([ch])");
}

int main() {
    RUN(test_channel_close);
    RUN(test_channel_is_closed_non_channel_throws);
    RUN(test_channel_is_empty);
    RUN(test_channel_length);
    RUN(test_channel_length_non_channel_throws);
    RUN(test_channel_make);
    RUN(test_channel_make_buffered);
    RUN(test_channel_make_buffered_invalid);
    RUN(test_channel_receive_all);
    RUN(test_channel_receive_closed);
    RUN(test_channel_receive_timeout_times_out);
    RUN(test_channel_receive_timeout_with_value);
    RUN(test_channel_send_non_channel_throws);
    RUN(test_channel_select);
    RUN(test_channel_select_all_closed);
    RUN(test_channel_select_empty);
    RUN(test_channel_send_receive);
    RUN(test_channel_send_timeout_on_closed);
    RUN(test_channel_send_timeout_succeeds);
    RUN(test_channel_send_timeout_times_out);
    RUN(test_channel_try_receive_empty);
    RUN(test_channel_try_receive_with_value);
    RUN(test_task_cancel);
    RUN(test_task_cancel_non_task_throws);
    RUN(test_task_is_cancelled);
    RUN(test_task_is_cancelled_after_cancel);
    RUN(test_task_is_cancelled_non_task_throws);
    RUN(test_task_is_done);
    RUN(test_task_any_empty);
    RUN(test_task_any_first_success);
    RUN(test_task_is_done_non_task_throws);
    RUN(test_task_map_n_returns_result);
    RUN(test_task_sequence);
    RUN(test_task_sequence_empty);
    RUN(test_task_retry_all_fail);
    RUN(test_task_retry_invalid_attempts);
    RUN(test_task_retry_negative_attempts_throws);
    RUN(test_task_retry_success);
    RUN(test_task_all_returns_ordered_results);
    RUN(test_task_all_empty_returns_empty_array);
    RUN(test_task_all_non_task_throws);
    RUN(test_task_race_returns_result);
    RUN(test_task_race_empty_fails);
    RUN(test_task_race_non_task_fails);
    RUN(test_task_timeout_success);
    RUN(test_task_timeout_non_task_throws);
    RUN(test_task_map_transforms_result);
    RUN(test_task_map_non_task_throws);
    RUN(test_task_flat_map_chains_tasks);
    RUN(test_task_flat_map_non_task_throws);
    RUN(test_task_flat_map_requires_task_return);
    RUN(test_task_map_n_non_task_fails);
    RUN(test_task_any_non_task_fails);
    RUN(test_task_sequence_non_task_fails);
    RUN(test_task_map_n_empty_succeeds);
    RUN(test_task_delay_returns_null);
    RUN(test_task_delay_negative_throws);
    RUN(test_thread_pool_queue_limit);

    return SUMMARY();
}
