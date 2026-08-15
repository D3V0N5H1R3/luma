#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

namespace luma::gui_detail {

// Register a command callback immediately if an active app is available,
// otherwise defer it for binding at command execution time.
void register_or_defer_command_callback(std::shared_ptr<DictionaryValue>& w, const Value& cb_arg) {
    if (!cb_arg.is_callable()) {
        return;
    }

    if (active_app != nullptr) {
        auto cb_id = active_app->allocate_id();
        active_app->register_command_callback(cb_id, cb_arg);
        w->set("_callback_id", Value{cb_id});
    } else {
        w->set(key::deferred_command_callback, cb_arg);
    }
}

// Aggregator -- delegates to per-category registration files.
void register_graphicalui_widgets(const EnvPtr& env, bool sandbox) {
    register_basic_widgets(env);
    register_layout_widgets(env);
    register_chart_widgets(env);
    register_commands_and_subscriptions(env, sandbox);
    register_advanced_widgets(env);
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
