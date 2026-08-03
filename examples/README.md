# Luma Examples

This directory contains runnable Luma programs grouped by purpose so examples are easier to browse than a single flat list.

## Categories

| Directory                                  | Purpose                                                                         |
| ------------------------------------------ | ------------------------------------------------------------------------------- |
| [`applications/`](#applications)           | Complete sample programs such as calculators, editors, servers, and data tools. |
| [`debug/`](#debug)                         | Small programs intended for debugger and stepping workflows.                    |
| [`design-patterns/`](#design-patterns)     | Idiomatic patterns and higher-level composition techniques.                     |
| [`language-features/`](#language-features) | Focused examples for specific syntax and language constructs.                   |

## Language Features

| File                       | Topics Covered                                                                    | Difficulty         |
| -------------------------- | --------------------------------------------------------------------------------- | ------------------ |
| `hello`                    | Minimal "Hello, World!" program                                                   | 🟢 Beginner       |
| `fibonacci`                | Recursion, `@test` annotation, basic assertions                                   | 🟢 Beginner       |
| `control_flow`             | `while` loops, `break`, `continue`, dictionary iteration, match multi-patterns    | 🟢 Beginner       |
| `data_structures`          | Arrays, dictionaries, tuples, queues, stacks                                      | 🟢 Beginner       |
| `operators_and_literals`   | Hex/binary literals, bitwise ops, compound assignment, `++`/`--`, `in`, slicing   | 🟢 Beginner       |
| `records`                  | Records: declaration, default fields, value semantics, equality, `with` overrides | 🟢 Beginner       |
| `strings`                  | String interpolation `${}`, escape sequences, multi-line (triple-quoted) strings  | 🟢 Beginner       |
| `choice_types`             | Choice types (ADTs), pattern matching on variants                                 | 🟡 Intermediate   |
| `converter_functions`      | Base conversions (binary, hex), Roman numerals, ordinals, number-to-words         | 🟡 Intermediate   |
| `datetime`                 | DateTime module — formatting, arithmetic, timezones                               | 🟡 Intermediate   |
| `encoding`                 | Base64, URL-safe Base64, URL percent-encoding (Encoder module)                    | 🟡 Intermediate   |
| `error_handling`           | `result<T>`, typed errors, `!>` pipe, `?` propagation, `try/catch/finally`        | 🟡 Intermediate   |
| `hashing`                  | Hash module — SHA-256, MD5, HMAC, CRC32                                           | 🟡 Intermediate   |
| `math_functions`           | Math module — trig, rounding, roots, statistics, primes, constants                | 🟡 Intermediate   |
| `multi_file` / `_utils`    | `include` directive for multi-file projects                                       | 🟡 Intermediate   |
| `optional_values`          | `optional<T>`, `some`/`none`, `?.`, `??`, pattern matching                        | 🟡 Intermediate   |
| `pipeline`                 | Pipe operator `\|>`, lambdas, higher-order functions, functional transforms       | 🟡 Intermediate   |
| `closures`                 | Closures: variable capture, returning closures, stateful counters via `Reference` | 🟡 Intermediate   |
| `process_and_terminal`     | Process module, Terminal module                                                   | 🟡 Intermediate   |
| `string_functions`         | String module — trim, case, split/join, search, replace, pad, template, parse     | 🟡 Intermediate   |
| `text_processing`          | RegularExpression and Set modules                                                 | 🟡 Intermediate   |
| `type_system`              | Interfaces, generics, type aliases, `downcast`, `is<T>`                           | 🟡 Intermediate   |
| `advanced_data_structures` | Binary trees, graphs, hash sets, linked lists, partition/filter/reduce operations | 🔴 Advanced       |
| `advanced_functions`       | Optional/default parameters, named arguments, mutable parameters, lambda storage  | 🔴 Advanced       |
| `advanced_types`           | `with` expression, default fields, bounded generics, generic interfaces, `use`    | 🔴 Advanced       |
| `concurrency`              | Channels, tasks, `task_scope`, cooperative concurrency                            | 🔴 Advanced       |
| `encapsulation`            | Namespaces, `internal` visibility, qualified access                               | 🔴 Advanced       |
| `linear_algebra`           | LinearAlgebra module — vectors, matrices, operations                              | 🔴 Advanced       |
| `numerical_calculus`       | Calculus module — derivatives, integrals                                          | 🔴 Advanced       |
| `ownership`                | `unique` ownership, `borrow` references, `_` prefix suppression                   | 🔴 Advanced       |
| `references`               | Reference and Resource modules                                                    | 🔴 Advanced       |
| `task_cancellation`        | `Task.cancel`, `Task.is_cancelled`, cooperative cancellation                      | 🔴 Advanced       |

## Applications

| File                   | Description                                        | Difficulty         |
| ---------------------- | -------------------------------------------------- | ------------------ |
| `guess_the_number`     | Number guessing game with Random                   | 🟢 Beginner       |
| `quiz_game`            | Interactive quiz game                              | 🟢 Beginner       |
| `todo_list`            | Console-based todo list                            | 🟢 Beginner       |
| `word_counter`         | Word frequency counter                             | 🟢 Beginner       |
| `bank_accounts`        | Bank account ledger with typed domain errors       | 🟡 Intermediate   |
| `calculator`           | Expression calculator with match dispatch          | 🟡 Intermediate   |
| `compression`          | Compression module demonstration                   | 🟡 Intermediate   |
| `csv_processing`       | Csv module — parsing and generating CSV data       | 🟡 Intermediate   |
| `editor`               | Simple text editor                                 | 🟡 Intermediate   |
| `expression_evaluator` | Recursive expression evaluator with choice types   | 🟡 Intermediate   |
| `game_of_life`         | Conway's Game of Life cellular automaton           | 🟡 Intermediate   |
| `http_client`          | Http module demonstration                          | 🟡 Intermediate   |
| `json_processing`      | Json module — parsing and generating JSON          | 🟡 Intermediate   |
| `key_value_store`      | KeyValueStore module demonstration                 | 🟡 Intermediate   |
| `library_catalog`      | Library catalog with check-out tracking            | 🟡 Intermediate   |
| `logging`              | Log module — structured logging                    | 🟡 Intermediate   |
| `markdown_converter`   | Markdown to HTML converter                         | 🟡 Intermediate   |
| `password_toolkit`     | Password generator and strength meter              | 🟡 Intermediate   |
| `statistics_report`    | Statistics — mean, median, stddev, correlation     | 🟡 Intermediate   |
| `text_adventure`       | Text adventure with a choice-type state machine    | 🟡 Intermediate   |
| `xml_processing`       | Xml module — parsing and generating XML            | 🟡 Intermediate   |
| `chat_server`          | Socket-based multi-client chat server              | 🔴 Advanced       |
| `data_pipeline`        | Multi-stage data processing pipeline               | 🔴 Advanced       |
| `gui_2048`             | GraphicalUi 2048 sliding-tile keyboard game        | 🔴 Advanced       |
| `gui_animation`        | GraphicalUi transitions and keyframe animations    | 🔴 Advanced       |
| `gui_calculator`       | GraphicalUi calculator with keypad and keyboard    | 🔴 Advanced       |
| `gui_calendar`         | GraphicalUi month calendar with DateTime nav       | 🔴 Advanced       |
| `gui_conduit`          | GraphicalUi RealWorld/Conduit blog SPA (CRUD)      | 🔴 Advanced       |
| `gui_contacts`         | GraphicalUi contact book with table and dialogs    | 🔴 Advanced       |
| `gui_counter`          | GraphicalUi counter application                    | 🔴 Advanced       |
| `gui_dashboard`        | GraphicalUi dashboard with charts and tabs         | 🔴 Advanced       |
| `gui_gallery`          | GraphicalUi gallery of every widget and modifier   | 🔴 Advanced       |
| `gui_http`             | GraphicalUi HTTP integration demo                  | 🔴 Advanced       |
| `gui_layout`           | GraphicalUi elm-ui-inspired layout features demo   | 🔴 Advanced       |
| `gui_markdown`         | GraphicalUi Markdown editor with live preview      | 🔴 Advanced       |
| `gui_paint`            | GraphicalUi pixel paint canvas with palette        | 🔴 Advanced       |
| `gui_quiz`             | GraphicalUi multiple-choice quiz with scoring      | 🔴 Advanced       |
| `gui_router`           | GraphicalUi client-side routing                    | 🔴 Advanced       |
| `gui_settings`         | GraphicalUi settings panel with form widgets       | 🔴 Advanced       |
| `gui_styled`           | GraphicalUi styling, theming, and responsive demo  | 🔴 Advanced       |
| `gui_tic_tac_toe`      | GraphicalUi tic-tac-toe with win detection         | 🔴 Advanced       |
| `gui_timer`            | GraphicalUi Pomodoro timer with on_tick            | 🔴 Advanced       |
| `gui_todo`             | GraphicalUi todo list application                  | 🔴 Advanced       |
| `gui_virtual_list`     | GraphicalUi virtual scrolling for large lists      | 🔴 Advanced       |
| `gui_website`          | GraphicalUi multi-page website with routing        | 🔴 Advanced       |
| `matrix_calculator`    | Matrix calculator using LinearAlgebra module       | 🔴 Advanced       |
| `maze_solver`          | Maze solver — shortest path via BFS                | 🔴 Advanced       |
| `mouse_draw`           | Terminal mouse drawing application                 | 🔴 Advanced       |
| `solaris_counter`      | Solaris counter — canonical MVU surface example    | 🔴 Advanced       |
| `solaris_gallery`      | Solaris gallery: every component, effects, theming | 🔴 Advanced       |
| `solaris_showcase`     | Solaris surface tour: inputs, cards, lists, tokens | 🔴 Advanced       |
| `sudoku_solver`        | Sudoku solver using backtracking                   | 🔴 Advanced       |
| `tic_tac_toe`          | Tic-tac-toe with a minimax AI opponent             | 🔴 Advanced       |

## Design Patterns

| File                   | Pattern                            | Difficulty         |
| ---------------------- | ---------------------------------- | ------------------ |
| `adapter`              | Adapter pattern                    | 🔴 Advanced       |
| `builder`              | Builder pattern                    | 🔴 Advanced       |
| `composite`            | Composite pattern                  | 🔴 Advanced       |
| `decorator`            | Decorator pattern                  | 🔴 Advanced       |
| `dependency_injection` | Dependency injection               | 🔴 Advanced       |
| `factory`              | Factory pattern                    | 🔴 Advanced       |
| `fluent_interface`     | Fluent interface (method chaining) | 🔴 Advanced       |
| `publish_subscribe`    | Publish–subscribe pattern          | 🔴 Advanced       |
| `singleton`            | Singleton pattern                  | 🔴 Advanced       |
| `strategy`             | Strategy pattern                   | 🔴 Advanced       |
| `template_method`      | Template method pattern            | 🔴 Advanced       |
| `visitor`              | Visitor pattern                    | 🔴 Advanced       |

## Debug

Minimal programs that exercise the debugger and stepping workflows described in the [Luma Debugger guide](../documents/Luma_Debugger.md). Each keeps its Luma code deliberately small so the focus stays on a single debugging scenario rather than the program itself. Every example has an `@main` entry point, so the [example runner](#running-and-verifying-every-example) also runs each one end to end.

| File                  | Demonstrates                                           |
| --------------------- | ------------------------------------------------------ |
| `breakpoint_basic`    | Setting a line breakpoint on a single statement        |
| `conditional_loop`    | Conditional breakpoints gated on a loop variable       |
| `function_breakpoint` | Function breakpoints on named functions                |
| `data_breakpoint`     | Data breakpoints that watch a variable change          |
| `step_into_function`  | Stepping into a function call from `main`              |
| `step_over_loop`      | Stepping over the iterations of a loop                 |
| `variables_basic`     | Inspecting variables across the built-in types         |
| `structured_values`   | Records and choice types expand in the variable view   |
| `closure_variables`   | Captured variables shown in a dedicated Closure scope  |
| `set_variable`        | Modifying mutable variables from the debugger          |
| `recursive_stack`     | Deep, multi-frame call stacks from recursion           |
| `concurrent_tasks`    | Multiple threads via `task_scope` for thread debugging |
| `long_loop`           | A long-running loop for async pause / interrupt        |
| `exception_caught`    | A division-by-zero handled by `try` / `catch`          |
| `exception_unhandled` | An unhandled division by zero that halts execution     |

## Usage

Run any example with the interpreter:

```bash
build/luma examples/language-features/<file>.luma
```

> **Note:** On Windows with MSVC, the binary is at `build\Release\luma.exe`.

Use `applications/` when you want end-to-end samples, and the other directories when you want shorter, topic-specific references.

### Running and verifying every example

To execute **and** verify every example in this directory — including ones that normally need user input — use the runner script:

```bash
python scripts/run_luma_examples.py
```

It runs each program end to end and checks that it completes successfully:

- Console examples (such as `calculator`, `guess_the_number`, `todo_list`) are driven with scripted stdin.
- Terminal raw-mode examples (`process_and_terminal`, `mouse_draw`, `editor`) are driven through the headless Terminal harness: scripted keys are fed to `read_key` / `get_input` via `LUMA_TERMINAL_INPUT`, so the real input loop runs unattended.
- GUI examples (`gui_*` and `solaris_*`) run in **headless mode**, executing their full `init` / `view` / `subscribe` lifecycle without opening a window.
- Any example that declares `@test` functions is additionally run with `luma --test`, so its embedded assertions are verified — not just that `@main` completes.
- One example is intentionally skipped (`multi_file_utils`, an include-only helper); the script reports why.

Headless GUI execution is controlled by two environment variables, so a single GUI example can also be run unattended by hand:

| Variable              | Effect                                                                                                  |
| --------------------- | ------------------------------------------------------------------------------------------------------ |
| `LUMA_GUI_HEADLESS=1` | Run a GUI app without a window: render the initial view, evaluate subscriptions, then return.            |
| `LUMA_GUI_MESSAGES`   | Optional comma-separated `update` messages applied after the initial render (for example, `inc,inc,dec`). |

```bash
LUMA_GUI_HEADLESS=1 build/luma examples/applications/gui_counter.luma
```

> **Note:** These variables only affect headless test runs. When unset, a GUI app opens a real window as usual.

### Testing GUI interactions

The headless variables above run a GUI app's lifecycle, but to *verify behaviour* — that clicking a button or typing into a field produces the right state — use the `GraphicalUi.test_*` family. These render a view without a window, simulate a real interaction, and return the resulting model, so GUI logic can be asserted in `@test` blocks (see `gui_counter` for a worked example):

| Function                                                | Returns      | Simulates                                                         |
| ------------------------------------------------------- | ------------ | ---------------------------------------------------------------- |
| `GraphicalUi.test_init(config)`                           | model        | Running `init` (or the configured initial model)                 |
| `GraphicalUi.test_render(config, model)`                  | widget tree  | Rendering `view(model)` for structural assertions                |
| `GraphicalUi.test_click(config, model, label)`            | new model    | Clicking the widget whose text matches `label`                   |
| `GraphicalUi.test_input(config, model, locator, value)`   | new model    | Entering `value` into a text field, checkbox, toggle, or dropdown |
| `GraphicalUi.test_message(config, model, message)`        | new model    | Delivering `message` to `update(model, message)`                 |

```luma
@test
function void test_increment_button() {
    # counter_config() is the same dictionary passed to GraphicalUi.app.
    assert(GraphicalUi.test_click(counter_config(), 41, "+ Increment") == 42)
}
```

### Testing Terminal interactions

Terminal/TUI examples are imperative rather than declarative, so they use a different harness: the `Terminal.test_*` family drives a program's real input loop without a terminal by feeding scripted keys and capturing output. A session is bracketed by `test_start(keys)` … `test_stop()`; inside it `read_key` / `get_input` consume the queued keys in order (then report end-of-input once drained), and `write` / `overwrite_line` / `bell` are captured for assertions.

| Function                    | Returns | Simulates                                              |
| --------------------------- | ------- | ------------------------------------------------------ |
| `Terminal.test_start(keys)` | —       | Begin a session and queue scripted key / mouse input   |
| `Terminal.test_feed(keys)`  | —       | Append more scripted input mid-session                 |
| `Terminal.test_output()`    | string  | Everything captured so far                             |
| `Terminal.test_remaining()` | integer | Scripted keys not yet consumed                         |
| `Terminal.test_stop()`      | string  | End the session and return the final captured output   |

```luma
@test
function void test_counter_responds_to_keys() {
    Terminal.test_start(["+", "+", "+", "-", "q"])
    integer total = run_counter(10)   # the example's own input loop
    string output = Terminal.test_stop()

    assert(total == 12)
    assert(output == "")
}
```

The same machinery is reachable without Luma code via the `LUMA_TERMINAL_INPUT` environment variable (one key per line), which the runner uses to drive the raw-mode examples unattended:

```bash
printf 'a\nshift+b\nctrl+c\n' | LUMA_TERMINAL_INPUT="$(cat)" build/luma examples/language-features/process_and_terminal.luma
```
