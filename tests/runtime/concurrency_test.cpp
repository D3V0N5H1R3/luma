// Concurrency unit tests — Channel, ThreadPool, TaskScope, CancellationToken.

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include "runtime/concurrency/cancellation_token.hpp"
#include "runtime/concurrency/channel.hpp"
#include "runtime/concurrency/task_scope.hpp"
#include "runtime/concurrency/thread_pool.hpp"
#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Channel tests ───

static void test_channel_send_receive() {
    Channel channel;

    channel.send(Value{42});

    const auto result = channel.try_receive();

    ASSERT_EQ(result.to_string(), "42");
}

static void test_channel_fifo_order() {
    Channel channel;

    channel.send(Value{1});
    channel.send(Value{2});
    channel.send(Value{3});

    ASSERT_EQ(channel.try_receive().to_string(), "1");
    ASSERT_EQ(channel.try_receive().to_string(), "2");
    ASSERT_EQ(channel.try_receive().to_string(), "3");
}

static void test_channel_try_receive_empty() {
    Channel channel;

    ASSERT_THROWS_AS(channel.try_receive(), ChannelEmptyError);
}

static void test_channel_close_send_returns_false() {
    Channel channel;
    channel.close();

    ASSERT_THROWS_AS(channel.send(Value{1}), ChannelClosedError);
}

static void test_channel_close_drains() {
    Channel channel;

    channel.send(Value{10});
    channel.send(Value{20});

    channel.close();

    // Values enqueued before close should still be receivable.
    const auto v1 = channel.try_receive();

    ASSERT_EQ(v1.to_string(), "10");

    const auto v2 = channel.try_receive();

    ASSERT_EQ(v2.to_string(), "20");

    // After draining, receive throws ChannelClosedError.
    ASSERT_THROWS_AS(channel.try_receive(), ChannelClosedError);
}

static void test_channel_is_closed() {
    Channel channel;

    ASSERT_FALSE(channel.is_closed());

    channel.close();

    ASSERT_TRUE(channel.is_closed());
}

static void test_channel_length() {
    Channel channel;

    ASSERT_EQ(channel.length(), static_cast<std::size_t>(0));
    channel.send(Value{1});
    ASSERT_EQ(channel.length(), static_cast<std::size_t>(1));
    channel.send(Value{2});
    ASSERT_EQ(channel.length(), static_cast<std::size_t>(2));

    (void)channel.try_receive();

    ASSERT_EQ(channel.length(), static_cast<std::size_t>(1));
}

static void test_channel_try_send_closed() {
    Channel channel;
    channel.close();

    ASSERT_THROWS_AS(channel.try_send(Value{1}), ChannelClosedError);
}

static void test_channel_buffered_try_send_full() {
    Channel channel{2}; // capacity = 2

    channel.try_send(Value{1});
    channel.try_send(Value{2});
    // Buffer is full, try_send should throw ChannelFullError.
    ASSERT_THROWS_AS(channel.try_send(Value{3}), ChannelFullError);

    // Drain one, then try_send should succeed again.
    (void)channel.try_receive();

    channel.try_send(Value{3});
}

static void test_channel_receive_timeout() {
    Channel channel;

    // Receive on empty channel with short timeout should time out.
    const auto result = channel.receive_timeout(std::chrono::milliseconds{10});

    ASSERT_TRUE(result.timed_out);
    ASSERT_FALSE(result.value.has_value());
}

static void test_channel_send_timeout_buffered() {
    Channel channel{1}; // capacity = 1

    channel.send(Value{1}); // fill the buffer

    // Send on full buffered channel with short timeout should time out.
    const auto result = channel.send_timeout(Value{2}, std::chrono::milliseconds{10});

    ASSERT_TRUE(result.timed_out);
}

static void test_channel_receive_timeout_returns_value() {
    Channel channel;

    channel.send(Value{42});

    // A value is already available, so receive_timeout returns it immediately
    // without timing out.
    const auto result = channel.receive_timeout(std::chrono::milliseconds{1000});

    ASSERT_FALSE(result.timed_out);
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->to_string(), "42");
}

