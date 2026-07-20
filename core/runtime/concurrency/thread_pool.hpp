#ifndef LUMA_CONCURRENCY_THREAD_POOL_HPP
#define LUMA_CONCURRENCY_THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace luma {

// ThreadPool manages a pool of worker threads for running async tasks.
// The interpreter owns one ThreadPool instance for the lifetime of the program.
// The destructor drains the pending task queue before joining all threads.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count = std::thread::hardware_concurrency());

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    // Move operations are deleted because the thread pool holds running threads
    // that capture `this`; moving would invalidate those pointers.
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ~ThreadPool() noexcept;

    /// @note The queue size limit is a soft cap — see thread_pool.cpp for details.
    void enqueue(std::function<void()>&& task);

private:
    // Signal all workers to stop and join every joinable worker.  Shared by
    // the destructor and the constructor's failure path so the concurrency-
    // critical "store should_stop_ under mutex_, notify, then join" sequence
    // lives in one place.  noexcept — safe to call during stack unwinding.
    void request_stop_and_join() noexcept;

    void worker_loop(std::size_t worker_id);

    // Select the worker with the shortest queue for a new task.
    [[nodiscard]] std::size_t pick_best_worker() const;

    // Enqueue a task to the specified worker, enforcing the queue size limit.
    void enqueue_to_worker(std::size_t worker_idx, std::function<void()>&& task);

    // Fetch the next task for this worker (local or stolen). Returns true on success.
    [[nodiscard]] bool fetch_next_task(std::size_t worker_id, std::function<void()>& task);

    // Execute a single task with exception safety.
    void execute_task(std::function<void()>& task);

    // Try to dequeue a task from the worker's own local queue.
    // Returns true and populates `task` on success.
    [[nodiscard]] bool try_dequeue_local(std::size_t worker_id, std::function<void()>& task);

    // Try to steal a task from the worker with the longest queue.
    // Returns true and populates `task` on success.
    [[nodiscard]] bool try_steal_from_others(std::size_t worker_id, std::function<void()>& task);

    // Check whether any worker queue has pending tasks.
    [[nodiscard]] bool has_any_work() const;

    // Per-worker data — each worker has its own queue and mutex to reduce
    // contention.  Workers first drain their own queue, then steal from others.
    struct WorkerState {
        std::deque<std::function<void()>> local_queue;
        mutable std::mutex local_mutex;
        // Approximate size — always updated under local_mutex but read locklessly
        // for work-stealing heuristics. May transiently over-count (safe: just
        // means an extra steal attempt) but must never under-count when checked
        // under the lock.
        std::atomic<std::size_t> queue_size{0};
    };

    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<WorkerState>> worker_states_;
    std::atomic<std::size_t> total_task_count_{0}; // Atomic pending task counter.
    std::mutex mutex_;                             // Guards the condition variable.
    std::condition_variable task_available_cv_;
    std::atomic<bool> should_stop_{false};
};

} // namespace luma

#endif // LUMA_CONCURRENCY_THREAD_POOL_HPP
