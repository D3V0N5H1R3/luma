#include "thread_state_manager.hpp"

#include <algorithm>
#include <cassert>

#include "runtime/vm/vm.hpp"

namespace luma::dap {

void ThreadStateManager::clear() {
    const auto lock = lock_states();
    thread_state_map_.clear();
    paused_count_.store(0, std::memory_order_release);
}

void ThreadStateManager::add_thread(std::shared_ptr<ThreadState> ts) {
    const auto lock = lock_states();
    thread_state_map_[ts->thread_id] = std::move(ts);
}

void ThreadStateManager::remove_thread(int thread_id) {
    const auto lock = lock_states();
    auto it = thread_state_map_.find(thread_id);

    if (it == thread_state_map_.end()) {
        return;
    }

    {
        const auto ts_lock = lock_state(*it->second);

        if (it->second->is_paused) {
            decrement_paused_count();
        }

        it->second->vm = nullptr;
    }

    thread_state_map_.erase(it);
}

void ThreadStateManager::null_all_vms() {
    const auto lock = lock_states();

    for (auto& [id, state] : thread_state_map_) {
        const auto ts_lock = lock_state(*state);
        state->vm = nullptr;
    }
}

void ThreadStateManager::force_unpause_all() {
    const auto lock = lock_states();

    for (auto& [id, state] : thread_state_map_) {
        const auto ts_lock = lock_state(*state);

        if (state->is_paused) {
            state->is_paused = false;
            decrement_paused_count();
        }

        state->cv.notify_all();

        if (state->vm != nullptr) {
            state->vm->request_pause_check();
        }
    }
}

std::shared_ptr<ThreadState> ThreadStateManager::get_thread(int thread_id) const {
    const auto lock = lock_states();
    auto it = thread_state_map_.find(thread_id);

    if (it != thread_state_map_.end()) {
        return it->second;
    }

    return nullptr;
}

bool ThreadStateManager::is_thread_valid(int thread_id) const {
    return get_thread(thread_id) != nullptr;
}

std::shared_ptr<ThreadState> ThreadStateManager::current_thread() const {
    return get_thread(tl_debug_thread_id);
}

std::vector<std::pair<int, std::string>> ThreadStateManager::get_threads() const {
    std::vector<std::pair<int, std::string>> result;

    {
        const auto lock = lock_states();

        for (const auto& [id, state] : thread_state_map_) {
            result.emplace_back(id, state->name);
        }
    }

    std::ranges::sort(result, [](const auto& a, const auto& b) { return a.first < b.first; });

    return result;
}

bool ThreadStateManager::all_threads_stopped() const {
    const auto lock = lock_states();
    return count_paused_locked() >= static_cast<int>(thread_state_map_.size());
}

void ThreadStateManager::increment_paused_count() {
    paused_count_.fetch_add(1, std::memory_order_release);
}

void ThreadStateManager::decrement_paused_count() {
    paused_count_.fetch_sub(1, std::memory_order_release);
}

int ThreadStateManager::paused_count() const noexcept {
    return paused_count_.load(std::memory_order_acquire);
}

int ThreadStateManager::count_paused_locked() const {
#ifndef NDEBUG
    assert(detail::dap_lock_is_held(DapLockId::ThreadStates) &&
           "count_paused_locked() requires the thread states lock");
#endif
    int count = 0;

    for (const auto& [id, state] : thread_state_map_) {
        const auto ts_lock = lock_state(*state);

        if (state->is_paused) {
            ++count;
        }
    }

    return count;
}

void ThreadStateManager::signal_all_vms_pause_check() {
    const auto lock = lock_states();

    for (auto& [id, state] : thread_state_map_) {
        const auto ts_lock = lock_state(*state);

        if (state->vm != nullptr) {
            state->vm->request_pause_check();
        }
    }
}

std::vector<std::shared_ptr<ThreadState>> ThreadStateManager::all_threads_snapshot() const {
    std::vector<std::shared_ptr<ThreadState>> result;
    const auto lock = lock_states();

    result.reserve(thread_state_map_.size());
    for (const auto& [id, state] : thread_state_map_) {
        result.push_back(state);
    }

    return result;
}

OrderedLockGuard<DapLockId> ThreadStateManager::lock_states() const {
    return OrderedLockGuard<DapLockId>(states_mutex_, DapLockId::ThreadStates);
}

OrderedLockGuard<DapLockId> ThreadStateManager::lock_state(ThreadState& ts) const {
    return OrderedLockGuard<DapLockId>(ts.mutex, DapLockId::PerThread);
}

OrderedUniqueLock<DapLockId> ThreadStateManager::lock_state_unique(ThreadState& ts) const {
    return OrderedUniqueLock<DapLockId>(ts.mutex, DapLockId::PerThread);
}

} // namespace luma::dap