static void test_channel_send_timeout_succeeds_with_room() {
    Channel channel{2}; // capacity = 2, starts empty

    // The buffer has room, so send_timeout enqueues without timing out.
    const auto result = channel.send_timeout(Value{7}, std::chrono::milliseconds{1000});

    ASSERT_FALSE(result.timed_out);
    ASSERT_EQ(channel.length(), static_cast<std::size_t>(1));
    ASSERT_EQ(channel.try_receive().to_string(), "7");
}

static void test_channel_send_timeout_unblocks_on_receive() {
    // A full buffered channel blocks the sender until a receiver drains a slot,
    // at which point send_timeout completes successfully rather than timing out.
    Channel channel{1};
    channel.send(Value{1}); // fill the buffer

    std::thread receiver{[&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});

        (void)channel.try_receive(); // free a slot
    }};

    const auto result = channel.send_timeout(Value{2}, std::chrono::milliseconds{2000});

    receiver.join();

    ASSERT_FALSE(result.timed_out);
}

static void test_channel_unbounded_try_send_never_full() {
    // Unbounded channels grow on demand, so try_send never reports "full" for a
    // modest number of values (the cap is ResourceLimits::max_channel_queue_size).
    Channel channel; // capacity = 0 → unbounded

    for (int i{0}; i < 1000; ++i) {
        channel.try_send(Value{i});
    }

    ASSERT_EQ(channel.length(), static_cast<std::size_t>(1000));
}

static void test_channel_concurrent_send_receive() {
    Channel channel;

    constexpr int count{100};

    std::atomic<int> received{0};

    // Producer thread sends values.
    std::thread producer{[&] {
        for (int i{0}; i < count; ++i) {
            (void)channel.send(Value{i});
        }

        channel.close();
    }};

    // Consumer thread receives values.
    std::thread consumer{[&] {
        while (true) {
            try {
                const auto value = channel.receive();
                ++received;
            } catch (const ChannelClosedError&) {
                break;
            }
        }
    }};

    producer.join();
    consumer.join();

    ASSERT_EQ(received.load(), count);
}

static void test_channel_receive_blocks_until_value() {
    Channel channel;

    std::thread sender{[&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});

        (void)channel.send(Value{42});
    }};

    const auto result = channel.receive();

    ASSERT_EQ(result.to_string(), "42");

    sender.join();
}

static void test_channel_receive_unblocks_on_close() {
    Channel channel;

    std::thread closer{[&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});

        channel.close();
    }};

    // receive() should unblock when channel is closed.
    ASSERT_THROWS_AS(channel.receive(), ChannelClosedError);

    closer.join();
}

// ─── ThreadPool tests ───

static void test_thread_pool_executes_all_tasks() {
    std::atomic<int> counter{0};

    {
        ThreadPool pool{2};

        for (int i{0}; i < 50; ++i) {
            pool.enqueue([&counter] { ++counter; });
        }
    } // pool destroyed here — waits for all tasks to complete

    ASSERT_EQ(counter.load(), 50);
}

static void test_thread_pool_concurrent_enqueue() {
    std::atomic<int> counter{0};

    {
        ThreadPool pool{4};

        std::vector<std::thread> producers;

        for (int t{0}; t < 4; ++t) {
            producers.emplace_back([&pool, &counter] {
                for (int i{0}; i < 25; ++i) {
                    pool.enqueue([&counter] { ++counter; });
                }
            });
        }

        for (auto& thread : producers) {
            thread.join();
        }
    } // pool destroyed — all tasks complete

    ASSERT_EQ(counter.load(), 100);
}

// ─── CancellationToken tests ───

static void test_cancellation_token_initial_state() {
    const CancellationToken token;

    ASSERT_FALSE(token.is_cancelled());
}

static void test_cancellation_token_cancel() {
    CancellationToken token;
    token.cancel();

    ASSERT_TRUE(token.is_cancelled());
}

static void test_cancellation_token_throw_if_cancelled() {
    CancellationToken token;

    // Should not throw when not cancelled.
    token.throw_if_cancelled();

    token.cancel();

    ASSERT_THROWS(token.throw_if_cancelled());
}

// ─── TaskScope tests ───

