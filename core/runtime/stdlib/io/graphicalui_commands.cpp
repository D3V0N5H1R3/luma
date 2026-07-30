#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <random>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "common/base64_codec.hpp"
#include "runtime/stdlib/io/graphicalui_css.hpp"
#include "runtime/stdlib/io/http_module_request.hpp"

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Command and subscription helpers
// ═══════════════════════════════════════════════════════════

[[nodiscard]] bool is_command_pair(const Value& v) {
    if (!v.is_dictionary()) {
        return false;
    }

    const auto& d = *v.as_dictionary();
    return d.find(key::gui_model) != nullptr && d.find(key::gui_command) != nullptr;
}

// Build a GraphicalUi.HttpResponse record {status, headers, body} from a
// successful fetch result — the payload http_get_full / http_post_full deliver
// inside a result<...>.  Mirrors Http.Response's shape.  Defined in gui_detail
// (not the anonymous namespace) so the unit tests can link against it.
Value build_http_response_record_gui(
    int status, std::string body, const std::vector<std::pair<std::string, std::string>>& headers) {
    auto headers_dict = std::make_shared<DictionaryValue>();

    for (const auto& [name, value] : headers) {
        headers_dict->set(name, Value{value});
    }

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "HttpResponse";
    rec->fields.emplace_back("status", Value{static_cast<std::int64_t>(status)});
    rec->fields.emplace_back("headers", Value{std::move(headers_dict)});
    rec->fields.emplace_back("body", Value{std::move(body)});

    return Value{std::move(rec)};
}

// ═══════════════════════════════════════════════════════════
// Command handlers — one function per command type
// ═══════════════════════════════════════════════════════════

// Forward declarations for handlers that need them.

