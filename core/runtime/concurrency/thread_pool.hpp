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

    // RAII marker for a pool worker that is about to block until other pool
    // work finishes (await, task_scope join, a blocking channel wait, …).
    //
    // A fixed-size pool deadlocks when every worker blocks on work that is
    // still queued: nothing is left to run it.  While a region is active the
    // worker counts as blocked; when every worker is blocked and work is
    // pending (or arrives later), the pool starts a compensating worker so the
    // queued work can make progress.  On a thread that is not a pool worker
    // (e.g. the main VM thread) the region is a no-op.
    class BlockingRegion {
    public:
        BlockingRegion();
        ~BlockingRegion();

        BlockingRegion(const BlockingRegion&) = delete;
        BlockingRegion& operator=(const BlockingRegion&) = delete;
        BlockingRegion(BlockingRegion&&) = delete;
        BlockingRegion& operator=(BlockingRegion&&) = delete;

    private:
        ThreadPool* pool_;
    };

    // Number of worker threads currently running (base workers plus any
    // compensating workers started for blocked workers).  For tests and
    // diagnostics.
    [[nodiscard]] std::size_t worker_count() const noexcept;

private:
    // Upper bound on compensating workers started on top of the base pool, so
    // a program that blocks unboundedly cannot exhaust OS threads.
    static constexpr std::size_t k_max_compensating_workers{256};

    // Start one more worker if every live worker is blocked and work is
    // pending.  Safe to call from any thread; a no-op once stopping.
    void maybe_add_compensating_worker();
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

    // Per-worker data — each base worker has its own queue and mutex to reduce
    // contention.  Workers first drain their own queue, then steal from others.
    // The vector is fixed-size after construction (compensating workers own no
    // queue), so it is safe to scan without a lock.
    struct WorkerState {
        std::deque<std::function<void()>> local_queue;
        mutable std::mutex local_mutex;
        // Approximate size — always updated under local_mutex but read locklessly
        // for work-stealing heuristics. May transiently over-count (safe: just
        // means an extra steal attempt) but must never under-count when checked
        // under the lock.
        std::atomic<std::size_t> queue_size{0};
    };

    std::vector<std::thread> workers_; // Grows only under mutex_ (compensation).
    std::vector<std::unique_ptr<WorkerState>> worker_states_;
    std::atomic<std::size_t> total_task_count_{0}; // Atomic pending task counter.
    std::mutex mutex_;                             // Guards the condition variable.
    std::condition_variable task_available_cv_;
    std::atomic<bool> should_stop_{false};
    std::atomic<std::size_t> live_workers_{0};    // Started worker threads.
    std::atomic<std::size_t> blocked_workers_{0}; // Workers inside a BlockingRegion.
};

} // namespace luma

#endif // LUMA_CONCURRENCY_THREAD_POOL_HPP
