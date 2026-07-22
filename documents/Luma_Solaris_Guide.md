# Luma — Solaris Guide

**Solaris** is Luma's beginner-first way to build desktop GUI applications.
You write only Luma — never HTML, CSS, or JavaScript — and a blank app already
looks polished. Solaris follows the **Model-View-Update (MVU)** architecture:
your screen is a pure function of your data, and data changes in exactly one
place.

Solaris is a **built-in standard library module** — there is nothing to
install and nothing to `include`. The moment your program refers to
`Solaris`, its surface (the design tokens, the `View` tree, and the
`Solaris` functions) is available. Under the hood, Solaris renders
through Luma's hardened webview engine, the
[`GraphicalUi`](Luma_GraphicalUi_Guide.md) module; you never touch that layer for an
everyday app. For the design philosophy behind this surface, see the
[Solaris design concept](Luma_Solaris_Architecture.md).

## Table of Contents

1. [Quick Start](#1--quick-start)
2. [The Three Concepts](#2--the-three-concepts)
3. [Components](#3--components)
4. [Modifiers](#4--modifiers)
5. [Design Tokens](#5--design-tokens)
6. [Effects, Subscriptions, and Startup](#6--effects-subscriptions-and-startup)
7. [Theming, Windows, and Persistence](#7--theming-windows-and-persistence)
8. [Running and Testing](#8--running-and-testing)
9. [How Solaris Relates to GraphicalUi](#9--how-solaris-relates-to-graphicalui)
10. [Limitations](#10--limitations)
11. [See Also](#11--see-also)

---

## 1 — Quick Start

A Solaris program is a single Luma file. There is no include and no
configuration — referring to `Solaris` is enough.

```luma
choice Msg {
    Increment,
    Decrement
}

record Model {
    integer count = 0
}

function Model update(Model model, Msg msg) {
    return match msg {
        case Msg.Increment { model with { count = model.count + 1 } }
        case Msg.Decrement { model with { count = model.count - 1 } }
    }
}

function View view(Model model) {
    return Solaris.column([
        Solaris.heading("Counter"),
        Solaris.text("${model.count}") |> Solaris.size(TextScale.Title) |> Solaris.center(),
        Solaris.row([
            Solaris.button("-") |> Solaris.on_click(Msg.Decrement) |> Solaris.muted(),
            Solaris.button("+") |> Solaris.on_click(Msg.Increment) |> Solaris.primary()
        ]) |> Solaris.gap(Spacing.M)
    ]) |> Solaris.padding(Spacing.L) |> Solaris.gap(Spacing.M)
}

@main
function void main() {
    Solaris.run(Solaris.app("Counter", Model { count = 0 }, update, view))
}
```

Run it with `luma your_app.luma`. A complete, runnable version of this program
lives at [`examples/applications/solaris_counter.luma`](../examples/applications/solaris_counter.luma),
and a broader tour of the components is at
[`examples/applications/solaris_showcase.luma`](../examples/applications/solaris_showcase.luma).

---

## 2 — The Three Concepts

A beginner learns just three things — **Model**, **Message**, and the two pure
functions **update** and **view**.

### Model — a typed record

The Model is a `record` holding all of your application's state. Because Luma
values are immutable by default, you never mutate a widget; you describe the next
state and return it.

```luma
record Model {
    integer count = 0,
    string name = "",
    boolean subscribed = false
}
```

### Message — a choice type

A Message is a `choice` type enumerating every way the state can change. Variants
may carry data (for example, the new text from an input field). Because `update`
handles messages with an exhaustive `match` (no `else`), **forgetting a message
is a compile error** — the type checker keeps your logic complete.

```luma
choice Msg {
    Increment,
    Reset,
    SetName(string value),
    ToggleSubscribed(boolean value)
}
```

### update — a pure Model, Msg → Model transition

`update` is the single place state changes. It takes the current model and a
message and returns the next model, typically with the record `with` expression.

```luma
function Model update(Model model, Msg msg) {
    return match msg {
        case Msg.Increment            { model with { count = model.count + 1 } }
        case Msg.Reset                { model with { count = 0 } }
        case Msg.SetName(value)       { model with { name = value } }
        case Msg.ToggleSubscribed(on) { model with { subscribed = on } }
    }
}
```

### view — a pure Model → View function

`view` turns the model into an immutable `View` tree. It is called after every
update; you never manipulate widgets directly. This is the heart of Solaris's
promise: **UI = f(state)**.

```luma
function View view(Model model) {
    return Solaris.column([
        Solaris.heading("Hello, ${model.name}"),
        Solaris.text_field(model.name)
            |> Solaris.placeholder("Your name")
            |> Solaris.on_change((string v) -> Msg.SetName(v))
    ]) |> Solaris.padding(Spacing.L)
}
```

---

## 3 — Components

Every component is a function in the `Solaris` namespace that returns a
`View`. Containers take an `array<View>` of children. Components are grouped
below by role.

### Text and typography

| Component | Signature | Purpose |
|---|---|---|
| Text | `Solaris.text(string content)` | A run of body text. |
| Heading | `Solaris.heading(string content)` | A prominent section title. |
| Badge | `Solaris.badge(string text)` | A small status pill; colour it with `emphasis`. |
| Icon | `Solaris.icon(string name)` | A named glyph; size it with `icon_size`. |
| Spinner | `Solaris.spinner(string label)` | An indeterminate busy indicator. |
| Divider | `Solaris.divider()` | A horizontal rule between sections. |

### Inputs

| Component | Signature | Purpose |
|---|---|---|
| Button | `Solaris.button(string label)` | A clickable action; pair with `on_click`. |
| Text field | `Solaris.text_field(string value)` | Single-line text input; pair with `on_change`. |
| Text area | `Solaris.text_area(string value)` | Multi-line text input; pair with `on_change`. |
| Checkbox | `Solaris.checkbox(string label)` | A boolean toggle; pair with `checked` and `on_toggle`. |
| Switch | `Solaris.switch(string label, boolean state)` | An on/off switch; pair with `on_toggle`. |
| Radio | `Solaris.radio(array<string> options, string chosen)` | A single choice from a set; pair with `on_select`. |
| Dropdown | `Solaris.dropdown(array<string> options, string chosen)` | A compact single choice; pair with `on_select`. |
| Slider | `Solaris.slider(number value, number min, number max)` | A numeric range control; pair with `on_slide`. |
| Date picker | `Solaris.date_picker(string value)` | A date input (`YYYY-MM-DD`); pair with `on_change`. |

### Layout containers

| Component | Signature | Purpose |
|---|---|---|
| Column | `Solaris.column(array<View> children)` | Stacks children vertically. |
| Row | `Solaris.row(array<View> children)` | Arranges children horizontally. |
| Grid | `Solaris.grid(integer columns, array<View> children)` | Lays children out in a fixed number of columns. |
| Z-stack | `Solaris.z_stack(array<View> children)` | Layers children on top of one another. |
| Scroll | `Solaris.scroll(array<View> children)` | A vertically scrollable region. |
| Spacer | `Solaris.spacer()` | Flexible empty space between siblings. |
| Card | `Solaris.card(array<View> children)` | A padded, elevated group of children. |
| List | `Solaris.list(array<View> items)` | A vertical list of items. |
| Panel | `Solaris.panel(string title, array<View> children)` | A titled, bordered group of children. |

### Data display

| Component | Signature | Purpose |
|---|---|---|
| Table | `Solaris.table(array<string> headers, array<array<string>> rows)` | A read-only data grid. |
| Progress | `Solaris.progress(number value, number max)` | A determinate progress bar. |
| Image | `Solaris.image(string source)` | A picture from a path or URL. |
| Line chart | `Solaris.line_chart(array<string> labels, array<number> values)` | Plots `values` as a line over the `labels`. |
| Bar chart | `Solaris.bar_chart(array<string> labels, array<number> values)` | One vertical bar per label, sized by its value. |
| Pie chart | `Solaris.pie_chart(array<string> labels, array<number> values)` | One slice per label, sized by its value. |

The three charts share one shape: a `labels` array names each data point and a
`values` array gives its size, so the two arrays line up index for index. Size a
chart with the usual `width`/`height` modifiers and give it a `label` for
screen-reader users, exactly like any other component:

```luma
Solaris.bar_chart(["Mon", "Tue", "Wed"], [12, 19, 7])
    |> Solaris.height(Length.Fixed(220.0))
    |> Solaris.label("Sales this week")
```

Because the values can be plain whole numbers like `[12, 19, 7]`, a beginner
never has to think about the `integer`-versus-`number` distinction here.

### Navigation and overlays

| Component | Signature | Purpose |
|---|---|---|
| Tabs | `Solaris.tabs(array<string> labels, integer active, array<View> panels)` | Switches between panels; pair with `on_tab`. |
| Menu | `Solaris.menu(string label, array<string> items)` | An in-page dropdown menu; pair with `on_select`. |
| Dialog | `Solaris.dialog(string title, boolean open, array<View> children)` | A modal shown when `open` is true; pair with `on_close`. |
| Toast | `Solaris.toast(string message)` | A transient notification banner. |
| Sidebar | `Solaris.sidebar(array<View> children)` | A fixed-width navigation rail. |
| App shell | `Solaris.app_shell(View side, View content)` | A full-height sidebar-plus-content layout. |

> **Note.** `Solaris.menu` is an in-page dropdown, not a native OS menu bar,
> and Solaris does not add a system tray icon. For an OS-level toast use the
> `Solaris.notify` **command** (see [§6](#6--effects-subscriptions-and-startup)).

---

## 4 — Modifiers

Modifiers are functions that take a `View` and return a new `View`, so they chain
naturally with the pipe operator `|>`. There is exactly one obvious modifier for
each visual concern.

### Behaviour

| Modifier | Applies to | Effect |
|---|---|---|
| `Solaris.on_click(msg)` | Button | Sends `msg` (a `choice` value) when clicked. |
| `Solaris.on_change(fn)` | Text field, text area, date picker | Calls `fn(string) -> Msg` on every edit. |
| `Solaris.on_toggle(fn)` | Checkbox, switch | Calls `fn(boolean) -> Msg` when toggled. |
| `Solaris.on_select(fn)` | Radio, dropdown, menu | Calls `fn(string) -> Msg` with the chosen option. |
| `Solaris.on_slide(fn)` | Slider | Calls `fn(number) -> Msg` as the value changes. |
| `Solaris.on_tab(fn)` | Tabs | Calls `fn(integer) -> Msg` with the selected tab index. |
| `Solaris.on_close(msg)` | Dialog | Sends `msg` when the dialog is dismissed. |

### Typography

| Modifier | Effect |
|---|---|
| `Solaris.size(TextScale scale)` | Sets the text scale (see [§5](#5--design-tokens)). |
| `Solaris.level(integer n)` | Sets a heading level (1–6). |
| `Solaris.weight(Weight w)` | Sets the font weight. |
| `Solaris.bold()` | Shorthand for bold weight. |

### Emphasis (semantic colour)

| Modifier | Meaning |
|---|---|
| `Solaris.primary()` | The main call to action. |
| `Solaris.secondary()` | A secondary action. |
| `Solaris.danger()` | A destructive action. |
| `Solaris.muted()` | De-emphasised / subtle. |
| `Solaris.emphasis(Emphasis chosen)` | Any emphasis token explicitly. |

### Layout and shape

| Modifier | Effect |
|---|---|
| `Solaris.gap(Spacing s)` | Space between a container's children. |
| `Solaris.padding(Spacing s)` | Inner padding of a container. |
| `Solaris.width(Length len)` | Sets width (see `Length` in [§5](#5--design-tokens)). |
| `Solaris.height(Length len)` | Sets height. |
| `Solaris.align(Align a)` | Cross-axis alignment of children. |
| `Solaris.justify(Justify j)` | Main-axis distribution of children. |
| `Solaris.center()` | Centres content. |
| `Solaris.rounded(Radius r)` | Corner rounding. |

### Input state and identity

| Modifier | Effect |
|---|---|
| `Solaris.checked(boolean state)` | The checkbox's checked state. |
| `Solaris.placeholder(string text)` | Placeholder text for a text field or area. |
| `Solaris.icon_size(integer size)` | The pixel size of an `icon`. |
| `Solaris.label(string text)` | An accessible name, emitted as an ARIA label. Use it on icon-only controls. |
| `Solaris.key(string id)` | A stable identity that keeps focus, caret, and scroll position steady when a list re-orders, and lets tests address the control. |

---

## 5 — Design Tokens

Solaris's look comes from **semantic tokens**, not raw pixels or colours.
Each token is a Luma `choice` type, so passing an invalid value is a compile
error, and editor autocomplete shows every option. The defaults are chosen for
good contrast and spacing out of the box. Because the tokens are global, you
write them unqualified — `TextScale.Title`, `Spacing.L`, `Length.Fixed(140)` —
never `Solaris.TextScale`.

| Token | Variants |
|---|---|
| `Emphasis` | `Normal`, `Primary`, `Secondary`, `Success`, `Warning`, `Danger`, `Muted` |
| `TextScale` | `Caption`, `Body`, `Large`, `Heading`, `Title` |
| `Weight` | `Regular`, `Bold` |
| `Spacing` | `None`, `XS`, `S`, `M`, `L`, `XL` |
| `Align` | `Start`, `Center`, `End`, `Stretch` |
| `Justify` | `Start`, `Center`, `End`, `SpaceBetween`, `SpaceAround` |
| `Length` | `Shrink`, `Fill`, `Fixed(number value)`, `FillPortion(integer weight)` |
| `Radius` | `None`, `Small`, `Medium`, `Large`, `Full` |
| `Scheme` | `Light`, `Dark`, `Auto` |

`Length.Fixed` carries a number, so a fixed width is written
`Solaris.width(Length.Fixed(140))`. `Length.Fill` expands to fill available
space, and `Length.Shrink` hugs the content. `Length.FillPortion(n)` divides
the free space among siblings in proportion to their weights — two children at
`FillPortion(1)` and `FillPortion(3)` split the row one quarter to three
quarters. `Scheme` is used with `Solaris.color_scheme` (see
[§7](#7--theming-windows-and-persistence)).

---

## 6 — Effects, Subscriptions, and Startup

Pure `update`/`view` cover most apps, but real programs also need *effects*:
notifications, timers, HTTP requests, dark-mode switches. Solaris expresses
these as plain values — **commands** and **subscriptions** — that it runs for
you, so your code stays pure and testable.

### Commands (effects from `update`)

`update` may return either the next model **or** a `(model, command)` pair built
with `Solaris.with_command`. When it returns a pair, Solaris applies the model
and then performs the command. A branch that returns a command widens the
function's return type to `any`; that is expected — Solaris inspects the value at
runtime and unwraps the pair.

```luma
function any update(Model model, Msg msg) {
    return match msg {
        case Msg.Save {
            Solaris.with_command(
                model with { saved = true },
                Solaris.notify("Saved", "Your work is safe."))
        }
        case Msg.Tick { model with { seconds = model.seconds + 1 } }
    }
}
```

| Command builder | Effect |
|---|---|
| `Solaris.no_command()` | Do nothing (an explicit empty effect). |
| `Solaris.with_command(model, command)` | Return the next model together with a command. |
| `Solaris.batch(array<any> commands)` | Run several commands at once. |
| `Solaris.after(integer ms, msg)` | Send `msg` once after a delay. |
| `Solaris.fetch(string url, fn)` | HTTP GET `url`; `fn(result) -> Msg` handles the reply. |
| `Solaris.notify(string title, string body)` | Show an OS desktop notification. |
| `Solaris.set_scheme(Scheme scheme)` | Switch light/dark/auto at runtime. |

### Subscriptions (effects from the outside world)

A subscription is a standing source of messages — a timer or a keyboard
shortcut. Provide a function `fn(model) -> array<any>` to `Solaris.subscribe`;
Solaris calls it after every update and keeps the listed subscriptions active.

```luma
function array<any> subscriptions(Model _model) {
    return [
        Solaris.every("clock", 1000, Msg.Tick),
        Solaris.on_key_press("save", "Ctrl+S", Msg.Save)
    ]
}
```

| Subscription builder | Effect |
|---|---|
| `Solaris.every(string id, integer ms, msg)` | Send `msg` on a repeating timer. |
| `Solaris.on_key_press(string id, string key, msg)` | Send `msg` on a global key press. |

### Startup

`Solaris.on_start(command)` runs a command once, when the app launches — ideal
for loading initial data or greeting the user.

```luma
Solaris.app("Timer", Model {}, update, view)
    |> Solaris.subscribe(subscriptions)
    |> Solaris.on_start(Solaris.notify("Welcome", "The timer is running."))
```

---

## 7 — Theming, Windows, and Persistence

These modifiers refine the app configuration returned by `Solaris.app`. Each
takes the configuration as its first argument, so they chain with `|>`.

### Theming and dark mode

| Modifier | Effect |
|---|---|
| `Solaris.accent(string color)` | The accent colour (a CSS colour, e.g. `"#6C4CF1"`). |
| `Solaris.font(string family)` | The UI font family. |
| `Solaris.color_scheme(Scheme scheme)` | Pin `Light`/`Dark`, or follow the OS with `Auto`. |
| `Solaris.theme(dictionary overrides)` | Advanced: override individual theme tokens. |

Dark mode is free: with `Scheme.Auto` (the default behaviour) the app follows the
operating system, and every component already has accessible light and dark
colours.

### Windows and native chrome

| Modifier | Effect |
|---|---|
| `Solaris.window(integer width, integer height)` | The initial window size. |
| `Solaris.min_size(integer width, integer height)` | The minimum window size. |
| `Solaris.max_size(integer width, integer height)` | The maximum window size. |
| `Solaris.resizable(boolean allowed)` | Whether the user may resize the window. |
| `Solaris.fullscreen()` | Start the app fullscreen. |
| `Solaris.devtools()` | Open the web inspector (useful while debugging). |

### Persistence and error handling

| Modifier | Effect |
|---|---|
| `Solaris.persist(string path)` | Save and restore the model across runs. |
| `Solaris.on_error(fn)` | A custom error view `fn(string) -> View`. |

If `view` or `update` ever throws, Solaris keeps the last good frame on screen
and renders `on_error` instead of crashing — your app degrades gracefully.

```luma
function View error_view(string message) {
    return Solaris.card([
        Solaris.heading("Something went wrong") |> Solaris.danger(),
        Solaris.text(message) |> Solaris.muted()
    ])
}

function dictionary my_app() {
    return Solaris.app("My App", Model {}, update, view)
        |> Solaris.window(1024, 720)
        |> Solaris.min_size(720, 480)
        |> Solaris.accent("#6C4CF1")
        |> Solaris.font("Inter")
        |> Solaris.color_scheme(Scheme.Auto)
        |> Solaris.persist("my-app-state.json")
        |> Solaris.on_error(error_view)
}
```

A fuller program combining components, effects, a subscription, theming, and
window sizing lives at
[`examples/applications/solaris_gallery.luma`](../examples/applications/solaris_gallery.luma).

---

## 8 — Running and Testing

### Running

`Solaris.app` builds an application configuration; `Solaris.run` opens
the window and starts the MVU loop.

```luma
function dictionary my_app() {
    return Solaris.app("My App", Model { count = 0 }, update, view)
}

@main
function void main() {
    Solaris.run(my_app())
}
```

`Solaris.app(title, model, update, view)` returns a config `dictionary` (with
a sensible default window size); `Solaris.run(config)` runs it. Exposing the
configuration through a named function (here `my_app`) lets your tests drive the
exact same application.

### Headless rendering

Set the environment variable `LUMA_GUI_HEADLESS=1` to render once and exit
without opening a window. This is how continuous integration verifies that a GUI
program builds its initial view successfully.

```text
[gui-headless] My App: initial render OK (640 JSON bytes)
```

### Testing interactions

Because `update` and `view` are pure, most logic is tested by calling `update`
directly. To test the wiring end to end, the engine's headless harness renders
the view and drives a control without opening a window:

- `GraphicalUi.test_click(app, model, label)` clicks a button by its label and
  returns the resulting model.
- `GraphicalUi.test_input(app, model, locator, value)` fires an input control's
  handler (text field, switch, radio, dropdown, slider, tabs, …) with `value`.
  A control given a `Solaris.key("id")` can be addressed by that `id`.
- `GraphicalUi.test_render(app, model)` returns the rendered widget tree, and
  `GraphicalUi.test_count(app, model, locator)` counts matching widgets.

The `test_*` family lives on the low-level
[`GraphicalUi`](Luma_GraphicalUi_Guide.md) module, since it drives the same
reconciler the real window uses.

```luma
@test
function void test_increment_button() {
    Model next = GraphicalUi.test_click(my_app(), Model { count = 0 }, "+")
    assert(next.count == 1)
}

@test
function void test_rename_field() {
    # The text field carries |> Solaris.key("name").
    Model next = GraphicalUi.test_input(my_app(), Model {}, "name", "Ada")
    assert(next.name == "Ada")
}

@test
function void test_update_is_exhaustive() {
    assert(update(Model { count = 3 }, Msg.Increment).count == 4)
    assert(update(Model { count = 3 }, Msg.Reset).count == 0)
}
```

---

## 9 — How Solaris Relates to GraphicalUi

Solaris and [`GraphicalUi`](Luma_GraphicalUi_Guide.md) are two layers of the same
system:

- **Solaris** is the beginner-facing authoring surface: typed `record`
  models, `choice` messages, the immutable `View` tree, fluent `|>` modifiers,
  and semantic design tokens. It ships built in — no `include`.
- **GraphicalUi** is the low-level engine underneath: the platform webview bridge,
  the keyed reconciler, the CSS generator and sanitiser, accessibility, HiDPI,
  and the headless test harness.

When you build a `View` tree, Solaris reconciles it into GraphicalUi widgets,
and GraphicalUi renders them. This mirrors the surface's design principle that
*"the web is an implementation detail"* — you write clean Luma and never touch
the layers beneath. Reach for the raw `GraphicalUi` API only for advanced scenarios
not yet surfaced by Solaris; for everyday applications, Solaris is the one
obvious way.

---

## 10 — Limitations

Solaris surfaces the common, beginner-friendly subset of the engine. A few
things are deliberately out of scope for the surface:

- **Some visual details are approximate.** Modifiers such as `align`, `justify`,
  `rounded`, `height`, `key`, and `label` are honoured, but a handful of design
  tokens render at the engine's nearest built-in value rather than a pixel-exact
  match to the design concept. Layouts stay consistent and accessible; the exact
  spacing may differ by a point or two.
- **`menu` is in-page, not a native menu bar,** and Solaris does not add a system
  tray icon. Desktop notifications are available through the `Solaris.notify`
  command.
- **Interactive controls need their handler.** Attach `on_click` to a `button`,
  `on_change` to a `text_field`, `on_toggle` to a `switch`, and so on. A control
  left without its handler is **inert** — interacting with it does nothing — so a
  forgotten handler is a harmless no-op, never a corrupted model.
- **Advanced components** (routers, drag-and-drop) are not part of the
  Solaris surface yet. Applications needing them can use the raw
  [`GraphicalUi`](Luma_GraphicalUi_Guide.md) module directly.

These are surface-level limits, not engine limits — the underlying `GraphicalUi`
runtime supports far more, and the Solaris surface will grow to expose it.

---

## 11 — See Also

- [Solaris Tutorial](Luma_Solaris_Tutorial.md) — a step-by-step, beginner-first introduction to building GUIs with this surface
- [GraphicalUi Guide](Luma_GraphicalUi_Guide.md) — the low-level webview engine beneath Solaris, and its raw API for advanced scenarios
- [Solaris design concept](Luma_Solaris_Architecture.md) — the design philosophy, architecture, and rationale behind the surface
- [User Manual](Luma_User_Manual.md) — language syntax and semantics, including `record`, `choice`, `match`, and the pipe operator
- [Standard Library Reference — §14 (Solaris and GraphicalUi)](Luma_Standard_Library_Reference.md#14--solaris-and-graphicalui) — concise API listing for the surface and the engine