namespace {

using CommandHandler = void (*)(AppState&, const DictionaryValue&);

// Allow only URL schemes that cannot execute script when handed to
// window.open() or an anchor's href.  Relative URLs (no scheme) are permitted.
// ASCII tab/newline characters are stripped and the scheme is lowercased before
// matching, mirroring how browsers normalise a URL before resolving its scheme,
// so they cannot be used to smuggle a "javascript:" / "vbscript:" scheme past
// this check (e.g. "java\tscript:" or " JavaScript:").
[[nodiscard]] bool has_allowed_url_scheme(std::string url,
                                          std::initializer_list<std::string_view> allowed) {
    std::erase_if(url, [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u == 0x09 || u == 0x0A || u == 0x0D;
    });

    std::size_t start = 0;

    while (start < url.size() && static_cast<unsigned char>(url[start]) <= 0x20) {
        ++start;
    }

    std::string scheme;

    for (std::size_t i = start; i < url.size(); ++i) {
        const auto c = static_cast<unsigned char>(url[i]);

        if (c == ':') {
            return std::ranges::any_of(allowed,
                                       [&scheme](std::string_view s) { return s == scheme; });
        }

        if ((std::isalnum(c) != 0) || c == '+' || c == '-' || c == '.') {
            scheme.push_back(static_cast<char>(std::tolower(c)));
        } else {
            return true; // Non-scheme character before ':' → relative URL.
        }
    }

    return true; // No ':' → relative URL.
}

void cmd_batch(AppState& state, const DictionaryValue& d) {
    const auto* cmds = d.find("commands");

    if ((cmds != nullptr) && cmds->is_array()) {
        // Suppress intermediate renders during batch execution.
        // AtomicBoolGuard restores the original value on all exit paths.
        bool was_suppressed = false;

        {
            const AtomicBoolGuard render_guard{state.render_suppressed, true};
            was_suppressed = render_guard.original;

            // Enqueue all batch sub-commands for iterative processing.
            for (const auto& c : *cmds->as_array()->elements) {
                state.command_queue.push_back(c);
            }

            // Drain the queue iteratively (the drain guard handles re-entrancy).
            drain_command_queue(state);
        } // render_guard restores render_suppressed here.

        // Trigger a single render if we were the outermost batch.
        if (!was_suppressed) {
            render_view(state);
        }
    }
}

void cmd_delay(AppState& state, const DictionaryValue& d) {
    const auto ms = dict_int(d, "milliseconds", 0);
    const auto cb_id = dict_string(d, "_callback_id");

    if (!cb_id.empty() && ms > 0) {
        auto js = std::format("setTimeout(function(){{__gui_event(JSON.stringify("
                              "{{type:'command_result',id:'{}'}}));}},{})",
                              luma::js_string_escape(cb_id), ms);
        webview_eval(state.webview, js.c_str());
    }
}

// Heap payload marshalled from an HTTP worker thread back to the UI thread via
// webview_dispatch.  Only raw strings and the shared hub cross the thread
// boundary — never a Value, because the interpreter is not thread-safe.
struct HttpResultPayload {
    std::shared_ptr<AsyncDispatchHub> hub;
    std::string cb_id;
    bool ok{false};
    bool typed{false}; // true for http_*_full: deliver a GraphicalUi.HttpResponse.
    int status{0};
    std::string body;                                         // Response text on success.
    std::vector<std::pair<std::string, std::string>> headers; // Response headers (typed only).
    std::string error;                                        // Error message on failure.
};

// Runs on the UI thread (posted by the worker through webview_dispatch).  Builds
// the result Value, invokes the Luma callback, and processes its result.  Owns
// the payload and frees it on every path.
void deliver_http_result(webview_t /*w*/, void* arg) {
    const std::unique_ptr<HttpResultPayload> payload{static_cast<HttpResultPayload*>(arg)};
    auto& hub = *payload->hub;

    AppState* app = nullptr;
    {
        const std::lock_guard lock{hub.mutex};

        // The app may have been torn down after this dispatch was queued (e.g.
        // it runs while the webview is depleting its event queue on close).
        if (hub.cancelled) {
            return;
        }

        app = hub.app;
    }

    // Both this delivery and AppState's destructor run on the UI thread, so once
    // we have observed a live app it cannot be destroyed underneath us here.
    if (app == nullptr) {
        return;
    }

    auto callback = app->find_command_callback(payload->cb_id);

    if (callback.is_null() || !callback.is_callable()) {
        return;
    }

    Value result_val;

    if (payload->ok) {
        Value success_val = payload->typed
                                ? build_http_response_record_gui(
                                      payload->status, std::move(payload->body), payload->headers)
                                : Value{std::move(payload->body)};
        result_val = Value{ResultValue::success(std::move(success_val))};
    } else {
        result_val = Value{ResultValue::failure(Value{std::move(payload->error)})};
    }

    std::vector<Value> args{std::move(result_val)};
    auto result = invoke_callable(callback, args, app->loc);

    if (!result.is_null()) {
        process_callback_result(*app, std::move(result));
    }
}

void cmd_http(AppState& state, const DictionaryValue& d) {
    const auto type = dict_string(d, "_command_type");
    const auto url = dict_string(d, "url");
    const auto cb_id = dict_string(d, "_callback_id");
    const auto body = dict_string(d, "body");
    const auto timeout_ms = dict_int(d, "timeout", 0);

    auto callback = state.find_command_callback(cb_id);

    if (callback.is_null() || !callback.is_callable()) {
        return;
    }

    // Derive the HTTP method from the command type (e.g. "http_get" → "GET").
    // Typed variants carry a "_full" suffix (http_get_full) that selects a
    // GraphicalUi.HttpResponse payload; strip it before deriving the method.
    const bool typed = type.size() > 5 && type.ends_with("_full");
    auto method_part = std::string{type.substr(5)}; // strip the "http_" prefix
    if (typed) {
        method_part.erase(method_part.size() - 5); // strip the "_full" suffix
    }
    auto method = std::move(method_part);
    std::ranges::transform(method, method.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    // Extract request headers from the optional headers dictionary.
    std::vector<std::pair<std::string, std::string>> headers;
    const auto* headers_val = d.find("headers");

    if ((headers_val != nullptr) && headers_val->is_dictionary()) {
        headers = extract_headers(*headers_val->as_dictionary(), state.loc);
    }

    // Perform the request natively (raw sockets / TLS) rather than via the
    // webview's fetch(). Native requests are not subject to the browser's
    // same-origin / CORS policy, so the response body is readable even when the
    // server sends no Access-Control-Allow-Origin header (e.g. ordinary web
    // pages).
    //
    // The request runs OFF the UI thread on a detached worker, so the window
    // keeps rendering and processing input while it is in flight. do_http_fetch_text
    // builds no Value and touches no interpreter state, so it is safe to run on
    // a background thread; only the raw response bytes are marshalled back to
    // the UI thread (via webview_dispatch), where the Luma callback is invoked.
    //
    // Shutdown is join-free: the worker keeps the coordination hub alive through
    // its own shared_ptr and, at its single delivery point, checks hub->cancelled
    // under the mutex before dispatching. AppState's destructor sets that flag
    // (and nulls the webview) before the webview is destroyed, so an in-flight
    // request neither hangs shutdown (no join) nor touches freed state. See
    // AsyncDispatchHub.
    constexpr int k_gui_http_default_timeout_ms = 8000;
    const int effective_timeout =
        timeout_ms > 0 ? static_cast<int>(timeout_ms) : k_gui_http_default_timeout_ms;

    auto hub = state.ensure_async_hub();
    const auto loc = state.loc;

    std::thread worker{[hub = std::move(hub), method = std::move(method), url, body,
                        headers = std::move(headers), effective_timeout, cb_id, typed,
                        loc]() mutable {
        auto fetched = do_http_fetch_text(method, url, body, headers, effective_timeout, loc);

        auto payload = std::make_unique<HttpResultPayload>();
        payload->hub = hub;
        payload->cb_id = std::move(cb_id);
        payload->ok = fetched.ok;
        payload->typed = typed;

        if (fetched.ok) {
            payload->status = fetched.status_code;
            payload->body = std::move(fetched.body);
            payload->headers = std::move(fetched.headers);
        } else {
            payload->error = std::move(fetched.error);
        }

        const std::lock_guard lock{hub->mutex};

        if (hub->cancelled || hub->webview == nullptr) {
            return; // App gone — drop the result (payload is freed here).
        }

        // Ownership of the payload transfers to deliver_http_result, which runs
        // on the UI thread and frees it.
        webview_dispatch(hub->webview, &deliver_http_result, payload.release());
    }};

    worker.detach();
}

void cmd_random(AppState& state, const DictionaryValue& d) {
    const auto cb_id = dict_string(d, "_callback_id");
    auto callback = state.find_command_callback(cb_id);

    if (!callback.is_null() && callback.is_callable()) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        const auto* min_v = d.find("min");
        const auto* max_v = d.find("max");
        const double min_val = ((min_v != nullptr) && min_v->is_number()) ? min_v->as_number()
                               : ((min_v != nullptr) && min_v->is_integer())
                                   ? static_cast<double>(min_v->as_integer())
                                   : 0.0;
        const double max_val = ((max_v != nullptr) && max_v->is_number()) ? max_v->as_number()
                               : ((max_v != nullptr) && max_v->is_integer())
                                   ? static_cast<double>(max_v->as_integer())
                                   : 1.0;
        // std::uniform_real_distribution requires a <= b; order the bounds so an
        // inverted range like GraphicalUi.random(10, 1, cb) is well-defined
        // rather than undefined behaviour.
        std::uniform_real_distribution<double> dist{std::min(min_val, max_val),
                                                    std::max(min_val, max_val)};

        std::vector<Value> args{Value{dist(rng)}};
        auto result = invoke_callable(callback, args, state.loc);

        if (!result.is_null()) {
            process_callback_result(state, std::move(result));
        }
    }
}

void cmd_focus(AppState& state, const DictionaryValue& d) {
    const auto widget_id = dict_string(d, "widget_id");

    if (!widget_id.empty()) {
        auto js = std::format("document.getElementById('{}')?.focus()",
                              luma::js_string_escape(widget_id));
        webview_eval(state.webview, js.c_str());
    }
}

void cmd_announce(AppState& state, const DictionaryValue& d) {
    const auto text = dict_string(d, "text");

    if (!text.empty()) {
        auto js =
            std::format("(function(){{var a=document.getElementById('__gui_announcer');"
                        "if(!a){{a=document.createElement('div');a.id='__gui_announcer';"
                        "a.setAttribute('aria-live','assertive');a.setAttribute('role','status');"
                        "a.style.cssText='position:absolute;left:-9999px;width:1px;height:1px;"
                        "overflow:hidden';document.body.appendChild(a);}}"
                        "a.textContent='';setTimeout(function(){{a.textContent='{}';}},100);}})()",
                        luma::js_string_escape(text));
        webview_eval(state.webview, js.c_str());
    }
}

void cmd_write_clipboard(AppState& state, const DictionaryValue& d) {
    const auto text = dict_string(d, "text");
    auto js = std::format("navigator.clipboard.writeText('{}')", luma::js_string_escape(text));
    webview_eval(state.webview, js.c_str());
}

// Route a navigation event through the update function if one is registered.
// Returns true if the update function handled it (caller should return);
// false if the caller should apply the fallback direct-mutation path.
[[nodiscard]] bool try_nav_through_update(AppState& state,
                                          std::shared_ptr<DictionaryValue> nav_msg) {
    if (state.update_fn.is_null() || !state.update_fn.is_callable()) {
        return false;
    }

    std::vector<Value> args{state.model, Value{std::move(nav_msg)}};
    auto result = invoke_callable(state.update_fn, args, state.loc);
    process_callback_result(state, std::move(result));
    return true;
}

// Push the current route onto the route history stack.
void push_route_history(const std::shared_ptr<DictionaryValue>& model) {
    const auto* current = model->find("route");

    if (current == nullptr) {
        return;
    }

    // Copy the current route out before mutating the model below: model->set()
    // may append a new entry to the dictionary's entries vector, reallocating it
    // and invalidating the `current` pointer returned by find().  Reading through
    // the dangling pointer afterwards previously pushed a garbage/empty route.
    Value current_route = *current;

    auto new_history = std::make_shared<ArrayValue>();
    const auto* history = model->find(key::route_history);

    if ((history != nullptr) && history->is_array()) {
        new_history->elements = history->as_array()->elements;
        new_history->ensure_unique();
    }

    new_history->elements->push_back(std::move(current_route));
    model->set(key::route_history, Value{std::move(new_history)});
}

// Pop the most recent route from the route history stack and set it as the current route.
// Returns true if a route was restored, false if history was empty.
[[nodiscard]] bool pop_route_history(const std::shared_ptr<DictionaryValue>& model) {
    auto* history = model->find(key::route_history);

    if ((history == nullptr) || !history->is_array() || history->as_array()->elements->empty()) {
        return false;
    }

    auto new_history = std::make_shared<ArrayValue>();
    new_history->elements = history->as_array()->elements;
    new_history->ensure_unique();
    model->set("route", new_history->elements->back());
    new_history->elements->pop_back();
    model->set(key::route_history, Value{std::move(new_history)});
    return true;
}

void cmd_navigate(AppState& state, const DictionaryValue& d) {
    const auto route = dict_string(d, "route");

    // Maintain route history centrally so navigate_back works regardless of
    // whether the user's update() manages history. Push the current route
    // before transitioning, committing it to the model so update() preserves it.
    if (state.model.is_dictionary()) {
        auto m = clone_dict(state.model.as_dictionary());
        push_route_history(m);
        state.model = Value{std::move(m)};
    }

    // Route navigation through the update function so it remains the single
    // source of model transitions.  If no update function is registered,
    // fall back to direct model mutation for backwards compatibility.
    auto nav_msg = make_dict();
    nav_msg->set(key::gui_nav, Value{std::string{nav::navigate}});
    nav_msg->set("route", Value{route});

    if (try_nav_through_update(state, std::move(nav_msg))) {
        return;
    }

    // Fallback: direct model mutation (no update function).
    if (state.model.is_dictionary()) {
        auto new_model = clone_dict(state.model.as_dictionary());
        new_model->set("route", Value{route});
        update_model_and_render(state, Value{std::move(new_model)});
    }
}

void cmd_navigate_back(AppState& state, const DictionaryValue& /*d*/) {
    if (!state.model.is_dictionary()) {
        return;
    }

    auto m = clone_dict(state.model.as_dictionary());

    // Pop the previous route from the centrally-maintained history. If history
    // is empty there is nothing to go back to.
    if (!pop_route_history(m)) {
        return;
    }

    const auto prev_route = dict_string(*m, "route");

    // Commit the popped history, then transition the route through update()
    // exactly like a forward navigation so derived state stays consistent.
    state.model = Value{std::move(m)};

    auto nav_msg = make_dict();
    nav_msg->set(key::gui_nav, Value{std::string{nav::navigate}});
    nav_msg->set("route", Value{prev_route});

    if (try_nav_through_update(state, std::move(nav_msg))) {
        return;
    }

    // Fallback: no update function — render the popped model directly.
    update_model_and_render(state, state.model);
}

void cmd_stylesheet(AppState& state, const DictionaryValue& d) {
    auto css = dict_string(d, "css");

    if (!css.empty()) {
        auto js = std::format("__gui_inject_css('{}')", luma::js_string_escape(css));
        webview_eval(state.webview, js.c_str());
    }
}

void cmd_load_stylesheet(AppState& state, const DictionaryValue& d) {
    auto path = dict_string(d, "path");
    auto base_dir = dict_string(d, "base_dir");

    if (path.empty() || base_dir.empty()) {
        return;
    }

    // Resolve relative to the source file directory.
    auto resolved = std::filesystem::path{base_dir} / path;
    resolved = std::filesystem::weakly_canonical(resolved);

    // Security: verify the resolved path is under the base directory.
    // Use std::filesystem::relative() — if the result starts with ".."
    // the path escapes the base directory (handles symlinks, case, trailing slashes).
    auto base_canonical = std::filesystem::weakly_canonical(std::filesystem::path{base_dir});

    std::error_code ec;
    auto rel = std::filesystem::relative(resolved, base_canonical, ec);

    if (ec || rel.empty() || *rel.begin() == "..") {
        return; // Path traversal attempt — silently ignore.
    }

    // Deduplicate by resolved path.
    auto resolved_str = resolved.string();

    if (state.loaded_stylesheet_paths.contains(resolved_str)) {
        return;
    }

    state.loaded_stylesheet_paths.insert(resolved_str);

    // Check file size before reading to enforce the 1 MB limit without
    // loading potentially large files into memory.
    std::error_code size_ec;
    auto file_size = std::filesystem::file_size(resolved, size_ec);

    if (size_ec) {
        throw RuntimeError{error_msg("GraphicalUi", "load_stylesheet",
                                     std::format("cannot open '{}'", resolved.string())),
                           state.loc};
    }

    if (file_size > 1024ULL * 1024) {
        return; // File too large — silently ignore.
    }

    auto css = read_file_to_string(resolved);

    // read_file_to_string returns empty on I/O failure; file_size > 0 rules out
    // the legitimate-empty-file case, so empty here means the open failed.
    if (css.empty() && file_size > 0) {
        throw RuntimeError{error_msg("GraphicalUi", "load_stylesheet",
                                     std::format("cannot open '{}'", resolved.string())),
                           state.loc};
    }

    if (!css.empty()) {
        // ── Strict CSS sanitisation ────────────────────
        // Tokenise the CSS and reject any token that looks dangerous rather
        // than relying on fragile string stripping.
        auto sanitised = sanitise_loaded_css(css);

        auto js = std::format("__gui_inject_css('{}')", luma::js_string_escape(sanitised));
        webview_eval(state.webview, js.c_str());
    }
}

void cmd_font_face(AppState& state, const DictionaryValue& d) {
    auto path = dict_string(d, "path");
    auto base_dir = dict_string(d, "base_dir");
    auto family = dict_string(d, "family");
    auto weight = dict_string(d, "weight", "normal");
    auto style = dict_string(d, "style", "normal");
    const bool set_default = dict_bool(d, "set_default", true);

    if (path.empty() || base_dir.empty() || family.empty()) {
        return;
    }

    // Extension → MIME/format. Should always resolve (validated at construction);
    // bail out defensively if it does not.
    const auto fmt = font_format_for_path(path);

    if (!fmt.has_value()) {
        return;
    }

    // Resolve relative to the working directory.
    auto resolved = std::filesystem::path{base_dir} / path;
    resolved = std::filesystem::weakly_canonical(resolved);

    // Security: verify the resolved path is still under the base directory (see
    // cmd_load_stylesheet — guards against traversal, symlinks, case, slashes).
    auto base_canonical = std::filesystem::weakly_canonical(std::filesystem::path{base_dir});

    std::error_code ec;
    auto rel = std::filesystem::relative(resolved, base_canonical, ec);

    if (ec || rel.empty() || *rel.begin() == "..") {
        return; // Path traversal attempt — silently ignore.
    }

    // Deduplicate by resolved path so re-returning the command (e.g. from every
    // update) does not re-embed the same font. Shared with load_stylesheet: both
    // inject a keyed <style>, and font/CSS extensions never collide.
    auto resolved_str = resolved.string();

    if (state.loaded_stylesheet_paths.contains(resolved_str)) {
        return;
    }

    // Check the file size before reading to bound the inlined payload (fonts are
    // legitimately large, so the ceiling is higher than for stylesheets).
    std::error_code size_ec;
    auto file_size = std::filesystem::file_size(resolved, size_ec);

    if (size_ec) {
        throw RuntimeError{error_msg("GraphicalUi", "font_face",
                                     std::format("cannot open '{}'", resolved.string())),
                           state.loc};
    }

    if (file_size > 8ULL * 1024 * 1024) {
        return; // Font too large — silently ignore.
    }

    auto bytes = read_file_to_string(resolved);

    // read_file_to_string returns empty on I/O failure; file_size > 0 rules out
    // the legitimate-empty-file case, so empty here means the open failed.
    if (bytes.empty() && file_size > 0) {
        throw RuntimeError{error_msg("GraphicalUi", "font_face",
                                     std::format("cannot open '{}'", resolved.string())),
                           state.loc};
    }

    if (bytes.empty()) {
        return;
    }

    state.loaded_stylesheet_paths.insert(resolved_str);

    auto encoded = luma::base64_encode(bytes);

    // Build the @font-face rule. family/weight/style were validated at command
    // construction (safe to embed), and the data: URI is built from a trusted
    // local file, so this bypasses the url()-blocking inline-CSS validator by
    // design. font-display: swap keeps first paint from blocking on the font.
    auto css =
        std::format("@font-face{{font-family:\"{}\";src:url(data:{};base64,{}) format(\"{}\");"
                    "font-weight:{};font-style:{};font-display:swap;}}",
                    family, fmt->mime, encoded, fmt->format, weight, style);

    // Optionally make this the default UI font. A :root rule (not an inline
    // style) is used so that an explicit theme "font" — applied as an inline
    // style on the document element — still takes precedence.
    if (set_default) {
        css += std::format(":root{{--gui-font:\"{}\",system-ui,sans-serif;}}", family);
    }

    auto js = std::format("__gui_inject_css('{}')", luma::js_string_escape(css));
    webview_eval(state.webview, js.c_str());
}

void cmd_set_theme_mode(AppState& state, const DictionaryValue& d) {
    auto mode = dict_string(d, "mode");

    if (mode == "light" || mode == "dark" || mode == "auto") {
        auto js = std::format("__gui_set_theme_mode('{}')", luma::js_string_escape(mode));
        webview_eval(state.webview, js.c_str());
    }
}

void cmd_read_clipboard(AppState& state, const DictionaryValue& d) {
    const auto cb_id = dict_string(d, "_callback_id");

    if (!cb_id.empty()) {
        auto js = std::format(
            "(function(){{navigator.clipboard.readText().then(function(t){{"
            "__gui_event(JSON.stringify({{type:'command_result',id:'{0}',value:t}}));"
            "}}).catch(function(e){{"
            "__gui_event(JSON.stringify({{type:'command_error',id:'{0}',value:e.message}}));"
            "}})}})()",
            cb_id);
        webview_eval(state.webview, js.c_str());
    }
}

void cmd_get_local_storage(AppState& state, const DictionaryValue& d) {
    const auto key = dict_string(d, "key");
    const auto cb_id = dict_string(d, "_callback_id");

    if (!cb_id.empty()) {
        auto js = std::format(
            "(function(){{var v=localStorage.getItem('{0}');"
            "__gui_event(JSON.stringify({{type:'command_result',id:'{1}',value:v==null?'':v}}));"
            "}})()",
            luma::js_string_escape(key), cb_id);
        webview_eval(state.webview, js.c_str());
    }
}

void cmd_set_local_storage(AppState& state, const DictionaryValue& d) {
    const auto key = dict_string(d, "key");
    const auto val = dict_string(d, "value");
    auto js = std::format("localStorage.setItem('{}','{}')", luma::js_string_escape(key),
                          luma::js_string_escape(val));
    webview_eval(state.webview, js.c_str());
}

void cmd_remove_local_storage(AppState& state, const DictionaryValue& d) {
    const auto key = dict_string(d, "key");
    auto js = std::format("localStorage.removeItem('{}')", luma::js_string_escape(key));
    webview_eval(state.webview, js.c_str());
}

void cmd_clear_local_storage(AppState& state, const DictionaryValue& /*d*/) {
    webview_eval(state.webview, "localStorage.clear()");
}

void cmd_scroll_to(AppState& state, const DictionaryValue& d) {
    const auto widget_id = dict_string(d, "widget_id");
    const auto behavior = dict_string(d, "behavior");

    if (!widget_id.empty()) {
        // Only "smooth" and "instant"/"auto" are valid scrollIntoView behaviours;
        // default to "smooth" for anything else so a stray value can't inject.
        const std::string safe_behavior =
            (behavior == "instant" || behavior == "auto") ? "auto" : "smooth";
        auto js = std::format(
            "document.getElementById('{}')?.scrollIntoView({{behavior:'{}',block:'nearest'}})",
            luma::js_string_escape(widget_id), safe_behavior);
        webview_eval(state.webview, js.c_str());
    }
}

void cmd_blur(AppState& state, const DictionaryValue& d) {
    const auto widget_id = dict_string(d, "widget_id");

    // With an id, blur that element; without one, blur whatever currently has
    // focus (the DOM active element).
    auto js = widget_id.empty()
                  ? std::string{"document.activeElement && document.activeElement.blur()"}
                  : std::format("document.getElementById('{}')?.blur()",
                                luma::js_string_escape(widget_id));
    webview_eval(state.webview, js.c_str());
}

void cmd_download_file(AppState& state, const DictionaryValue& d) {
    const auto url = dict_string(d, "url");
    const auto filename = dict_string(d, "filename");

    // Downloads legitimately use data:/blob: URLs (client-generated files), but
    // javascript:/vbscript: must never reach the anchor href.
    if (url.empty() || !has_allowed_url_scheme(url, {"http", "https", "data", "blob"})) {
        return;
    }

    auto js = std::format(
        "(function(){{var "
        "a=document.createElement('a');a.href='{}';a.download='{}';a.style.display='none';"
        "document.body.appendChild(a);a.click();document.body.removeChild(a);}})()",
        luma::js_string_escape(url), luma::js_string_escape(filename));
    webview_eval(state.webview, js.c_str());
}

void cmd_notify(AppState& state, const DictionaryValue& d) {
    const auto title = dict_string(d, "title");
    const auto body = dict_string(d, "body");
    const auto icon = dict_string(d, "icon");

    // Build JS options object inline — only includes non-empty fields.
    std::string opts = "{";
    auto escaped_body = luma::js_string_escape(body);
    auto escaped_icon = luma::js_string_escape(icon);

    if (!escaped_body.empty()) {
        opts += "body:'" + escaped_body + "'";
    }

    if (!escaped_icon.empty()) {
        if (opts.size() > 1) {
            opts += ',';
        }

        opts += "icon:'" + escaped_icon + "'";
    }

    opts += '}';

    auto escaped_title = luma::js_string_escape(title);
    auto js = std::format("(function(){{if('Notification' in window){{"
                          "if(Notification.permission==='granted'){{new Notification('{}',{});}}"
                          "else "
                          "if(Notification.permission!=='denied'){{Notification.requestPermission()"
                          ".then(function(p){{"
                          "if(p==='granted'){{new Notification('{}',{});}}"
                          "}});}}"
                          "}}}})()",
                          escaped_title, opts, escaped_title, opts);
    webview_eval(state.webview, js.c_str());
}

void cmd_open_url(AppState& state, const DictionaryValue& d) {
    const auto url = dict_string(d, "url");

    // Reject schemes that would execute script (e.g. javascript:, vbscript:,
    // data:) — only ordinary navigation targets may reach window.open().
    if (!url.empty() && has_allowed_url_scheme(url, {"http", "https", "mailto", "tel"})) {
        auto js = std::format("window.open('{}','_blank')", luma::js_string_escape(url));
        webview_eval(state.webview, js.c_str());
    }
}

void cmd_set_title(AppState& state, const DictionaryValue& d) {
    const auto title = dict_string(d, "title");

    if (!title.empty()) {
        webview_set_title(state.webview, title.c_str());
    }
}

void cmd_print(AppState& state, const DictionaryValue& /*d*/) {
    webview_eval(state.webview, "window.print()");
}

void cmd_debounce(AppState& state, const DictionaryValue& d) {
    const auto debounce_id = dict_string(d, "debounce_id");
    const auto ms = dict_int(d, "milliseconds", 0);
    const auto cb_id = dict_string(d, "_callback_id");

    if (!debounce_id.empty() && !cb_id.empty() && ms > 0) {
        // The debounce id is interpolated into a JS *string literal* (an object
        // key), never an identifier, so js_string_escape fully neutralises it and
        // ids containing hyphens, spaces, or dots work correctly.
        auto js = std::format(
            "(function(){{var t=window.__gui_debounce||(window.__gui_debounce={{}});"
            "if(t['{0}'])clearTimeout(t['{0}']);"
            "t['{0}']=setTimeout(function(){{"
            "__gui_event(JSON.stringify({{type:'command_result',id:'{1}'}}));}},{2})}})()",
            luma::js_string_escape(debounce_id), luma::js_string_escape(cb_id), ms);
        webview_eval(state.webview, js.c_str());
    }
}

// ── Command dispatch table ──────────────────────────────

// Lazily-initialised on first use so construction (which allocates) happens in a
// catchable context rather than during unsequenced static initialisation.
[[nodiscard]] const std::unordered_map<std::string_view, CommandHandler>& command_dispatch() {
    static const std::unordered_map<std::string_view, CommandHandler> table = {
        {cmd::batch, cmd_batch},
        {cmd::delay, cmd_delay},
        {cmd::http_get, cmd_http},
        {cmd::http_post, cmd_http},
        {cmd::http_get_full, cmd_http},
        {cmd::http_post_full, cmd_http},
        {cmd::random, cmd_random},
        {cmd::focus, cmd_focus},
        {cmd::announce, cmd_announce},
        {cmd::write_clipboard, cmd_write_clipboard},
        {cmd::read_clipboard, cmd_read_clipboard},
        {cmd::get_local_storage, cmd_get_local_storage},
        {cmd::set_local_storage, cmd_set_local_storage},
        {cmd::remove_local_storage, cmd_remove_local_storage},
        {cmd::clear_local_storage, cmd_clear_local_storage},
        {cmd::scroll_to, cmd_scroll_to},
        {cmd::blur, cmd_blur},
        {cmd::download_file, cmd_download_file},
        {cmd::notify, cmd_notify},
        {cmd::navigate, cmd_navigate},
        {cmd::navigate_back, cmd_navigate_back},
        {cmd::stylesheet, cmd_stylesheet},
        {cmd::load_stylesheet, cmd_load_stylesheet},
        {cmd::font_face, cmd_font_face},
        {cmd::set_theme_mode, cmd_set_theme_mode},
        {cmd::http_put, cmd_http},
        {cmd::http_delete, cmd_http},
        {cmd::http_patch, cmd_http},
        {cmd::open_url, cmd_open_url},
        {cmd::set_title, cmd_set_title},
        {cmd::print, cmd_print},
        {cmd::debounce, cmd_debounce},
    };
    return table;
}

} // anonymous namespace

