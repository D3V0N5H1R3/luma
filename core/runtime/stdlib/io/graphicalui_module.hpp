#ifndef LUMA_STDLIB_GRAPHICALUI_MODULE_HPP
#define LUMA_STDLIB_GRAPHICALUI_MODULE_HPP

/// ═══════════════════════════════════════════════════════════════════════════
/// Architecture: why GraphicalUi does not use ModuleBuilder
/// ═══════════════════════════════════════════════════════════════════════════
///
/// All other stdlib modules use ModuleBuilder (or ContainerModuleBuilder) to
/// register their functions as a declarative builder chain.  GraphicalUi
/// intentionally bypasses that pattern for four reasons:
///
///   1. Blocking event loop — GraphicalUi.app runs a native OS event loop
///      (webview::run) that blocks until the window is closed.  It is not a
///      normal function call; it manages the complete application lifecycle.
///
///   2. Split implementation — the module is split across multiple .cpp files
///      (commands, events, widgets, CSS, serialisation) to manage complexity.
///      ModuleBuilder's chained API assumes a single registration site.
///
///   3. Conditional compilation — the module is guarded by #ifdef
///      LUMA_HAS_WEBVIEW.  The stub path registers placeholder functions
///      dynamically from a name list, which does not map to a builder chain.
///
///   4. Shared mutable state — several functions capture and mutate
///      thread-local webview state (active_app) tightly coupled to the
///      webview callback model; that state is not expressible as a builder
///      parameter.
///
/// Pattern used instead:
///   define_native(env, "GraphicalUi.<fn>", ...) is called directly from
///   register_graphicalui_ns (and the helpers it delegates to), giving each
///   function full access to env and the webview state without the constraints
///   of the builder chain.
///
/// How future "special" modules should handle this:
///   If a module shares these constraints (blocking lifecycle, conditional
///   compilation, shared mutable state), use the same define_native pattern
///   and add a note at the top of its .hpp explaining which constraints apply.
///   Otherwise, prefer ModuleBuilder for consistency.
/// ═══════════════════════════════════════════════════════════════════════════

/// ═══════════════════════════════════════════════════════════════════════════
/// GraphicalUi module file organization
/// ═══════════════════════════════════════════════════════════════════════════
///
/// Public API
/// ----------
/// - graphicalui_module.hpp          — Module header (this file)
/// - graphicalui_module.cpp          — Module registration and public API
///
/// Internal infrastructure
/// -----------------------
/// - graphicalui_internal.hpp        — Shared internal state, helpers, and
///                                     widget-tree data structures
/// - graphicalui_constants.hpp       — Compile-time constants for command,
///                                     subscription, and widget types
/// - graphicalui_helpers.hpp / .cpp  — Shared utility functions (app config,
///                                     model updates, file I/O)
/// - graphicalui_assets.hpp          — Embedded assets (fonts, icons);
///                                     auto-generated — do not edit
///
/// Widget implementation (split by category)
/// -----------------------------------------
/// - graphicalui_widgets.cpp         — Core widget creation and management
/// - graphicalui_widgets_basic.cpp   — Basic widgets (text, button, input, slider…)
/// - graphicalui_widgets_layout.cpp  — Layout containers (row, column, grid, toggle…)
/// - graphicalui_widgets_charts.cpp  — Chart / data-visualisation widgets
/// - graphicalui_widgets_advanced.cpp — Advanced widgets (table, tree, tabs…)
///
/// Subsystems
/// ----------
/// - graphicalui_events.cpp          — Event handling and subscriptions
/// - graphicalui_css.hpp / .cpp      — CSS parsing, validation, and application
/// - graphicalui_serialization.cpp   — Widget-tree serialization to/from JSON
/// - graphicalui_commands.cpp        — Command dispatch (HTTP, clipboard, etc.)
/// - graphicalui_commands_registration.cpp — Registers command & subscription
///                                     builders (none, batch, http_*, on_key…)
/// ═══════════════════════════════════════════════════════════════════════════

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_graphicalui_ns(const EnvPtr& env, bool sandbox = false);

} // namespace luma

#endif // LUMA_STDLIB_GRAPHICALUI_MODULE_HPP
