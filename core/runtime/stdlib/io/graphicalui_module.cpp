#include "runtime/stdlib/io/graphicalui_module.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/graphicalui_css.hpp"
#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
// The bundled webview backend already pulls in the GTK development headers on
// Linux (webview.h includes <gtk/gtk.h>), so call GTK's real
// gtk_window_maximize(GtkWindow*) directly. A local `extern "C"` forward
// declaration taking a void* — used before the WebKitGTK headers were on the
// include path — clashes with GTK's own prototype once they are.
#include <gtk/gtk.h>
#endif

namespace luma {

namespace {

// Maximise the application window using the native window handle exposed by the
// webview backend. macOS opens at the configured size (no-op there).
void maximize_native_window([[maybe_unused]] webview_t webview) {
#if defined(_WIN32)
    if (auto* hwnd = static_cast<HWND>(webview_get_window(webview))) {
        ShowWindow(hwnd, SW_MAXIMIZE);
    }
#elif defined(__linux__)
    if (auto* window = static_cast<GtkWindow*>(webview_get_window(webview))) {
        gtk_window_maximize(window);
    }
#endif
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Module registration
// ═══════════════════════════════════════════════════════════

// ─── ModuleBuilder note ──────────────────────────────────────────────────────
// This file intentionally uses `define_native` directly rather than
// `ModuleBuilder`.  The GraphicalUi module has a fundamentally different
// architecture from other stdlib modules:
//   1. `GraphicalUi.app` runs a blocking OS-native event loop (webview_run)
//      and manages the entire application lifecycle — it is not a regular
//      function call.
//   2. The module is split across multiple .cpp files (commands, events,
//      widgets, CSS, serialisation) to manage complexity.
//   3. The whole module is conditionally compiled behind `#ifdef
//      LUMA_HAS_WEBVIEW`; the stub path iterates a name list to register
//      stubs dynamically, which does not map to the builder chain.
//   4. Several functions capture and mutate thread-local webview state
//      (active_app) that is tightly coupled to the webview callback model.
// ─────────────────────────────────────────────────────────────────────────────

// ═══════════════════════════════════════════════════════════
// Headless execution (LUMA_GUI_HEADLESS)
// ═══════════════════════════════════════════════════════════

// Run the application's Luma-side lifecycle without creating a window:
// render the initial view, evaluate subscriptions, and apply any scripted
// update messages. This exercises and validates the example's view/update/
// subscribe logic for unattended testing. No webview_* calls are made.
namespace {

// Generate a 128-bit random nonce, hex-encoded, for the page's
// Content-Security-Policy. The bootstrap <script> carries this nonce so it is
// the only inline script the web view will execute; injected or reflected
// scripts without the nonce are blocked. webview_eval-driven code (e.g.
// __gui_render) is host-initiated and not subject to script-src.
[[nodiscard]] std::string generate_csp_nonce() {
    std::random_device rd;
    const std::array<std::uint32_t, 4> parts{rd(), rd(), rd(), rd()};
    std::string nonce;
    nonce.reserve(parts.size() * 8);
    for (const auto part : parts) {
        std::format_to(std::back_inserter(nonce), "{:08x}", part);
    }
    return nonce;
}

void run_gui_headless(gui_detail::AppState& state, const gui_detail::AppConfig& cfg,
                      SourceLocation loc) {
    using namespace gui_detail;
    auto render_once = [&]() -> std::size_t {
        std::vector<Value> view_args{state.model};
        auto tree = invoke_callable(state.view_fn, view_args, loc);
        return value_to_json(tree).size();
    };

    const auto initial_bytes = render_once();
    std::cout << "[gui-headless] " << cfg.title << ": initial render OK (" << initial_bytes
              << " JSON bytes)\n";

    if (!state.subscribe_fn.is_null() && state.subscribe_fn.is_callable()) {
        std::vector<Value> sub_args{state.model};
        (void)invoke_callable(state.subscribe_fn, sub_args, loc);
        std::cout << "[gui-headless] subscriptions evaluated\n";
    }

    if (state.update_fn.is_null() || !state.update_fn.is_callable()) {
        return;
    }

    for (const auto& msg : gui_headless_messages()) {
        try {
            std::vector<Value> update_args{state.model, Value{msg}};
            auto result = invoke_callable(state.update_fn, update_args, loc);

            if (is_command_pair(result)) {
                if (const auto* model_ptr = result.as_dictionary()->find(key::gui_model)) {
                    state.model = *model_ptr;
                }
            } else {
                state.model = std::move(result);
            }

            const auto bytes = render_once();
            std::cout << "[gui-headless] update(\"" << msg << "\") OK (" << bytes
                      << " JSON bytes)\n";
        } catch (const std::exception& ex) {
            std::cout << "[gui-headless] update(\"" << msg << "\") raised: " << ex.what() << "\n";
        }
    }
}

} // namespace

void register_graphicalui_ns(const EnvPtr& env, bool sandbox) {
    using namespace gui_detail;

    // ─── Constants ───────────────────────────────────────

    gui_detail::register_graphicalui_constants(env);

    // ─── Register all widgets, commands, subscriptions ───

    register_graphicalui_widgets(env, sandbox);

    // ─── Headless interaction-testing API (GraphicalUi.test_*) ───

    register_graphicalui_testing(env);

    // ─── Application lifecycle ───────────────────────────

    // GraphicalUi.app(config) -> none
    // config is a dictionary with:
    //   "title"     : string                       — window title
    //   "width"     : integer                      — window width  (default: 800)
    //   "height"    : integer                      — window height (default: 600)
    //   "min_width" / "min_height" : integer       — minimum window size (resizable only)
    //   "max_width" / "max_height" : integer       — maximum window size (resizable only)
    //   "resizable" : boolean                      — allow window resize (default: true)
    //   "devtools"  : boolean                      — enable developer tools (default: false)
    //   "model"     : any                          — initial model state
    //   "init"      : func(model) -> model|pair    — init function (optional, overrides "model")
    //   "update"    : func(model, msg) -> model    — update function
    //   "view"      : func(model) -> widget        — view function
    //   "subscribe" : func(model) -> array         — subscription function (optional)
    define_native(
        env, "GraphicalUi.app", [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_args("GraphicalUi.app", args, 1, loc);

            (void)expect_dict(args[0], "GraphicalUi.app", loc);

            const auto& config = *args[0].as_dictionary();

            // ── Extract and validate configuration ──
            auto cfg = extract_app_config(config, loc);

            // ── Headless mode: run the lifecycle without a window ──
            if (gui_detail::gui_headless_enabled()) {
                ActiveAppScope app_scope;
                AppState state;
                init_app_state(state, cfg, nullptr, loc);
                app_scope.set(&state);
                run_gui_headless(state, cfg, loc);

                if (!cfg.persist_path.empty()) {
                    save_persisted_model(cfg.persist_path, state.model);
                }

                return Value{NullValue{}};
            }

            // ── Create the webview ──
            auto* w = webview_create(cfg.devtools ? 1 : 0, nullptr);

            if (!w) {
                throw RuntimeError{"GraphicalUi.app: failed to create window (webview runtime "
                                   "unavailable)",
                                   loc,
                                   "ensure WebView2 (Windows), WebKitGTK (Linux), or WebKit "
                                   "(macOS) is installed"};
            }

            // RAII guard — ensures webview_destroy on all exit paths.
            const WebviewGuard guard{w};

            // Set thread-local active app scope (RAII).
            ActiveAppScope app_scope;

            webview_set_title(w, cfg.title.c_str());

            // A non-resizable window is created with the FIXED hint, which both
            // sets the size and removes the resize/maximise affordances. A
            // resizable window optionally gains minimum and maximum size
            // constraints (the MIN/MAX hints only store the bounds, so the base
            // size is applied first).
            if (cfg.resizable) {
                webview_set_size(w, cfg.width, cfg.height, WEBVIEW_HINT_NONE);

                if (cfg.min_width > 0 && cfg.min_height > 0) {
                    webview_set_size(w, cfg.min_width, cfg.min_height, WEBVIEW_HINT_MIN);
                }

                if (cfg.max_width > 0 && cfg.max_height > 0) {
                    webview_set_size(w, cfg.max_width, cfg.max_height, WEBVIEW_HINT_MAX);
                }
            } else {
                webview_set_size(w, cfg.width, cfg.height, WEBVIEW_HINT_FIXED);
            }

            if (cfg.maximized) {
                maximize_native_window(w);
            }

            // ── Initialise application state ──
            AppState state;
            init_app_state(state, cfg, w, loc);

            // Set thread-local active app so widget functions can register callbacks.
            app_scope.set(&state);

            // Bind the event handler (before first render so IDs are resolvable).
            webview_bind(w, "__gui_event", on_gui_event, &state);

            // Build the initial widget tree by calling the view function.
            // Style-dict event handlers are processed inline during widget
            // construction (finalize_widget), so no separate tree walk is needed.
            std::vector<Value> view_args{state.model};
            auto tree = invoke_callable(state.view_fn, view_args, state.loc);
            auto initial_json = value_to_json(tree);

            // Embed the widget framework JS and initial render directly in the
            // HTML page.  This avoids the race between webview_init / webview_eval
            // and page load on WebView2.
            //
            // SECURITY: initial_json and theme_js are interpolated inside the
            // <script> element below.  value_to_json escapes '/' (so "</script>"
            // in user data becomes "<\/script>"), which is what stops the data
            // from breaking out of the script context — see value_to_json.
            std::string theme_js;

            if (cfg.theme_ptr && cfg.theme_ptr->is_dictionary()) {
                theme_js = "\n__gui_apply_theme(" + value_to_json(*cfg.theme_ptr) + ");";
            }

            auto framework_js = build_gui_framework_js();
            auto framework_css = build_gui_framework_css();
            const auto* devtools_flag = cfg.devtools ? "window.__gui_devtools=true;" : "";
            const auto* remote_images_flag =
                cfg.allow_remote_images ? "window.__gui_allow_remote_images=true;" : "";

            // Strict Content-Security-Policy for the host page. The whole UI is
            // first-party and self-contained: markup, CSS and JS are inlined,
            // icons are inline SVG, and there is no fetch/XHR/WebSocket (HTTP
            // goes through the native runtime). So everything is denied by
            // default and only the specific things the framework needs are
            // allowed:
            //   - script-src 'nonce-...'  the bootstrap <script> only
            //   - style-src 'unsafe-inline'  lit-html sets inline style attrs
            //   - img-src  data:/blob: only by default; widened to remote
            //     (https:/http:) when the app opts in with allow_remote_images
            //   - font-src data:  inlined fonts only
            //   - connect-src 'none'  no network from the page itself
            const auto nonce = generate_csp_nonce();
            const auto* img_src =
                cfg.allow_remote_images ? "https: http: data: blob:" : "data: blob:";
            const auto csp = std::format("default-src 'none'; "
                                         "script-src 'nonce-{}'; "
                                         "style-src 'unsafe-inline'; "
                                         "img-src {}; "
                                         "font-src data:; "
                                         "connect-src 'none'; "
                                         "base-uri 'none'; "
                                         "form-action 'none'; "
                                         "frame-src 'none'; "
                                         "object-src 'none'",
                                         nonce, img_src);

            auto html = std::format(
                "<html><head>"
                "<meta http-equiv=\"Content-Security-Policy\" content=\"{}\">"
                "<style>{}</style></head>"
                "<body><div id=\"gui-root\"></div>"
                "<script nonce=\"{}\">{}{}{}{}\n__gui_render({});</script></body></html>",
                csp, framework_css, nonce, devtools_flag, remote_images_flag, framework_js,
                theme_js, initial_json);
            webview_set_html(w, html.c_str());

            // Seed the host-side render de-dup cache with the tree baked into
            // the initial HTML, so the first post-init render_view is correctly
            // skipped when the view has not changed (C++ now owns the identity
            // check that the JS renderer used to perform on every frame).
            state.prev_tree_json = std::move(initial_json);

            // If init() returned a command, execute it now that the webview
            // framework is loaded.
            manage_subscriptions(state);

            if (!cfg.init_command.is_null()) {
                execute_command(state, cfg.init_command);
            }

            // Run the event loop (blocks until window is closed).
            webview_run(w);

            // One-line persistence: write the final model back to disk now that
            // the window has closed. Best-effort — never blocks or throws.
            if (!cfg.persist_path.empty()) {
                save_persisted_model(cfg.persist_path, state.model);
            }

            // Guard destructor handles cleanup (webview_destroy + active_app = nullptr).
            return Value{NullValue{}};
        });

    // ─── Style helpers ───────────────────────────────────

    // GraphicalUi.style(properties) -> dictionary
    define_native(env, "GraphicalUi.style",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.style", args, 1, loc);
                      (void)expect_dict(args[0], "GraphicalUi.style", loc);
                      return args[0];
                  });

    // GraphicalUi.merge_styles(styles...) -> dictionary
    define_native(env, "GraphicalUi.merge_styles",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.merge_styles", args, 2, loc);

                      auto result = make_dict();

                      for (const auto& arg : args) {
                          if (arg.is_null()) {
                              continue;
                          }

                          (void)expect_dict(arg, "GraphicalUi.merge_styles", loc);
                          const auto& d = *arg.as_dictionary();

                          for (const auto& [k, v] : d.entries) {
                              result->set(k, v);
                          }
                      }

                      return Value{std::move(result)};
                  });