void execute_command(AppState& state, const Value& cmd) {
    if (cmd.is_null() || !cmd.is_dictionary()) {
        return;
    }

    // Headless mode has no webview to evaluate JS against. All command handlers
    // ultimately call webview_eval(state.webview, ...), so skip command
    // execution entirely when running without a window.
    if (state.webview == nullptr) {
        return;
    }

    auto& d = *cmd.as_dictionary();
    const auto type = dict_string(d, "_command_type");

    if (type.empty() || type == cmd::none) {
        return;
    }

    // Deferred command callback binding: if the command was created without
    // an active app context, the callback is stored as key::deferred_command_callback.
    // Bind it now that we are inside execute_command (which always has app context).
    const auto* deferred_cb = d.find(key::deferred_command_callback);

    if ((deferred_cb != nullptr) && deferred_cb->is_callable()) {
        auto cb_id = state.allocate_id();
        state.register_command_callback(cb_id, *deferred_cb);
        d.set("_callback_id", Value{cb_id});
        d.erase(key::deferred_command_callback);
    }

    const auto& dispatch = command_dispatch();
    const auto it = dispatch.find(type);

    if (it != dispatch.end()) {
        it->second(state, d);
    }
}

void process_callback_result(AppState& state, Value result) {
    if (result.is_null()) {
        return;
    }

    if (is_command_pair(result)) {
        auto& d = *result.as_dictionary();
        auto cmd = *d.find(key::gui_command);
        update_model_and_render(state, *d.find(key::gui_model));

        // Enqueue the command for iterative processing instead of
        // recursive execute_command → process_callback_result calls.
        state.command_queue.push_back(std::move(cmd));
        drain_command_queue(state);
    } else {
        update_model_and_render(state, std::move(result));
    }
}

