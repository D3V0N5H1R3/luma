#ifndef LUMA_STDLIB_TERMINAL_MODULE_INTERNAL_HPP
#define LUMA_STDLIB_TERMINAL_MODULE_INTERNAL_HPP

// Internal header for the Terminal module split.
// Shared between terminal_module.cpp and terminal_module_ansi.cpp.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace luma::terminal_internal {

extern std::mutex terminal_mutex;
extern std::atomic<bool> raw_mode_active;
extern std::atomic<bool> mouse_mode_active;

// Shared "detail" text for the failure returned when an input/query function is
// called before Terminal.enable_raw_mode().  Emitted via error_msg("Terminal",
// fn, k_raw_mode_required) so every call site produces an identical message.
inline constexpr std::string_view k_raw_mode_required =
    "raw mode is not enabled. Call Terminal.enable_raw_mode() first";

// ─── Headless interaction-testing harness ──────────────────────────────────
// Lets Luma code drive a Terminal program without a real terminal: scripted
// key input is fed to read_key/read_key_timeout/get_input, and all terminal
// output is captured for assertions.  Activated by Terminal.test_start() or by
// the LUMA_TERMINAL_INPUT environment variable (one key per line).  State is
// defined in terminal_testing.cpp and guarded by terminal_mutex.

extern std::atomic<bool> headless_input_active;
extern std::atomic<bool> capture_active;
extern std::string capture_buffer;

/// Lazily activate env-driven headless input from LUMA_TERMINAL_INPUT (runs the
/// check at most once per process).
void ensure_env_script();

/// True when scripted (headless) input is active.
[[nodiscard]] bool headless_input_is_active();

/// Pop the next scripted key, or nullopt when the queue is drained.
[[nodiscard]] std::optional<std::string> next_scripted_key();

[[nodiscard]] bool stdout_is_terminal();

/// Ensure virtual terminal processing is enabled (Windows only; no-op on other platforms).
void prepare();

/// Thread-safe ANSI sequence output.
void emit(std::string_view sequence);

/// Write an ANSI sequence without locking.  Caller must hold terminal_mutex.
void emit_unlocked(std::string_view sequence);

/// Look up all valid color names (for error messages).
[[nodiscard]] std::string valid_color_names();

/// Look up a foreground ANSI code by color name. Returns empty view if unknown.
[[nodiscard]] std::string_view fg_code_for(std::string_view name);

/// Look up a background ANSI code by color name. Returns empty view if unknown.
[[nodiscard]] std::string_view bg_code_for(std::string_view name);

/// Map a Terminal.Color choice variant (PascalCase, e.g. "BrightBlack") to the
/// lowercase colour name used by color_map() (e.g. "bright_black").  Returns
/// nullopt for an unrecognised variant.
[[nodiscard]] std::optional<std::string_view> color_name_from_variant(std::string_view variant);

struct TerminalDimensions {
    int cols{80};
    int rows{24};
};

[[nodiscard]] TerminalDimensions query_terminal_size();

} // namespace luma::terminal_internal

#endif // LUMA_STDLIB_TERMINAL_MODULE_INTERNAL_HPP
