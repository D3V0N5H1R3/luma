# Luma — Concurrent Debugging Guide

> Debugging concurrent Luma programs — tasks, channels, breakpoints, and common pitfalls.

> **See also.** For the debugger's design and architecture, see [Luma — Debugger](Luma_Debugger.md). For the concurrency language features, see the [User Manual](Luma_User_Manual.md).

---

## Table of Contents

1. [Concurrency Model Overview](#1--concurrency-model-overview)
2. [How DAP Presents Concurrent Tasks](#2--how-dap-presents-concurrent-tasks)
3. [Breakpoints in Concurrent Code](#3--breakpoints-in-concurrent-code)
4. [Inspecting Channel State](#4--inspecting-channel-state)
5. [Debugging Deadlocks and Races](#5--debugging-deadlocks-and-races)
6. [Common Pitfalls](#6--common-pitfalls)
7. [Step-by-Step Example](#7--step-by-step-example)

- [See Also](#see-also)

---

## 1 — Concurrency Model Overview

Luma provides structured concurrency through three core primitives:

### Tasks (`spawn` / `await`)

Use `spawn` to run a function call concurrently. Each spawned task executes on the internal thread pool in its own VM instance. Use `await` to block until a task completes and retrieve its result:

```luma
task<integer> t = spawn compute(42)
integer result = await t
```

Each task may be awaited only once. A second `await` on the same task produces a runtime error.

### Task Scopes (`task_scope`)

A `task_scope` block provides structured lifetime management. All tasks spawned inside the block are automatically awaited before the scope exits. The scope returns an `array` of results in spawn order:

```luma
array<integer> results = task_scope {
    spawn compute(1)
    spawn compute(2)
    spawn compute(3)
}
# results == [1, 4, 9]
```

If any child task fails, the remaining siblings are cancelled cooperatively and the error propagates out of the scope. Scopes can be nested.

### Channels

Channels are thread-safe FIFO queues for passing values between tasks. Values are deep-copied on send to prevent shared mutable state:

```luma
channel<integer> ch = Channel.new()
Channel.send(ch, 42)
result<integer> value = Channel.receive(ch)
Channel.close(ch)
```

Key `Channel` functions:

| Function                  | Description                                |
| ------------------------- | ------------------------------------------ |
| `Channel.new()`           | Create an unbounded channel                |
| `Channel.new_buffered(n)` | Create a bounded channel with capacity `n` |
| `Channel.send(ch, v)`     | Send a value (`false` if closed)           |
| `Channel.receive(ch)`     | Blocking receive (`fail` if closed)        |
| `Channel.try_receive(ch)` | Non-blocking receive                       |
| `Channel.close(ch)`       | Close the channel                          |
| `Channel.is_closed(ch)`   | Check if the channel is closed             |
| `Channel.length(ch)`      | Number of buffered values                  |

### Cancellation

Cancellation is cooperative. `Task.cancel(t)` sets a cancellation token on the task. The task must check `Task.is_cancelled(t)` and exit gracefully — there is no forced termination:

```luma
task<integer> t = spawn long_running_work()
Task.cancel(t)
# The task should check Task.is_cancelled(t) in its loop
```

---

## 2 — How DAP Presents Concurrent Tasks

The Luma debugger (`luma_dap`) maps each concurrent task to a DAP thread. Your editor's **Threads** panel shows one entry per active task:

| DAP Thread ID | Luma Concept                    |
| ------------- | ------------------------------- |
| 1             | Main thread (top-level program) |
| 2+            | Spawned tasks                   |

When the program enters a `task_scope`, the debugger emits `thread` started events for each spawned task. When a task completes or is cancelled, a `thread` exited event fires.

### Thread-Level Control

- **Pause one thread:** Select a specific thread in the Threads panel and pause it. Other threads continue executing.
- **Pause all threads:** Use the global pause button. The `stopped` event includes `allThreadsStopped: true`.
- **Continue one thread:** Resume a single paused thread while others remain paused.
- **Step within a thread:** Step Over, Step Into, and Step Out apply to the selected thread only.

Each thread has its own call stack. When a thread hits a breakpoint, the editor automatically switches to that thread and shows its stack trace, local variables, and source location.

---

## 3 — Breakpoints in Concurrent Code

Breakpoints work identically in concurrent and single-threaded code. Set them by file and line number in your editor.

### Behaviour When Multiple Tasks Share Code

If two tasks execute the same function and a breakpoint is set inside it, **both tasks will hit the breakpoint independently**. Each stop produces a separate `stopped` event with the corresponding thread ID. You can inspect each task's local state separately.

### Conditional Breakpoints

Use conditional breakpoints to filter stops by task-specific state. For example, pause only when a particular argument value is present:

```text
Condition: n > 100
```

### Log Points

Log points (non-breaking tracepoints) are especially useful in concurrent code because they do not alter the timing of task execution. Set a log message like:

```text
Task processing item: {item}
```

The output appears in the debug console with the thread ID.

### Exception Breakpoints

The debugger supports two exception filters:

| Filter     | Behaviour                                          |
| ---------- | -------------------------------------------------- |
| `caught`   | Pause on exceptions caught by `try`/`catch`        |
| `uncaught` | Pause on unhandled exceptions (enabled by default) |

When a task throws an unhandled exception inside a `task_scope`, the debugger pauses on the exception. The remaining sibling tasks may continue until the scope propagates the cancellation.

---

## 4 — Inspecting Channel State

When execution is paused, you can inspect channel variables in the **Variables** panel. Channel values display:

- Whether the channel is open or closed.
- The number of buffered values (`Channel.length`).
- The buffered values themselves (as an expandable list).

### Using the Debug Console

Evaluate channel expressions directly in the debug console while paused:

```text
> Channel.length(ch)
=> 3
> Channel.is_closed(ch)
=> false
> Channel.try_receive(ch)
=> success(42)
```

> **Warning:** `Channel.receive(ch)` is a blocking call. Using it in the debug console while paused will deadlock if no value is available. Use `Channel.try_receive(ch)` instead.

---

## 5 — Debugging Deadlocks and Races

### Detecting Deadlocks

A deadlock occurs when tasks are waiting on each other indefinitely. Common signs:

- The program stops making progress.
- Multiple threads appear paused on `Channel.receive` or `await` calls.
- No output is produced.

**Strategy:**

1. Pause all threads.
2. Inspect the call stack of each thread. Look for threads blocked on `Channel.receive` or `await`.
3. Check whether the channel they are waiting on has any senders still running.
4. Check whether awaited tasks have completed.

### Detecting Race Conditions

Luma prevents shared mutable state by deep-copying values on channel send. However, logical race conditions can still occur — for example, the order in which tasks complete may vary between runs.

**Strategy:**

1. Use log points to trace the order of operations without altering timing.
2. Set breakpoints at the points where tasks interact (channel sends/receives).
3. Step through one task at a time while others are paused to understand the interleaving.
4. Use `Task.timeout` in your code to surface unexpectedly slow operations.

### Using Timeouts to Surface Issues

Replace indefinite waits with timed variants during debugging:

```luma
# Instead of:
result<integer> value = Channel.receive(ch)

# Use:
result<integer> value = Channel.receive_timeout(ch, 5000)
```

This converts a silent deadlock into a visible failure after 5 seconds.

---

## 6 — Common Pitfalls

### Awaiting a Task Twice

Each `task` value can be awaited only once. A second `await` throws a runtime error:

```text
Error: await called on an already-consumed task
```

**Fix:** Store the result of `await` in a variable and reuse the variable.

### Forgetting to Close Channels

If a producer task finishes without calling `Channel.close(ch)`, consumer tasks blocked on `Channel.receive(ch)` will wait forever.

**Fix:** Always close channels when the producer is done. Use `task_scope` to ensure cleanup.

### Spawn Outside `task_scope`

Using `spawn` outside a `task_scope` runs the task fire-and-forget. The type checker emits a warning. If the spawned task fails, the error is silently lost.

**Fix:** Wrap spawns in a `task_scope` block.

### Blocking the Debug Console

Calling a blocking function like `Channel.receive` or `await` in the debug console freezes the debugger's expression evaluator.

**Fix:** Use non-blocking alternatives: `Channel.try_receive`, `Task.is_done`.

### Task Queue Overflow

The runtime limits the task queue to 100,000 pending tasks. Spawning beyond this limit throws:

```text
Error: task queue is full — too many pending tasks
```

**Fix:** Await or scope your tasks to limit concurrency.

---

## 7 — Step-by-Step Example

This example demonstrates debugging a producer-consumer pattern with channels.

### The Program

```luma
function none producer(channel<integer> ch, integer count) {
    for integer i in 0..count {
        Channel.send(ch, i * i)
    }
    Channel.close(ch)
}

function integer consumer(channel<integer> ch) {
    mutable integer sum = 0
    mutable result<integer> value = Channel.receive(ch)
    while Result.is_success(value) {
        sum = sum + Result.unwrap(value)
        value = Channel.receive(ch)
    }
    return sum
}

@main
function void main() {
    channel<integer> ch = Channel.new()
    array<integer> results = task_scope {
        spawn producer(ch, 5)
        spawn consumer(ch)
    }
    print("Sum: ${results[1]}")
}
```

### Debugging Steps

1. **Set breakpoints.** Place breakpoints on:
    - The `Channel.send(ch, i * i)` line in `producer`.
    - The `sum = sum + Result.unwrap(value)` line in `consumer`.

2. **Start debugging.** Launch the program with your editor's debug configuration. The Threads panel shows **Thread 1 (Main)**.

3. **Enter `task_scope`.** When execution reaches the `task_scope` block, two new threads appear:
    - Thread 2 — `producer`
    - Thread 3 — `consumer`

4. **Hit the producer breakpoint.** Thread 2 pauses at `Channel.send`. Inspect `i` (should be `0`) and `ch` (should show 0 buffered values). Click **Continue** on Thread 2.

5. **Hit the consumer breakpoint.** Thread 3 pauses at `sum = sum + ...`. Inspect `value` (should be `success(0)`) and `sum` (should be `0`). Click **Continue** on Thread 3.

6. **Repeat.** Continue through iterations, watching `i` increment in the producer and `sum` accumulate in the consumer.

7. **Producer finishes.** After `i == 4`, the producer calls `Channel.close(ch)` and Thread 2 exits. The Threads panel shows only Thread 1 and Thread 3.

8. **Consumer drains.** The consumer receives all remaining values. When `Channel.receive` returns a failure (channel closed), the consumer returns `sum`. Thread 3 exits.

9. **Scope completes.** The `task_scope` collects results. Thread 1 resumes and prints the sum.

### What to Watch For

- **Channel length:** After each `Channel.send`, check `Channel.length(ch)` in the debug console to verify values are buffering as expected.
- **Thread lifecycle:** Watch the Threads panel for `thread` started/exited events to confirm tasks are running and completing.
- **Error propagation:** If you introduce a bug in the producer (e.g., division by zero), the exception pauses Thread 2. The `task_scope` cancels Thread 3, and the error propagates to Thread 1.

---

## See Also

- [Debugger](Luma_Debugger.md) — Debug Adapter Protocol design and architecture
- [User Manual](Luma_User_Manual.md) — task, channel, and `task_scope` language features
- [Standard Library Reference — §5 Channel](Luma_Standard_Library_Reference.md#5--channel) — channel operations
- [Standard Library Reference — §37 Task](Luma_Standard_Library_Reference.md#37--task) — task operations
- [Coding Guidelines — §20 Concurrency](Luma_Coding_Guidelines.md#20--concurrency) — concurrency idioms and best practices