// Apply the result of an event callback (button click, input change, key press,
// timer tick, command result).  GraphicalUi supports two callback styles:
//   - "return the new model"  — a structured value (number, dictionary, record,
//     boolean, array) or a (model, command) pair via with_command.
//   - "return a message"      — a string (Elm style, e.g. () -> "inc") or a typed
//     `choice` message (Solaris style, e.g. () -> Msg.Increment), both handled by
//     update(model, msg).
// A string or choice result is therefore routed through update when an update
// function is defined; every other value is applied directly as the new model.
// Apps whose model is itself a string or a choice type should return models
// directly (omit update) so their state is not mistaken for a message.
//
// As a special case, a navigation_link delivers its message as a plain string.
// The reserved "navigate:<route>" / "navigate_back" convention (mirroring the
// GraphicalUi.navigate / navigate_back commands) is interpreted here so the
// click drives the same routing flow — reaching update() as a structured
// {_gui_nav, route} message — instead of being passed to update() verbatim,
// which a dictionary-typed update would crash on when indexing the raw string.
void apply_event_result(AppState& state, Value result) {
    if (result.is_string()) {
        const auto& msg = result.as_string();
        constexpr std::string_view nav_prefix{nav::navigate_prefix};

        if (msg.starts_with(nav_prefix)) {
            auto nav_cmd = make_dict();
            nav_cmd->set("route", Value{msg.substr(nav_prefix.size())});
            cmd_navigate(state, *nav_cmd);
            return;
        }

        if (msg == nav::navigate_back) {
            cmd_navigate_back(state, *make_dict());
            return;
        }
    }

    // A string message (Elm style) or a typed `choice` message (Solaris style) is
    // delivered to update(model, msg); this is what lets typed messages flow
    // through clicks and keyboard events without the view computing update itself.
    const bool is_message = result.is_string() || result.is_choice();

    if (is_message && !state.update_fn.is_null() && state.update_fn.is_callable()) {
        std::vector<Value> args{state.model, std::move(result)};
        auto updated = invoke_callable(state.update_fn, args, state.loc);
        process_callback_result(state, std::move(updated));
        return;
    }

    process_callback_result(state, std::move(result));
}

void drain_command_queue(AppState& state) {
    // Re-entrancy guard: if we are already draining, let the outer loop
    // pick up newly enqueued commands — avoids recursive draining.
    if (state.draining_commands) {
        return;
    }

    // ValueGuard restores draining_commands to its original value on all exit paths.
    const ValueGuard drain_guard{state.draining_commands, true};

    // Process commands iteratively.  Commands may enqueue further commands
    // (e.g. batch sub-commands, or synchronous command results that produce
    // new commands), which are picked up in subsequent iterations.
    while (!state.command_queue.empty()) {
        auto cmd = std::move(state.command_queue.front());
        state.command_queue.pop_front();
        execute_command(state, cmd);
    }
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
