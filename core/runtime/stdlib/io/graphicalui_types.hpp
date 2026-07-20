#ifndef LUMA_STDLIB_GRAPHICALUI_TYPES_HPP
#define LUMA_STDLIB_GRAPHICALUI_TYPES_HPP

// Type definitions for the GraphicalUi module.
// Contains all struct, class, enum, and using declarations shared between
// graphicalui_module.cpp, graphicalui_serialization.cpp,
// graphicalui_commands.cpp, graphicalui_events.cpp, and graphicalui_widgets*.cpp.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

#include "runtime/interpreter/value.hpp"

#ifdef LUMA_HAS_WEBVIEW

#include <atomic>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "analysis/source/source_location.hpp"
#include "common/lru_cache.hpp"
#include "common/string_hash.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "webview.h"

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Structured subscription configuration for type-safe diffing
// ═══════════════════════════════════════════════════════════

// Parsed configuration for GraphicalUi.app().
// Extracted from the dictionary argument before webview creation.
struct AppConfig {
    std::string title{"Luma Application"};
    int width{800};
    int height{600};
    int min_width{0};     // Minimum window width  (0 = no constraint).
    int min_height{0};    // Minimum window height (0 = no constraint).
    int max_width{0};     // Maximum window width  (0 = no constraint).
    int max_height{0};    // Maximum window height (0 = no constraint).
    bool resizable{true}; // Allow the user to resize the window.
    bool devtools{false};
    bool maximized{false}; // Start the window maximized / full screen.
    bool allow_remote_images{
        false}; // Load http(s) <img> sources (off by default; data:/blob: always allowed).
    std::string persist_path; // File path for one-line model persistence ("" = disabled).
    Value initial_model;
    Value init_fn;      // User-provided init() function, invoked in init_app_state.
    Value init_command; // Deferred command from init(), executed after webview starts.
    Value view_fn;
    Value update_fn;
    Value on_error_fn;
    Value subscribe_fn;
    const Value* theme_ptr{nullptr};
};

struct SubscriptionConfig {
    std::string sub_type;        // "timer", "keyboard", "resize", "focus", "mouse"
    std::int64_t interval{0};    // timer interval (ms)
    std::string filter;          // keyboard filter or mouse event type
    std::int64_t throttle_ms{0}; // mouse throttle (ms)

    [[nodiscard]] bool operator==(const SubscriptionConfig& other) const {
        return sub_type == other.sub_type && interval == other.interval && filter == other.filter &&
               throttle_ms == other.throttle_ms;
    }
};

// ═══════════════════════════════════════════════════════════
// Application state — managed per GraphicalUi.app() call
// ═══════════════════════════════════════════════════════════

// Callback lifetime tags for the unified callback store.
enum class CallbackLifetime : std::uint8_t {
    render,
    command,
    subscription
};

struct CallbackEntry {
    CallbackLifetime lifetime;
    Value fn;
};

// Memoized render result for a component: the hash of the model slice that
// produced it, paired with the rendered widget value.  Stored in an LruCache
// keyed by component id, giving O(1) hash-based memoization with LRU eviction.
struct CachedSlice {
    std::size_t slice_hash;
    Value rendered;
};

// Hot reload state for dev asset modification tracking.
struct DevModeState {
    std::filesystem::file_time_type css_mtime{};
    std::filesystem::file_time_type js_mtime{};
    bool enabled{false};
};

// Forward declaration so AsyncDispatchHub can hold a back-pointer to the app.
struct AppState;

// Coordination point shared between the UI thread and detached async worker
// threads (off-thread HTTP; see cmd_http).  It is owned by AppState via a
// shared_ptr, but every worker also holds its own shared_ptr, so the hub always
// outlives any worker.  All fields are guarded by `mutex`.
//
// Shutdown protocol (no join, no hang, no use-after-free):
//   - A worker, at its single delivery point, locks the mutex and only calls
//     webview_dispatch when `cancelled` is false and `webview` is non-null.
//   - AppState's destructor locks the mutex, sets `cancelled`, and clears
//     `app`/`webview` — and, by declaration order, this runs BEFORE the webview
//     is destroyed.  After that no worker will dispatch, and any dispatch
//     already queued is dropped when it runs because it re-checks `cancelled`.
// Because the request itself runs before the lock is taken, the destructor only
// ever waits for the brief post-a-message step, never for the HTTP round-trip.
struct AsyncDispatchHub {
    std::mutex mutex;
    bool cancelled{false};
    webview_t webview{nullptr};
    AppState* app{nullptr};
};

struct AppState {
    std::mutex mutex;

    // Unified callback store: all render, command, and subscription callbacks
    // live in a single map, distinguished by a lifetime tag.
    StringMap<CallbackEntry> callbacks;

