// Terminal module — headless interaction-testing API (Terminal.test_*).
//
// These functions let Luma code drive a Terminal program without a real
// terminal, so Terminal/TUI examples can be tested by feeding scripted key (and
// mouse) input and asserting on the captured output.  The Terminal harness
// intercepts the imperative I/O primitives — scripted input replaces console
// reads and a capture buffer replaces console writes.
//
// A session is bracketed by Terminal.test_start(keys) ... Terminal.test_stop():
//   * read_key / read_key_timeout / get_input consume `keys` in order, then
//     report end-of-input once the queue drains;
//   * enable_raw_mode / enable_mouse succeed as no-ops and is_terminal() reports
//     true, so the program under test runs its real loop;
//   * every byte written via emit()/write()/overwrite_line()/bell() is appended
//     to a capture buffer instead of the real terminal.
//
// The same machinery is reachable without Luma code via the LUMA_TERMINAL_INPUT
// environment variable (one key per line), which the example runner uses to
// drive the raw-mode example programs unattended.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/platform_utils.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/terminal_module.hpp"
#include "runtime/stdlib/io/terminal_module_internal.hpp"

namespace luma {

namespace terminal_internal {

// ─── Harness state (guarded by terminal_mutex where noted) ──────────────────

std::atomic<bool> headless_input_active{false};
std::atomic<bool> capture_active{false};
std::string capture_buffer; // guarded by terminal_mutex

namespace {

// Scripted-input queue, guarded by terminal_mutex.  Held in a function-local
// static so the (potentially throwing) std::deque constructor runs lazily on
// first use rather than during static initialization, where an uncaught
// exception could not be handled.
std::deque<std::string>& scripted_keys() {
    static std::deque<std::string> keys;
    return keys;
}

// Ensures the LUMA_TERMINAL_INPUT environment variable is consulted at most once.
std::atomic<bool> env_script_checked{false};

// Split text into lines on '\n', trimming a trailing '\r' (so scripts authored
// with CRLF line endings work) and dropping empty lines.
[[nodiscard]] std::vector<std::string> split_script_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;

    for (const char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }

            if (!current.empty()) {
                lines.push_back(std::move(current));
            }

            current.clear();
        } else {
            current += c;
        }
    }

    if (!current.empty() && current.back() == '\r') {
        current.pop_back();
    }

    if (!current.empty()) {
        lines.push_back(std::move(current));
    }

    return lines;
}

} // namespace

void ensure_env_script() {
    bool expected = false;

    if (!env_script_checked.compare_exchange_strong(expected, true)) {
        return; // Already checked this process.
    }

    const auto raw = safe_getenv("LUMA_TERMINAL_INPUT");

    if (!raw || raw->empty()) {
        return;
    }

    const std::scoped_lock lock{terminal_mutex};

    for (auto& line : split_script_lines(*raw)) {
        scripted_keys().push_back(std::move(line));
    }

    headless_input_active.store(true);
}

bool headless_input_is_active() {
    return headless_input_active.load();
}

std::optional<std::string> next_scripted_key() {
    const std::scoped_lock lock{terminal_mutex};

    if (scripted_keys().empty()) {
        return std::nullopt;
    }

    std::string key = std::move(scripted_keys().front());
    scripted_keys().pop_front();

    return key;
}

} // namespace terminal_internal

// ─── Registration ───────────────────────────────────────────────────────────

namespace {

using namespace terminal_internal;

// Append the elements of an array<string> argument to the scripted-key queue.
// The caller must hold terminal_mutex.
void enqueue_keys_locked(const std::shared_ptr<ArrayValue>& keys, std::string_view fn,
                         const SourceLocation& loc) {
    for (const auto& element : *keys->elements) {
        scripted_keys().push_back(expect_string(element, fn, loc));
    }
}

} // namespace

void register_terminal_testing(const EnvPtr& env) {
    // Activate env-driven headless input immediately so that programs which call
    // enable_raw_mode() before their first read see the harness as active.
    terminal_internal::ensure_env_script();

    ModuleBuilder{"Terminal", env} // Terminal.test_start(keys: array<string>) -> void
        // Begin a headless test session: queue `keys` as the scripted input
        // stream (each element is a key name such as "a", "enter", "up",
        // "ctrl+c", or a mouse event "mouse:left_press:5:10"), start capturing
        // all terminal output, and make raw mode / mouse / is_terminal report
        // as an active terminal.  Resets any prior session.
        .func("test_start", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& keys = expect_array(args[0], "Terminal.test_start", loc);

            const std::scoped_lock lock{terminal_mutex};

            scripted_keys().clear();
            capture_buffer.clear();
            enqueue_keys_locked(keys, "Terminal.test_start", loc);

            headless_input_active.store(true);
            capture_active.store(true);
            raw_mode_active.store(true);
            mouse_mode_active.store(false);

            return NullValue{};
        })
        // Terminal.test_feed(keys: array<string>) -> void
        // Append more keys to the scripted input stream (for multi-phase tests).
        .func("test_feed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& keys = expect_array(args[0], "Terminal.test_feed", loc);

            const std::scoped_lock lock{terminal_mutex};

            enqueue_keys_locked(keys, "Terminal.test_feed", loc);
            headless_input_active.store(true);

            return NullValue{};
        })
        // Terminal.test_output() -> string
        // Return all terminal output captured since test_start (ANSI sequences
        // and text), so tests can assert on what was rendered.
        .func("test_output", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            const std::scoped_lock lock{terminal_mutex};

            return Value{capture_buffer};
        })
        // Terminal.test_remaining() -> integer
        // Number of scripted keys not yet consumed (0 means the program read
        // everything that was queued).
        .func("test_remaining", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            const std::scoped_lock lock{terminal_mutex};

            return Value{static_cast<std::int64_t>(scripted_keys().size())};
        })
        // Terminal.test_stop() -> string
        // End the session, restore normal terminal I/O, and return the final
        // captured output.
        .func("test_stop", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            const std::scoped_lock lock{terminal_mutex};

            std::string output = std::move(capture_buffer);

            capture_buffer.clear();
            scripted_keys().clear();
            headless_input_active.store(false);
            capture_active.store(false);
            raw_mode_active.store(false);
            mouse_mode_active.store(false);

            return Value{std::move(output)};
        });
}

} // namespace luma
