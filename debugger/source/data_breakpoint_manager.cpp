#include "data_breakpoint_manager.hpp"

namespace luma::dap {

// ─── Data breakpoints ───

void DataBreakpointManager::set_data_breakpoint(const std::string& variable_name,
                                                const std::string& access_type,
                                                const std::string& condition) {
    const std::scoped_lock lock(ctx_->mutex);
    data_breakpoints_.insert_or_assign(variable_name,
                                       DataBreakpointInfo{.variable_name = variable_name,
                                                          .access_type = access_type,
                                                          .condition = condition});
}

void DataBreakpointManager::clear_data_breakpoints() {
    const std::scoped_lock lock(ctx_->mutex);
    data_breakpoints_.clear();
}

bool DataBreakpointManager::check_data_breakpoint(
    const std::string& variable_name, const ConditionEvaluatorFn& eval_condition) const {
    std::string condition;

    {
        const std::scoped_lock lock(ctx_->mutex);

        auto it = data_breakpoints_.find(variable_name);
        if (it == data_breakpoints_.end()) {
            return false;
        }

        condition = it->second.condition;
    }

    if (condition.empty()) {
        return true;
    }

    if (eval_condition) {
        auto result = eval_condition(condition);
        return result == "true";
    }

    return true;
}

bool DataBreakpointManager::has_any_breakpoints() const {
    return !data_breakpoints_.empty();
}

} // namespace luma::dap
