# Luma — GraphicalUi Guide

> Build native-window GUI applications with the Elm Architecture — widgets, layouts, charts, theming, routing, animations, and accessibility.

> **New to GUIs in Luma? Start with the [Solaris Guide](Luma_Solaris_Guide.md).**
> `Solaris` is the beginner-first authoring surface — typed models, `choice`
> messages, and fluent `|>` modifiers. This guide documents the lower-level
> `GraphicalUi` module directly: the webview engine that powers that surface, which
> you reach for only in advanced scenarios the surface does not yet expose.

---

## Table of Contents

1. [Overview](#1--overview)
2. [Architecture — The Elm Pattern](#2--architecture--the-elm-pattern)
3. [Getting Started](#3--getting-started)
4. [Widgets](#4--widgets)
5. [Layout Containers](#5--layout-containers)
6. [Nearby / Overlay Elements](#6--nearby--overlay-elements)
7. [Sizing, Alignment, and Spacing](#7--sizing-alignment-and-spacing)
8. [Styling and Theming](#8--styling-and-theming)
9. [Charts](#9--charts)
10. [Commands (Side Effects)](#10--commands-side-effects)
11. [Subscriptions](#11--subscriptions)
12. [Components and Routing](#12--components-and-routing)
13. [Animation](#13--animation)
14. [Accessibility](#14--accessibility)
15. [Virtual Lists](#15--virtual-lists)
16. [Layout Debugging](#16--layout-debugging)
17. [Responsive Design](#17--responsive-design)
18. [Layout & Styling Best Practices](#18--layout--styling-best-practices)
19. [Performance Tips](#19--performance-tips)
20. [Examples](#20--examples)
21. [Testing Without a Window](#21--testing-without-a-window)
22. [Icon Reference](#22--icon-reference)

- [See Also](#see-also)

---

## 1 — Overview

`GraphicalUi` is Luma's declarative GUI library. It follows the **Elm Architecture** — a pattern where the entire UI is a pure function of your application state. The runtime handles rendering via an embedded HTML/CSS/JS webview and manages side effects through commands and subscriptions.

> **API summary.** For a concise list of every `GraphicalUi` function and constant, see [Standard Library Reference — §14 (Solaris and GraphicalUi)](Luma_Standard_Library_Reference.md#14--solaris-and-graphicalui).

**Platform support.**Requires platform webview support: WebView2 on Windows, WebKit on macOS, WebKitGTK on Linux. When built without webview support (`LUMA_HAS_WEBVIEW` not defined), all functions throw a descriptive error at runtime.

### Feature Summary

| Category            | Count | Highlights                                                               |
| ------------------- | ----- | ------------------------------------------------------------------------ |
| Basic widgets       | 15    | label, heading, button (variants), inputs, pickers, progress, spinner, image |
| Composite widgets   | 24    | card, accordion, switch, toast, toast_region, empty_state, combobox, menu, popover, confirm, field, wizard |
| Layout containers   | 9     | row, column, grid, tabs, panel, toolbar, scroll, wrapped row             |
| Overlay positioning | 6     | above, below, on_left, on_right, in_front, behind                        |
| Charts              | 7     | bar, line, pie, donut, scatter, area, horizontal bar (hover tooltips, axis labels) |
| Commands            | 21    | HTTP verbs, clipboard, localStorage, download, notify, delay, debounce, random, focus, announce, open URL, set title, print |
| Subscriptions       | 14    | tick, key, resize, focus, mouse, visibility, online/offline, media query, scroll, idle, storage, animation frame, drag |
| Styling helpers     | 17    | spacing, padding, alignment, sizing, merge, responsive, validate         |
| Routing             | 4     | router, navigate, navigate_back, navigation_link                         |
| Accessibility       | 6     | accessible, keyed, focus, announce, aria_live, aria_describedby          |
| Animation           | 2     | transition, animate                                                      |
| Icons               | 129   | Lucide line-icon set, kebab-case names (see §22)                         |

---

## 2 — Architecture — The Elm Pattern

Every GraphicalUi application has three parts:

```text
Model  ──→  View  ──→  Widget Tree  ──→  Screen
  ↑                                        │
  └──────  Update  ←──  Message  ←─────────┘
```

- **Model** — the application state (any Luma value)
- **View** — a pure function `(model) -> widget` that builds the UI
- **Update** — a function `(model, msg) -> model` that produces a new state from an event

After every `update`, the runtime calls `view` to build a fresh widget tree,
serialises that whole tree to JSON, and hands it to the hidden webview; there a
bundled renderer (lit-html) performs a **keyed diff against the live DOM** and
patches only the nodes that actually changed. The core additionally skips the
hand-off entirely when the new tree serialises byte-for-byte identically to the
previous frame, so an update that leaves the UI unchanged costs nothing. You
never touch this machinery — from your side the screen is simply a pure function
of the model.

### Commands and Subscriptions

Side effects (HTTP, clipboard, timers) are expressed as **commands** — data values returned from `update` via `with_command`. The runtime executes them and delivers results as messages.

**Subscriptions** are ongoing listeners (keyboard, timers, resize) declared in a `subscribe` function. The runtime diffs subscriptions across renders and sets up or tears down listeners automatically.

### Error Handling and Recovery

If `view` or `update` raises at runtime, the app does **not** crash or blank the
window:

- Provide an optional **`"on_error"`** config function — `func(string) -> widget`
  — to render your own error screen from the error message (a friendly "something
  went wrong" panel, a retry button, and so on).
- If no `on_error` is set (or it throws in turn), the runtime **keeps the last
  successfully rendered frame on screen** and surfaces the error as a
  **dismissible toast** overlaid on top of it. A transient error therefore never
  wipes the UI — the user keeps whatever was last visible. The toast clears
  itself automatically on the next successful render, and the error message is
  inserted as plain text, so it can never inject markup.

### Security Model

The UI is rendered in a hidden webview whose host page ships with a strict
**Content-Security-Policy** you never have to configure. Everything is denied by
default (`default-src 'none'`); only the framework's own nonce-tagged bootstrap
script and inline styles run, and the page itself has **no network access**
(`connect-src 'none'` — the HTTP commands go through Luma's native runtime, not
the page). Plugins, iframes, and form posts are all blocked. **Remote images
are off by default:** `image()` and `avatar()` load only self-contained
`data:`/`blob:` sources and relative URLs, so a beginner app never silently
phones home to a third-party server (an `<img>` tracking pixel) just by showing a
picture. Opt in with `"allow_remote_images": true` to load `http(s)` sources —
this both widens the CSP `img-src` and enables remote URLs in the renderer.
Beginners get injection-resistant, privacy-preserving defaults for free, and
there is no HTML/CSS/JS surface for user data to escape into.

---

## 3 — Getting Started

### Minimal Counter

```luma
@main
function void main() {
    # An app config holds mixed value types (strings, numbers, functions), so
    # build it as a dictionary (dictionary<any>) rather than one literal.
    mutable dictionary config = {}
    config["title"] = "Counter"
    config["model"] = 0
    config["view"] = (integer count) -> GraphicalUi.column([
        GraphicalUi.heading("Counter"),
        GraphicalUi.label("Count: ${count}"),
        GraphicalUi.row([
            GraphicalUi.button("-", () -> "dec"),
            GraphicalUi.button("+", () -> "inc")
        ])
    ])
    config["update"] = (integer model, string msg) -> if msg == "inc" { model + 1 }
        else if msg == "dec" { model - 1 }
        else { model }
    GraphicalUi.app(config)
}
```

Both buttons return a **message** (`"inc"` or `"dec"`); the runtime forwards each
message to `update(model, msg)`, which returns the next model. An event callback
may return either form:

- a **message** — a string, or a typed `choice` value (e.g. `() -> Msg.Increment`)
  — routed through `update`, or
- a **new model** (any other value) — applied directly, bypassing `update`.

For example, a reset button can set the model directly with `() -> 0`. Either way
the data flow stays predictable: the model is always **replaced wholesale** (never
mutated in place) and `view` re-runs on the new model — the direct form is just a
shortcut that skips the `update` hop.

Returning a typed **`choice`** message lets `update` be an exhaustive `match` over
a dedicated message type, so the compiler flags any unhandled message. This is the
recommended style for non-trivial apps: define a `choice` message type, return its
variants from callbacks (`() -> Msg.Increment`), and handle every variant in
`update(model, msg)`. The `examples/applications/solaris_counter.luma` example is
written on the `Solaris` surface, which provides exactly this typed-message layer
over `GraphicalUi`.

> **Caveat — string and choice models.** A returned **string or `choice` value is
> always treated as a message**, so a model that is itself a `string` or a `choice`
> type cannot be round-tripped through `update`. Either keep such state out of the
> bare `string`/`choice` type (wrap it in a record or dictionary, e.g.
> `{"text": "..."}`) or return the new model directly and omit `update`. This is the
> one case where the message-vs-model distinction can surprise you.

### App Config Keys

| Key           | Type                        | Default              | Description                          |
| ------------- | --------------------------- | -------------------- | ------------------------------------ |
| `"model"`     | any                         | `null`               | Initial application state            |
| `"view"`      | `func(model) -> widget`     | _required_           | View function                        |
| `"update"`    | `func(model, msg) -> model` | optional             | Update function (may return command) |
| `"subscribe"` | `func(model) -> array<sub>` | optional             | Subscription function                |
| `"init"`      | `func(model) -> model`      | optional             | Initialisation (may return command)  |
| `"on_error"`  | `func(string) -> widget`    | optional             | Custom error view when `view`/`update` raises (see [§21](#21--testing-without-a-window)) |
| `"persist"`   | string (file path)          | optional             | Save the model to a JSON file on exit and restore it on the next launch (see below) |
| `"title"`     | string                      | `"Luma Application"` | Window title                         |
| `"width"`     | integer                     | `800`                | Window width                         |
| `"height"`    | integer                     | `600`                | Window height                        |
| `"min_width"` / `"min_height"` | integer        | _none_               | Minimum window size (resizable windows only) |
| `"max_width"` / `"max_height"` | integer        | _none_               | Maximum window size (resizable windows only) |
| `"resizable"` | boolean                     | `true`               | Allow the user to resize the window (`false` fixes the size and removes the resize/maximise affordances) |
| `"fullscreen"`| boolean                     | `false`              | Start the window maximised / full screen (alias: `"maximized"`) |
| `"allow_remote_images"` | boolean           | `false`              | Load remote `http(s)` `image()`/`avatar()` sources. Off by default (only `data:`/`blob:` and relative URLs load); see [Security Model](#security-model) |
| `"theme"`     | dictionary                  | optional             | Theme overrides                      |

Use the provided constants (`GraphicalUi.MODEL`, `GraphicalUi.VIEW`, etc.) instead of raw strings for config keys.

> **Window decorations.** Size and resizability are controlled through the keys
> above. A native **menu bar, system-tray icon, and custom window icon** are not
> exposed by the cross-platform webview backend — build in-window navigation
> (a `toolbar`, `menu`, or `router`) instead.

### State Persistence

Set `"persist"` to a file path and the runtime saves the current model to that
file as JSON when the window closes, then restores it automatically the next time
the app launches. This is the one-line way to remember window state, form drafts,
or user preferences between runs — no manual save/load code required.

```luma
mutable dictionary config = {}
config["persist"] = "settings.json"   # saved on exit, restored on launch
config["model"]   = {"volume": 50, "muted": false}
config["view"]    = view_settings
config["update"]  = update_settings
GraphicalUi.app(config)
```

How restore interacts with `init`:

- **With `init`** — the restored model is passed to `init(model)`, so you can
  migrate or validate an older saved shape before it becomes the live model.
- **Without `init`** — the restored model becomes the initial model directly.

> **JSON round-trip limitation.** The model is stored as JSON, so it must be
> JSON-representable (booleans, numbers, strings, arrays, dictionaries). A model
> that is a **record** is restored as a **dictionary** with the same fields;
> read it back through dictionary access, or normalise it in `init`. Values that
> have no JSON form (functions, tasks, channels, sockets) cannot be persisted.
> Saving and restoring are best-effort: an unreadable or unparseable file is
> ignored with a warning rather than crashing the app.

---

## 4 — Widgets

### Basic Widgets

| Function                           | Parameters                        | Returns  | Description                |
| ---------------------------------- | --------------------------------- | -------- | -------------------------- |
| `label(text, style?)`              | `(string, dictionary?)`           | `widget` | Static text                |
| `heading(text, level?, style?)`    | `(string, integer?, dictionary?)` | `widget` | Heading (level 1–6, default 1)           |
| `image(source, style?)`            | `(string, dictionary?)`           | `widget` | Image from URL or data URI |
| `separator(style?)`                | `(dictionary?)`                   | `widget` | Horizontal rule            |
| `spacer(height?, style?)`          | `(integer?, dictionary?)`         | `widget` | Vertical spacing           |
| `horizontal_spacer(width?)`        | `(integer?)`                      | `widget` | Horizontal spacing         |
| `flexible_space(style?)`           | `(dictionary?)`                   | `widget` | Fills remaining space      |
| `when(condition, child)`           | `(boolean, widget)`               | `widget` | Renders child only when true |
| `progress(value, max, style?)`     | `(number, number, dictionary?)`   | `widget` | Progress bar               |
| `progress_bar(value, max, style?)` | `(number, number, dictionary?)`   | `widget` | Styled progress bar        |
| `spinner(label?, style?)`          | `(string?, dictionary?)`          | `widget` | Indeterminate busy indicator (default label `"Loading…"`) |

### Interactive Widgets

| Function                                             | Parameters                                        | Returns  | Description             |
| ---------------------------------------------------- | ------------------------------------------------- | -------- | ----------------------- |
| `button(label, on_click, style?)`                    | `(string, function, dictionary?)`                 | `widget` | Clickable button (set a `"variant"` of `primary`/`secondary`/`ghost`/`danger` for hierarchy — see [§8](#8--styling-and-theming)) |
| `text_input(value, on_change, placeholder?, on_commit?, style?)` | `(string, function, string?, function?, dictionary?)`        | `widget` | Single-line text input (optional commit handler — see below) |
| `text_area(value, on_change, on_commit?, style?)`                | `(string, function, function?, dictionary?)`                 | `widget` | Multi-line text input (optional commit handler — see below)  |
| `checkbox(label, checked, on_toggle, style?)`        | `(string, boolean, function, dictionary?)`        | `widget` | Checkbox toggle         |
| `dropdown(options, value, on_select, style?)`        | `(array<string>, string, function, dictionary?)`  | `widget` | Select dropdown         |
| `radio_group(options, selected, on_select, style?)`  | `(array<string>, string, function, dictionary?)`  | `widget` | Radio button group      |
| `slider(value, min, max, on_change, style?)`         | `(number, number, number, function, dictionary?)` | `widget` | Range slider            |
| `toggle(label, checked, on_toggle, style?)`          | `(string, boolean, function, dictionary?)`        | `widget` | Toggle switch           |
| `file_input(on_select, accept?, style?)`             | `(function, string?, dictionary?)`                | `widget` | File picker             |
| `date_picker(value, on_change, style?)`              | `(string, function, dictionary?)`                 | `widget` | Date input (YYYY-MM-DD) |
| `time_picker(value, on_change, style?)`              | `(string, function, dictionary?)`                 | `widget` | Time input (HH:MM)      |
| `color_picker(value, on_change, style?)`             | `(string, function, dictionary?)`                 | `widget` | Colour input (#RRGGBB)  |

#### Commit Handlers (`on_commit`)

`text_input` and `text_area` accept an **optional commit handler** — a second
callback that fires only when the user _finishes_ editing, as distinct from
`on_change`, which fires on every keystroke. Pass it as an extra function
argument (the builder tells the callbacks apart by position and type):

```luma
GraphicalUi.text_input(
    model["draft"],
    (string text) -> GraphicalUi.with_command(       # on_change: every keystroke
        Dictionary.set(model, "draft", text), GraphicalUi.none()),
    "Search…",                                        # placeholder
    (string text) -> "commit:${text}"                 # on_commit: blur / Enter
)
```

A commit fires when the field **loses focus** or the user presses a completion
key: **Enter** for `text_input`, **Ctrl+Enter** (⌘+Enter on macOS) for
`text_area` (a plain Enter there inserts a newline). Use `on_commit` for work
that should not run on every keystroke — validation, autosave, or a network
request — while `on_change` keeps the model in sync as the user types. Both are
optional and may be used together or independently.

### Display Widgets

| Function                                              | Parameters                                                      | Returns  | Description              |
| ----------------------------------------------------- | --------------------------------------------------------------- | -------- | ------------------------ |
| `alert(message, severity?, style?)`                   | `(string, string?, dictionary?)`                                | `widget` | Styled alert box         |
| `badge(text, style?)`                                 | `(string, dictionary?)`                                         | `widget` | Small status label       |
| `dialog(title, children, is_open, on_close?, style?)` | `(string, array<widget>, boolean, function?, dictionary?)`      | `widget` | Modal dialog             |
| `icon(name, size?, style?)`                           | `(string, integer?, dictionary?)`                               | `widget` | Lucide SVG icon by name — see [§22](#22--icon-reference) |
| `link(text, url_or_callback, style?)`                 | `(string, string\|function, dictionary?)`                       | `widget` | Link or message dispatch |
| `list(items, on_select?, style?)`                     | `(array, function?, dictionary?)`                               | `widget` | Vertical list            |
| `table(headers, rows, on_row_click?, options?)`      | `(array<string>, array<array<string>>, function?, dictionary?)` | `widget` | Data table — sticky header and zebra striping by default; the trailing dictionary also carries table options (see below) |
| `tooltip(text, child, style?)`                       | `(string, widget, dictionary?)`                                 | `widget` | Hover tooltip            |

> **Security:** widget URLs are scheme-restricted to block script injection.
> `link` navigates only to `http`, `https`, `mailto`, and `tel` targets (any
> other scheme renders as a non-navigating link). `image` and `avatar` load only
> self-contained `data:` and `blob:` sources (plus relative URLs) by default;
> remote `http`/`https` sources are stripped unless the app sets
> `"allow_remote_images": true` (see [Security Model](#security-model)). Relative
> URLs are always allowed.

#### Table Options

`table`'s trailing dictionary doubles as the style bag and a carrier for these
optional keys (any other key is treated as CSS on the table element). Sticky
headers and zebra striping are applied automatically.

| Key                | Type                | Effect                                                                 |
| ------------------ | ------------------- | --------------------------------------------------------------------- |
| `"align"`          | `array<string>`     | Per-column text alignment (`"left"`/`"right"`/`"center"`)              |
| `"selected"`       | `integer` / `array<integer>` | Highlight the given row(s) as selected (`aria-selected`)      |
| `"on_sort"`        | `func(integer)`     | Make headers clickable; the callback receives the clicked column index |
| `"sort_column"`    | `integer`           | The currently sorted column (drives `aria-sort` + the ▲/▼ indicator)   |
| `"sort_direction"` | `string`            | `"asc"` (default) or `"desc"`                                          |

```luma
# Sortable, with the active sort and a selected row
GraphicalUi.table(headers, rows, on_row_click, {
    "align": ["left", "right"],
    "selected": model["selected_row"],
    "on_sort": (integer col) -> "sort:${col}",
    "sort_column": model["sort_col"],
    "sort_direction": model["sort_dir"]
})
```

Sorting follows the Elm pattern: `on_sort` reports the clicked column, your
`update` re-orders the data and records `sort_column`/`sort_direction`, and the
next render reflects the new order.

### Composite & Advanced Widgets

Higher-level widgets built from the primitives above. Each is a first-class
catalog function with a browser renderer; their callbacks flow through the same
`update` cycle as the basic widgets.

| Function                                                       | Parameters                                       | Returns  | Description                                                |
| -------------------------------------------------------------- | ------------------------------------------------ | -------- | ---------------------------------------------------------- |
| `accordion(sections)`                                          | `(array<dictionary>)`                            | `widget` | Collapsible sections (each: `title`/`label` + content)     |
| `avatar(name, url?)`                                           | `(string, string?)`                              | `widget` | Image avatar, or the name's initials when no `url`         |
| `breadcrumb(items, on_navigate?)`                              | `(array<string>, function?)`                     | `widget` | Navigation trail; non-final items are clickable            |
| `card(children)`                                               | `(array<widget>)`                                | `widget` | Bordered surface stacking its children (titleless panel)   |
| `combobox(value, options, on_change, on_select?)`              | `(string, array<string>, function, function?)`   | `widget` | Autocomplete text input with a filtered, keyboard-navigable list |
| `confirm(title, message, on_confirm, on_cancel?, options?)`    | `(string, string, function, function?, dictionary?)` | `widget` | Modal confirmation dialog (`role="alertdialog"`); `options`: `confirm_label`/`cancel_label`/`danger` |
| `draggable(child, data)`                                       | `(widget, string)`                               | `widget` | Drag source carrying `data`                                |
| `drop_target(child, on_drop)`                                  | `(widget, function)`                             | `widget` | Drop zone; `on_drop` receives the dragged data             |
| `field(label, control, options?)`                             | `(string, widget, dictionary?)`                  | `widget` | Labelled form control with optional `required`/`help`/`error` |
| `field_error(message)`                                         | `(string)`                                       | `widget` | Inline form validation message (icon + text)               |
| `empty_state(message, options?)`                              | `(string, dictionary?)`                          | `widget` | Friendly placeholder for a blank list/panel; `options`: `title`, `icon` (default `inbox`), `action_label` + `on_action` |
| `form(children, on_submit)`                                    | `(array<widget>, function)`                      | `widget` | Form container; submits on Enter or a submit button        |
| `infinite_scroll(items, item_height, on_load_more)`            | `(array, integer, function)`                     | `widget` | Scrolling list that loads more near the end                |
| `inspect(child)`                                               | `(widget)`                                       | `widget` | Wraps a child with a debug inspector overlay               |
| `menu(label, items, on_select)`                                | `(string, array<string>, function)`              | `widget` | Click-to-open action menu; arrow-key + Esc keyboard support |
| `number_input(value, min, max, on_change)`                     | `(number, number, number, function)`             | `widget` | Numeric input field                                        |
| `paginator(current_page, total_pages, on_page_change)`         | `(integer, integer, function)`                   | `widget` | Page navigation control                                    |
| `popover(label, content)`                                      | `(string, widget)`                               | `widget` | Click-to-open floating panel anchored to a trigger button; closes on outside-click or Esc |
| `search_input(value, on_change, on_clear?)`                    | `(string, function, function?)`                  | `widget` | Text field with an optional clear button                   |
| `skeleton(width?, height?)`                                    | `(integer?, integer?)`                           | `widget` | Shimmering placeholder for loading states                  |
| `switch(label, checked, on_toggle)`                            | `(string, boolean, function)`                    | `widget` | On/off switch (toggle with the `switch` ARIA role)         |
| `toast(message, severity?, duration?, action_label?, on_action?)` | `(string, string?, integer?, string?, function?)` | `widget` | Transient notification (reuses alert severities); optional inline action (e.g. "Undo") |
| `toast_region(toasts, options?)`                              | `(array<widget>, dictionary?)`                   | `widget` | Fixed-position stack of toasts; `options.position` (default `bottom-right`) — see [§12](#12--components-and-routing) note below |
| `wizard(steps, active_step, on_step_change)`                   | `(array<widget>, integer, function)`             | `widget` | Multi-step flow with a step indicator                      |

> **Toast region & auto-dismiss.** `toast_region` only stacks and positions the
> toasts you give it — keep the live toasts in your model and render them inside
> one region (`position`: `top-left`/`top-right`/`top-center`/`bottom-left`/
> `bottom-right`/`bottom-center`). The array order is the on-screen order
> (top-to-bottom), so you decide which end is newest by how you build the list.
> Because the Elm loop is pure, schedule dismissal yourself: when you add a
> toast, also return a `GraphicalUi.delay(duration, () -> "dismiss:<id>")`
> command whose message removes that toast from the model.
>
> ```luma
> GraphicalUi.toast_region([
>     GraphicalUi.toast("Saved", GraphicalUi.SUCCESS),
>     GraphicalUi.toast("Upload failed", GraphicalUi.ERROR, "Retry", on_retry)
> ], {"position": "top-right"})
> ```
>
> **Empty state.** Replace a blank list or panel with `empty_state` so the view
> never reads as broken. Because the `options` carry both strings and the
> `on_action` callback, build them as a `dictionary<any>` (assign keys to a
> `mutable dictionary`) rather than one mixed literal:
>
> ```luma
> mutable dictionary opts = {}
> opts["icon"] = "users"
> opts["title"] = "Your address book is empty"
> opts["action_label"] = "Add contact"
> opts["on_action"] = () -> "add_contact"
> GraphicalUi.empty_state("No contacts yet", opts)
> ```

---

## 5 — Layout Containers

All layout containers accept `array<widget>` for children. A `result<array<widget>>` is also accepted and automatically unwrapped.

| Function                                            | Parameters                                                       | Returns  | Description        |
| --------------------------------------------------- | ---------------------------------------------------------------- | -------- | ------------------ |
| `row(children, style?)`                             | `(array<widget>, dictionary?)`                                   | `widget` | Horizontal flex    |
| `column(children, style?)`                          | `(array<widget>, dictionary?)`                                   | `widget` | Vertical flex      |
| `wrapped_row(children, style?)`                     | `(array<widget>, dictionary?)`                                   | `widget` | Wrapping row       |
| `scroll_row(children, style?)`                      | `(array<widget>, dictionary?)`                                   | `widget` | Horizontal scroll  |
| `scroll_column(children, style?)`                   | `(array<widget>, dictionary?)`                                   | `widget` | Vertical scroll    |
| `panel(title, children, style?)`                    | `(string, array<widget>, dictionary?)`                           | `widget` | Bordered card      |
| `toolbar(children, style?)`                         | `(array<widget>, dictionary?)`                                   | `widget` | Horizontal toolbar |
| `grid(columns, children, style?)`                   | `(integer, array<widget>, dictionary?)`                          | `widget` | CSS grid           |
| `tabs(labels, active, on_select, children, style?)` | `(array<string>, integer, function, array<widget>, dictionary?)` | `widget` | Tabbed container   |

```luma
# Three-column grid
GraphicalUi.grid(3, [
    GraphicalUi.panel("A", [GraphicalUi.label("Cell 1")]),
    GraphicalUi.panel("B", [GraphicalUi.label("Cell 2")]),
    GraphicalUi.panel("C", [GraphicalUi.label("Cell 3")])
])
```

---

## 6 — Nearby / Overlay Elements

Position overlay widgets relative to a child using absolute CSS positioning.

| Function                           | Description                    |
| ---------------------------------- | ------------------------------ |
| `above(child, overlay, style?)`    | Overlay above                  |
| `below(child, overlay, style?)`    | Overlay below                  |
| `on_left(child, overlay, style?)`  | Overlay to the left            |
| `on_right(child, overlay, style?)` | Overlay to the right           |
| `in_front(child, overlay, style?)` | Overlay centred on top         |
| `behind(child, overlay, style?)`   | Overlay behind (lower z-index) |

```luma
GraphicalUi.above(
    GraphicalUi.button("Hover me", () -> "click"),
    GraphicalUi.label("Tooltip!", {"background": "#333", "color": "white", "padding": "4px"})
)
```

---

## 7 — Sizing, Alignment, and Spacing

### Sizing

| Function                     | Returns      | Description                       |
| ---------------------------- | ------------ | --------------------------------- |
| `fill()`                     | `string`     | Fill available space (`"1 1 0%"`) |
| `fill_portion(n)`            | `string`     | Fill with weight `n`              |
| `shrink()`                   | `string`     | Shrink to content                 |
| `px(n)`                      | `string`     | Fixed pixel size                  |
| `constrained_fill(min, max)` | `dictionary` | Fill with min/max constraints     |

### Alignment

| Function         | Returns      | Description       |
| ---------------- | ------------ | ----------------- |
| `center()`       | `dictionary` | Centre both axes  |
| `center_x()`     | `dictionary` | Centre horizontal |
| `center_y()`     | `dictionary` | Centre vertical   |
| `align_left()`   | `dictionary` | Align left        |
| `align_right()`  | `dictionary` | Align right       |
| `align_top()`    | `dictionary` | Align top         |
| `align_bottom()` | `dictionary` | Align bottom      |

### Spacing

| Function           | Returns      | Description                           |
| ------------------ | ------------ | ------------------------------------- |
| `spacing(px)`      | `dictionary` | Uniform gap                           |
| `spacing_xy(x, y)` | `dictionary` | Separate horizontal and vertical gap  |
| `padding(px)`      | `dictionary` | Uniform padding                       |
| `padding_xy(x, y)` | `dictionary` | Horizontal and vertical padding       |
| `space_evenly()`   | `dictionary` | Equal space around each child         |
| `space_between()`  | `dictionary` | Space between children, none at edges |
| `space_around()`   | `dictionary` | Equal space around each child         |

```luma
# Sidebar + content layout
GraphicalUi.row([
    GraphicalUi.column(sidebar, {"flex": GraphicalUi.px(250)}),
    GraphicalUi.column(content, {"flex": GraphicalUi.fill()})
], GraphicalUi.merge_styles(GraphicalUi.spacing(16), GraphicalUi.padding(12)))
```

For a consistent rhythm, prefer the named [spacing tokens](#spacing-tokens)
(`GraphicalUi.VAR_SPACE_*`) over arbitrary pixel values.

---

## 8 — Styling and Theming

Styling is layered as **progressive disclosure** — there is one obvious way to
start, and the rest are escape hatches you only reach for when a simpler layer
cannot express what you need. Prefer the earliest option in this list that does
the job:

1. **Theme first.** Set colours, fonts, and radius once via the app config
   `"theme"` and every widget inherits them (see [Theme](#theme)). Most apps need
   nothing more to look polished.
2. **Inline style dictionaries** for a one-off tweak on a single widget — the
   default form you will use most (see [Style Dictionaries](#style-dictionaries)).
3. **Pseudo-class prefixes** (`hover_`, `focus_`, …) for interaction states on
   that same widget.
4. **`class` + `stylesheet()` / `load_stylesheet()`** only to share one rule
   across many widgets or keep CSS in a file — an advanced escape hatch, not the
   starting point.

Every layer is allowlist-sanitised and runs under a strict Content-Security-Policy,
so none of them can inject script (see [Security Model](#security-model)).

### Button Variants

Give actions a clear hierarchy with the `"variant"` key in a button's style
dictionary. Use **one** `primary` action per view; let supporting actions recede
as `secondary` or `ghost`, and reserve `danger` for destructive actions. The
variant is lifted off the style dictionary, so it never leaks into the inline
CSS. The variant constants (`GraphicalUi.PRIMARY`, `SECONDARY`, `GHOST`,
`DANGER`) are the preferred way to spell the values.

| Variant     | Appearance                          | Use for                          |
| ----------- | ----------------------------------- | -------------------------------- |
| `primary`   | Filled accent, white text           | The single main action of a view |
| `secondary` | Outlined accent                     | Supporting actions               |
| `ghost`     | Borderless, subtle hover            | Low-emphasis / toolbar actions   |
| `danger`    | Filled error (AA-safe white text)   | Destructive actions (delete, …)  |

```luma
GraphicalUi.row([
    GraphicalUi.button("Save", on_save, {"variant": GraphicalUi.PRIMARY}),
    GraphicalUi.button("Cancel", on_cancel, {"variant": GraphicalUi.SECONDARY}),
    GraphicalUi.button("Delete", on_delete, {"variant": GraphicalUi.DANGER})
])
```

A button with no `variant` keeps the theme's default filled-accent look, so
existing code is unaffected.

### Style Dictionaries

Most widgets accept an optional `style?` dictionary as their last parameter. Keys are CSS property names written with underscores (converted to hyphens at render time).

```luma
GraphicalUi.label("Hello", {"background_color": "#eef", "border_radius": "4px", "padding": "8px"})
```

### Pseudo-Class Styles

Prefix CSS property names to apply conditional styles:

| Prefix      | CSS Pseudo-Class |
| ----------- | ---------------- |
| `hover_`    | `:hover`         |
| `focus_`    | `:focus`         |
| `active_`   | `:active`        |
| `disabled_` | `:disabled`      |
| `checked_`  | `:checked`       |

```luma
GraphicalUi.button("Submit", on_click, {
    "background_color": "#4361ee",
    "hover_background_color": "#3a56d4",
    "active_transform": "scale(0.98)"
})
```

### CSS Classes

Use the `"class"` key with `stylesheet` or `load_stylesheet`:

```luma
GraphicalUi.stylesheet(".card { padding: 16px; border-radius: 8px; }")
GraphicalUi.panel("Info", children, {"class": "card"})
```

### Style Functions

| Function                           | Parameters                      | Returns              | Description                            |
| ---------------------------------- | ------------------------------- | -------------------- | -------------------------------------- |
| `style(props)`                     | `(dictionary)`                  | `dictionary`         | Validate and return a style            |
| `merge_styles(base, overrides...)` | `(dictionary, dictionary, ...)` | `dictionary`         | Merge styles (later wins)              |
| `stylesheet(css)`                  | `(string)`                      | `command`            | Inject a `<style>` block               |
| `load_stylesheet(path)`            | `(string)`                      | `command`            | Load external `.css` file              |
| `font_face(path, family, opts?)`   | `(string, string, dictionary)`  | `command`            | Embed a local font file, optionally as the default |
| `set_theme_mode(mode)`             | `(string)`                      | `command`            | Force `"light"`, `"dark"`, or `"auto"` |
| `responsive(breakpoints)`          | `(dictionary)`                  | `dictionary`         | Mobile-first responsive style          |
| `validate_style(style)`            | `(dictionary)`                  | `result<dictionary>` | Validate CSS property names            |

### Theme

Pass a `"theme"` dictionary in the app config. The defaults below derive from the
bundled [Pico CSS](https://picocss.com/) theme — the swatches show the light-mode
rendering, and dark mode flips automatically (see below). Override any key to
replace the Pico-derived default.

| Theme Key      | CSS Variable          | Default (Pico-derived)                      |
| -------------- | --------------------- | ------------------------------------------- |
| `accent`       | `--gui-primary`       | `var(--pico-primary)` (≈ `#0172ad`)         |
| `accent_hover` | `--gui-primary-hover` | `var(--pico-primary-hover)` (≈ `#015887`)   |
| `background`   | `--gui-bg`            | `var(--pico-background-color)` (≈ `#fff`)   |
| `text_color`   | `--gui-fg`            | `var(--pico-color)` (≈ `#373c44`)           |
| `text_muted`   | `--gui-text-muted`    | `var(--pico-muted-color)` (secondary text)  |
| `border`       | `--gui-border`        | `var(--pico-muted-border-color)`            |
| `radius`       | `--gui-radius`        | `var(--pico-border-radius)` (`0.25rem`/4px) |
| `font`         | `--gui-font`          | `var(--pico-font-family)` (system stack)    |
| `success`      | `--gui-success`       | `hsl(160 84% 39%)` (≈ `#10b981`)            |
| `warning`      | `--gui-warning`       | `hsl(38 92% 50%)` (≈ `#f59e0b`)             |
| `error`        | `--gui-error`         | `hsl(0 85% 60%)` (≈ `#ef4444`)              |

Dark mode is automatic via `@media (prefers-color-scheme: dark)`. Override per-mode with nested dictionaries:

```luma
"theme": {
    "background": {"light": "#ffffff", "dark": "#1a1a2e"},
    "accent": "#4361ee"
}
```

Custom theme variables use the `custom_` prefix:

```luma
"theme": {
    "custom_sidebar_width": "280px"
}
# Use in styles: "width": "var(--gui-custom-sidebar-width)"
```

Set `"animations": false` to turn off **all** framework motion — every
transition and animation is stilled, independent of the OS reduced-motion
setting (which is always honoured too — see [§13](#reduced-motion)). Busy
indicators such as the spinner stay visible as static shapes. This is the
global equivalent of the user's reduced-motion preference, exposed as a theme
switch for previews, tests, or a user-facing "reduce motion" option:

```luma
"theme": {
    "animations": false
}
```

### Custom Fonts

The default UI font is the platform's native system stack (`system-ui`), which
costs nothing to ship and looks native on every OS. There are two ways to change
it, depending on where the font lives.

**1. A system or already-installed font — set the theme `font` key.** No file is
embedded; the name is used directly. Always end with a fallback stack so the UI
stays legible if the font is missing:

```luma
"theme": {
    "font": "\"Segoe UI\", system-ui, sans-serif"
}
```

**2. A font file you ship with your app — embed it with `font_face`.** This reads
a local `.woff2`, `.woff`, `.ttf`, or `.otf` file, inlines it into the page as a
`@font-face` rule (required by the webview's strict content-security policy, which
forbids remote font URLs), and — unless you opt out — makes it the default UI
font. Return the command from `update` (typically once, on startup) via
`with_command`:

```luma
# Embed ./fonts/Inter.woff2 and use it everywhere.
GraphicalUi.with_command(model, GraphicalUi.font_face("fonts/Inter.woff2", "Inter"))
```

The optional third argument tunes the descriptors:

| Option      | Type      | Default    | Meaning                                                    |
| ----------- | --------- | ---------- | ---------------------------------------------------------- |
| `"weight"`  | string    | `"normal"` | `font-weight` — a keyword or number, e.g. `"bold"`, `"400"`, or a variable-font range `"100 900"` |
| `"style"`   | string    | `"normal"` | `font-style` — `"normal"`, `"italic"`, or `"oblique"`      |
| `"default"` | boolean   | `true`     | Whether to set this family as the default UI font (`--gui-font`) |

```luma
# A single-axis options dictionary (all-string values) can be written inline.
GraphicalUi.font_face("fonts/Inter-Variable.woff2", "Inter",
    {"weight": "100 900", "style": "normal"})

# To combine string options with the boolean "default", build the dictionary
# imperatively — a literal that mixes value types will not type-check.
mutable dictionary opts = {"default": false}
opts["weight"] = "700"
GraphicalUi.font_face("fonts/Inter-Bold.woff2", "Inter", opts)
```

Notes:

- **Register the family once, then reference it.** After embedding with
  `"default": false`, use the family by name in styles or in the theme `font`
  key: `"font": "\"Inter\", system-ui, sans-serif"`.
- **The theme `font` key wins.** `font_face`'s default is applied as a `:root`
  rule, whereas an explicit theme `font` is an inline style on the document
  element, so a theme `font` always takes precedence. Set at most one as the
  source of truth for the default.
- **Path safety.** The path must be relative to the working directory — absolute
  paths, remote URLs, and `..` traversal are rejected. Files larger than 8 MB are
  ignored.
- **Zero default cost.** No font ships with the interpreter; you only pay for the
  bytes of the file you embed.

### CSS Variable Constants

Use `GraphicalUi.VAR_*` constants in styles to reference theme values:

```luma
GraphicalUi.label("Themed", {
    "color": GraphicalUi.VAR_FG,
    "background_color": GraphicalUi.VAR_BG,
    "border_radius": GraphicalUi.VAR_RADIUS
})
```

Use `VAR_TEXT_MUTED` for secondary, de-emphasised text (captions, hints,
metadata) so it stays legible and theme-aware in both light and dark mode
instead of hard-coding a grey:

```luma
GraphicalUi.label("Last saved 2 min ago", { "color": GraphicalUi.VAR_TEXT_MUTED })
```

### Spacing Tokens

Prefer the named spacing scale over arbitrary pixel values — it keeps padding,
gaps, and sizing on a consistent 4 px rhythm. Reference the tokens through the
`GraphicalUi.VAR_SPACE_*` constants in any spacing-related style property.

| Constant       | CSS Variable     | Value           |
| -------------- | ---------------- | --------------- |
| `VAR_SPACE_XS` | `--gui-space-xs` | `0.25rem` (4px) |
| `VAR_SPACE_SM` | `--gui-space-sm` | `0.5rem` (8px)  |
| `VAR_SPACE_MD` | `--gui-space-md` | `1rem` (16px)   |
| `VAR_SPACE_LG` | `--gui-space-lg` | `1.5rem` (24px) |
| `VAR_SPACE_XL` | `--gui-space-xl` | `2rem` (32px)   |

```luma
GraphicalUi.column(children, {
    "gap": GraphicalUi.VAR_SPACE_MD,
    "padding": GraphicalUi.VAR_SPACE_LG
})
```

### Type Scale Tokens

A modular type scale mirrors the spacing tokens so font sizes stay on a single,
predictable ramp. Reference them through the `GraphicalUi.VAR_TEXT_*` constants
rather than hard-coding `rem`/`px` font sizes. The headings (`heading` levels
1–6) already consume these tokens internally.

| Constant      | CSS Variable         | Value               |
| ------------- | -------------------- | ------------------- |
| `VAR_TEXT_XS` | `--gui-font-size-xs` | `0.75rem` (12px)    |
| `VAR_TEXT_SM` | `--gui-font-size-sm` | `0.875rem` (14px)   |
| `VAR_TEXT_MD` | `--gui-font-size-md` | `1rem` (16px, base) |
| `VAR_TEXT_LG` | `--gui-font-size-lg` | `1.25rem` (20px)    |
| `VAR_TEXT_XL` | `--gui-font-size-xl` | `1.5rem` (24px)     |
| `VAR_TEXT_2XL` | `--gui-font-size-2xl` | `2rem` (32px)      |

For comfortable reading, cap long-form text and forms at a sensible measure
(line length) with `VAR_MEASURE` (`--gui-measure`, `65ch`). The built-in `form`
container already applies this cap; apply it to prose blocks yourself:

```luma
GraphicalUi.column(paragraphs, { "max_width": GraphicalUi.VAR_MEASURE })
```

### Radius Tokens

Corners use the same named-scale discipline as spacing and type: pick a
`GraphicalUi.VAR_RADIUS_*` step instead of a raw pixel radius. `VAR_RADIUS`
remains the default corner every widget already applies; the scale is for when
you want a specific rounder or squarer corner (e.g. a pill button, a square
avatar frame).

| Constant           | CSS Variable        | Value            |
| ------------------ | ------------------- | ---------------- |
| `VAR_RADIUS_NONE`  | `--gui-radius-none` | `0` (square)     |
| `VAR_RADIUS_SM`    | `--gui-radius-sm`   | `0.25rem` (4px)  |
| `VAR_RADIUS_MD`    | `--gui-radius-md`   | `0.5rem` (8px)   |
| `VAR_RADIUS_LG`    | `--gui-radius-lg`   | `1rem` (16px)    |
| `VAR_RADIUS_FULL`  | `--gui-radius-full` | `999px` (pill)   |

```luma
# A pill-shaped tag
GraphicalUi.label("New", {
    "padding": "2px 8px",
    "border_radius": GraphicalUi.VAR_RADIUS_FULL
})
```

### Subtle Tint Tokens

Beyond the base `VAR_*` colours, the framework defines theme-aware _tint_ variables derived from the accent and status colours via `color-mix`. Use them for semantic badges, selected surfaces, and status panels — they adapt automatically to light and dark mode.

| Variable                      | Typical use                     |
| ----------------------------- | ------------------------------- |
| `--gui-primary-subtle-bg`     | Highlighted / selected surface  |
| `--gui-primary-subtle-border` | Border of a highlighted surface |
| `--gui-success-subtle-bg`     | Positive badge / success panel  |
| `--gui-success-subtle-border` | Positive badge border           |
| `--gui-warning-subtle-bg`     | Warning badge / caution panel   |
| `--gui-warning-subtle-border` | Warning badge border            |
| `--gui-error-subtle-bg`       | Error / danger badge or panel   |
| `--gui-error-subtle-border`   | Error / danger badge border     |

```luma
# A semantic "success" pill
GraphicalUi.label("+12.4%", {
    "color": GraphicalUi.VAR_SUCCESS,
    "background_color": "var(--gui-success-subtle-bg)",
    "padding": "2px 8px",
    "border_radius": GraphicalUi.VAR_RADIUS_FULL
})
```

For an arbitrary tint, call `color-mix` directly so the result still tracks the theme:

```luma
{"background_color": "color-mix(in srgb, var(--gui-primary) 12%, transparent)"}
```

### Tonal Ramp Tokens

The accent colour is also exposed as a **nine-step tonal ramp**, from the
lightest tint (`50`) through the base accent (`500`) to the darkest shade
(`900`). Each step is a `color-mix` of the themed `--gui-primary` with white or
black, so the **whole ramp re-tones automatically** whenever the theme changes
the accent or dark mode flips — you never hand-pick a second set of shades.

| Variable            | Step        | Typical use                                  |
| ------------------- | ----------- | -------------------------------------------- |
| `--gui-primary-50`  | lightest    | page-level accent wash, hovered table row    |
| `--gui-primary-100` |             | subtle fills, selected-row background        |
| `--gui-primary-200` |             | chips, badges, disabled accent surfaces      |
| `--gui-primary-300` |             | borders on tinted surfaces                   |
| `--gui-primary-400` |             | hover state for light accent elements        |
| `--gui-primary-500` | base accent | same as `--gui-primary`                      |
| `--gui-primary-600` |             | accent hover / pressed                       |
| `--gui-primary-700` |             | accent active, high-emphasis accent text     |
| `--gui-primary-800` |             | accent text on light backgrounds             |
| `--gui-primary-900` | darkest     | maximum-contrast accent text / headings      |

```luma
# A selected list row that tracks the theme accent at a fixed tonal step
GraphicalUi.row(children, {
    "background_color": "var(--gui-primary-100)",
    "border": "1px solid var(--gui-primary-300)"
})
```

> **Contrast check (devtools).** When `"devtools": true` is set in the app
> config, the runtime checks the theme's `text_color` and `accent` against the
> `background` at startup and prints a warning to the console for any pair below
> the **WCAG AA** contrast minimum (4.5:1 for normal text). The check is a
> development aid only — it never blocks the app, and it is silent unless
> `devtools` is enabled. Only hex colours (`#rgb`/`#rrggbb`/`#rrggbbaa`) are
> analysed; `rgb()`, `hsl()`, and named colours are skipped.

### Elevation Tokens

Drop shadows follow a **six-step elevation scale**, from a barely-lifted resting
control (`1`) up to a modal surface (`6`). The built-in surfaces already consume
the appropriate step — a tooltip lifts less than a popover, which lifts less than
a dialog — so overlapping layers read in the right depth order without any manual
tuning. Every step is built on the shared `--gui-shadow-color`, so the **whole
scale re-tones together** when the theme or dark mode changes; you never hand-pick
a second set of shadows. Reference the steps through their CSS variables (as with
the [tonal ramp](#tonal-ramp-tokens)); for a single generic shadow the
`GraphicalUi.VAR_SHADOW` constant remains the shorthand.

| Variable            | Step             | Typical use                                  |
| ------------------- | ---------------- | -------------------------------------------- |
| `--gui-elevation-1` | resting control  | switch thumbs and other low resting lift     |
| `--gui-elevation-2` | interactive hover | hovered buttons and other lifting controls  |
| `--gui-elevation-3` | tooltip          | tooltips and other small transient overlays  |
| `--gui-elevation-4` | toast            | toasts and inline notifications              |
| `--gui-elevation-5` | floating panel   | menus, popovers, dropdown surfaces           |
| `--gui-elevation-6` | modal surface    | dialogs and other focus-stealing overlays    |

```luma
# A custom raised surface that tracks the theme's shadow tone
GraphicalUi.column(children, {
    "background_color": GraphicalUi.VAR_BG,
    "border_radius": GraphicalUi.VAR_RADIUS,
    "box_shadow": "var(--gui-elevation-3)"
})
```

---

## 9 — Charts

`labels` and `values` arrays must have the same length. **Hovering a data point
(or pie/donut slice) shows a floating tooltip** with its label and value — no
configuration needed.

| Function                                             | Parameters                                                      | Description      |
| ---------------------------------------------------- | --------------------------------------------------------------- | ---------------- |
| `vertical_bar_chart(labels, values, options?)`       | `(array<string>, array<number>, dictionary?)`                   | Vertical bars    |
| `horizontal_bar_chart(labels, values, options?)`     | `(array<string>, array<number>, dictionary?)`                   | Horizontal bars  |
| `line_chart(labels, values, options?)`               | `(array<string>, array<number>, dictionary?)`                   | Line with points |
| `area_chart(labels, values, options?)`               | `(array<string>, array<number>, dictionary?)`                   | Filled area      |
| `pie_chart(labels, values, options?)`                | `(array<string>, array<number>, dictionary?)`                   | Pie with legend  |
| `donut_chart(labels, values, center_label?, options?)` | `(array<string>, array<number>, string?, dictionary?)`        | Donut chart      |
| `scatter_plot(x, y, x_label?, y_label?, options?)`   | `(array<number>, array<number>, string?, string?, dictionary?)` | Scatter plot     |

```luma
GraphicalUi.line_chart(
    ["Jan", "Feb", "Mar", "Apr"],
    [10.0, 25.0, 18.0, 32.0],
    {"height": "300px"}
)
```

### Chart Options

The trailing dictionary doubles as the style bag and a carrier for these
presentation options (any other key is treated as CSS on the chart container):

| Key          | Type      | Effect                                                              |
| ------------ | --------- | ------------------------------------------------------------------- |
| `"x_label"`  | string    | Title for the category / x axis (bar, line, area; also scatter)     |
| `"y_label"`  | string    | Title for the value / y axis                                        |
| `"legend"`   | boolean   | Show uPlot's value legend below the plot (default `false`)          |
| `"tooltip"`  | boolean   | Hover tooltip; on by default — set `false` to disable               |

```luma
GraphicalUi.line_chart(["Jan", "Feb", "Mar"], [10.0, 25.0, 18.0], {
    "height": "300px",
    "x_label": "Month",
    "y_label": "Revenue",
    "legend": true
})
```

---

## 10 — Commands (Side Effects)

Commands are data values returned from `update` via `with_command`. The runtime executes them and delivers results as messages.

### Core Commands

| Function                       | Parameters         | Returns      | Description          |
| ------------------------------ | ------------------ | ------------ | -------------------- |
| `none()`                       | `()`               | `command`    | No-op                |
| `batch(commands)`              | `(array<command>)` | `command`    | Execute multiple     |
| `with_command(model, command)` | `(any, command)`   | `dictionary` | Pair model + command |

### HTTP

| Function                                   | Parameters                                | Returns   | Description  |
| ------------------------------------------ | ----------------------------------------- | --------- | ------------ |
| `http_get(url, callback, headers?)`        | `(string, function, dictionary?)`         | `command` | GET request  |
| `http_post(url, body, callback, headers?)` | `(string, string, function, dictionary?)` | `command` | POST request |
| `http_put(url, body, callback, headers?)`  | `(string, string, function, dictionary?)` | `command` | PUT request  |
| `http_patch(url, body, callback, headers?)`| `(string, string, function, dictionary?)` | `command` | PATCH request |
| `http_delete(url, callback, headers?)`     | `(string, function, dictionary?)`         | `command` | DELETE request |

> **Non-blocking.** HTTP requests run on a **background thread**, so the window
> stays fully responsive while a request is in flight — renders, clicks, and
> other messages keep flowing. When the response (or error) arrives, your
> callback runs back on the UI thread and its result is delivered as a normal
> message, so `update` and `view` stay single-threaded and you never touch a
> lock. The default timeout is 8 s; pass an explicit timeout in milliseconds as a
> trailing argument (after the optional `headers`) to raise it for slow but
> trusted servers. In-flight requests are abandoned cleanly if the window closes
> before they finish.

### Clipboard

| Function                   | Parameters   | Returns   | Description         |
| -------------------------- | ------------ | --------- | ------------------- |
| `write_clipboard(text)`    | `(string)`   | `command` | Copy to clipboard   |
| `read_clipboard(callback)` | `(function)` | `command` | Read from clipboard |

### Local Storage

| Function                           | Parameters           | Returns   | Description                  |
| ---------------------------------- | -------------------- | --------- | ---------------------------- |
| `get_local_storage(key, callback)` | `(string, function)` | `command` | Read value from localStorage |
| `set_local_storage(key, value)`    | `(string, string)`   | `command` | Write value to localStorage  |

```luma
# Save a preference
GraphicalUi.with_command(model, GraphicalUi.set_local_storage("theme", "dark"))

# Load a preference
GraphicalUi.with_command(model, GraphicalUi.get_local_storage("theme", (string val) -> {
    "loaded_theme:${val}"
}))
```

### File Download

| Function                       | Parameters         | Returns   | Description              |
| ------------------------------ | ------------------ | --------- | ------------------------ |
| `download_file(url, filename)` | `(string, string)` | `command` | Trigger browser download |

> **Security:** only `http`, `https`, `data`, and `blob` URLs (or relative
> paths) are allowed; script-bearing schemes such as `javascript:` are rejected.

### Notifications

| Function                      | Parameters                   | Returns   | Description          |
| ----------------------------- | ---------------------------- | --------- | -------------------- |
| `notify(title, body?, icon?)` | `(string, string?, string?)` | `command` | Web Notification API |

### Other Commands

| Function                     | Parameters                   | Returns   | Description                |
| ---------------------------- | ---------------------------- | --------- | -------------------------- |
| `delay(ms, callback)`        | `(integer, function)`        | `command` | Wait then invoke callback  |
| `random(min, max, callback)` | `(number, number, function)` | `command` | Random number in range     |
| `debounce(id, ms, callback)` | `(string, integer, function)`| `command` | Coalesce rapid calls; fire after `ms` of quiet |
| `focus(widget_id)`           | `(string)`                   | `command` | Move keyboard focus        |
| `announce(text)`             | `(string)`                   | `command` | Screen reader announcement |
| `open_url(url)`              | `(string)`                   | `command` | Open a URL in the default browser |
| `set_title(title)`           | `(string)`                   | `command` | Update the window title    |
| `print()`                    | `()`                         | `command` | Open the browser print dialog |

> **Security:** `open_url` accepts only `http`, `https`, `mailto`, and `tel` URLs
> (or relative paths); script-bearing schemes such as `javascript:` are rejected.

### Returning Commands from Update

```luma
"update": (dictionary model, string msg) -> {
    if msg == "fetch" {
        GraphicalUi.with_command(model,
            GraphicalUi.http_get("https://api.example.com/data", (result<string> res) -> {
                match res {
                    success(body) { "got:${body}" }
                    failure(err) { "error:${err}" }
                }
            })
        )
    } else {
        model
    }
}
```

---

## 11 — Subscriptions

Subscriptions react to external events not tied to a specific widget. Declare them in a `subscribe` function that returns an array based on the current model. The runtime diffs subscriptions by `id` and manages listeners automatically.

| Function                              | Parameters                    | Returns        | Description                   |
| ------------------------------------- | ----------------------------- | -------------- | ----------------------------- |
| `on_tick(id, ms, callback)`           | `(string, integer, function)` | `subscription` | Timer every `ms` milliseconds |
| `on_key(id, filter, callback)`        | `(string, string, function)`  | `subscription` | Key press (`"*"` = any key)   |
| `on_resize(id, callback)`             | `(string, function)`          | `subscription` | Window resize with `(w, h)`   |
| `on_focus(id, callback)`              | `(string, function)`          | `subscription` | Window focus/blur             |
| `on_mouse(id, event_type, callback)`  | `(string, string, function)`  | `subscription` | Mouse events                  |
| `on_visibility_change(id, callback)`  | `(string, function)`          | `subscription` | Page visibility change        |
| `on_visibility_change_typed(id, callback)` | `(string, func(GraphicalUi.VisibilityState) -> any)` | `subscription` | Page visibility as a typed `Visible`/`Hidden` choice |
| `on_online(id, callback)`             | `(string, function)`          | `subscription` | Network comes online          |
| `on_offline(id, callback)`            | `(string, function)`          | `subscription` | Network goes offline          |
| `on_media_query(id, query, callback)` | `(string, string, function)`  | `subscription` | CSS media query match         |
| `on_scroll(id, callback)`             | `(string, function)`          | `subscription` | Document scroll position changes |
| `on_wheel_typed(id, callback)`        | `(string, func(GraphicalUi.WheelDelta) -> any)` | `subscription` | Scroll-wheel deltas as a typed `WheelDelta` record |
| `on_idle(id, timeout_ms, callback)`   | `(string, integer, function)` | `subscription` | User idle for `timeout_ms` milliseconds |
| `on_storage_change(id, key, callback)`| `(string, string, function)`  | `subscription` | `localStorage` `key` changed (another tab) |
| `on_storage_change_typed(id, key, callback)` | `(string, string, func(GraphicalUi.StorageEvent) -> any)` | `subscription` | `localStorage` `key` change as a typed `StorageEvent` record |
| `on_animation_frame(id, callback)`    | `(string, function)`          | `subscription` | Per-frame tick (`requestAnimationFrame`) |
| `on_drag(id, event_type, callback)`   | `(string, string, function)`  | `subscription` | Drag events; callback receives a position dictionary |

### Mouse Event Types

The `on_mouse` event_type parameter accepts: `"click"`, `"move"`, `"down"`, `"up"`, `"scroll"`. The callback receives a dictionary with `x`, `y`, `button`, and modifier keys (`ctrl`, `shift`, `alt`).

### Examples

```luma
"subscribe": (dictionary model) -> {
    mutable array subs = []

    # Timer that fires every second when running
    if model["running"] == "true" {
        subs = Array.push(subs, GraphicalUi.on_tick("timer", 1000, () -> "tick"))
    }

    # Always listen for keyboard
    subs = Array.push(subs, GraphicalUi.on_key("keys", "*", (string key) -> "key:${key}"))

    # Detect when page becomes hidden/visible
    subs = Array.push(subs, GraphicalUi.on_visibility_change("vis", (boolean visible) -> {
        if visible { "resumed" } else { "paused" }
    }))

    # Detect network status
    subs = Array.push(subs, GraphicalUi.on_online("net-on", () -> "online"))
    subs = Array.push(subs, GraphicalUi.on_offline("net-off", () -> "offline"))

    # Responsive breakpoint
    subs = Array.push(subs, GraphicalUi.on_media_query("mobile", "(max-width: 640px)", (boolean matches) -> {
        if matches { "mobile" } else { "desktop" }
    }))

    subs
}
```

---

## 12 — Components and Routing

### Components

Components memoize a render function by `id` and `model_slice`. If the slice is unchanged (by JSON comparison), the cached widget is returned.

```luma
GraphicalUi.component("counter", count, (integer n) -> {
    GraphicalUi.row([
        GraphicalUi.label("Count: ${n}"),
        GraphicalUi.button("+", () -> "inc")
    ])
})
```

### Error Boundaries

Wrap views that might fail with a fallback:

```luma
GraphicalUi.error_boundary(
    (string err) -> GraphicalUi.alert("Error: ${err}", GraphicalUi.ERROR),
    () -> render_complex_widget(data)
)
```

### Routing

Build multi-page apps with client-side routing:

| Function                                 | Parameters                   | Returns   | Description                |
| ---------------------------------------- | ---------------------------- | --------- | -------------------------- |
| `router(route, routes)`                  | `(string, dictionary)`       | `widget`  | Render matching route      |
| `navigate(route)`                        | `(string)`                   | `command` | Navigate to route          |
| `navigate_back()`                        | `()`                         | `command` | Navigate to previous route |
| `navigation_link(text, message, style?)` | `(string, any, dictionary?)` | `widget`  | Clickable nav link         |

Route dictionaries support parameterised routes with `{name}` placeholders:

```luma
GraphicalUi.router(route, {
    "/": GraphicalUi.label("Home"),
    "/about": GraphicalUi.label("About"),
    "/user/{id}": (dictionary params) -> {
        GraphicalUi.label("User ${params["id"]}")
    },
    "_": GraphicalUi.label("404 — Not Found")
})
```

---

## 13 — Animation

### Transitions

Apply CSS transitions to widget style properties:

```luma
GraphicalUi.transition(
    GraphicalUi.panel("Box", children, {
        "width": if expanded { "400px" } else { "200px" },
        "background_color": if expanded { "#4361ee" } else { "#ffffff" }
    }),
    {"width": "0.3s ease", "background_color": "0.3s ease"}
)
```

### Keyframe Animations

Define multi-step keyframe animations:

```luma
GraphicalUi.animate(
    GraphicalUi.label("Pulsing!"),
    [
        {"opacity": "1", "transform": "scale(1)"},
        {"opacity": "0.7", "transform": "scale(0.97)"},
        {"opacity": "1", "transform": "scale(1)"}
    ],
    {"duration": "2s", "iterations": "infinite"}
)
```

The options dictionary accepts:

| Key            | Type   | Default      | Description                          |
| -------------- | ------ | ------------ | ------------------------------------ |
| `"duration"`   | string | `"0.3s"`     | Animation duration                   |
| `"iterations"` | string | `"1"`        | Repeat count (`"infinite"` for loop) |
| `"easing"`     | string | `"ease"`     | Timing function                      |
| `"delay"`      | string | `"0s"`       | Start delay                          |
| `"fill"`       | string | `"forwards"` | Fill mode                            |

### Reduced Motion

The framework honours the user's `prefers-reduced-motion: reduce` setting. When it
is active, the bundled stylesheet neutralises its own transitions, hover lifts, and
spinner/skeleton loops, and collapses `transition()` and `animate()` to a single,
near-instant step so motion-sensitive users are not subjected to movement.

You can also disable framework motion unconditionally — regardless of the OS
setting — with the `"animations": false` theme key (see [Theme](#theme)). This is
useful for deterministic previews and tests, or to back a user-facing "reduce
motion" toggle in your own settings screen.

This automatic handling only covers motion the framework itself emits. **If you add
movement through raw CSS (custom `@keyframes`, `transition`, `transform`, or
`animation` in a `stylesheet()` block), you are obliged to wrap it in your own
`@media (prefers-reduced-motion: reduce)` query** and provide a static fallback.
Prefer opacity/colour changes over large positional movement, and never convey
essential information through motion alone.

---

## 14 — Accessibility

### Built-in Behaviour

Many widgets ship keyboard and screen-reader support out of the box, so the common
cases need no extra wiring:

- **Focus rings.** Every interactive widget exposes a visible `:focus-visible`
  outline that follows the theme accent, so keyboard users always see where they are.
- **Tabs** render as a real `role="tablist"` of `role="tab"` buttons with
  `aria-selected`; Left/Right (and Home/End) arrow keys move between tabs.
- **Clickable lists, `toggle`, and `switch`** are true focusable buttons/inputs —
  reachable with Tab and operable with Enter/Space.
- **`dialog`** is a genuine modal: it sets `aria-modal`, traps Tab focus inside the
  panel, closes on Esc, and restores focus to the trigger when dismissed.
- **`menu`, `popover`, and `combobox`** open on click or keyboard, dismiss on Esc or
  outside-click, and expose the matching `aria-expanded`/`aria-haspopup` state.
- **`alert` and `toast`** pair each severity with an icon (info, success, warning,
  error) rather than relying on colour alone, and meet WCAG AA text contrast.
- **`field`** associates its label with the control via a real `<label>`, marks
  required fields, and exposes help text and an `role="alert"` error message
  (icon + text) for accessible inline validation.
- **`confirm`** is a true modal `role="alertdialog"`: it labels itself from the
  title/message, traps focus, closes on Esc, and restores focus on dismiss.
- **Charts** carry `role="img"` with a generated `aria-label` plus a
  visually-hidden text summary (type, series, and value range), so non-visual
  users get the gist without seeing the SVG.
- **Reduced motion** is honoured automatically — see [§13](#reduced-motion).

The helpers below let you add ARIA semantics to anything else.

### ARIA Attributes

Wrap any widget with ARIA attributes:

```luma
GraphicalUi.accessible(
    GraphicalUi.button("X", () -> "close"),
    {"role": "button", "aria_label": "Close dialog"}
)
```

For a single attribute you can skip the wrapper and pass `role` or any `aria_*`
key directly in a widget's style dictionary; it is applied to the rendered
element:

```luma
GraphicalUi.button("X", () -> "close", {"aria_label": "Close dialog"})
```

> **Accessible-name check (devtools).** When `"devtools": true` is set in the app
> config, the runtime scans each rendered frame for interactive controls
> (buttons and links) that have no discernible accessible name — the icon-only or
> glyph-only button (`"+"`, `"×"`, `"☰"`) a screen reader would announce as
> nothing — and prints a one-time console warning naming the offending element.
> Give the control visible text, an `aria_label` style key, or a labelled
> `image`/`icon` child to satisfy it. Like the contrast check, this is a
> development aid only: it never blocks the app, warns at most once per element,
> and is silent unless `devtools` is enabled.

### Live Regions

Wrap content in an ARIA live region for screen reader announcements:

```luma
# Polite announcement — read when user is idle
GraphicalUi.aria_live("polite", GraphicalUi.label("Status: ${status}"))

# Assertive announcement — read immediately
GraphicalUi.aria_live("assertive", GraphicalUi.label("Error: connection lost"))
```

### Description Association

Associate a description element with a widget:

```luma
GraphicalUi.aria_describedby("password-help",
    GraphicalUi.column([
        GraphicalUi.text_input(password, on_change, "Enter password"),
        GraphicalUi.label("Must be at least 8 characters", {"id": "password-help"})
    ])
)
```

### Focus Management

Move keyboard focus programmatically:

```luma
GraphicalUi.with_command(model, GraphicalUi.focus("search-input"))
```

### Screen Reader Announcements

Announce text to screen readers:

```luma
GraphicalUi.with_command(model, GraphicalUi.announce("Item deleted"))
```

### Keyed Lists

Assign stable identities for efficient list rendering:

```luma
Array.map(items, (dictionary item) -> {
    GraphicalUi.keyed("item_${item["id"]}", render_item(item))
}) ?? []
```

---

## 15 — Virtual Lists

Render large lists efficiently by only rendering visible items:

```luma
GraphicalUi.virtual_list(items, item_height, visible_count, style?)
```

| Parameter       | Type            | Description                     |
| --------------- | --------------- | ------------------------------- |
| `items`         | `array<widget>` | All items in the list           |
| `item_height`   | `integer`       | Height of each item in pixels   |
| `visible_count` | `integer`       | Number of items visible at once |
| `style?`        | `dictionary`    | Optional container styles       |

```luma
mutable array<widget> items = []
mutable integer i = 0
loop {
    if i >= 1000 { break }
    items = Array.push(items, GraphicalUi.label("Item ${i + 1}"))
    i = i + 1
}

GraphicalUi.virtual_list(items, 40, 15)
```

---

## 16 — Layout Debugging

Wrap any widget tree to visualise layout with coloured borders:

```luma
GraphicalUi.debug(
    GraphicalUi.column([
        GraphicalUi.row([GraphicalUi.label("A"), GraphicalUi.label("B")]),
        GraphicalUi.label("C")
    ])
)
```

`debug` cycles through red, blue, and green borders at each nesting depth.

---

## 17 — Responsive Design

### Device Classification

```luma
dictionary device = GraphicalUi.classify_device(width, height)
# device["class"] → "phone" | "tablet" | "desktop" | "big_desktop"
# device["orientation"] → "portrait" | "landscape"
```

| Width Range | Class           |
| ----------- | --------------- |
| < 640       | `"phone"`       |
| 640 – 1023  | `"tablet"`      |
| 1024 – 1919 | `"desktop"`     |
| ≥ 1920      | `"big_desktop"` |

### Responsive Styles

Mobile-first breakpoints with `responsive`:

```luma
dictionary style = GraphicalUi.responsive({
    "0": {"flex_direction": "column", "padding": "8px"},
    "640": {"flex_direction": "row", "padding": "16px"},
    "1024": {"padding": "24px"}
})
```

### Automatic Grid Reflow

`grid` containers reflow to a single column below the phone breakpoint
(≤ 640 px) automatically, and the page gutter tightens — no `responsive` wiring
needed. This is driven by the stylesheet, not inline styles, so it costs nothing
per render. Supplying an explicit `grid_template_columns` (via the style
dictionary) opts a grid out of the automatic reflow when you need fixed columns.

### Media Query Subscription

React to CSS media query changes at runtime:

```luma
GraphicalUi.on_media_query("dark-mode", "(prefers-color-scheme: dark)", (boolean matches) -> {
    if matches { "dark_mode_on" } else { "dark_mode_off" }
})
```

---

## 18 — Layout & Styling Best Practices

The sections above describe _what_ each function does. This section distils _how_ to combine them into modern, usable, theme-aware interfaces. Every guideline below is demonstrated by the programs in `examples/applications/`.

### Layout structure

- **Build every screen from nested `row` and `column`.** Treat them as the only positioning primitive — do not use margins to position siblings.
- **Separate groups with `flexible_space()`, never a spacer.** It expands to fill the gap and pushes its siblings to opposite edges. Reach for it in toolbars and footers; do not abuse `spacer(0)` or an oversized `spacer(n)` as a pusher.

  ```luma
  GraphicalUi.toolbar([
      GraphicalUi.heading("Inbox", 2),
      GraphicalUi.flexible_space(),          # pushes the action to the far edge
      GraphicalUi.button("Compose", on_new, {"variant": GraphicalUi.PRIMARY})
  ])
  ```

- **Grow the right child with `flex`.** Put `{"flex": GraphicalUi.fill()}` on the element that should absorb spare space (search fields, content panes). Use `fill_portion(n)` for weighted columns and `px(n)` for fixed ones.
- **`grid(n)` is always _equal_ columns.** For an unequal split — e.g. a narrow sidebar beside wide content — use a `row` with explicit `flex` values instead:

  ```luma
  GraphicalUi.row([
      GraphicalUi.column(sidebar, {"flex": "0 0 280px"}),         # fixed-width sidebar
      GraphicalUi.column(content, {"flex": GraphicalUi.fill()})   # fills the remainder
  ], GraphicalUi.spacing(16))
  ```

- **Set the gap once on the container** with `spacing(n)` rather than inserting a spacer between every child.
- **Rows align children to the top by default** (`align_items: flex-start`). Icons, buttons, and text inputs placed directly in a `row` or `toolbar` are handled for you: icons are vertically centred and scaled to match the adjacent text or heading, and buttons and inputs are normalised to a shared height and baseline — so a search field and its button line up without manual tweaks. To vertically centre any _other_ widget against its siblings, set `{"align_items": "center"}` on the row.

### Colour and theming

- **Never hard-code colours — use the theme.** Reference the `GraphicalUi.VAR_*` constants (`VAR_PRIMARY`, `VAR_FG`, `VAR_BORDER`, `VAR_SUCCESS` / `VAR_WARNING` / `VAR_ERROR`, `VAR_RADIUS`, …). Literal hex values such as `#f5f5f5` look broken in dark mode.
- **For tinted surfaces and badges use the subtle tokens** (see §8) or `color-mix`. Both are derived from the theme and adapt to light/dark automatically.
- **Set button hierarchy with the `variant` key**, not hand-rolled style helpers. `{"variant": GraphicalUi.PRIMARY}` (and `SECONDARY`/`GHOST`/`DANGER`) give a theme-aware, AA-safe appearance out of the box; reach for `merge_styles` and custom style functions only for bespoke controls the variants do not cover.
- **Buttons ship with a built-in hover/press affordance** (a subtle brightness and shadow shift on `:hover`, deeper on `:active`). Add extra feedback with the pseudo-prefixes (`hover_…`, `focus_…`, `active_transform: "scale(0.97)"`) when a control needs more emphasis; links and custom controls should still respond visibly to the pointer.

### Interaction and UX

- **Give actions a clear hierarchy.** Use one solid _primary_ button per view (`{"variant": GraphicalUi.PRIMARY}`), outlined _secondary_ or borderless _ghost_ buttons for supporting actions, and the _danger_ variant for destructive ones. Right-align footer and dialog actions with `{"justify_content": "flex-end"}`.
- **Reflect state visually.** Selected tabs, active navigation links, and segmented toggles must look different from their inactive siblings — compare against the model and swap the style.
- **Render conditionally with `when(condition, child)`** instead of leaving an empty placeholder widget.
- **Always provide an empty state.** Replace a blank list or panel with the `empty_state(message, options?)` widget — it pairs an icon, title, message, and an optional call-to-action so a blank view never reads as broken.
- **Show progress for slow work.** Use `progress(value, max)` when you know the proportion done, and `spinner(label?)` for indeterminate waits; surface transient outcomes through `toast` inside a single `toast_region`.

### Anti-patterns to avoid

| Avoid                                                       | Prefer                                                          |
| ----------------------------------------------------------- | -------------------------------------------------------------- |
| Hand-rolled `primary_button_style()` / `ghost_button_style()` helpers | the built-in `{"variant": GraphicalUi.PRIMARY/SECONDARY/GHOST/DANGER}` key |
| `spacer(0)` / oversized `spacer(n)` used as a pusher        | `flexible_space()`                                             |
| Hard-coded hex colours for UI surfaces                      | `VAR_*` constants and the `*-subtle-bg` tokens                 |
| `grid(2)` for a sidebar + content split                     | `row` with `{"flex": "0 0 <width>"}` and `fill()`             |
| `center_x()` / `center_y()` when the row/column axis is ambiguous | explicit `{"align_items": …}` / `{"justify_content": …}` |
| Interpolating a `result`/`optional` into a label (`"${a_result}"` → `success(…)`) | unwrap first (`match`, `?`, `Result.unwrap_or`) and interpolate the inner value |

---

## 19 — Performance Tips

1. **Use `component` for expensive subtrees.** Memoization avoids re-rendering when the model slice is unchanged.

2. **Use `keyed` for dynamic lists.** Stable keys enable efficient DOM patching when lists change.

3. **Use `virtual_list` for large datasets.** Only visible items are rendered in the DOM.

4. **Keep the model flat.** Deeply nested models are expensive to diff and copy.

5. **Batch commands.** Use `GraphicalUi.batch([cmd1, cmd2])` instead of multiple `with_command` calls.

6. **Use `on_tick` judiciously.** High-frequency timers (< 100ms) can cause excessive re-renders.

7. **Avoid inline lambdas in `component`.** The memoization cache compares `model_slice` by JSON serialisation — new closures do not break the cache, but building them on every render is wasteful.

8. **Use `responsive` over manual breakpoint logic.** The runtime evaluates it once per render, not per-widget.

---

## 20 — Examples

The `examples/applications/` directory contains complete applications demonstrating various patterns:

| File                    | Description                                                       |
| ----------------------- | ---------------------------------------------------------------- |
| `gui_counter.luma`      | Minimal counter; button hierarchy and a large value display      |
| `solaris_counter.luma`  | Same counter, written on the `Solaris` surface (typed Model/Msg) |
| `gui_todo.luma`         | To-do list with add/toggle/delete, a live count, and empty state |
| `gui_contacts.luma`     | Master/detail contacts with search, a dialog, and danger actions |
| `gui_settings.luma`     | Settings form with grouped controls and a save/reset footer      |
| `gui_http.luma`         | HTTP client with a filling URL bar and a status badge            |
| `gui_dashboard.luma`    | Analytics dashboard: metric cards, charts, tabs, and components   |
| `gui_layout.luma`       | Sizing, alignment, and spacing helpers with theme-aware tints    |
| `gui_styled.luma`       | Style composition, validation, and a segmented theme toggle      |
| `gui_router.luma`       | Multi-page routing with a persistent nav bar and active links    |
| `gui_animation.luma`    | CSS transitions, keyframe animations, and input widgets          |
| `gui_virtual_list.luma` | Large list (1000 items) with a header and zebra striping         |

### Running an Example

```bash
luma examples/applications/gui_counter.luma
```

---

## 21 — Testing Without a Window

Because the Elm pattern keeps all logic in pure `view`, `update`, and `subscribe`
functions, a GraphicalUi application can be driven and verified **without opening a
window**. The `GraphicalUi.test_*` functions render the view, simulate a real
interaction through the same callback/update cycle the live app uses, and return
the resulting model. They take the same `config` dictionary that `GraphicalUi.app`
consumes, so the example's own configuration is exercised directly.

Expose the configuration as a named function so both `@main` and `@test` blocks
can share it:

```luma
function dictionary counter_config() {
    mutable dictionary config = {"_": "gui_config"}
    config["model"] = 0
    config["view"] = view_counter
    config["update"] = (integer model, string msg) -> match msg {
        case "inc" { model + 1 }
        case "dec" { model - 1 }
        else { model }
    }
    return config
}

@test
function void counter_increments() {
    # A button () -> "inc" returns a message routed through update.
    assert(GraphicalUi.test_click(counter_config(), 0, "+") == 1)
}
```

### Locating Widgets

Widgets are found by their visible text — a button label, a text-input
placeholder, a current value, a `name`, or a `title`. When several widgets share
a locator, disambiguate them in one of two ways:

- **By index** — pass a 0-based `index` (the last argument of `test_click`,
  `test_input`, `test_event`, and `test_find`). `GraphicalUi.test_count(config,
  model, locator)` reports how many widgets match.
- **By style `id`** — give a widget a unique `id` in its style dictionary. The id
  becomes a locator, so it can be addressed by identity regardless of position.

```luma
mutable dictionary id_a = {}
id_a["id"] = "step-a"

# ... GraphicalUi.button("Step", () -> "inc", id_a)

assert(GraphicalUi.test_count(app, 0, "Step") == 2)   # two duplicates
assert(GraphicalUi.test_click(app, 0, "Step", 1) == ...)  # the second one
assert(GraphicalUi.test_click(app, 0, "step-a") == ...)   # by identity
```

### The Test Functions

| Function       | Purpose                                                                        |
| -------------- | ------------------------------------------------------------------------------ |
| `test_init`    | Run `init` (or the configured model) and return the initial model              |
| `test_render`  | Render `view(model)` and return the widget tree for structural assertions      |
| `test_count`   | Count the widgets matching a locator                                           |
| `test_find`    | Return a matching widget's rendered dictionary for state assertions            |
| `test_click`   | Click the matching widget (optionally the `index`-th) and return the new model |
| `test_input`   | Send a value to the matching widget (text, checkbox, toggle, slider, dropdown) |
| `test_event`   | Fire a named widget event, forwarding optional arguments                       |
| `test_key`     | Deliver a key to the application's `on_key` subscriptions                      |
| `test_message` | Deliver a message to `update(model, message)` directly                         |

### Secondary Events and Keyboard Shortcuts

`test_event` fires the handlers a widget exposes — the style-dictionary pointer
handlers `click`, `change`, `double_click`, `right_click`, `mouse_enter`,
`mouse_leave`, and `mouse_move`, plus the dedicated handlers `close`
(a `dialog`'s `on_close`), `clear` (a `search_input`'s `on_clear`), and `commit`
(a `text_input`/`text_area`'s `on_commit`, fired on blur/Enter in the live app).
An optional `args` array is forwarded to the handler, so handlers of any arity
can be tested:

```luma
mutable dictionary pad = {}
pad["on_mouse_move"] = (integer x, integer y) -> model + x + y
# ... GraphicalUi.button("Pad", () -> "noop", pad)

assert(GraphicalUi.test_event(app, 5, "Pad", "mouse_move", [3, 4]) == 12)

# A dialog (located by its title) is dismissed through its on_close handler,
# and a search_input (located by its current value) through its on_clear:
assert(GraphicalUi.test_event(app, model, "Add Contact", "close") == closed_model)
assert(GraphicalUi.test_event(app, model, "query text", "clear") == cleared_model)

# A text_input's on_commit (located by placeholder) is driven with "commit",
# forwarding the committed text as the argument:
assert(GraphicalUi.test_event(app, model, "Search…", "commit", ["done"]) == committed_model)
```

`test_key` drives the keyboard path end-to-end — `subscribe` produces the
`on_key` subscriptions, the matching one's callback runs, and its message is
routed through `update`, exactly as the live runtime dispatches a key:

```luma
assert(GraphicalUi.test_key(counter_config(), 0, "ArrowUp") == 1)
```

### Asserting on Rendered State

`test_find` returns the rendered widget dictionary so a test can assert on its
serialized state (its `type`, `text`, `value`, `_element_id`, …) without firing
an interaction:

```luma
dictionary label = GraphicalUi.test_find(counter_config(), 7, "Count: 7")
assert(Dictionary.get_or(label, "text", "") == "Count: 7")
```

> A locator that matches no widget — or an out-of-range `index` — raises a runtime
> error, so a mistyped label fails the test loudly rather than silently passing.
> Command side effects are inert without a window, so model updates remain
> deterministic.

---

## 22 — Icon Reference

`icon(name, size?, style?)` renders a [Lucide](https://lucide.dev) line icon as
inline SVG. The icon inherits the current text colour (`currentColor`) and
defaults to 24×24 px; pass `size` (pixels) to override. Placed directly in a `row`
or `toolbar`, an icon is automatically vertically centred and scaled to match the
adjacent text or heading — see [§18](#18--layout--styling-best-practices).

```luma
GraphicalUi.icon("check-circle")          # 24 px, follows the text colour
GraphicalUi.icon("trending-up", 32)       # 32 px
```

**Naming.** `name` is a **kebab-case** Lucide identifier (e.g. `"trending-up"`).
Lookups are case-insensitive and underscores are treated as hyphens, so
`"trending-up"`, `"trending_up"`, and `"Trending-Up"` all resolve to the same
icon. A name that is **not** in the set below renders a dimmed **fallback glyph**
(a `help-circle`) with the attempted name exposed as its tooltip / accessible
label, rather than the raw text — so a misspelt or unbundled name shows as a
clearly "missing icon" mark instead of stray words. Enable `devtools` to log a
console warning for each unknown name.

**Available icons (129).** The bundled set is a curated subset of Lucide:

`activity`, `alert-circle`, `alert-triangle`, `aperture`, `arrow-down`,
`arrow-left`, `arrow-right`, `arrow-up`, `award`, `battery`, `bell`, `bold`,
`book`, `bookmark`, `box`, `briefcase`, `calendar`, `camera`, `cast`, `check`,
`check-circle`, `check-square`, `chevron-down`, `chevron-left`, `chevron-right`,
`chevron-up`, `circle`, `clipboard`, `clock`, `cloud`, `code`, `compass`, `copy`,
`cpu`, `credit-card`, `database`, `disc`, `download`, `edit`, `external-link`,
`eye`, `eye-off`, `feather`, `file`, `file-text`, `filter`, `flag`, `folder`,
`globe`, `hash`, `headphones`, `heart`, `help-circle`, `home`, `image`, `inbox`,
`info`, `italic`, `key`, `layers`, `link`, `list`, `lock`, `log-in`, `log-out`,
`mail`, `map-pin`, `menu`, `message-circle`, `mic`, `minus`, `monitor`, `moon`,
`more-horizontal`, `more-vertical`, `music`, `navigation`, `package`, `pause`,
`pen-line`, `phone`, `play`, `plus`, `power`, `printer`, `radio`, `refresh-cw`,
`save`, `search`, `send`, `server`, `settings`, `share`, `shield`,
`shopping-cart`, `sliders`, `smartphone`, `smile`, `sparkles`, `speaker`,
`square`, `star`, `sun`, `table`, `tag`, `terminal`, `thumbs-down`, `thumbs-up`,
`trash`, `trending-up`, `truck`, `tv`, `type`, `underline`, `undo`, `unlock`,
`upload`, `user`, `users`, `video`, `volume`, `watch`, `wifi`, `wind`, `wrench`,
`x`, `zap`, `zoom-in`, `zoom-out`.

> **Extending the set.** The subset is defined by the `wanted` array in
> `external/lucide/extract_icons.js`, which emits `lucide-subset.json` and the two
> embedded `lucide-icons-part*.json` files. To add an icon, append its PascalCase
> Lucide name there, rerun that script, then regenerate the GUI assets
> (`scripts/generate_gui_assets.mjs`) and rebuild.

---

## See Also

- [Solaris Tutorial](Luma_Solaris_Tutorial.md) — a beginner-first, step-by-step introduction to GUI programming with Solaris
- [Solaris Guide](Luma_Solaris_Guide.md) — the beginner-first authoring surface built on this engine; start here for new GUI apps
- [Standard Library Reference — §14 (Solaris and GraphicalUi)](Luma_Standard_Library_Reference.md#14--solaris-and-graphicalui) — concise API listing for every function and constant
- [User Manual](Luma_User_Manual.md) — language syntax and semantics
- [Performance Guide](Luma_Performance_Guide.md) — runtime performance and optimisation advice