    // §3: Stylesheet injection command.
    define_native(env, "GraphicalUi.stylesheet",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.stylesheet", args, 1, loc);

                      auto css = expect_string(args[0], "GraphicalUi.stylesheet", loc);

                      // Reject CSS containing dangerous content (script injection, unsafe schemes).
                      gui_detail::validate_inline_css(css, loc);

                      auto w = make_command_dict(cmd::stylesheet);
                      w->set("css", Value{std::move(css)});
                      return Value{std::move(w)};
                  });

    // §8: External stylesheet loading command.
    define_native(env, "GraphicalUi.load_stylesheet",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.load_stylesheet", args, 1, loc);

                      auto path = expect_string(args[0], "GraphicalUi.load_stylesheet", loc);

                      gui_detail::validate_stylesheet_path(path, loc);

                      auto base_dir = std::filesystem::current_path().string();

                      auto w = make_command_dict(cmd::load_stylesheet);
                      w->set("path", Value{std::move(path)});
                      w->set("base_dir", Value{std::move(base_dir)});
                      return Value{std::move(w)};
                  });

    // §8: Bundle a local font file as the UI font.  Reads a .woff2/.woff/.ttf/
    // .otf file, base64-inlines it into an @font-face rule (satisfying the
    // font-src data: CSP), and — unless "default": false — sets it as the app
    // font via --gui-font.  The file is read when the command runs, so the path
    // is only validated (not opened) here.
    define_native(env, "GraphicalUi.font_face",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.font_face", args, 2, loc);

                      auto path = expect_string(args[0], "GraphicalUi.font_face", loc);
                      auto family = expect_string(args[1], "GraphicalUi.font_face", loc);

                      gui_detail::validate_font_path(path, loc);
                      gui_detail::validate_font_family(family, loc);

                      std::string weight = "normal";
                      std::string style = "normal";
                      bool set_default = true;

                      if (args.size() >= 3 && args[2].is_dictionary()) {
                          const auto& opts = *args[2].as_dictionary();
                          weight = gui_detail::dict_string(opts, "weight", "normal");
                          style = gui_detail::dict_string(opts, "style", "normal");
                          set_default = gui_detail::dict_bool(opts, "default", true);
                      }

                      gui_detail::validate_font_weight(weight, loc);
                      gui_detail::validate_font_style(style, loc);

                      auto base_dir = std::filesystem::current_path().string();

                      auto w = make_command_dict(cmd::font_face);
                      w->set("path", Value{std::move(path)});
                      w->set("base_dir", Value{std::move(base_dir)});
                      w->set("family", Value{std::move(family)});
                      w->set("weight", Value{std::move(weight)});
                      w->set("style", Value{std::move(style)});
                      w->set("set_default", Value{set_default});
                      return Value{std::move(w)};
                  });

    // §6: Theme mode override command.
    define_native(env, "GraphicalUi.set_theme_mode",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.set_theme_mode", args, 1, loc);

                      auto mode = expect_string(args[0], "GraphicalUi.set_theme_mode", loc);

                      if (mode != "light" && mode != "dark" && mode != "auto") {
                          throw RuntimeError{"GraphicalUi.set_theme_mode: mode must be \"light\", "
                                             "\"dark\", or \"auto\"",
                                             loc};
                      }

                      auto w = make_command_dict(cmd::set_theme_mode);
                      w->set("mode", Value{std::move(mode)});
                      return Value{std::move(w)};
                  });

    // §6: Typed theme-mode override command.
    // GraphicalUi.set_theme_mode_of(mode) -> command
    // Typo-proof companion to set_theme_mode: takes a GraphicalUi.ThemeMode choice
    // and lowers it to the same "light"/"dark"/"auto" command the string form
    // builds.  Mirrors alert_of/severity bridging.
    define_native(env, "GraphicalUi.set_theme_mode_of",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.set_theme_mode_of", args, 1, loc);

                      auto w = make_command_dict(cmd::set_theme_mode);
                      w->set("mode", Value{gui_detail::theme_mode_to_lower(args[0])});
                      return Value{std::move(w)};
                  });

    // GraphicalUi.theme_mode_to_string(mode) -> string
    // Bridge from the GraphicalUi.ThemeMode choice to the "light"/"dark"/"auto"
    // string accepted by the string-based set_theme_mode command.
    define_native(env, "GraphicalUi.theme_mode_to_string",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.theme_mode_to_string", args, 1, loc);
                      return Value{gui_detail::theme_mode_to_lower(args[0])};
                  });
    define_native(env, "GraphicalUi.responsive",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.responsive", args, 1, loc);
                      (void)expect_dict(args[0], "GraphicalUi.responsive", loc);

                      auto& app = gui_detail::require_active_app("responsive");

                      const auto& breakpoints = *args[0].as_dictionary();
                      const auto width = app.window_width.load();

                      std::int64_t best_threshold = -1;
                      const DictionaryValue* base_style = nullptr;
                      const DictionaryValue* matched_style = nullptr;

                      for (const auto& [key, val] : breakpoints.entries) {
                          if (!val.is_dictionary()) {
                              continue;
                          }

                          std::int64_t threshold = 0;

                          try {
                              threshold = std::stoll(key);
                          } catch (const std::exception&) {
                              // Non-numeric breakpoint key — skip this entry.
                              continue;
                          }

                          if (threshold == 0) {
                              base_style = val.as_dictionary().get();
                          }

                          if (threshold <= width && threshold > best_threshold) {
                              best_threshold = threshold;
                              matched_style = val.as_dictionary().get();
                          }
                      }

                      auto result = make_dict();

                      if (base_style) {
                          for (const auto& [k, v] : base_style->entries) {
                              result->set(k, v);
                          }
                      }

                      if (matched_style && matched_style != base_style) {
                          for (const auto& [k, v] : matched_style->entries) {
                              result->set(k, v);
                          }
                      }

                      return Value{std::move(result)};
                  });

    // §7: Type-safe CSS validation.
    define_native(env, "GraphicalUi.validate_style",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.validate_style", args, 1, loc);
                      (void)expect_dict(args[0], "GraphicalUi.validate_style", loc);

                      const auto& style = *args[0].as_dictionary();

                      for (const auto& [key, val] : style.entries) {
                          if (!gui_detail::is_known_css_property(key)) {
                              auto suggestion = gui_detail::suggest_css_property(key);
                              std::string msg = "Unknown CSS property '" + key + "'";

                              if (!suggestion.empty()) {
                                  msg += ". Did you mean '" + suggestion + "'?";
                              }

                              return make_failure_value(std::move(msg));
                          }
                      }

                      return make_success_value(args[0]);
                  });
}

} // namespace luma

