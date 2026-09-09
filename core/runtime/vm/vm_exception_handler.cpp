#include "runtime/vm/vm_exception_handler.hpp"

namespace luma {

void VMExceptionHandler::push(ExceptionHandler handler) {
    manager_.push_handler(handler);
}

ExceptionHandler VMExceptionHandler::pop() {
    return manager_.pop_handler();
}

void VMExceptionHandler::discard() {
    manager_.pop_handler_discard();
}

bool VMExceptionHandler::has_handler_for(std::size_t base_depth) const {
    return manager_.has_handler_for(base_depth);
}

const ExceptionHandler& VMExceptionHandler::current() const {
    return manager_.current();
}

bool VMExceptionHandler::empty() const noexcept {
    return manager_.empty();
}

std::size_t VMExceptionHandler::size() const noexcept {
    return manager_.size();
}

void VMExceptionHandler::clear() noexcept {
    manager_.clear();
}

} // namespace luma