static void test_task_scope_join_empty() {
    TaskScope scope;
    const auto results = scope.join_all();

    ASSERT_EQ(results.size(), static_cast<std::size_t>(0));
}

static void test_task_scope_join_collects_results() {
    ThreadPool pool{2};
    TaskScope scope;

    const auto promise1 = std::make_shared<std::promise<Value>>();
    auto future1 = promise1->get_future();
    const auto task1 = std::make_shared<TaskValue>(std::move(future1), scope.token());
    scope.add_child(task1);

    const auto promise2 = std::make_shared<std::promise<Value>>();
    auto future2 = promise2->get_future();
    const auto task2 = std::make_shared<TaskValue>(std::move(future2), scope.token());
    scope.add_child(task2);

    pool.enqueue([&] { promise1->set_value(Value{10}); });
    pool.enqueue([&] { promise2->set_value(Value{20}); });

    const auto results = scope.join_all();

    ASSERT_EQ(results.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(results[0].to_string(), "10");
    ASSERT_EQ(results[1].to_string(), "20");
}

// Regression: a Task value must deep-copy to a DISTINCT TaskValue object so
// that two threads (e.g. a spawned worker that received the task as an argument
// and the parent's task_scope join) each hold their OWN shared_future object
// over the same shared task state.  Before the fix, deep_copy() returned *this —
// the SAME std::future shared across threads — which is undefined behaviour and
// surfaced as a racy "await called on an already-consumed task" error.
static void test_task_deep_copy_yields_independent_shared_future() {
    const auto promise = std::make_shared<std::promise<Value>>();
    const auto shared = promise->get_future().share();

    TaskScope scope;
    const auto task = std::make_shared<TaskValue>(shared, scope.token());
    const Value original{task};

    const Value clone = original.deep_copy();

    ASSERT_TRUE(clone.is_task());
    // Distinct wrapper objects (pre-fix they were the same shared_ptr).
    ASSERT_TRUE(clone.as_task().get() != original.as_task().get());
    // The cancellation token is still shared so cancellation propagates.
    ASSERT_TRUE(clone.as_task()->token == original.as_task()->token);

    promise->set_value(Value{99});

    // Both copies observe the same completed result and remain valid — a
    // shared_future is multi-consumer, unlike the single-consumer std::future
    // used before the fix.
    ASSERT_EQ(original.as_task()->future.get().to_string(), "99");
    ASSERT_EQ(clone.as_task()->future.get().to_string(), "99");
    ASSERT_TRUE(original.as_task()->future.valid());
    ASSERT_TRUE(clone.as_task()->future.valid());
}

static void test_task_scope_error_cancels_siblings() {
    ThreadPool pool{2};
    TaskScope scope;

    const auto promise1 = std::make_shared<std::promise<Value>>();
    auto future1 = promise1->get_future();
    const auto task1 = std::make_shared<TaskValue>(std::move(future1), scope.token());
    scope.add_child(task1);

    const auto promise2 = std::make_shared<std::promise<Value>>();
    auto future2 = promise2->get_future();
    const auto task2 = std::make_shared<TaskValue>(std::move(future2), scope.token());
    scope.add_child(task2);

    // First task fails.
    pool.enqueue([&] {
        promise1->set_exception(std::make_exception_ptr(std::runtime_error{"task1 failed"}));
    });

    // Second task checks cancellation (simulating cooperative check).
    pool.enqueue([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});

        if (scope.token()->is_cancelled()) {
            promise2->set_exception(std::make_exception_ptr(CancelledException{"task cancelled"}));
        } else {
            promise2->set_value(Value{42});
        }
    });

    bool threw{false};

    try {
        (void)scope.join_all();
    } catch (const std::runtime_error& e) {
        threw = true;

        ASSERT_EQ(std::string{e.what()}, "task1 failed");
    }

    ASSERT_TRUE(threw);
    ASSERT_TRUE(scope.token()->is_cancelled());
}

static void test_task_scope_nested() {
    TaskScope outer;
    TaskScope inner{&outer};

    ASSERT_TRUE(inner.parent() == &outer);
    ASSERT_TRUE(outer.parent() == nullptr);
}

