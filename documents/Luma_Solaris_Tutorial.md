# Luma — Solaris Tutorial

> A step-by-step introduction to building desktop apps with a graphical user interface, using Luma's beginner-first GUI framework, Solaris. You write only Luma — no HTML, CSS, or JavaScript.

---

## Table of Contents

1. [What a GUI Is, and What Solaris Is](#1--what-a-gui-is-and-what-solaris-is)
2. [How a Solaris App Thinks](#2--how-a-solaris-app-thinks)
3. [Your First Window](#3--your-first-window)
4. [Making It Interactive](#4--making-it-interactive)
5. [A Field Guide to Controls](#5--a-field-guide-to-controls)
6. [Laying Out Your Interface](#6--laying-out-your-interface)
7. [Styling with Design Tokens](#7--styling-with-design-tokens)
8. [Showing Lists of Data](#8--showing-lists-of-data)
9. [Navigation and Overlays](#9--navigation-and-overlays)
10. [Effects and Subscriptions](#10--effects-and-subscriptions)
11. [Configuring and Theming the App](#11--configuring-and-theming-the-app)
12. [Testing Your App](#12--testing-your-app)
13. [Project — A To-Do App](#13--project--a-to-do-app)
14. [Where to Go Next](#14--where-to-go-next)
15. [Glossary](#15--glossary)

---

## 1 — What a GUI Is, and What Solaris Is

So far you may have written programs that print text to a terminal. This tutorial is about a different kind of program: one with a **window** you can click, type into, and look at — a **graphical user interface**, or **GUI** (say it "gooey").

### What Is a GUI?

A GUI is made of **controls** — the visible, interactive pieces on the screen: buttons, text boxes, checkboxes, sliders, lists, and so on. The person using your program (the **user**) does things — clicks a button, types a name, drags a slider — and your program responds by changing what's on screen. A calculator, a music player, and a settings screen are all GUIs.

Writing a GUI raises a question a terminal program never has to answer: *when* does anything happen? There's no top-to-bottom script. Instead the program sits and waits, and things happen in response to the user. This is called **event-driven** programming, and it is the heart of every GUI.

### What Is Solaris?

**Solaris** is Luma's built-in framework for building GUIs. Its promise is that you describe *what the screen should look like for your current data*, and Solaris figures out the rest — drawing the window, tracking clicks and keystrokes, and updating the display. You never write HTML, CSS, or JavaScript; you write plain Luma, and a blank app already looks tidy.

Solaris is **always available**. There is nothing to install and nothing to `include` — the moment your program mentions `Solaris`, the whole framework is there. Under the hood it draws through a hidden web view, but you never touch that layer.

> **Note:** This tutorial focuses on *GUI programming*, not the Luma language itself. You should already be comfortable with Luma basics — variables, functions, `record` types, `choice` types, `match`, and the pipe operator `|>`. If any of those are new, read the [Tutorial](Luma_Tutorial.md) first, then come back. When this tutorial reaches for a language feature, it gives a one-line reminder as it goes.

By the end you will have built a working to-do app: type a task, add it to a list, tick it off, and clear the finished ones. Let's start with the one idea that makes everything else simple.

---

## 2 — How a Solaris App Thinks

Most of learning Solaris is learning a single pattern. Once it clicks, every app you build is a variation on it. The pattern is called **Model–View–Update**, or **MVU**.

### UI = f(state)

Here is the big idea, in one line:

> **Your screen is a function of your data.**

In an old-fashioned GUI you would find a widget and poke it: "set the label's text to 5", "now make the button grey". Bugs creep in because the screen and the data drift apart. Solaris takes that worry away. You give it **one bundle of data** — your app's current state — and **one function** that turns that data into a picture of the screen. Whenever the data changes, Solaris calls your function again and updates the display for you. The screen can never disagree with the data, because the screen *is* the data, drawn.

### The Four Pieces

Every Solaris app is built from exactly four things:

- **Model** — a `record` holding all of your app's state (the count, the typed-in name, whether a box is ticked).
- **Message** — a `choice` type listing every way the state can change (the user clicked *increment*, the user typed a new name).
- **update** — a function that takes the current model and a message and returns the **next** model. This is the *only* place your state changes.
- **view** — a function that takes the model and returns a **View**: a description of what to show.

They connect in a loop:

```text
        ┌──────────────────────────────────────────────┐
        │                                              │
        ▼                                              │
   ┌─────────┐   view(model)    ┌────────┐   user does │ something
   │  Model  │ ───────────────▶ │ Screen │ ────────────┘
   └─────────┘                  └────────┘
        ▲                            │ sends a Message
        │   update(model, message)   │
        └────────────────────────────┘
```

Read it round: the **view** draws the **model**; the user interacts and a **message** is sent; **update** turns the old model plus that message into a new model; the view draws the new model. Solaris spins this loop for you. You only write the Model, the Message, `update`, and `view` — all four are ordinary Luma, and `update` and `view` are **pure** functions (same input, same output, no surprises), which is exactly what makes an app easy to reason about and to test.

Keep this picture in mind. The next sections fill in each piece.

---

## 3 — Your First Window

Let's put something on screen. This first app has no interaction yet — it just greets you — so we can focus on the skeleton every Solaris program shares.

Create a file called `greeting.luma`:

```luma
record Model {
    string who = "world"
}

function Model update(Model model, any _msg) {
    return model
}

function View view(Model model) {
    return Solaris.column([
        Solaris.heading("Hello, ${model.who}!"),
        Solaris.text("This is my first Solaris window.")
    ]) |> Solaris.padding(Spacing.L) |> Solaris.gap(Spacing.M)
}

@main
function void main() {
    Solaris.run(Solaris.app("Greeting", Model { who = "world" }, update, view))
}
```

Run it exactly like any Luma program:

```bash
luma greeting.luma
```

A window titled **Greeting** opens, showing a bold heading and a line of text. Let's walk through it.

- **`record Model`** holds the state. Here that's a single string, `who`, with a default of `"world"`.
- **`update`** must exist even though nothing changes yet. It takes the model and a message and returns the next model; with no interactions, it just hands the model straight back. The parameter is written `any _msg` — `any` because we don't have a message type yet, and the leading underscore tells Luma "I'm deliberately not using this", which silences the unused-parameter warning.
- **`view`** returns a `View`. `Solaris.column([...])` stacks its children vertically. Inside it are a `heading` and a line of `text`. `"${model.who}"` is ordinary Luma string interpolation, so the heading reads *Hello, world!*.
- The `|>` pipes add **modifiers**: `Solaris.padding(Spacing.L)` gives the column comfortable inner spacing, and `Solaris.gap(Spacing.M)` puts a medium gap between its children. (More on modifiers and those `Spacing` values soon.)
- **`main`** builds the app with `Solaris.app(title, model, update, view)` and starts it with `Solaris.run(...)`. Every Solaris program ends this way.

### Running Without a Window

Sometimes you want to check that an app *builds* its screen correctly without actually opening a window — for example on a server, or in an automated test. Set the environment variable `LUMA_GUI_HEADLESS` to `1` and Solaris renders once and exits:

```text
[gui-headless] Greeting: initial render OK (260 JSON bytes)
```

That line means your `view` produced a valid screen. We'll lean on this mode later when testing.

> **Try it:** Add a second field to the model, `string mood = "cheerful"`, and a third child to the column: `Solaris.text("Feeling ${model.mood} today.")`. Run it and watch both lines appear. Notice you changed *data and view* — never a widget directly.

---

## 4 — Making It Interactive

A greeting is nice, but a GUI earns its keep when the user can *do* something. Let's build the classic first interactive app: a counter with **+** and **−** buttons. This is the whole MVU loop in miniature.

Create `counter.luma`:

```luma
choice Msg {
    Increment,
    Decrement,
    Reset
}

record Model {
    integer count = 0
}

function Model update(Model model, Msg msg) {
    return match msg {
        case Msg.Increment { model with { count = model.count + 1 } }
        case Msg.Decrement { model with { count = model.count - 1 } }
        case Msg.Reset     { model with { count = 0 } }
    }
}

function View view(Model model) {
    return Solaris.column([
        Solaris.heading("Counter"),
        Solaris.text("${model.count}") |> Solaris.size(TextScale.Title) |> Solaris.center(),
        Solaris.row([
            Solaris.button("−") |> Solaris.on_click(Msg.Decrement) |> Solaris.muted(),
            Solaris.button("+") |> Solaris.on_click(Msg.Increment) |> Solaris.primary()
        ]) |> Solaris.gap(Spacing.M),
        Solaris.button("Reset") |> Solaris.on_click(Msg.Reset) |> Solaris.danger()
    ]) |> Solaris.padding(Spacing.L) |> Solaris.gap(Spacing.M)
}

@main
function void main() {
    Solaris.run(Solaris.app("Counter", Model { count = 0 }, update, view))
}
```

Run it, click the buttons, and the number changes. Here is how each piece of the loop shows up.

### The Message Lists Every Change

```luma
choice Msg {
    Increment,
    Decrement,
    Reset
}
```

A `Msg` is a `choice` — a type whose value is exactly one of the listed variants. This is your app's complete vocabulary of "things that can happen". Naming them up front keeps the logic honest: if a button can do it, it's in this list.

### Buttons Send Messages

```luma
Solaris.button("+") |> Solaris.on_click(Msg.Increment)
```

`Solaris.button("+")` makes a button labelled `+`. On its own a button does nothing — you must **wire** it with a handler. `Solaris.on_click(Msg.Increment)` says "when this button is clicked, send the `Msg.Increment` message". A button with no `on_click` is simply inert (a harmless no-op), never a crash.

### update Handles Every Message

```luma
function Model update(Model model, Msg msg) {
    return match msg {
        case Msg.Increment { model with { count = model.count + 1 } }
        case Msg.Decrement { model with { count = model.count - 1 } }
        case Msg.Reset     { model with { count = 0 } }
    }
}
```

`update` is where state changes — and the *only* place it does. It uses `match` to handle each message. `model with { count = ... }` is Luma's record-update expression: it produces a **new** model that is a copy of the old one with `count` changed. (Luma values are immutable by default, so you never edit the model in place; you describe the next one and return it.)

Because the `match` is **exhaustive** — it has no `else` — Luma refuses to compile if you forget a message. Add a `Msg.Double` variant and the compiler immediately tells you `update` doesn't handle it. Your logic can't silently fall out of date.

### view Draws the Current Model

The `view` reads `model.count` and shows it. When `update` returns a new model, Solaris calls `view` again and redraws. You never tell the label to change — you change the data, and the label follows.

That's the entire loop. Everything else in this tutorial is *more controls*, *nicer layout*, and *better styling* — but the four pieces never change.

> **Try it:** Add a `Msg.Double` variant, handle it in `update` with `model with { count = model.count * 2 }`, and add a `Solaris.button("×2") |> Solaris.on_click(Msg.Double)`. Before you add the `update` branch, try compiling — the exhaustive `match` will flag the missing case. That safety net is the point.

---

## 5 — A Field Guide to Controls

Solaris comes with a generous set of ready-made controls. Every one is a function in the `Solaris` namespace that returns a `View`. This section is a catalogue you can skim now and return to later. The interactive ones each take a **handler** modifier that turns user actions into messages.

### Handlers: Turning Actions into Messages

You've met `on_click`, which takes a message directly. The other handlers take a **function** — a small `lambda` — that receives the new value and returns a message. For example, a text field reports the text the user typed:

```luma
Solaris.text_field(model.name)
    |> Solaris.on_change((string value) -> Msg.SetName(value))
```

The lambda `(string value) -> Msg.SetName(value)` means "given the new text, produce a `SetName` message carrying it". For this to work, your `Msg` needs a **data-carrying** variant:

```luma
choice Msg {
    SetName(string value)
}
```

and `update` unpacks the value:

```luma
case Msg.SetName(value) { model with { name = value } }
```

That trio — a data-carrying message, a handler lambda, and a `match` branch that unpacks it — is the pattern behind *every* input control. Here are the handlers and the value each one delivers:

| Handler | Used on | Lambda shape |
|---|---|---|
| `Solaris.on_click(msg)` | Button | (no lambda — takes a message directly) |
| `Solaris.on_change(fn)` | Text field, text area, date picker | `(string) -> Msg` |
| `Solaris.on_toggle(fn)` | Checkbox, switch | `(boolean) -> Msg` |
| `Solaris.on_select(fn)` | Radio, dropdown, menu | `(string) -> Msg` |
| `Solaris.on_slide(fn)` | Slider | `(number) -> Msg` |
| `Solaris.on_tab(fn)` | Tabs | `(integer) -> Msg` |
| `Solaris.on_close(msg)` | Dialog | (takes a message directly) |

### Text and Typography

| Control | Signature | What it shows |
|---|---|---|
| `Solaris.text(content)` | `text(string)` | A run of body text. |
| `Solaris.heading(content)` | `heading(string)` | A prominent title. |
| `Solaris.badge(text)` | `badge(string)` | A small status pill; colour it with `emphasis`. |
| `Solaris.icon(name)` | `icon(string)` | A named glyph (e.g. `"bell"`, `"check"`); size it with `icon_size`. |
| `Solaris.spinner(label)` | `spinner(string)` | A "busy" indicator. |
| `Solaris.divider()` | `divider()` | A horizontal rule between sections. |

### Inputs

```luma
# A button sends a message when clicked.
Solaris.button("Save") |> Solaris.on_click(Msg.Save)

# A single-line text box reports what the user typed.
Solaris.text_field(model.name)
    |> Solaris.placeholder("Your name")
    |> Solaris.on_change((string v) -> Msg.SetName(v))

# A checkbox is a boolean toggle; show its state with `checked`.
Solaris.checkbox("Subscribed")
    |> Solaris.checked(model.subscribed)
    |> Solaris.on_toggle((boolean b) -> Msg.SetSubscribed(b))

# A switch carries its own on/off state as a second argument.
Solaris.switch("Notifications", model.notify)
    |> Solaris.on_toggle((boolean b) -> Msg.SetNotify(b))

# Radio and dropdown pick one option from a list.
Solaris.radio(["Light", "Dark", "Auto"], model.theme)
    |> Solaris.on_select((string v) -> Msg.PickTheme(v))
Solaris.dropdown(["English", "Spanish"], model.language)
    |> Solaris.on_select((string v) -> Msg.PickLanguage(v))

# A slider is a numeric range.
Solaris.slider(model.volume, 0.0, 100.0)
    |> Solaris.on_slide((number v) -> Msg.SetVolume(v))

# A date picker takes and reports a "YYYY-MM-DD" string.
Solaris.date_picker(model.birthday)
    |> Solaris.on_change((string v) -> Msg.SetBirthday(v))
```

Here is the full input roster:

| Control | Signature |
|---|---|
| Button | `Solaris.button(string label)` |
| Text field | `Solaris.text_field(string value)` |
| Text area | `Solaris.text_area(string value)` (multi-line) |
| Checkbox | `Solaris.checkbox(string label)` |
| Switch | `Solaris.switch(string label, boolean state)` |
| Radio | `Solaris.radio(array<string> options, string chosen)` |
| Dropdown | `Solaris.dropdown(array<string> options, string chosen)` |
| Slider | `Solaris.slider(number value, number min, number max)` |
| Date picker | `Solaris.date_picker(string value)` |

### Data Display

| Control | Signature | What it shows |
|---|---|---|
| `Solaris.table(headers, rows)` | `table(array<string>, array<array<string>>)` | A read-only grid. |
| `Solaris.progress(value, max)` | `progress(number, number)` | A determinate progress bar. |
| `Solaris.image(source)` | `image(string)` | A picture from a path or URL. |

A table takes a row of headers and a list of string rows:

```luma
Solaris.table(
    ["Field", "Value"],
    [
        ["Name", model.name],
        ["Theme", model.theme]
    ])
```

> **Try it:** Extend your counter from §4. Add `string name = ""` to the model, a `SetName(string value)` message, and a `Solaris.text_field(model.name) |> Solaris.placeholder("Your name") |> Solaris.on_change((string v) -> Msg.SetName(v))`. Add a `Solaris.text("Hi, ${model.name}!")` above it. Type in the box and watch the greeting update as you type.

---

## 6 — Laying Out Your Interface

Controls need arranging. Solaris gives you **containers** that hold other views, and **modifiers** that tune spacing, size, and alignment. You build a screen by nesting containers — a column of rows of cards, and so on.

### Containers

| Container | Signature | Arranges children… |
|---|---|---|
| `Solaris.column(children)` | `column(array<View>)` | vertically (top to bottom). |
| `Solaris.row(children)` | `row(array<View>)` | horizontally (left to right). |
| `Solaris.grid(columns, children)` | `grid(integer, array<View>)` | in a fixed number of columns. |
| `Solaris.card(children)` | `card(array<View>)` | grouped in a padded, raised box. |
| `Solaris.panel(title, children)` | `panel(string, array<View>)` | in a titled, bordered box. |
| `Solaris.list(items)` | `list(array<View>)` | as a vertical list. |
| `Solaris.scroll(children)` | `scroll(array<View>)` | in a scrollable region. |
| `Solaris.z_stack(children)` | `z_stack(array<View>)` | layered on top of each other. |
| `Solaris.spacer()` | `spacer()` | flexible empty space that pushes siblings apart. |

`column` and `row` are the workhorses. Everything else is a convenience: a `card` is a column with a nice box around it; a `grid` wraps children into a set number of columns.

```luma
Solaris.column([
    Solaris.heading("Profile"),
    Solaris.row([
        Solaris.text("Name:"),
        Solaris.text(model.name)
    ]) |> Solaris.gap(Spacing.S),
    Solaris.card([
        Solaris.text("Bio"),
        Solaris.text(model.bio) |> Solaris.muted()
    ]) |> Solaris.padding(Spacing.M)
]) |> Solaris.gap(Spacing.M)
```

### Spacing: gap and padding

Two modifiers handle almost all spacing:

- **`Solaris.gap(Spacing s)`** — the space *between* a container's children.
- **`Solaris.padding(Spacing s)`** — the space *inside* a container, around its children.

Both take a `Spacing` token — a semantic size rather than a raw pixel count: `Spacing.None`, `Spacing.XS`, `Spacing.S`, `Spacing.M`, `Spacing.L`, `Spacing.XL`. Using tokens (instead of "8px here, 12px there") keeps every screen consistent.

### Alignment: align and justify

Inside a row or column, two modifiers position the children:

- **`Solaris.align(Align a)`** — the **cross-axis** alignment. In a row (which runs left-to-right) this is *vertical*: `Align.Start`, `Align.Center`, `Align.End`, `Align.Stretch`.
- **`Solaris.justify(Justify j)`** — the **main-axis** distribution: `Justify.Start`, `Justify.Center`, `Justify.End`, `Justify.SpaceBetween`, `Justify.SpaceAround`.

A common trick is a header bar with a title on the left and a button on the right — push them apart with `SpaceBetween`, or with a `spacer`:

```luma
Solaris.row([
    Solaris.heading("Inbox"),
    Solaris.spacer(),
    Solaris.button("Compose") |> Solaris.primary()
]) |> Solaris.align(Align.Center)
```

The `spacer` soaks up all the free space between the heading and the button, shoving them to opposite ends. `align(Align.Center)` lines them up on the shared centre line.

### Size: width, height, and Length

By default a control is only as big as its content. To change that, use `Solaris.width(Length len)` and `Solaris.height(Length len)` with a `Length` token:

- **`Length.Shrink`** — hug the content (the default).
- **`Length.Fill`** — expand to fill the available space.
- **`Length.Fixed(n)`** — an exact size, e.g. `Length.Fixed(140)`.
- **`Length.FillPortion(n)`** — split free space in proportion to a weight. Two children at `FillPortion(1)` and `FillPortion(3)` share a row one quarter to three quarters.

```luma
Solaris.row([
    Solaris.slider(model.volume, 0.0, 100.0)
        |> Solaris.on_slide((number v) -> Msg.SetVolume(v))
        |> Solaris.width(Length.FillPortion(3)),
    Solaris.text("${model.volume}%") |> Solaris.width(Length.FillPortion(1))
]) |> Solaris.align(Align.Center) |> Solaris.gap(Spacing.M)
```

> **Try it:** Build a three-column `Solaris.grid(3, [...])` of six `Solaris.card([...])` children, each holding a `Solaris.text`. Give the grid `|> Solaris.gap(Spacing.M)`. Resize the window and watch the cards keep their columns. Then swap the grid for a `Solaris.row` and see the difference.

---

## 7 — Styling with Design Tokens

Solaris apps look consistent because you style them with **semantic tokens**, not raw colours and pixel sizes. Each token is a Luma `choice`, so a typo is a compile error and your editor can autocomplete the options. The defaults are chosen for good contrast and spacing, in both light and dark mode.

Because the tokens are global, you write them unqualified — `TextScale.Title`, `Spacing.L` — never `Solaris.TextScale`.

### Text Size and Weight

- **`Solaris.size(TextScale scale)`** — the text scale: `TextScale.Caption`, `TextScale.Body`, `TextScale.Large`, `TextScale.Heading`, `TextScale.Title`.
- **`Solaris.bold()`** — shorthand for bold text (or `Solaris.weight(Weight.Bold)`).
- **`Solaris.level(integer n)`** — a heading level from 1 to 6.

```luma
Solaris.text("BIG") |> Solaris.size(TextScale.Title) |> Solaris.bold()
Solaris.text("a quiet footnote") |> Solaris.size(TextScale.Caption) |> Solaris.muted()
```

### Emphasis: Semantic Colour

Rather than pick a colour, you pick a **meaning**, and Solaris maps it to the right colour in the current theme:

| Modifier | Meaning |
|---|---|
| `Solaris.primary()` | The main call to action. |
| `Solaris.secondary()` | A secondary action. |
| `Solaris.danger()` | A destructive action (delete, reset). |
| `Solaris.muted()` | De-emphasised, subtle. |
| `Solaris.emphasis(Emphasis e)` | Any emphasis token explicitly. |

The full `Emphasis` set (used with `emphasis`, and ideal for `badge`) is `Normal`, `Primary`, `Secondary`, `Success`, `Warning`, `Danger`, `Muted`:

```luma
Solaris.row([
    Solaris.badge("OK") |> Solaris.emphasis(Emphasis.Success),
    Solaris.badge("Careful") |> Solaris.emphasis(Emphasis.Warning),
    Solaris.badge("Stop") |> Solaris.emphasis(Emphasis.Danger)
]) |> Solaris.gap(Spacing.S)
```

### Shape

**`Solaris.rounded(Radius r)`** rounds a container's corners: `Radius.None`, `Radius.Small`, `Radius.Medium`, `Radius.Large`, `Radius.Full` (a pill or circle).

```luma
Solaris.card([ Solaris.text("Nicely rounded") ])
    |> Solaris.rounded(Radius.Large)
    |> Solaris.padding(Spacing.L)
```

### One Obvious Modifier per Concern

Notice the shape of all of this: there is exactly one obvious modifier for each visual concern — one for size, one for emphasis, one for gaps, one for rounding — and they chain with `|>`. You compose a look by piping a handful of tokens onto a control, and because they're types, you can't pass a nonsensical value.

> **Note:** A few tokens render at the engine's nearest built-in value rather than a pixel-exact match, so spacing may differ by a point or two. Layouts stay consistent and accessible — you just don't get pixel-level control, which is exactly the trade that keeps Solaris apps tidy by default.

> **Try it:** Take any control and pipe several tokens onto it at once, e.g. `Solaris.text("Hello") |> Solaris.size(TextScale.Heading) |> Solaris.bold() |> Solaris.primary()`. Change one token at a time and re-run to see each effect in isolation.

---

## 8 — Showing Lists of Data

Real apps show *collections*: a list of messages, a table of rows, a grid of photos. The data lives in your model as an `array`, and your `view` turns each element into a `View`.

Containers such as `Solaris.column` and `Solaris.list` want a plain `array<View>`. The simplest way to turn a list of data into a list of views is a small **helper function with a `for` loop** that builds the array:

```luma
record Model {
    array<string> items = []
}

function array<View> item_rows(array<string> items) {
    mutable array<View> rows = []
    for it in items {
        rows = Array.append(rows, Solaris.text("• ${it}"))
    }
    return rows
}

function View view(Model model) {
    return Solaris.column([
        Solaris.heading("Shopping"),
        Solaris.list(item_rows(model.items))
    ]) |> Solaris.padding(Spacing.L)
}
```

The helper starts with an empty `mutable` array, appends one `View` per item, and returns the finished `array<View>`, which drops straight into `Solaris.list(...)`. Add or remove items in the model and the list redraws itself — you never touch the screen.

### Rows That Know Their Place

Often each row needs to send a message about *itself* — "toggle item 2", "delete item 5". Track the position with a counter as you build the rows, and put it in the message:

```luma
record Task {
    string title = "",
    boolean done = false
}

choice Msg {
    Toggle(integer index)
}

function array<View> task_rows(array<Task> tasks) {
    mutable array<View> rows = []
    mutable integer i = 0
    for t in tasks {
        rows = Array.append(rows,
            Solaris.checkbox(t.title)
                |> Solaris.checked(t.done)
                |> Solaris.on_toggle((boolean _b) -> Msg.Toggle(i)))
        i = i + 1
    }
    return rows
}
```

Each checkbox captures its own `i`, so ticking the third box sends `Msg.Toggle(2)`. In `update`, rebuild the array with just that item changed:

```luma
function array<Task> toggle_at(array<Task> tasks, integer index) {
    mutable array<Task> next = []
    mutable integer i = 0
    for t in tasks {
        if i == index {
            next = Array.append(next, t with { done = !t.done })
        } else {
            next = Array.append(next, t)
        }
        i = i + 1
    }
    return next
}
```

This "walk the array, copy every element, change the one that matches" shape is the standard way to update one item immutably. You'll use it in the capstone.

> **Note:** You might expect to use `Array.map` to turn data into views. `Array.map` returns a `result` (it can fail if the callback throws), and Solaris containers want a plain `array<View>`, so the small `for`-loop helper shown here is the direct, beginner-friendly path.

> **Try it:** Give the shopping model `["Milk", "Eggs", "Bread"]` and render it. Then change `item_rows` to show the position too: `Solaris.text("${i + 1}. ${it}")` with a counter, just like `task_rows`. Numbered lists, for free.

---

## 9 — Navigation and Overlays

As an app grows past one screen you need ways to switch views and to pop things up over the top. Solaris has ready-made pieces for both.

### Tabs

`Solaris.tabs(labels, active, panels)` shows a row of tab labels and the panel matching the active index. Wire `on_tab` to remember which is selected:

```luma
Solaris.tabs(["One", "Two"], model.tab, [
    Solaris.text("First panel"),
    Solaris.text("Second panel")
]) |> Solaris.on_tab((integer i) -> Msg.SelectTab(i))
```

### Sidebar and App Shell

For a classic desktop layout — a fixed navigation rail beside the main content — pair `Solaris.sidebar([...])` with `Solaris.app_shell(side, content)`:

```luma
Solaris.app_shell(
    Solaris.sidebar([
        Solaris.heading("Nav") |> Solaris.size(TextScale.Title),
        Solaris.divider(),
        Solaris.menu("Actions", ["Refresh", "Export"])
            |> Solaris.on_select((string v) -> Msg.Pick(v))
    ]),
    Solaris.scroll([
        Solaris.heading("Main content")
    ]) |> Solaris.padding(Spacing.L))
```

`Solaris.menu(label, items)` is an in-page dropdown menu (not a native OS menu bar); it reports the chosen item through `on_select`.

### Dialogs and Toasts

A **dialog** is a modal box shown *when a flag in your model is true*. That flag is ordinary state, so opening and closing it is just two messages:

```luma
Solaris.dialog("Details", model.show, [
    Solaris.text("Picked: ${model.picked}"),
    Solaris.button("Close") |> Solaris.on_click(Msg.Close)
]) |> Solaris.on_close(Msg.Close)
```

When `model.show` is `true` the dialog appears; `on_close` fires when the user dismisses it.

A **toast** is a brief banner. Show one only when it's wanted by choosing the view with `match`. When you don't want it, return a `Solaris.spacer()` (an empty placeholder):

```luma
function View banner(Model model) {
    return match model.saved {
        case true { Solaris.toast("Saved!") }
        case false { Solaris.spacer() }
    }
}
```

This is a key Solaris habit: **a `view` is just a function, so you can use `if`/`match`, call helpers, and choose different views for different states** — all with plain Luma. There is no special template language.

> **Try it:** Add a `boolean show = false` to a model, an `Open` and a `Close` message, and a button that sends `Open`. Render a `Solaris.dialog(...)` bound to `model.show`. Click to open, close it, and confirm the flag drives everything.

---

## 10 — Effects and Subscriptions

Pure `update` and `view` cover most of an app. But sometimes you need to reach *outside* your program: show a desktop notification, wait a second and then act, fetch data from the web, or run a clock. Solaris keeps `update` pure by treating these as **data** — **commands** you return and **subscriptions** you declare — which it then performs for you.

### Commands: Effects Returned from update

Normally `update` returns the next model. To also perform an effect, return a **pair** of `(model, command)` built with `Solaris.with_command`:

```luma
choice Msg { Save, Ping, Pong }
record Model { boolean saved = false, integer pings = 0 }

function any update(Model model, Msg msg) {
    return match msg {
        case Msg.Save {
            Solaris.with_command(
                model with { saved = true },
                Solaris.notify("Saved", "Your work is safe."))
        }
        case Msg.Ping {
            Solaris.with_command(
                model with { pings = model.pings + 1 },
                Solaris.after(1000, Msg.Pong))
        }
        case Msg.Pong { model with { pings = model.pings + 1 } }
    }
}
```

Two things to notice. First, the return type is now `any`, not `Model`: some branches return a plain model and some return a `(model, command)` pair, and `any` covers both. Solaris inspects the value and unwraps the pair for you. Second, the *plain* branches (like `Pong`) still just return a model — you only wrap the branches that need an effect.

The commands you can return:

| Command | Effect |
|---|---|
| `Solaris.no_command()` | Do nothing (an explicit "no effect"). |
| `Solaris.with_command(model, command)` | Return the next model together with a command. |
| `Solaris.batch(commands)` | Run several commands at once. |
| `Solaris.after(ms, msg)` | Send `msg` once, after a delay in milliseconds. |
| `Solaris.fetch(url, fn)` | HTTP GET `url`; `fn(result) -> Msg` handles the reply. |
| `Solaris.notify(title, body)` | Show an OS desktop notification. |
| `Solaris.set_scheme(scheme)` | Switch light/dark/auto at runtime. |

`Solaris.after(1000, Msg.Pong)` above sends `Pong` one second later — a one-shot timer, entirely in the MVU loop.

### Subscriptions: Effects from the Outside World

A **subscription** is a *standing* source of messages — a repeating timer, or a global keyboard shortcut. You provide a function `fn(model) -> array<any>` and hand it to `Solaris.subscribe`; Solaris keeps the listed subscriptions running:

```luma
choice Msg { Tick, Reset }
record Model { integer seconds = 0 }

function Model update(Model model, Msg msg) {
    return match msg {
        case Msg.Tick { model with { seconds = model.seconds + 1 } }
        case Msg.Reset { model with { seconds = 0 } }
    }
}

function View view(Model model) {
    return Solaris.column([
        Solaris.heading("${model.seconds}s") |> Solaris.size(TextScale.Title),
        Solaris.text("Press R to reset.") |> Solaris.muted()
    ]) |> Solaris.padding(Spacing.L)
}

function array<any> subscriptions(Model _model) {
    return [
        Solaris.every("clock", 1000, Msg.Tick),
        Solaris.on_key_press("reset", "r", Msg.Reset)
    ]
}

@main
function void main() {
    Solaris.run(
        Solaris.app("Clock", Model {}, update, view)
            |> Solaris.subscribe(subscriptions))
}
```

| Subscription | Effect |
|---|---|
| `Solaris.every(id, ms, msg)` | Send `msg` on a repeating timer. |
| `Solaris.on_key_press(id, key, msg)` | Send `msg` on a global key press. |

The `id` (a string) keeps a subscription stable across redraws, so the timer isn't restarted every frame.

### Running a Command at Startup

`Solaris.on_start(command)` runs a command once, when the app launches — perfect for a greeting or loading initial data:

```luma
Solaris.app("Timer", Model {}, update, view)
    |> Solaris.subscribe(subscriptions)
    |> Solaris.on_start(Solaris.notify("Welcome", "The timer is running."))
```

> **Try it:** Take the clock above and add a `Solaris.on_start(Solaris.notify("Hello", "Clock started"))` to the app. Run it and you'll get a desktop notification once, at launch, while the clock keeps ticking via its subscription.

---

## 11 — Configuring and Theming the App

`Solaris.app(...)` returns a **config**, and a family of modifiers refine it — window size, theme, persistence, error handling — each chaining with `|>`. A clean habit is to build the config in its own named function (we'll see in §12 that this also makes testing easy):

```luma
function View error_view(string message) {
    return Solaris.card([
        Solaris.heading("Something went wrong") |> Solaris.danger(),
        Solaris.text(message) |> Solaris.muted()
    ])
}

function dictionary my_app() {
    return Solaris.app("My App", Model {}, update, view)
        |> Solaris.window(900, 600)
        |> Solaris.min_size(600, 400)
        |> Solaris.accent("#6C4CF1")
        |> Solaris.font("Inter")
        |> Solaris.color_scheme(Scheme.Auto)
        |> Solaris.on_error(error_view)
}

@main
function void main() {
    Solaris.run(my_app())
}
```

### Windows

| Modifier | Effect |
|---|---|
| `Solaris.window(width, height)` | The initial window size (default about 960×640). |
| `Solaris.min_size(width, height)` | The smallest the window may be resized to. |
| `Solaris.max_size(width, height)` | The largest the window may be resized to. |
| `Solaris.resizable(allowed)` | Whether the user may resize the window. |
| `Solaris.fullscreen()` | Start the app fullscreen. |
| `Solaris.devtools()` | Open the web inspector (handy while debugging). |

### Theme and Dark Mode

| Modifier | Effect |
|---|---|
| `Solaris.accent(color)` | The accent colour, a CSS colour string like `"#6C4CF1"`. |
| `Solaris.font(family)` | The UI font family. |
| `Solaris.color_scheme(Scheme)` | Pin `Scheme.Light` / `Scheme.Dark`, or follow the OS with `Scheme.Auto`. |

Dark mode is free: with `Scheme.Auto` the app follows the operating system, and every control already has accessible light and dark colours. You only pin a scheme if you want to override the OS.

### Persistence and Graceful Failure

- **`Solaris.persist(path)`** saves the model to a file and restores it on the next launch, so the app remembers its state between runs.
- **`Solaris.on_error(fn)`** supplies a fallback view. If `view` or `update` ever throws, Solaris keeps the last good frame on screen and renders `fn(message)` instead of crashing — your app degrades gracefully.

> **Try it:** Add `|> Solaris.persist("my-app-state.json")` to your counter's app config, run it, click a few times, close it, and run it again. The count is right where you left it. Delete the JSON file to reset.

---

## 12 — Testing Your App

Because `update` and `view` are pure, a Solaris app is unusually easy to test — and you can do it without ever opening a window. Luma's own `@test` blocks (run with `luma --test your_app.luma`) are all you need.

### A Counter, Ready to Test

The examples below build on the counter from §4, restructured slightly so tests can drive it. Three small changes: rename `update` and `view` to `counter_update` and `counter_view` (any names work — the tests just call them by name), expose the config from a `counter_app()` function so every test launches the identical app, and — to demonstrate input testing — add a `string name` field with a `SetName` message and a text field tagged `key("name")`.

```luma
choice Msg {
    Increment,
    Decrement,
    Reset,
    SetName(string value)
}

record Model {
    integer count = 0,
    string name = ""
}

function Model counter_update(Model model, Msg msg) {
    return match msg {
        case Msg.Increment      { model with { count = model.count + 1 } }
        case Msg.Decrement      { model with { count = model.count - 1 } }
        case Msg.Reset          { model with { count = 0 } }
        case Msg.SetName(value) { model with { name = value } }
    }
}

function View counter_view(Model model) {
    return Solaris.column([
        Solaris.heading("Counter"),
        Solaris.text("${model.count}") |> Solaris.size(TextScale.Title) |> Solaris.center(),
        Solaris.text_field(model.name)
            |> Solaris.placeholder("Your name")
            |> Solaris.on_change((string v) -> Msg.SetName(v))
            |> Solaris.key("name"),
        Solaris.row([
            Solaris.button("−") |> Solaris.on_click(Msg.Decrement) |> Solaris.muted(),
            Solaris.button("+") |> Solaris.on_click(Msg.Increment) |> Solaris.primary()
        ]) |> Solaris.gap(Spacing.M),
        Solaris.button("Reset") |> Solaris.on_click(Msg.Reset) |> Solaris.danger()
    ]) |> Solaris.padding(Spacing.L) |> Solaris.gap(Spacing.M)
}

function dictionary counter_app() {
    return Solaris.app("Counter", Model { count = 0 }, counter_update, counter_view)
}

@main
function void main() {
    Solaris.run(counter_app())
}
```

### Test the Logic Directly

Most of your app's behaviour lives in `update`, which is just a function from `(model, message)` to `model`. Call it and check the result:

```luma
@test
function void test_increment_adds_one() {
    Model next = counter_update(Model { count = 41 }, Msg.Increment)
    assert(next.count == 42)
}

@test
function void test_reset_returns_to_zero() {
    Model next = counter_update(Model { count = 99 }, Msg.Reset)
    assert(next.count == 0)
}
```

No window, no clicks — just data in, data out. This is the biggest practical payoff of the MVU pattern.

### Test the Wiring Headlessly

To check that a control is actually wired to the right message, Solaris's engine provides a **headless harness** that renders the view and drives a control without a window. It lives on the low-level `GraphicalUi` module (the layer beneath Solaris). Because `counter_app()` wraps the config in a named function, every test can launch the exact same app:

```luma
@test
function void test_plus_button_is_wired() {
    Model next = GraphicalUi.test_click(counter_app(), Model { count = 0 }, "+")
    assert(next.count == 1)
}
```

- **`GraphicalUi.test_click(app, model, label)`** clicks the button with that label and returns the resulting model.
- **`GraphicalUi.test_input(app, model, locator, value)`** fires an input control's handler (text field, switch, radio, dropdown, slider, tabs, …) with `value`.
- **`GraphicalUi.test_render(app, model)`** returns the rendered tree, and **`GraphicalUi.test_count(app, model, locator)`** counts matching widgets.

To address a specific control by name, tag it with **`Solaris.key("id")`** in the view — as `counter_view` above does for its text field — and pass that id as the locator:

```luma
@test
function void test_typing_sets_name() {
    Model next = GraphicalUi.test_input(counter_app(), Model {}, "name", "Ada")
    assert(next.name == "Ada")
}
```

`key` does double duty: it lets tests find a control, and it gives Solaris a stable identity so focus, caret, and scroll position stay put when a list re-orders.

> **Try it:** Put the restructured counter and the four `@test` blocks above into one `counter.luma` and run `luma --test counter.luma` — all four pass. Then break the wiring on purpose — point the `+` button at `Msg.Decrement` — and watch `test_plus_button_is_wired` fail. That's your safety net working.

---

## 13 — Project — A To-Do App

Time to put it all together. We'll build a small to-do app that ties in nearly everything from this tutorial: a text field wired to the model, a button that adds items, a dynamically rendered list with a per-item toggle, immutable updates, semantic styling, a live count, and a full set of tests.

Create `todo.luma`:

```luma
# A small to-do app built with Solaris.

choice Msg {
    SetDraft(string value),
    Add,
    Toggle(integer index),
    ClearDone
}

record Task {
    string title = "",
    boolean done = false
}

record Model {
    array<Task> tasks = [],
    string draft = ""
}

# ── Helpers: pure array transformations used by update and view. ──

function integer remaining(array<Task> tasks) {
    mutable integer count = 0
    for t in tasks {
        if !t.done {
            count = count + 1
        }
    }
    return count
}

function array<Task> toggle_at(array<Task> tasks, integer index) {
    mutable array<Task> next = []
    mutable integer i = 0
    for t in tasks {
        if i == index {
            next = Array.append(next, t with { done = !t.done })
        } else {
            next = Array.append(next, t)
        }
        i = i + 1
    }
    return next
}

function array<Task> without_done(array<Task> tasks) {
    mutable array<Task> next = []
    for t in tasks {
        if !t.done {
            next = Array.append(next, t)
        }
    }
    return next
}

# ── Update: the only place state changes. ──

function Model todo_update(Model model, Msg msg) {
    return match msg {
        case Msg.SetDraft(value) { model with { draft = value } }
        case Msg.Add {
            match model.draft == "" {
                case true { model }
                case false {
                    model with {
                        tasks = Array.append(model.tasks, Task { title = model.draft, done = false }),
                        draft = ""
                    }
                }
            }
        }
        case Msg.Toggle(index) { model with { tasks = toggle_at(model.tasks, index) } }
        case Msg.ClearDone { model with { tasks = without_done(model.tasks) } }
    }
}

# ── View: build the task rows, then the whole screen. ──

function array<View> task_rows(array<Task> tasks) {
    mutable array<View> rows = []
    mutable integer i = 0
    for t in tasks {
        rows = Array.append(rows,
            Solaris.checkbox(t.title)
                |> Solaris.checked(t.done)
                |> Solaris.on_toggle((boolean _b) -> Msg.Toggle(i)))
        i = i + 1
    }
    return rows
}

function View todo_view(Model model) {
    return Solaris.column([
        Solaris.heading("To-Do"),
        Solaris.row([
            Solaris.text_field(model.draft)
                |> Solaris.placeholder("What needs doing?")
                |> Solaris.on_change((string v) -> Msg.SetDraft(v))
                |> Solaris.key("draft")
                |> Solaris.width(Length.Fill),
            Solaris.button("Add") |> Solaris.primary() |> Solaris.on_click(Msg.Add)
        ]) |> Solaris.gap(Spacing.S) |> Solaris.align(Align.Center),
        Solaris.column(task_rows(model.tasks)) |> Solaris.gap(Spacing.S),
        Solaris.divider(),
        Solaris.row([
            Solaris.text("${remaining(model.tasks)} remaining") |> Solaris.muted(),
            Solaris.spacer(),
            Solaris.button("Clear completed") |> Solaris.muted() |> Solaris.on_click(Msg.ClearDone)
        ]) |> Solaris.align(Align.Center)
    ]) |> Solaris.padding(Spacing.L) |> Solaris.gap(Spacing.M)
}

# ── The app, exposed as a named function so tests reuse it. ──

function dictionary todo_app() {
    return Solaris.app("Luma To-Do", Model {}, todo_update, todo_view)
        |> Solaris.window(480, 640)
}

@main
function void main() {
    Solaris.run(todo_app())
}

# ══════════════════════════════════════════════════════════════════════
# TESTS — the logic is pure, so most tests just call `todo_update`.
# ══════════════════════════════════════════════════════════════════════

@test
function void test_add_appends_task_and_clears_draft() {
    Model next = todo_update(Model { draft = "Milk" }, Msg.Add)
    assert(Array.length(next.tasks) == 1)
    assert(next.draft == "")
}

@test
function void test_add_ignores_empty_draft() {
    Model next = todo_update(Model { draft = "" }, Msg.Add)
    assert(Array.length(next.tasks) == 0)
}

@test
function void test_toggle_flips_done() {
    Model start = Model { tasks = [Task { title = "A", done = false }] }
    Model next = todo_update(start, Msg.Toggle(0))
    assert(next.tasks[0].done)
}

@test
function void test_remaining_counts_undone() {
    array<Task> tasks = [
        Task { title = "A", done = false },
        Task { title = "B", done = true },
        Task { title = "C", done = false }
    ]
    assert(remaining(tasks) == 2)
}

@test
function void test_clear_done_removes_completed() {
    Model start = Model { tasks = [
        Task { title = "A", done = true },
        Task { title = "B", done = false }
    ] }
    Model next = todo_update(start, Msg.ClearDone)
    assert(Array.length(next.tasks) == 1)
    assert(next.tasks[0].title == "B")
}

@test
function void test_typing_updates_draft_via_field() {
    Model next = GraphicalUi.test_input(todo_app(), Model {}, "draft", "Eggs")
    assert(next.draft == "Eggs")
}
```

Run it:

```bash
luma todo.luma
```

Type a task, press **Add**, and it joins the list. Tick items off; the "N remaining" count keeps up. Click **Clear completed** to remove the ticked ones. Now run the tests:

```bash
luma --test todo.luma
```

```text
6 test(s): 6 passed
```

Look back at how little of this is about *the screen*. The model is four lines. The logic is three small helpers and an `update`. The view is a plain function that reads the model and returns controls. The screen never disagrees with the data, because — one last time — **UI = f(state)**. That single idea carried the whole app.

You've built a real, tested GUI application in one file of Luma. Every larger app is just more of the same pattern.

---

## 14 — Where to Go Next

You now know how to build a Solaris GUI end to end: the MVU loop (Model, Message, `update`, `view`); the controls and how to wire them; layout with containers and spacing; styling with semantic tokens; rendering lists of data; navigation and overlays; effects and subscriptions; app configuration and theming; and testing — all without a line of HTML, CSS, or JavaScript. That's genuinely most of Solaris.

Here's where to look as you keep building.

- **Keep the reference close.** The [Solaris Guide](Luma_Solaris_Guide.md) is the concise companion to this tutorial — every component, modifier, token, command, and config option in one place. Reach for it when you need the exact signature.
- **Study the worked examples.** Three complete, tested programs ship with Luma: [`solaris_counter.luma`](../examples/applications/solaris_counter.luma) (the minimal loop), [`solaris_showcase.luma`](../examples/applications/solaris_showcase.luma) (a broad tour of controls), and [`solaris_gallery.luma`](../examples/applications/solaris_gallery.luma) (effects, a subscription, theming, and window sizing together).
- **Understand the "why".** The [Solaris design concept](Luma_Solaris_Architecture.md) explains the philosophy — why UI is a function of state, and why the web is treated as an implementation detail.
- **Grow past the surface.** Solaris covers the common, beginner-friendly subset. A few things are deliberately out of scope: `menu` is an in-page dropdown rather than a native menu bar, there's no system tray icon, and advanced pieces (routers, drag-and-drop) aren't part of the surface yet. When you need them, the low-level [GraphicalUi Guide](Luma_GraphicalUi_Guide.md) exposes the full engine beneath Solaris.
- **Sharpen the language.** If any Luma feature here felt shaky — `record`, `choice`, `match`, `with`, lambdas, or the pipe — the [Tutorial](Luma_Tutorial.md) and [User Manual](Luma_User_Manual.md) cover them in depth.

The best next step is to build something small of your own — a tip calculator, a habit tracker, a flashcard app. Start from the to-do app, change the Model to fit your idea, and let the view follow the data.

---

## 15 — Glossary

| Term | Meaning |
|---|---|
| **command** | A description of an effect (notify, fetch, timer) that `update` returns for Solaris to perform. |
| **component** | A ready-made piece of UI; in Solaris, a `Solaris.*` function that returns a `View`. Also called a control or widget. |
| **container** | A component that holds other components, such as `column`, `row`, `card`, or `grid`. |
| **control** | An interactive component the user operates, such as a button, text field, or slider. |
| **declarative** | Describing *what* the screen should be for the current data, rather than *how* to change it step by step. |
| **design token** | A semantic style value (a `choice`), such as `Spacing.L` or `Emphasis.Danger`, used instead of raw pixels or colours. |
| **effect** | An action outside the pure MVU loop — a notification, a network request, a timer. Expressed as a command or subscription. |
| **event-driven** | A program that waits and responds to user actions rather than running top to bottom. |
| **handler** | A modifier that turns a user action into a message, such as `on_click` or `on_change`. |
| **headless** | Rendering without opening a window (`LUMA_GUI_HEADLESS=1`), used for checks and tests. |
| **message** | A `choice` value naming one way the state can change; the input to `update`. |
| **modifier** | A function that takes a `View` and returns a tuned `View`, chained with `\|>` (e.g. `padding`, `primary`). |
| **model** | The `record` holding all of an app's state. |
| **MVU** | Model–View–Update: the architecture where the screen is a pure function of state and state changes in one place. |
| **pure function** | A function whose output depends only on its input, with no side effects; `update` and `view` are pure. |
| **render** | To produce the on-screen picture from the `View` tree. |
| **subscription** | A standing source of messages, such as a repeating timer or a keyboard shortcut. |
| **update** | The pure function `(model, message) -> model` that is the only place state changes. |
| **view** | The pure function `model -> View` that describes the screen. |
| **View** | Solaris's immutable value describing one piece of UI; a `view` returns a tree of them. |
| **widget** | Another word for a component or control. |

---

## See Also

- [Solaris Guide](Luma_Solaris_Guide.md) — the concise reference companion to this tutorial: every component, modifier, token, and option
- [Tutorial](Luma_Tutorial.md) — the step-by-step introduction to the Luma language itself, assumed as background here
- [GraphicalUi Guide](Luma_GraphicalUi_Guide.md) — the low-level webview engine beneath Solaris, and its raw API for advanced scenarios
- [Solaris design concept](Luma_Solaris_Architecture.md) — the design philosophy, architecture, and rationale behind the surface
- [Standard Library Reference — §14 (Solaris and GraphicalUi)](Luma_Standard_Library_Reference.md#14--solaris-and-graphicalui) — concise API listing for the surface and the engine
- [User Manual](Luma_User_Manual.md) — language syntax and semantics, including `record`, `choice`, `match`, and the pipe operator
- [Documentation Index](README.md) — index of all Luma documentation