    StringMap<SubscriptionConfig> active_subs; // sub_id → typed config for value-based diffing.

    // Component memoization cache — O(1) LRU with hash-based invalidation.
    static constexpr std::size_t component_cache_capacity = 128;
    LruCache<std::string, CachedSlice> component_cache{component_cache_capacity};

    Value subscribe_fn; // subscribe(model) → array<subscription>.
    std::atomic<std::int64_t> next_id{1};
    std::atomic<std::int64_t> generation{0}; // Render generation counter.
    webview_t webview{nullptr};
    Value model;
    Value update_fn;
    Value view_fn;
    Value on_error_fn; // Optional error view function.
    SourceLocation loc;
    std::atomic<bool> managing_subscriptions{false};         // Re-entrancy guard.
    std::atomic<bool> render_suppressed{false};              // Command-batch render coalescing.
    std::atomic<std::int64_t> window_width{800};             // Current window width.
    std::unordered_set<std::string> loaded_stylesheet_paths; // Deduplicate by path.

    // Serialized JSON of the widget tree currently shown in the webview.  Host-
    // side render de-duplication compares each new frame's JSON against this and
    // skips the webview_eval when they are byte-for-byte identical, avoiding a
    // redundant IPC round-trip and a full-tree re-serialization in the JS
    // renderer.  An empty string means "force the next render" (e.g. after the
    // framework JS is hot-reloaded, or an error view is shown via the innerHTML
    // fallback).  Touched only on the UI thread (like `model`), so no locking.
    std::string prev_tree_json;

    // Command queue — commands are enqueued during callback processing and
    // drained iteratively after event handling, avoiding deep recursion
    // between execute_command ↔ process_callback_result.
    std::deque<Value> command_queue;
    bool draining_commands{false}; // Re-entrancy guard for drain_command_queue.

    // Hot reload state.
    DevModeState dev_mode;

    [[nodiscard]] std::string allocate_id() {
        return std::format("_gui_{}_{}", generation.load(), next_id.fetch_add(1));
    }

    // Advance the generation counter (called at each render cycle).
    void advance_generation() {
        generation.fetch_add(1);
    }

    // ── Unified callback registration and lookup ─────

    // Register a callback with the given lifetime.
    void register_callback(const std::string& id, Value fn,
                           CallbackLifetime lifetime = CallbackLifetime::render) {
        const std::lock_guard lock{mutex};
        callbacks[id] = {lifetime, std::move(fn)};
    }

    // Find a callback by ID (any lifetime).
    [[nodiscard]] Value find_callback(const std::string& id) {
        const std::lock_guard lock{mutex};
        auto it = callbacks.find(id);

        if (it != callbacks.end()) {
            return it->second.fn;
        }

        return Value{NullValue{}};
    }

    // Find a callback by ID and expected lifetime.
    [[nodiscard]] Value find_callback(const std::string& id, CallbackLifetime lifetime) {
        const std::lock_guard lock{mutex};
        auto it = callbacks.find(id);

        if (it != callbacks.end() && it->second.lifetime == lifetime) {
            return it->second.fn;
        }

        return Value{NullValue{}};
    }

    // Clear render-lifetime callbacks and advance the generation counter.
    void clear_render_callbacks() {
        const std::lock_guard lock{mutex};
        advance_generation();
        std::erase_if(callbacks, [](const auto& pair) {
            return pair.second.lifetime == CallbackLifetime::render;
        });
    }

    // Remove a single callback by ID.
    void erase_callback(const std::string& id) {
        const std::lock_guard lock{mutex};
        callbacks.erase(id);
    }

    // ── Convenience wrappers (preserve call-site readability) ─

    void register_command_callback(const std::string& id, Value fn) {
        register_callback(id, std::move(fn), CallbackLifetime::command);
    }

    void register_sub_callback(const std::string& id, Value fn) {
        register_callback(id, std::move(fn), CallbackLifetime::subscription);
    }

    [[nodiscard]] Value find_command_callback(const std::string& id) {
        return find_callback(id, CallbackLifetime::command);
    }

    [[nodiscard]] Value find_sub_callback(const std::string& id) {
        return find_callback(id, CallbackLifetime::subscription);
    }

    void erase_sub_callback(const std::string& id) {
        erase_callback(id);
    }

    // ── O(1) LRU component cache ─────────────────────

    [[nodiscard]] CachedSlice* find_cached_component(const std::string& id) {
        return component_cache.get(id);
    }

    void cache_component(const std::string& id, std::size_t hash, Value rendered) {
        static_cast<void>(component_cache.put(id, CachedSlice{hash, std::move(rendered)}));
    }

    // ── Cached dev asset directory ───────────────────────
    // Cached result of the LUMA_GUI_DEV_ASSETS env var, read once at init.
    std::filesystem::path cached_dev_dir;