#else // !LUMA_HAS_WEBVIEW

// ═══════════════════════════════════════════════════════════
// Stub implementation — webview runtime not available.
// Every GraphicalUi function throws a clear error message.
// Uses the authoritative name list from graphicalui_internal.hpp
// so new functions are automatically stubbed.
// ═══════════════════════════════════════════════════════════

namespace luma {

namespace {

[[noreturn]] void throw_unavailable(std::string_view name, SourceLocation loc) {
    throw RuntimeError{
        std::string{name} +
            ": GraphicalUi is not available (webview runtime was not found at build time)",
        loc,
        "install WebView2 (Windows), WebKitGTK (Linux), or WebKit/Cocoa (macOS) and rebuild Luma"};
}

} // namespace

void register_graphicalui_ns(const EnvPtr& env, bool /*sandbox*/) {
    // ─── Constants ───────────────────────────────────────
    gui_detail::register_graphicalui_constants(env);

    // ─── Stub all functions from the shared name list ────

    for (std::size_t i = 0; i < gui_detail::graphicalui_function_count; ++i) {
        const auto* name = gui_detail::graphicalui_function_names[i];
        define_native(env, name,
                      [n = std::string{name}](std::span<const Value>, SourceLocation loc) -> Value {
                          throw_unavailable(n, loc);
                      });
    }
}

} // namespace luma

#endif // LUMA_HAS_WEBVIEW