static void test_task_scope_cancel_all() {
    TaskScope scope;

    ASSERT_FALSE(scope.token()->is_cancelled());

    scope.cancel_all();

    ASSERT_TRUE(scope.token()->is_cancelled());
}

static void test_cancellation_token_idempotent() {
    CancellationToken token;
    token.cancel();
    token.cancel(); // second cancel is a no-op

    ASSERT_TRUE(token.is_cancelled());
}

static void test_task_scope_cancel_all_idempotent() {
    TaskScope scope;
    scope.cancel_all();
    scope.cancel_all(); // second call is a no-op

    ASSERT_TRUE(scope.token()->is_cancelled());
}

static void test_task_scope_parent_cancel_propagates() {
    TaskScope parent;
    TaskScope child{&parent};

    // Cancelling parent does not auto-cancel child (tokens are separate).
    parent.cancel_all();

    ASSERT_TRUE(parent.token()->is_cancelled());
    ASSERT_FALSE(child.token()->is_cancelled());
}

static void test_cancellation_token_shared_across_threads() {
    const auto token = std::make_shared<CancellationToken>();

    std::thread t{[&] {
        while (!token->is_cancelled()) {
            std::this_thread::yield();
        }
    }};

    token->cancel();
    t.join();

    ASSERT_TRUE(token->is_cancelled());
}

// ─── Resource-exhaustion and cancellation tests ───

static void test_task_cancellation_during_execution() {
    // Verify that a CancellationToken signals correctly between threads.
    const auto token = std::make_shared<CancellationToken>();

    ASSERT_FALSE(token->is_cancelled());

    // Cancel from this thread; check from another.
    token->cancel();

    std::atomic<bool> observed_cancel{false};
    std::thread observer{[&] {
        observed_cancel = token->is_cancelled();
    }};
    observer.join();

    ASSERT_TRUE(observed_cancel.load());
    ASSERT_TRUE(token->is_cancelled());
}

static void test_channel_send_after_close_multiple() {
    // Multiple sends after close should all throw ChannelClosedError (not crash).
    Channel channel;
    channel.close();

    for (int i{0}; i < 100; ++i) {
        ASSERT_THROWS_AS(channel.send(Value{i}), ChannelClosedError);
    }
}