    // ── Async worker coordination (off-thread HTTP) ──────
    // Created lazily on first async command.  See AsyncDispatchHub for the
    // shutdown protocol that lets detached workers finish without a join.
    std::shared_ptr<AsyncDispatchHub> async_hub;

    // Lazily create (once) the async coordination hub, wiring it to this
    // AppState and its webview.  Called on the UI thread only.
    [[nodiscard]] std::shared_ptr<AsyncDispatchHub> ensure_async_hub() {
        if (!async_hub) {
            async_hub = std::make_shared<AsyncDispatchHub>();
            async_hub->webview = webview;
            async_hub->app = this;
        }

        return async_hub;
    }

    AppState() = default;

    // Signals detached async workers that the app is gone.  Runs on the UI
    // thread before the webview is destroyed (see declaration order in the app
    // lifecycle), so no worker will call webview_dispatch on a dead webview and
    // no queued dispatch will touch this destroyed AppState.
    ~AppState() {
        if (async_hub) {
            const std::lock_guard lock{async_hub->mutex};
            async_hub->cancelled = true;
            async_hub->app = nullptr;
            async_hub->webview = nullptr;
        }
    }

    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;
    AppState(AppState&&) = delete;
    AppState& operator=(AppState&&) = delete;
};

// RAII guard for webview lifetime — ensures webview_destroy and active_app
// reset on all exit paths (normal return, exception, early throw).
struct WebviewGuard {
    webview_t handle;

    explicit WebviewGuard(webview_t h) noexcept : handle{h} {}

    WebviewGuard(const WebviewGuard&) = delete;
    WebviewGuard& operator=(const WebviewGuard&) = delete;
    WebviewGuard(WebviewGuard&&) = delete;
    WebviewGuard& operator=(WebviewGuard&&) = delete;

    ~WebviewGuard() noexcept {
        if (handle != nullptr) {
            webview_destroy(handle);
        }
    }
};

// Generic RAII guard that restores a value on destruction.
// Replaces one-off SuppressGuard / DrainGuard structs.
template <typename T> struct ValueGuard {
    T& ref;
    T original;

    explicit ValueGuard(T& r) noexcept : ref{r}, original{r} {}

    ValueGuard(T& r, T new_value) noexcept : ref{r}, original{r} {
        ref = new_value;
    }

    ValueGuard(const ValueGuard&) = delete;
    ValueGuard& operator=(const ValueGuard&) = delete;
    ValueGuard(ValueGuard&&) = delete;
    ValueGuard& operator=(ValueGuard&&) = delete;

    ~ValueGuard() noexcept {
        ref = original;
    }
};

// RAII guard for std::atomic<bool> — saves the current value on construction
// and restores it on destruction.  Used for re-entrancy guards on atomic flags
// (e.g. managing_subscriptions, render_suppressed).
struct AtomicBoolGuard {
    std::atomic<bool>& flag;
    bool original;

    explicit AtomicBoolGuard(std::atomic<bool>& f) noexcept : flag{f}, original{f.exchange(true)} {}

    AtomicBoolGuard(std::atomic<bool>& f, bool new_value) noexcept
        : flag{f}, original{f.exchange(new_value)} {}

    AtomicBoolGuard(const AtomicBoolGuard&) = delete;
    AtomicBoolGuard& operator=(const AtomicBoolGuard&) = delete;
    AtomicBoolGuard(AtomicBoolGuard&&) = delete;
    AtomicBoolGuard& operator=(AtomicBoolGuard&&) = delete;

    ~AtomicBoolGuard() noexcept {
        flag.store(original);
    }
};

// Thread-local pointer to the active application state.
// Set during GraphicalUi.app() execution via ActiveAppScope RAII guard.
// Interactive widgets (button, checkbox, dropdown, slider, etc.) require
// an active app context to register callbacks.  They must only be called
// from within a view function passed to GraphicalUi.app().
extern thread_local AppState* active_app;

// RAII guard for setting/clearing the thread-local active_app pointer.
struct ActiveAppScope {
    ActiveAppScope() noexcept = default;

    explicit ActiveAppScope(AppState& state) noexcept {
        active_app = &state;
    }

    void set(AppState* state) noexcept {
        active_app = state;
    }

    ActiveAppScope(const ActiveAppScope&) = delete;
    ActiveAppScope& operator=(const ActiveAppScope&) = delete;
    ActiveAppScope(ActiveAppScope&&) = delete;
    ActiveAppScope& operator=(ActiveAppScope&&) = delete;

    ~ActiveAppScope() noexcept {
        active_app = nullptr;
    }
};

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW

#endif // LUMA_STDLIB_GRAPHICALUI_TYPES_HPP
