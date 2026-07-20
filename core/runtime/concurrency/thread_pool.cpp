#include "runtime/concurrency/thread_pool.hpp"

#include <cassert>
#include <future>
#include <iostream>
#include <stdexcept>

#include "common/resource_limits.hpp"

namespace luma {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

ThreadPool::ThreadPool(std::size_t thread_count) {
    // hardware_concurrency() may return 0 on some platforms.
    if (thread_count == 0) {
        thread_count = 1;
    }

    worker_states_.reserve(thread_count);
    for (std::size_t i{0}; i < thread_count; ++i) {
        worker_states_.push_back(std::make_unique<WorkerState>());
    }

    // Create worker threads.  If thread creation fails partway through (e.g.
    // the OS thread limit is reached), the threads already created are running
    // and therefore joinable — and destroying a joinable std::thread calls
    // std::terminate.  Tear the partially-built pool down cleanly and rethrow
    // so the std::system_error surfaces as a catchable error (std::system_error
    // derives from std::runtime_error, which the VM loop catches) rather than a
    // hard abort.
    try {
        for (std::size_t i{0}; i < thread_count; ++i) {
            workers_.emplace_back([this, i] { worker_loop(i); });
        }
    } catch (...) {
        // Tear the partially-built pool down cleanly, then rethrow so the
        // std::system_error surfaces as a catchable error.
        request_stop_and_join();

        throw;
    }
}

// Signal all workers to stop and wait for them to finish. The store is done
// under mutex_ so the notify_all() that follows is guaranteed to wake threads
// that are sleeping in the CV wait — the mutex ensures no thread misses the
// wakeup between checking has_any_work() and entering wait().
//
// NOTE: notify_all() wakes every worker thread, each of which then calls
// has_any_work() — an O(workers) scan.  The resulting wake cost is
// O(workers²), which is acceptable for the small pools used in practice
// (hardware_concurrency() worker threads).
//
// Shared by the destructor and the constructor's failure path; joining an
// empty or already-joined set of workers is a no-op, so it is safe to call
// on a partially-constructed pool.
void ThreadPool::request_stop_and_join() noexcept {
    {
        const std::scoped_lock lock{mutex_};

        should_stop_.store(true, std::memory_order_release);
    }

    task_available_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

ThreadPool::~ThreadPool() noexcept {
    request_stop_and_join();

#ifndef NDEBUG
    // After all threads have joined, the task queue should be fully drained.
    for (const auto& ws : worker_states_) {
        assert(ws->local_queue.empty() && "ThreadPool destroyed with pending tasks in queue");
    }
    assert(total_task_count_.load(std::memory_order_relaxed) == 0 &&
           "ThreadPool destroyed with non-zero task count");
#endif
}

// Prefer the worker with the shortest queue (lock-free check using atomic counters).
std::size_t ThreadPool::pick_best_worker() const {
    std::size_t idx = 0;
    std::size_t min_size = worker_states_[0]->queue_size.load(std::memory_order_relaxed);

    for (std::size_t i = 1; i < worker_states_.size(); ++i) {
        const auto size = worker_states_[i]->queue_size.load(std::memory_order_relaxed);
        if (size < min_size) {
            min_size = size;
            idx = i;
            if (size == 0) {
                break; // Can't do better than empty.
            }
        }
    }

    return idx;
}

// Combine the limit check, enqueue, and counter increment under the
// per-worker lock so they are atomic.  This eliminates the TOCTOU window
// where total_task_count_ was incremented before the task was actually
// placed in a queue (risking a counter leak on push_back failure and
// spurious limit rejections during the gap).
//
// ── Soft limit semantics ──
// The task queue limit is a soft cap: concurrent enqueues to different
// workers can overshoot by at most (worker_count − 1) tasks because
// each worker checks total_task_count_ under its *own* local_mutex
// rather than a single global lock.  The practical maximum queue size
// is therefore:
//
//     effective_max = max_task_queue_size + worker_count − 1
//
// This is an intentional trade-off: a global lock would serialise all
// enqueues and become a contention bottleneck, while the bounded
// overshoot is acceptable for resource-limit enforcement.
void ThreadPool::enqueue_to_worker(std::size_t worker_idx, std::function<void()>&& task) {
    WorkerState& state = *worker_states_[worker_idx];

    const std::scoped_lock lock{state.local_mutex};

    if (should_stop_.load(std::memory_order_acquire)) {
        throw std::runtime_error{"enqueue on stopped ThreadPool"};
    }

    // Relaxed suffices — authoritative limit check is under the mutex
    if (total_task_count_.load(std::memory_order_relaxed) >= ResourceLimits::max_task_queue_size) {
        throw std::runtime_error{"task queue is full — too many pending tasks"};
    }

    state.local_queue.push_back(std::move(task));

    // Increment counts only after successful enqueue (under local_mutex to
    // maintain the invariant that queue_size >= local_queue.size() when
    // observed under the lock).
    state.queue_size.fetch_add(1, std::memory_order_relaxed);
    total_task_count_.fetch_add(1, std::memory_order_relaxed);
}

void ThreadPool::enqueue(std::function<void()>&& task) {
    const auto idx = pick_best_worker();

    enqueue_to_worker(idx, std::move(task));

    // Serialise with worker_loop()'s condition-variable wait before notifying.
    //
    // enqueue_to_worker() publishes the task by incrementing queue_size under
    // the per-worker local_mutex — NOT under mutex_, which is the mutex a
    // worker holds while it checks has_any_work() and enters the CV wait.
    // Without synchronising on mutex_ here, this sequence loses the wakeup:
    //   worker: checks has_any_work() == false (queue still empty)
    //   producer: enqueues task, notify_one() wakes nobody (no waiter yet)
    //   worker: registers as a CV waiter and sleeps forever
    // Acquiring mutex_ between the enqueue and the notify closes that window —
    // the worker cannot be between "checked the predicate" and "registered as
    // a waiter" while we notify.  This mirrors the destructor's shutdown
    // pattern (store should_stop_ under mutex_, then notify).
    { const std::scoped_lock lock{mutex_}; }

    task_available_cv_.notify_one();
}

bool ThreadPool::try_dequeue_local(std::size_t worker_id, std::function<void()>& task) {
    WorkerState& state = *worker_states_[worker_id];

    const std::scoped_lock lock{state.local_mutex};

    if (state.local_queue.empty()) {
        return false;
    }

    task = std::move(state.local_queue.front());
    state.local_queue.pop_front();
    // Decrement under local_mutex to keep queue_size in sync with the deque.
    state.queue_size.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

bool ThreadPool::try_steal_from_others(std::size_t worker_id, std::function<void()>& task) {
    std::size_t steal_from = worker_id;
    std::size_t max_size = 0;

    for (std::size_t i = 0; i < worker_states_.size(); ++i) {
        if (i == worker_id) {
            continue;
        }
        const auto size = worker_states_[i]->queue_size.load(std::memory_order_relaxed);
        if (size > max_size) {
            max_size = size;
            steal_from = i;
        }
    }

    if (steal_from == worker_id) {
        return false;
    }

    WorkerState& state = *worker_states_[steal_from];

    const std::scoped_lock lock{state.local_mutex};

    if (state.local_queue.empty()) {
        return false;
    }

    task = std::move(state.local_queue.back());
    state.local_queue.pop_back();
    // Decrement under local_mutex to keep queue_size in sync with the deque.
    state.queue_size.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

bool ThreadPool::has_any_work() const {
    // Use atomic queue_size counters instead of locking each worker mutex.
    // Relaxed is fine — a transient over-count just causes an extra
    // dequeue attempt which is handled by the per-worker lock + empty check.
    for (const auto& ws : worker_states_) {
        if (ws->queue_size.load(std::memory_order_relaxed) > 0) {
            return true;
        }
    }
    return false;
}

bool ThreadPool::fetch_next_task(std::size_t worker_id, std::function<void()>& task) {
    return try_dequeue_local(worker_id, task) || try_steal_from_others(worker_id, task);
}

void ThreadPool::execute_task(std::function<void()>& task) {
    try {
        task();
    } catch (const std::future_error&) { // NOLINT(bugprone-empty-catch)
        // Promise already satisfied (harmless double-set guard).
        // This can occur if the task lambda's inner and outer catch
        // both attempt to call promise->set_exception().
    } catch (const std::exception& exception) {
        // An exception escaped the task lambda — indicates a bug in the
        // task implementation.  Log and continue rather than crash the
        // worker thread (which would permanently reduce the pool size).
        std::cerr << "ThreadPool: unhandled exception in task: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "ThreadPool: unhandled non-standard exception in task\n";
    }

    total_task_count_.fetch_sub(1, std::memory_order_relaxed);
}

void ThreadPool::worker_loop(std::size_t worker_id) {
    while (true) {
        std::function<void()> task;

        if (fetch_next_task(worker_id, task)) {
            execute_task(task);
            continue;
        }

        // No work available — wait on the condition variable.
        std::unique_lock lock{mutex_};

        // Re-check under global lock before sleeping to avoid a lost wakeup.
        if (!has_any_work()) {
            task_available_cv_.wait(lock, [this] {
                if (should_stop_.load(std::memory_order_acquire)) {
                    return true;
                }
                return has_any_work();
            });
        }

        if (should_stop_.load(std::memory_order_acquire) && !has_any_work()) {
            return;
        }
    }
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace luma
