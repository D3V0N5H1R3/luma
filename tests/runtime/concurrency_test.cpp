// Concurrency unit tests — Channel, ThreadPool, TaskScope, CancellationToken.

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <exception>
#include <future>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common/scope_guard.hpp"
#include "runtime/concurrency/cancellation_token.hpp"
#include "runtime/concurrency/channel.hpp"
#include "runtime/concurrency/task_completion.hpp"
#include "runtime/concurrency/task_scope.hpp"
#include "runtime/concurrency/thread_pool.hpp"
#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"
#include "shared_eval.hpp"
#include "test_framework.hpp"

using luma::CancellationToken;
using luma::CancelledException;
using luma::Channel;
using luma::ChannelClosedError;
using luma::ChannelEmptyError;
using luma::ChannelFullError;
using luma::CompletionSubscription;
using luma::ScopeGuard;
using luma::TaskCompletion;
using luma::TaskScope;
using luma::TaskValue;
using luma::ThreadPool;
using luma::Value;

namespace {

// A std::jthread whose body can never reach std::terminate: an exception
// thrown by the body is captured and rethrown on the test thread by join().
// If a test assertion fails before join(), the destructor requests stop and
// joins, so the thread never outlives the locals it references.  A body may
// take a std::stop_token to observe that request.
class GuardedThread {
public:
    template <typename Body>
    explicit GuardedThread(Body body)
        : error_{std::make_shared<std::exception_ptr>()},
          thread_{[error = error_,
                   body = std::move(body)]([[maybe_unused]] const std::stop_token& stop) mutable {
              try {
                  if constexpr (std::invocable<Body&, const std::stop_token&>) {
                      body(stop);
                  } else {
                      body();
                  }
              } catch (...) {
                  *error = std::current_exception();
              }
          }} {}

    void request_stop() noexcept {
        thread_.request_stop();
    }

    // Wait for the body to finish and rethrow anything it threw.
    void join() {
        thread_.join();

        if (*error_) {
            std::rethrow_exception(*error_);
        }
    }

private:
    std::shared_ptr<std::exception_ptr> error_;
    std::jthread thread_;
};

} // namespace

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

static void test_channel_send_after_close_throws() {
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

    ASSERT_EQ(channel.length(), 0);
    channel.send(Value{1});
    ASSERT_EQ(channel.length(), 1);
    channel.send(Value{2});
    ASSERT_EQ(channel.length(), 2);

    ASSERT_EQ(channel.try_receive().to_string(), "1");

    ASSERT_EQ(channel.length(), 1);
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
    ASSERT_EQ(channel.try_receive().to_string(), "1");

    channel.try_send(Value{3});

    ASSERT_EQ(channel.length(), 2);
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
    ASSERT_EQ(channel.length(), 1);
    ASSERT_EQ(channel.try_receive().to_string(), "7");
}

