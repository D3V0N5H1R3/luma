#include "runtime/vm/vm_exception_manager.hpp"

#include <string>

#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

namespace {

void require_non_empty(const char* context) {
    // This is intentionally a helper that always throws — the caller
    // only invokes it on the [[unlikely]] path after checking empty().
    throw RuntimeError{std::string{context} + ": exception handler stack is empty", {}};
}

} // namespace

void VMExceptionManager::push_handler(ExceptionHandler handler) {
    if (handlers_.size() >= k_max_depth) [[unlikely]] {
        throw RuntimeError{std::string{vm_errors::too_many_exception_handlers}, {}};
    }
    handlers_.push_back(handler);
}

ExceptionHandler VMExceptionManager::pop_handler() {
    if (handlers_.empty()) [[unlikely]] {
        require_non_empty("pop_handler");
    }
    auto handler = handlers_.back();
    handlers_.pop_back();
    return handler;
}

void VMExceptionManager::pop_handler_discard() {
    if (handlers_.empty()) [[unlikely]] {
        require_non_empty("pop_handler_discard");
    }
    handlers_.pop_back();
}

bool VMExceptionManager::has_handler_for(std::size_t base_depth) const {
    return !handlers_.empty() && handlers_.back().frame_index >= base_depth;
}

const ExceptionHandler& VMExceptionManager::current() const {
    if (handlers_.empty()) [[unlikely]] {
        require_non_empty("current");
    }
    return handlers_.back();
}

} // namespace luma