static void test_channel_concurrent_close_during_send() {
    // Close a channel while another thread is sending — neither should hang.
    Channel channel{1};
    channel.send(Value{1}); // fill the buffer

    std::atomic<bool> send_returned{false};

    std::thread sender{[&] {
        // This send may block briefly, time out, or throw ChannelClosedError;
        // any outcome is acceptable.
        try {
            (void)channel.send_timeout(Value{2}, std::chrono::milliseconds{100});
        } catch (const ChannelClosedError&) { // NOLINT(bugprone-empty-catch)
            // Expected when channel is closed during the wait.
        }
        send_returned = true;
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    channel.close();

    sender.join();

    ASSERT_TRUE(send_returned.load());
    ASSERT_TRUE(channel.is_closed());
}

// ─── Error-type discrimination tests ───
// The three channel error classes exist so callers can tell *why* an operation
// failed.  These tests pin down which exact type each failure mode raises —
// crucially, that an empty-but-open buffer differs from a closed one, and a
// full-but-open buffer differs from a closed one.

static void test_channel_try_receive_empty_then_closed() {
    Channel channel;

    // Empty and open → ChannelEmptyError (retry might succeed later).
    ASSERT_THROWS_AS(channel.try_receive(), ChannelEmptyError);

    channel.close();

    // Empty and closed → ChannelClosedError (retry can never succeed).
    ASSERT_THROWS_AS(channel.try_receive(), ChannelClosedError);
}

static void test_channel_try_send_full_then_closed() {
    Channel channel{1}; // capacity = 1

    channel.try_send(Value{1}); // buffer now full

    // Full and open → ChannelFullError (back-pressure, retry after a receive).
    ASSERT_THROWS_AS(channel.try_send(Value{2}), ChannelFullError);

    channel.close();

    // Closed takes precedence over full → ChannelClosedError.
    ASSERT_THROWS_AS(channel.try_send(Value{3}), ChannelClosedError);
}

static void test_channel_receive_timeout_on_closed_throws() {
    Channel channel;
    channel.close();

    // A closed, drained channel reports closure immediately rather than
    // waiting out the timeout: receive_timeout throws ChannelClosedError.
    ASSERT_THROWS_AS(channel.receive_timeout(std::chrono::milliseconds{1000}), ChannelClosedError);
}

static void test_channel_send_timeout_on_closed_throws() {
    Channel channel{2};
    channel.close();

    // send_timeout on a closed channel throws ChannelClosedError without waiting.
    ASSERT_THROWS_AS(channel.send_timeout(Value{1}, std::chrono::milliseconds{1000}),
                     ChannelClosedError);
}

static void test_channel_bounded_ring_wraparound() {
    // Exercise the bounded buffer's modulo head_/tail_ wraparound: fill, drain
    // part, refill so the write index wraps past the end, then verify strict
    // FIFO order is preserved across the wrap boundary.
    Channel channel{3};

    channel.send(Value{1});
    channel.send(Value{2});
    channel.send(Value{3}); // buffer full: [1,2,3]

    ASSERT_EQ(channel.try_receive().to_string(), "1");
    ASSERT_EQ(channel.try_receive().to_string(), "2");

    // Two slots free; these pushes wrap the tail index back to the front.
    channel.send(Value{4});
    channel.send(Value{5}); // buffer holds [3,4,5] across the wrap

    ASSERT_EQ(channel.length(), static_cast<std::size_t>(3));
    ASSERT_EQ(channel.try_receive().to_string(), "3");
    ASSERT_EQ(channel.try_receive().to_string(), "4");
    ASSERT_EQ(channel.try_receive().to_string(), "5");

    // Drained and still open → ChannelEmptyError.
    ASSERT_THROWS_AS(channel.try_receive(), ChannelEmptyError);
}

int main() {
    // Channel tests
    RUN(test_channel_send_receive);
    RUN(test_channel_fifo_order);
    RUN(test_channel_try_receive_empty);
    RUN(test_channel_close_send_returns_false);
    RUN(test_channel_close_drains);
    RUN(test_channel_is_closed);
    RUN(test_channel_length);
    RUN(test_channel_try_send_closed);
    RUN(test_channel_buffered_try_send_full);
    RUN(test_channel_receive_timeout);
    RUN(test_channel_send_timeout_buffered);
    RUN(test_channel_receive_timeout_returns_value);
    RUN(test_channel_send_timeout_succeeds_with_room);
    RUN(test_channel_send_timeout_unblocks_on_receive);
    RUN(test_channel_unbounded_try_send_never_full);
    RUN(test_channel_concurrent_send_receive);
    RUN(test_channel_receive_blocks_until_value);
    RUN(test_channel_receive_unblocks_on_close);

    // ThreadPool tests
    RUN(test_thread_pool_executes_all_tasks);
    RUN(test_thread_pool_concurrent_enqueue);

    // CancellationToken tests
    RUN(test_cancellation_token_initial_state);
    RUN(test_cancellation_token_cancel);
    RUN(test_cancellation_token_throw_if_cancelled);

    // TaskScope tests
    RUN(test_task_scope_join_empty);
    RUN(test_task_scope_join_collects_results);
    RUN(test_task_deep_copy_yields_independent_shared_future);
    RUN(test_task_scope_error_cancels_siblings);
    RUN(test_task_scope_nested);
    RUN(test_task_scope_cancel_all);
    RUN(test_cancellation_token_idempotent);
    RUN(test_task_scope_cancel_all_idempotent);
    RUN(test_task_scope_parent_cancel_propagates);
    RUN(test_cancellation_token_shared_across_threads);

    // Resource-exhaustion and cancellation tests.
    RUN(test_task_cancellation_during_execution);
    RUN(test_channel_send_after_close_multiple);
    RUN(test_channel_concurrent_close_during_send);

    // Channel error-type discrimination and ring-buffer wraparound.
    RUN(test_channel_try_receive_empty_then_closed);
    RUN(test_channel_try_send_full_then_closed);
    RUN(test_channel_receive_timeout_on_closed_throws);
    RUN(test_channel_send_timeout_on_closed_throws);
    RUN(test_channel_bounded_ring_wraparound);
    return SUMMARY();
}