static void test_channel_send_timeout_unblocks_on_receive() {
    // A full buffered channel blocks the sender until a receiver drains a slot,
    // at which point send_timeout completes successfully rather than timing out.
    Channel channel{1};
    channel.send(Value{1}); // fill the buffer

    GuardedThread receiver{[&channel] {
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

    ASSERT_EQ(channel.length(), 1000);
}

static void test_channel_concurrent_send_receive() {
    Channel channel;

    constexpr int count{100};

    std::atomic<int> received{0};

    // Producer thread sends values.  It closes the channel on every path, so
    // the consumer can never wait forever.
    GuardedThread producer{[&channel] {
        const ScopeGuard close_channel{[&channel] { channel.close(); }};

        for (int i{0}; i < count; ++i) {
            channel.send(Value{i});
        }
    }};

    // Consumer thread receives values until the channel is closed and drained.
    GuardedThread consumer{[&channel, &received] {
        while (true) {
            try {
                (void)channel.receive();
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

    // The sender closes the channel after sending (on every path), so a failed
    // send surfaces as ChannelClosedError instead of leaving receive() blocked.
    GuardedThread sender{[&channel] {
        const ScopeGuard close_channel{[&channel] { channel.close(); }};

        std::this_thread::sleep_for(std::chrono::milliseconds{20});

        channel.send(Value{42});
    }};

    const auto result = channel.receive();

    sender.join();

    ASSERT_EQ(result.to_string(), "42");
}

static void test_channel_receive_unblocks_on_close() {
    Channel channel;

    GuardedThread closer{[&channel] {
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

        std::vector<GuardedThread> producers;

        for (int t{0}; t < 4; ++t) {
            producers.emplace_back([&pool, &counter] {
                for (int i{0}; i < 25; ++i) {
                    pool.enqueue([&counter] { ++counter; });
                }
            });
        }

        for (auto& producer : producers) {
            producer.join();
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

    ASSERT_THROWS_AS(token.throw_if_cancelled(), CancelledException);
    ASSERT_THROWS_WITH_MESSAGE(token.throw_if_cancelled(), "task cancelled");
}

// ─── TaskCompletion tests ───

static void test_task_completion_subscription_released_on_scope_exit() {
    // Regression: Task.race/Task.any left their signal registered on every
    // losing task, so racing a long-running task in a loop grew its subscriber
    // list by one per call until the task finished.
    const auto completion = std::make_shared<TaskCompletion>();

    for (int i = 0; i < 3; ++i) {
        const CompletionSubscription subscription{{completion, completion}};
        ASSERT_EQ(completion->subscriber_count(), 2);
    }

    ASSERT_EQ(completion->subscriber_count(), 0);
}

static void test_task_completion_wakes_waiter() {
    const auto completion = std::make_shared<TaskCompletion>();
    const CompletionSubscription subscription{{completion}};

    GuardedThread producer{[&completion] { completion->mark_complete(); }};

    // A generous timeout: a missed notification would make the wait run to it.
    const auto start = std::chrono::steady_clock::now();
    subscription.wait_for(std::chrono::seconds{10});
    const auto waited = std::chrono::steady_clock::now() - start;

    producer.join();

    ASSERT_LT(waited, std::chrono::seconds{5});
    ASSERT_EQ(completion->subscriber_count(), 0);
}

static void test_task_completion_subscribe_after_completion_notifies() {
    const auto completion = std::make_shared<TaskCompletion>();
    completion->mark_complete();

    const CompletionSubscription subscription{{completion}};

    const auto start = std::chrono::steady_clock::now();
    subscription.wait_for(std::chrono::seconds{10});

    ASSERT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds{5});
}

// ─── TaskScope tests ───

static void test_task_scope_join_empty() {
    TaskScope scope;
    const auto results = scope.join_all();

    ASSERT_EQ(results.size(), 0);
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

    pool.enqueue([promise1] { promise1->set_value(Value{10}); });
    pool.enqueue([promise2] { promise2->set_value(Value{20}); });

    const auto results = scope.join_all();

    ASSERT_EQ(results.size(), 2);
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
    ASSERT_NE(clone.as_task().get(), original.as_task().get());
    // The cancellation token is still shared so cancellation propagates.
    ASSERT_EQ(clone.as_task()->token, original.as_task()->token);

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

    // First task fails.  The pool tasks capture by value: join_all() may return
    // while a pool task is still unwinding, so they must not reference locals.
    pool.enqueue([promise1] {
        promise1->set_exception(std::make_exception_ptr(std::runtime_error{"task1 failed"}));
    });

    // Second task checks cancellation (simulating cooperative check).
    pool.enqueue([promise2, token = scope.token()] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});

        if (token->is_cancelled()) {
            promise2->set_exception(std::make_exception_ptr(CancelledException{"task cancelled"}));
        } else {
            promise2->set_value(Value{42});
        }
    });

    ASSERT_THROWS_WITH_MESSAGE((void)scope.join_all(), "task1 failed");
    ASSERT_TRUE(scope.token()->is_cancelled());
}

static void test_task_scope_nested() {
    TaskScope outer;
    const TaskScope inner{&outer};

    ASSERT_EQ(inner.parent(), &outer);
    ASSERT_EQ(outer.parent(), nullptr);
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

    GuardedThread spinner{[&token] {
        while (!token->is_cancelled()) {
            std::this_thread::yield();
        }
    }};

    token->cancel();
    spinner.join();

    ASSERT_TRUE(token->is_cancelled());
}

// ─── Resource-exhaustion and cancellation tests ───

static void test_cancelled_token_is_visible_to_a_later_thread() {
    // A token cancelled on this thread reads as cancelled from a thread started
    // afterwards.  (Cancelling a thread that is already running is covered by
    // test_cancellation_token_shared_across_threads.)
    const auto token = std::make_shared<CancellationToken>();

    ASSERT_FALSE(token->is_cancelled());

    token->cancel();

    std::atomic<bool> observed_cancel{false};
    GuardedThread observer{[&token, &observed_cancel] { observed_cancel = token->is_cancelled(); }};
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

    GuardedThread sender{[&channel, &send_returned] {
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

    ASSERT_EQ(channel.length(), 3);
    ASSERT_EQ(channel.try_receive().to_string(), "3");
    ASSERT_EQ(channel.try_receive().to_string(), "4");
    ASSERT_EQ(channel.try_receive().to_string(), "5");

    // Drained and still open → ChannelEmptyError.
    ASSERT_THROWS_AS(channel.try_receive(), ChannelEmptyError);
}

// ─── Review regressions ───

// Review #1: a worker blocked inside a BlockingRegion while the work it waits
// for is still queued must not deadlock the pool — the pool starts a
// compensating worker.  A one-worker pool reproduces the deadlock directly.
static void test_thread_pool_compensates_for_blocked_worker() {
    // The promises outlive the pool: its destructor waits for the tasks, which
    // reference them.
    std::promise<void> release;
    const auto released = release.get_future().share();
    std::promise<void> finish;
    const auto finished = finish.get_future();

    ThreadPool pool{1};

    pool.enqueue([released, &finish] {
        const ThreadPool::BlockingRegion blocking;
        released.wait(); // Blocks the pool's only base worker.
        finish.set_value();
    });
    pool.enqueue([&release] { release.set_value(); });

    const bool completed = finished.wait_for(std::chrono::seconds{10}) == std::future_status::ready;

    if (!completed) {
        // Unblock the stuck worker so the pool can shut down and the failure is
        // reported instead of hanging the test binary.
        release.set_value();
    }

    ASSERT_TRUE(completed);
    ASSERT_GE(pool.worker_count(), 2);
}

// Review #3: tokens form a tree — cancelling a task's own token leaves its
// siblings alone, while cancelling the parent (scope) token reaches every
// child.
static void test_cancellation_token_parent_chain() {
    const auto scope_token = std::make_shared<CancellationToken>();
    const auto first = std::make_shared<CancellationToken>(scope_token);
    const auto second = std::make_shared<CancellationToken>(scope_token);

    first->cancel();

    ASSERT_TRUE(first->is_cancelled());
    ASSERT_FALSE(second->is_cancelled());
    ASSERT_FALSE(scope_token->is_cancelled());

    scope_token->cancel();

    ASSERT_TRUE(second->is_cancelled());
}

// Review #3/#6: a child that ends with CancelledException contributes `none`;
// it is neither a failure nor a reason to cancel its siblings.
static void test_task_scope_cancelled_child_is_not_a_failure() {
    TaskScope scope;

    std::promise<Value> cancelled_promise;
    scope.add_child(
        std::make_shared<TaskValue>(cancelled_promise.get_future().share(), scope.token()));
    std::promise<Value> ok_promise;
    scope.add_child(std::make_shared<TaskValue>(ok_promise.get_future().share(), scope.token()));

    cancelled_promise.set_exception(std::make_exception_ptr(CancelledException{"task cancelled"}));
    ok_promise.set_value(Value{5});

    const auto results = scope.join_all();

    ASSERT_EQ(results.size(), 2);
    ASSERT_TRUE(results[0].is_null());
    ASSERT_EQ(results[1].to_string(), "5");
    ASSERT_FALSE(scope.token()->is_cancelled());
}

// Review #7: join_all is fail-fast in completion order.  Child 0 only finishes
// once the scope is cancelled; child 1 fails immediately.  Joining in spawn
// order waited on child 0 forever because the cancellation that would release
// it only happened after child 0 completed.
static void test_task_scope_fail_fast_in_completion_order() {
    TaskScope scope;

    const auto waiting_promise = std::make_shared<std::promise<Value>>();
    scope.add_child(
        std::make_shared<TaskValue>(waiting_promise->get_future().share(), scope.token()));
    const auto failing_promise = std::make_shared<std::promise<Value>>();
    scope.add_child(
        std::make_shared<TaskValue>(failing_promise->get_future().share(), scope.token()));

    const auto token = scope.token();
    // Releases child 0 once the scope is cancelled.  A regression that never
    // cancels is bounded by request_stop() below (or by the destructor, if an
    // exception escapes first), so the thread can never outlive the test.
    GuardedThread waiter{[waiting_promise, token](const std::stop_token& stop) {
        while (!token->is_cancelled() && !stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        waiting_promise->set_exception(
            std::make_exception_ptr(CancelledException{"task cancelled"}));
    }};

    failing_promise->set_exception(std::make_exception_ptr(std::runtime_error{"child failed"}));

    // Bound the wait so a regression fails instead of hanging the test binary.
    auto joined = std::async(std::launch::async, [&scope] {
        try {
            (void)scope.join_all();
        } catch (const std::runtime_error& error) {
            return std::string{error.what()};
        }
        return std::string{};
    });

    const bool finished = joined.wait_for(std::chrono::seconds{10}) == std::future_status::ready;
    waiter.request_stop();
    waiter.join();

    ASSERT_TRUE(finished);
    ASSERT_EQ(joined.get(), std::string{"child failed"});
    ASSERT_TRUE(token->is_cancelled());
}

// Review #8: a blocking channel wait with a cancellation token gives up with
// CancelledException once the token is cancelled.
static void test_channel_receive_is_cancellable() {
    Channel channel;
    CancellationToken token;

    GuardedThread canceller{[&token] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        token.cancel();
    }};

    ASSERT_THROWS_AS((void)channel.receive(&token), CancelledException);
    canceller.join();
}

// Review R4: the timeout waits used a plain wait_for with no cancellation
// token, so a cancelled task stayed blocked for the whole timeout.  They now
// poll the token like the blocking waits do.
static void test_channel_receive_timeout_is_cancellable() {
    Channel channel;
    CancellationToken token;

    GuardedThread canceller{[&token] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        token.cancel();
    }};

    const auto started = std::chrono::steady_clock::now();
    ASSERT_THROWS_AS((void)channel.receive_timeout(std::chrono::hours{1}, &token),
                     CancelledException);
    canceller.join();

    ASSERT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - started)
                  .count(),
              10'000);
}

static void test_channel_send_timeout_is_cancellable() {
    Channel channel{1};
    channel.send(Value{1});
    CancellationToken token;

    GuardedThread canceller{[&token] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        token.cancel();
    }};

    const auto started = std::chrono::steady_clock::now();
    ASSERT_THROWS_AS((void)channel.send_timeout(Value{2}, std::chrono::hours{1}, &token),
                     CancelledException);
    canceller.join();

    ASSERT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - started)
                  .count(),
              10'000);
}

// Review R6: a huge timeout must not overflow the clock arithmetic into an
// immediate timeout.
static void test_channel_receive_timeout_with_huge_timeout_waits() {
    Channel channel;

    GuardedThread sender{[&channel] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        channel.send(Value{7});
    }};

    const auto outcome = channel.receive_timeout(std::chrono::milliseconds::max());
    sender.join();

    ASSERT_FALSE(outcome.timed_out);
    ASSERT_TRUE(outcome.value.has_value());
    ASSERT_EQ(outcome.value->as_integer(), 7);
}

// Review R9: compensation is capped.  With every worker waiting on other pool
// tasks, work queued, and the cap reached, a further task wait can never
// finish; the pool used to hang silently.  The BlockingRegion now reports it.
// Pool: one base worker, at most one compensating worker.
//   A (base worker) queues B and waits for it  -> a compensating worker runs B
//   B queues C and waits for it                 -> no worker left: error
namespace {

// Short deadlock grace for pools built by these tests.
constexpr std::chrono::milliseconds k_test_deadlock_grace{50};

// Poll `region` the way a waiting VM/stdlib loop does, for `duration`.
void poll_verify(ThreadPool::BlockingRegion& region, std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;

    while (std::chrono::steady_clock::now() < deadline) {
        region.verify_not_deadlocked();
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
}

} // namespace

static void test_thread_pool_reports_nested_blocking_beyond_cap() {
    std::promise<std::string> outcome;
    auto outcome_future = outcome.get_future();
    std::promise<void> a_done;
    auto a_finished = a_done.get_future();

    ThreadPool pool{1, 1, k_test_deadlock_grace};

    pool.enqueue([&pool, &outcome, &a_done] {
        auto b_done = std::make_shared<std::promise<void>>();
        auto b_finished = b_done->get_future();

        pool.enqueue([&pool, &outcome, b_done] {
            pool.enqueue([] {}); // C: queued, with no worker left to run it.

            try {
                ThreadPool::BlockingRegion blocking;
                poll_verify(blocking, std::chrono::seconds{10});
                outcome.set_value("no error");
            } catch (const luma::RuntimeError& error) {
                outcome.set_value(error.what());
            }

            b_done->set_value();
        });

        {
            const ThreadPool::BlockingRegion blocking;
            b_finished.wait();
        }

        a_done.set_value();
    });

    ASSERT_EQ(outcome_future.wait_for(std::chrono::seconds{10}), std::future_status::ready);
    ASSERT_EQ(a_finished.wait_for(std::chrono::seconds{10}), std::future_status::ready);

    const auto message = outcome_future.get();
    ASSERT_NE(message.find("too many nested blocking tasks (limit 1)"), std::string::npos);
    ASSERT_EQ(pool.worker_count(), 2);

    // The compensating worker stays alive and is reused: a second one-level
    // nesting completes without starting another worker.
    std::promise<void> second_done;
    auto second_finished = second_done.get_future();

    pool.enqueue([&pool, &second_done] {
        auto inner_done = std::make_shared<std::promise<void>>();
        auto inner_finished = inner_done->get_future();

        pool.enqueue([inner_done] { inner_done->set_value(); });

        {
            const ThreadPool::BlockingRegion blocking;
            inner_finished.wait();
        }

        second_done.set_value();
    });

    ASSERT_EQ(second_finished.wait_for(std::chrono::seconds{10}), std::future_status::ready);
    ASSERT_EQ(pool.worker_count(), 2);
}

// External waits (channels, sleeps, timeouts, I/O) can be ended from outside
// the pool, so reaching the cap there is not an error.
static void test_thread_pool_external_wait_at_cap_does_not_throw() {
    std::promise<bool> threw;
    auto threw_future = threw.get_future();

    ThreadPool pool{1, 0};

    pool.enqueue([&pool, &threw] {
        pool.enqueue([] {}); // Queued while the only worker "blocks".

        try {
            const ThreadPool::BlockingRegion blocking{ThreadPool::BlockingRegion::Wait::External};
            threw.set_value(false);
        } catch (const luma::RuntimeError&) {
            threw.set_value(true);
        }
    });

    ASSERT_EQ(threw_future.wait_for(std::chrono::seconds{10}), std::future_status::ready);
    ASSERT_FALSE(threw_future.get());
}

// Regions nest on one thread (Task.map holds one while its callback awaits).
// The worker is still one blocked worker: counting it twice made the pool
// think every worker was blocked while another was running, and at the cap
// raised a false "too many nested blocking tasks".
// Pool: two base workers, no compensation.  Worker 1 runs a gate task that
// blocks without a region (busy, not blocked); worker 2 nests two regions
// while a task is queued.
static void test_thread_pool_nested_regions_count_one_worker() {
    std::promise<void> open_gate;
    const auto gate_opened = open_gate.get_future().share();
    std::promise<void> gate_running;
    auto gate_started = gate_running.get_future();
    std::promise<bool> threw;
    auto threw_future = threw.get_future();

    ThreadPool pool{2, 0, k_test_deadlock_grace};

    pool.enqueue([gate_opened, &gate_running] {
        gate_running.set_value();
        gate_opened.wait();
    });

    ASSERT_EQ(gate_started.wait_for(std::chrono::seconds{10}), std::future_status::ready);

    pool.enqueue([&pool, &threw, &open_gate] {
        pool.enqueue([] {}); // Queued: the other worker is busy with the gate.

        try {
            ThreadPool::BlockingRegion outer;
            ThreadPool::BlockingRegion inner;
            // Several grace periods: a double-counted worker would report.
            poll_verify(inner, 4 * k_test_deadlock_grace);
            poll_verify(outer, 4 * k_test_deadlock_grace);
            threw.set_value(false);
        } catch (const luma::RuntimeError&) {
            threw.set_value(true);
        }

        open_gate.set_value();
    });

    ASSERT_EQ(threw_future.wait_for(std::chrono::seconds{10}), std::future_status::ready);
    ASSERT_FALSE(threw_future.get());
}

// ─── Waits at the compensation cap that would not block (review follow-up) ───
//
// The nested-blocking check must only fire for a wait that actually blocks.
// Each test runs Luma code on the only worker of a ThreadPool{1, 0} (no
// compensation possible) while another task sits queued — the exact state in
// which a blocking pool-task wait raises "too many nested blocking tasks".
namespace {

// Evaluate `source` on the sole worker of a saturated pool; returns the result
// as text or "throw:<message>".
std::string eval_on_saturated_worker(const std::string& source) {
    std::promise<std::string> outcome;
    auto outcome_future = outcome.get_future();

    {
        ThreadPool pool{1, 0};

        pool.enqueue([&pool, &outcome, &source] {
            pool.enqueue([] {}); // Queued: no worker is free to run it.

            try {
                outcome.set_value(luma::test::eval(source).to_string());
            } catch (const std::exception& error) {
                outcome.set_value(std::string{"throw:"} + error.what());
            }
        });

        if (outcome_future.wait_for(std::chrono::seconds{30}) != std::future_status::ready) {
            return "timeout";
        }
    }

    return outcome_future.get();
}

} // namespace

static void test_await_of_finished_task_at_cap_does_not_throw() {
    ASSERT_EQ(eval_on_saturated_worker("function integer f() {\n"
                                       "    return await Task.completed(5)\n"
                                       "}\n"
                                       "f()"),
              "5");
}

static void test_task_all_of_finished_tasks_at_cap_does_not_throw() {
    ASSERT_EQ(eval_on_saturated_worker(
                  "Task.all([Task.completed(1), Task.completed(2)]) |> Result.unwrap()"),
              "[1, 2]");
}

static void test_task_map_with_slow_callback_at_cap_does_not_throw() {
    // The callback is work, not a blocked worker: no region may be held while
    // it runs, and the finished task needs no wait at all.
    ASSERT_EQ(eval_on_saturated_worker("Task.map(Task.completed(2), (integer x) -> {\n"
                                       "    mutable integer i = 0\n"
                                       "    while i < 200000 { i += 1 }\n"
                                       "    return x * 21\n"
                                       "}) |> Result.unwrap()"),
              "42");
}

// A scope join that would block forever at the cap reports the error but keeps
// its children; the teardown join (after cancel_all) then resolves the child
// that never started as cancelled — no worker needed — so nothing is left
// unjoined and the join returns.
static void test_task_scope_teardown_joins_unstarted_children_at_cap() {
    std::promise<std::string> outcome;
    auto outcome_future = outcome.get_future();

    // The child: a task that was "enqueued" but never started, with the same
    // abandon handler the VM's spawn installs.
    const auto promise = std::make_shared<std::promise<Value>>();
    const auto completion = std::make_shared<TaskCompletion>();
    completion->set_abandon_handler([promise] {
        promise->set_exception(std::make_exception_ptr(CancelledException{"task cancelled"}));
    });
    auto child_future = promise->get_future().share();

    {
        ThreadPool pool{1, 0, k_test_deadlock_grace};

        pool.enqueue([&pool, &outcome, child_future, completion] {
            pool.enqueue([] {}); // Queued: no worker is free to run it.

            TaskScope scope;
            scope.add_child(std::make_shared<TaskValue>(child_future, scope.token(), completion));
            std::string report;

            try {
                (void)scope.join_all();
                report = "joined";
            } catch (const luma::RuntimeError& error) {
                report = error.what();
            }

            scope.cancel_all();
            const auto results = scope.join_all(TaskScope::JoinMode::Teardown);
            report += results.size() == 1 && results[0].is_null() ? " | torn down" : " | ?";
            outcome.set_value(report);
        });

        ASSERT_EQ(outcome_future.wait_for(std::chrono::seconds{10}), std::future_status::ready);
    }

    const auto report = outcome_future.get();
    ASSERT_NE(report.find("too many nested blocking tasks"), std::string::npos);
    ASSERT_NE(report.find(" | torn down"), std::string::npos);

    // A worker that dequeues the abandoned task later must not run it.
    ASSERT_FALSE(completion->claim_start());
}

static void test_task_completion_claim_and_abandon_are_exclusive() {
    TaskCompletion started;
    ASSERT_TRUE(started.claim_start());
    ASSERT_FALSE(started.abandon_if_not_started());

    bool abandoned_ran = false;
    TaskCompletion abandoned;
    abandoned.set_abandon_handler([&abandoned_ran] { abandoned_ran = true; });
    ASSERT_TRUE(abandoned.abandon_if_not_started());
    ASSERT_TRUE(abandoned_ran);
    ASSERT_FALSE(abandoned.claim_start());
}

// ─── Cancelled joins and lagging blocked counts (second review follow-up) ───

namespace {

// A pool task modelled the way the VM's spawn builds one: a promise, a
// completion carrying the abandon handler, and its own token chained to
// `parent` (the enclosing scope's token).
struct FakeTask {
    std::shared_ptr<std::promise<Value>> promise{std::make_shared<std::promise<Value>>()};
    std::shared_ptr<TaskCompletion> completion{std::make_shared<TaskCompletion>()};
    std::shared_ptr<CancellationToken> token;
    std::shared_ptr<TaskValue> handle;
};

FakeTask make_fake_task(std::shared_ptr<const CancellationToken> parent) {
    FakeTask task;
    task.token = std::make_shared<CancellationToken>(std::move(parent));
    task.completion->set_abandon_handler([promise = task.promise] {
        promise->set_exception(std::make_exception_ptr(CancelledException{"task cancelled"}));
    });
    task.handle = std::make_shared<TaskValue>(task.promise->get_future().share(), task.token,
                                              task.completion);
    return task;
}

// Enqueue `body` as the task's work; skipped when the task was abandoned
// first, exactly like the VM's spawn lambda.
template <typename Body> void enqueue_fake_task(ThreadPool& pool, const FakeTask& task, Body body) {
    pool.enqueue([task, body = std::move(body)]() mutable {
        if (!task.completion->claim_start()) {
            return;
        }

        try {
            task.promise->set_value(body());
        } catch (...) {
            task.promise->set_exception(std::current_exception());
        }

        task.completion->mark_complete();
    });
}

// Join `scope` as handle_task_scope_end does: a failed join is followed by
// cancel_all() and a teardown join.  Returns the failed join's message, or ""
// when the join succeeded.
std::string join_like_scope_end(TaskScope& scope) {
    try {
        (void)scope.join_all();
        return "";
    } catch (const std::exception& error) {
        scope.cancel_all();

        try {
            (void)scope.join_all(TaskScope::JoinMode::Teardown);
        } catch (...) { // NOLINT(bugprone-empty-catch)
            // Children's own failures are secondary here, as in the VM.
        }

        return error.what();
    }
}

} // namespace

// Pool: two workers, no compensation.  W1 runs A (a scope; spawns C, later D).
// W2 starts C (a scope; spawns G, which stays queued) and C waits in its own
// join.  A then joins with G and D queued and both workers waiting: the stall
// is reported, A's teardown abandons D and waits for C.  C's join used to
// ignore cancellation and keep waiting for G (cancelled, but with no worker to
// run it), so A's teardown hung forever.  C's join now notices its cancelled
// scope and resolves G without running it.
static void test_teardown_join_completes_when_child_waits_in_its_own_join() {
    struct Reports {
        std::promise<std::string> a;
        std::promise<std::string> c;
        std::promise<void> c_joining;
    };

    const auto reports = std::make_shared<Reports>();
    auto a_report = reports->a.get_future();
    auto c_report = reports->c.get_future();
    const auto c_joining = reports->c_joining.get_future().share();

    // Heap-allocated so that a regression (a hang) can leak it instead of
    // blocking the whole suite in its joining destructor.
    auto* const pool = new ThreadPool{2, 0, k_test_deadlock_grace};

    pool->enqueue([pool, reports, c_joining] {
        TaskScope scope_a;
        const auto c = make_fake_task(scope_a.token());
        scope_a.add_child(c.handle);

        enqueue_fake_task(*pool, c, [pool, reports, c_token = c.token]() -> Value {
            TaskScope scope_c{nullptr, c_token};
            const auto g = make_fake_task(scope_c.token());
            scope_c.add_child(g.handle);
            enqueue_fake_task(*pool, g, [] { return Value{1}; });

            reports->c_joining.set_value();
            reports->c.set_value(join_like_scope_end(scope_c));
            return Value{2};
        });

        c_joining.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds{20}); // C enters its wait.

        const auto d = make_fake_task(scope_a.token());
        scope_a.add_child(d.handle);
        enqueue_fake_task(*pool, d, [] { return Value{3}; });

        reports->a.set_value(join_like_scope_end(scope_a));
    });

    const bool finished =
        a_report.wait_for(std::chrono::seconds{20}) == std::future_status::ready &&
        c_report.wait_for(std::chrono::seconds{20}) == std::future_status::ready;

    if (!finished) {
        ASSERT_TRUE(finished); // Leaks `pool`: its workers are stuck.
        return;
    }

    delete pool;

    // Whichever join first saw the stall persist reports it; both complete.
    const auto combined = a_report.get() + " | " + c_report.get();
    ASSERT_NE(combined.find("too many nested blocking tasks"), std::string::npos);
}

// A worker whose awaited task has just finished stays counted as blocked
// until it is scheduled and leaves its region.  Sampled in that window, the
// pool looked stalled (every worker "waiting", work queued, cap reached) and
// the error fired although that worker was about to run the queued work.
// The stall must now persist for the grace period before it is reported.
static void test_lagging_blocked_count_is_not_reported_as_stall() {
    std::promise<void> awaited_done;
    const auto awaited_ready = awaited_done.get_future().share();
    std::promise<void> first_waiting;
    const auto first_in_region = first_waiting.get_future().share();
    std::promise<bool> threw;
    auto threw_future = threw.get_future();

    {
        ThreadPool pool{2, 0}; // Default grace.

        pool.enqueue([awaited_ready, &first_waiting] {
            const ThreadPool::BlockingRegion blocking;
            first_waiting.set_value();
            awaited_ready.wait();
            // Runnable again but "not yet scheduled": still counted blocked.
            std::this_thread::sleep_for(std::chrono::milliseconds{150});
        });

        pool.enqueue([&pool, &awaited_done, &threw, first_in_region] {
            first_in_region.wait();
            awaited_done.set_value(); // The first worker's wait is now over.

            auto queued_done = std::make_shared<std::promise<void>>();
            auto queued_ready = queued_done->get_future();
            pool.enqueue([queued_done] { queued_done->set_value(); });

            try {
                ThreadPool::BlockingRegion blocking;

                while (queued_ready.wait_for(std::chrono::milliseconds{5}) !=
                       std::future_status::ready) {
                    blocking.verify_not_deadlocked();
                }

                threw.set_value(false);
            } catch (const luma::RuntimeError&) {
                threw.set_value(true);
            }
        });

        ASSERT_EQ(threw_future.wait_for(std::chrono::seconds{10}), std::future_status::ready);
    }

    ASSERT_FALSE(threw_future.get());
}

int main() {
    // Channel tests
    RUN(test_channel_send_receive);
    RUN(test_channel_fifo_order);
    RUN(test_channel_try_receive_empty);
    RUN(test_channel_send_after_close_throws);
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

    // TaskCompletion tests
    RUN(test_task_completion_subscription_released_on_scope_exit);
    RUN(test_task_completion_wakes_waiter);
    RUN(test_task_completion_subscribe_after_completion_notifies);

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
    RUN(test_cancelled_token_is_visible_to_a_later_thread);
    RUN(test_channel_send_after_close_multiple);
    RUN(test_channel_concurrent_close_during_send);

    // Channel error-type discrimination and ring-buffer wraparound.
    RUN(test_channel_try_receive_empty_then_closed);
    RUN(test_channel_try_send_full_then_closed);
    RUN(test_channel_receive_timeout_on_closed_throws);
    RUN(test_channel_send_timeout_on_closed_throws);
    RUN(test_channel_bounded_ring_wraparound);

    // Review regressions.
    RUN(test_thread_pool_compensates_for_blocked_worker);
    RUN(test_cancellation_token_parent_chain);
    RUN(test_task_scope_cancelled_child_is_not_a_failure);
    RUN(test_task_scope_fail_fast_in_completion_order);
    RUN(test_channel_receive_is_cancellable);
    RUN(test_channel_receive_timeout_is_cancellable);
    RUN(test_channel_send_timeout_is_cancellable);
    RUN(test_channel_receive_timeout_with_huge_timeout_waits);
    RUN(test_thread_pool_reports_nested_blocking_beyond_cap);
    RUN(test_thread_pool_external_wait_at_cap_does_not_throw);
    RUN(test_thread_pool_nested_regions_count_one_worker);
    RUN(test_await_of_finished_task_at_cap_does_not_throw);
    RUN(test_task_all_of_finished_tasks_at_cap_does_not_throw);
    RUN(test_task_map_with_slow_callback_at_cap_does_not_throw);
    RUN(test_task_scope_teardown_joins_unstarted_children_at_cap);
    RUN(test_task_completion_claim_and_abandon_are_exclusive);
    RUN(test_teardown_join_completes_when_child_waits_in_its_own_join);
    RUN(test_lagging_blocked_count_is_not_reported_as_stall);
    return SUMMARY();
}
