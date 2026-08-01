#include "runtime/stdlib/system/log_module.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "runtime/stdlib/system/datetime_codec.hpp"

namespace luma {

namespace {

// === Log level definitions ===

enum class LogLevel : std::uint8_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    Off = 4
};

// Unified level descriptor table.  Each entry holds the string argument form
// (lowercase), the uppercase display form used in ${level} output, the Luma
// choice variant name (PascalCase), and the LogLevel enum value.  All four
// lookup operations derive from this single table.
struct LevelDescriptor {
    std::string_view str_name;     // lowercase, for user arguments ("debug")
    std::string_view display_name; // uppercase, for ${level} output ("DEBUG")
    std::string_view variant_name; // PascalCase, for choice variants ("Debug")
    LogLevel level;
};

constexpr std::array<LevelDescriptor, 5> k_level_table = {{
    {.str_name = "debug",
     .display_name = "DEBUG",
     .variant_name = "Debug",
     .level = LogLevel::Debug},
    {.str_name = "info",
     .display_name = "INFO",
     .variant_name = "Information",
     .level = LogLevel::Info},
    {.str_name = "warn",
     .display_name = "WARN",
     .variant_name = "Warning",
     .level = LogLevel::Warn},
    {.str_name = "error",
     .display_name = "ERROR",
     .variant_name = "Error",
     .level = LogLevel::Error},
    {.str_name = "off", .display_name = "OFF", .variant_name = "Off", .level = LogLevel::Off},
}};

// Look up the uppercase display form ("DEBUG", "INFO", ...) used by ${level}.
[[nodiscard]] constexpr std::string_view level_to_string(LogLevel level) {
    const auto idx = static_cast<std::size_t>(level);
    return idx < k_level_table.size() ? k_level_table[idx].display_name : "UNKNOWN";
}

[[nodiscard]] LogLevel string_to_level(std::string_view s) {
    for (const auto& d : k_level_table) {
        if (d.str_name == s) {
            return d.level;
        }
    }
    return LogLevel::Info;
}

// Resolve a Log.Level choice variant or a lowercase string name to a LogLevel,
// mirroring the dual-form accepted by Log.set_level.  Throws on an unusable
// argument type.
[[nodiscard]] LogLevel resolve_level_arg(const Value& value, std::string_view function,
                                         const SourceLocation& loc) {
    if (value.is_choice()) {
        const auto& variant = value.as_choice()->variant;

        for (const auto& d : k_level_table) {
            if (d.variant_name == variant) {
                return d.level;
            }
        }

        return LogLevel::Info;
    }

    if (value.is_string()) {
        return string_to_level(value.as_string());
    }

    throw RuntimeError{std::string{function} + ": expected a Log.Level or string", loc,
                       "pass a Log.Level variant or a string level name"};
}

// Default log format used by LogState initialisation and Log.reset().
constexpr std::string_view k_default_log_format{"${timestamp} [${level}] ${message}"};

// === Global log state ===

// Where formatted log lines are written.  A file path is held by file_stream
// rather than duplicated here, so the sink is a plain tag instead of a string
// that overloads "stream name" and "file path".
enum class OutputTarget : std::uint8_t {
    Stderr,
    Stdout,
    File
};

// Process-global singleton. Log level, format, output target, and context
// persist across script boundaries. In REPL or multi-script scenarios, one
// script's configuration silently affects all subsequent scripts. Call
// Log.reset() to restore defaults between scripts if isolation is needed.
struct LogState {
    std::mutex mutex;
    LogLevel level{LogLevel::Info};
    std::string format{k_default_log_format};
    OutputTarget output{OutputTarget::Stderr};
    std::ofstream file_stream;
    std::vector<std::pair<std::string, std::string>> context;
};

[[nodiscard]] LogState& log_state() {
    static LogState state;

    return state;
}

[[nodiscard]] std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);

    // Delegate to the shared ISO-8601 renderer so the format literal lives in
    // one place (datetime_formatter.cpp).  The sentinel covers the nullopt
    // case, when the instant falls outside the renderer's supported range.
    return luma::datetime::format_iso8601(static_cast<double>(time_t_now))
        .value_or("0000-00-00T00:00:00Z");
}

[[nodiscard]] std::string
format_message(const std::string& fmt, LogLevel level, const std::string& message,
               const std::vector<std::pair<std::string, std::string>>& ctx) {
    const auto timestamp = current_timestamp();
    const auto level_str = level_to_string(level);

    // Recognised placeholders and the text each expands to.  Both the match
    // and the loop advance derive from token.size(), so there are no literal
    // length offsets to keep in sync with the spellings; adding a placeholder
    // is one more table entry.
    const std::array<std::pair<std::string_view, std::string_view>, 3> placeholders{{
        {"${timestamp}", timestamp},
        {"${level}", level_str},
        {"${message}", message},
    }};

    // Single-pass placeholder replacement: scan the format string once,
    // substituting any recognised ${...} token and passing the rest through.
    std::string result;
    result.reserve(fmt.size() + timestamp.size() + level_str.size() + message.size());

    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '$' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
            bool matched = false;

            for (const auto& [token, value] : placeholders) {
                if (fmt.compare(i, token.size(), token) == 0) {
                    result += value;
                    i += token.size() - 1;
                    matched = true;

                    break;
                }
            }

            if (!matched) {
                result += fmt[i];
            }
        } else {
            result += fmt[i];
        }
    }

    // Append context key-value pairs.
    if (!ctx.empty()) {
        result += " |";

        for (const auto& [key, val] : ctx) {
            result += std::format(" {}={}", key, val);
        }
    }

    return result;
}

void write_log(LogLevel level, const std::string& message) {
    auto& state = log_state();
    const std::scoped_lock lock{state.mutex};

    if (level < state.level) {
        return;
    }

    const auto formatted = format_message(state.format, level, message, state.context);

    switch (state.output) {
        case OutputTarget::Stderr:
            std::cerr << formatted << "\n";
            break;
        case OutputTarget::Stdout:
            std::cout << formatted << "\n";
            break;
        case OutputTarget::File:
            if (state.file_stream.is_open()) {
                state.file_stream << formatted << "\n";

                state.file_stream.flush();
            }
            break;
    }
}

} // namespace

void register_log_ns(const EnvPtr& env, bool sandbox) {
    ModuleBuilder builder{"Log", env};

    // The four emission functions differ only by their level and qualified
    // name, so register them from a table rather than four identical blocks.
    struct EmissionFunction {
        std::string_view name;
        LogLevel level;
    };

    constexpr std::array<EmissionFunction, 4> emission_functions = {{
        {.name = "debug", .level = LogLevel::Debug},
        {.name = "information", .level = LogLevel::Info},
        {.name = "warning", .level = LogLevel::Warn},
        {.name = "error", .level = LogLevel::Error},
    }};

    for (const auto& emission : emission_functions) {
        auto label = "Log." + std::string{emission.name};

        builder.func(emission.name, 1)
            .raw_body([level = emission.level, label = std::move(label)](
                          std::span<const Value> args, SourceLocation loc) -> Value {
                (void)expect_string(args[0], label, loc);

                write_log(level, args[0].as_string());

                return Value{NullValue{}};
            });
    }

    builder.func("set_level", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto& state = log_state();
            const std::scoped_lock lock{state.mutex};

            if (args[0].is_choice()) {
                // Accept LogLevel choice variant.
                const auto& variant = args[0].as_choice()->variant;

                state.level = LogLevel::Info; // default
                for (const auto& d : k_level_table) {
                    if (d.variant_name == variant) {
                        state.level = d.level;
                        break;
                    }
                }
            } else if (args[0].is_string()) {
                state.level = string_to_level(args[0].as_string());
            } else {
                throw RuntimeError{"Log.set_level: expected a LogLevel or string", loc,
                                   "pass a Log.Level variant or a string level name"};
            }

            return Value{NullValue{}};
        })
        .func("get_level", 0)
        .raw_body([]([[maybe_unused]] std::span<const Value> args,
                     [[maybe_unused]] SourceLocation loc) -> Value {
            auto& state = log_state();
            const std::scoped_lock lock{state.mutex};

            const auto idx = static_cast<std::size_t>(state.level);
            const auto variant = idx < k_level_table.size()
                                     ? std::string{k_level_table[idx].variant_name}
                                     : std::string{"Information"};

            auto cv = std::make_shared<ChoiceValue>();
            cv->type_name = "Level";
            cv->variant = variant;

            return Value{std::move(cv)};
        })
        .func("is_enabled", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto level = resolve_level_arg(args[0], "Log.is_enabled", loc);

            auto& state = log_state();
            const std::scoped_lock lock{state.mutex};

            return Value{level >= state.level};
        })
        .func("log", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto level = resolve_level_arg(args[0], "Log.log", loc);
            (void)expect_string(args[1], "Log.log", loc);

            write_log(level, args[1].as_string());

            return Value{NullValue{}};
        })
        .func("set_format", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Log.set_format", loc);

            auto& state = log_state();
            const std::scoped_lock lock{state.mutex};
            state.format = args[0].as_string();

            return Value{NullValue{}};
        })
        .func("set_context", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_string() || !args[1].is_string()) {
                throw RuntimeError{"Log.set_context: expected string key and value", loc,
                                   "pass strings for both the key and value arguments"};
            }

            const auto& key = args[0].as_string();
            const auto& val = args[1].as_string();

            auto& state = log_state();
            const std::scoped_lock lock{state.mutex};

            // Update existing or add new.
            for (auto& [k, v] : state.context) {
                if (k == key) {
                    v = val;

                    return Value{NullValue{}};
                }
            }

            state.context.emplace_back(key, val);

            return Value{NullValue{}};
        })
        .func("clear_context", 0)
        .raw_body([]([[maybe_unused]] std::span<const Value> args,
                     [[maybe_unused]] SourceLocation loc) -> Value {
            auto& state = log_state();
            const std::scoped_lock lock{state.mutex};

            state.context.clear();

            return Value{NullValue{}};
        })
        .func("reset", 0)
        .raw_body([]([[maybe_unused]] std::span<const Value> args,
                     [[maybe_unused]] SourceLocation loc) -> Value {
            auto& state = log_state();
            const std::scoped_lock lock{state.mutex};

            state.level = LogLevel::Info;
            state.format = std::string{k_default_log_format};

            if (state.file_stream.is_open()) {
                state.file_stream.close();
            }

            state.output = OutputTarget::Stderr;
            state.context.clear();

            return Value{NullValue{}};
        });

    // Log.set_output opens files, so it is withheld from sandboxed
    // environments; func_if registers it only when sandbox is false.  Accepts the
    // typed Log.Output choice (Stderr / Stdout / File(path)) or the equivalent
    // string form ("stderr" / "stdout" / a path), mirroring Log.set_level's
    // dual-form — routing on the File variant (not on the payload text) so a path
    // that happens to read "stdout" still opens a file.
    builder.func_if(
        !sandbox, "set_output", 1, [](std::span<const Value> args, SourceLocation loc) -> Value {
            auto& state = log_state();
            const std::scoped_lock lock{state.mutex};

            // Resolve the argument to either a standard stream or a file path.
            std::optional<OutputTarget> stream_target; // set for Stderr / Stdout
            std::string file_path;                     // set for File / path string

            if (args[0].is_choice()) {
                const auto& choice = *args[0].as_choice();

                if (choice.variant == "Stderr") {
                    stream_target = OutputTarget::Stderr;
                } else if (choice.variant == "Stdout") {
                    stream_target = OutputTarget::Stdout;
                } else if (choice.variant == "File") {
                    if (choice.fields.empty() || !choice.fields.front().is_string()) {
                        throw RuntimeError{
                            "Log.set_output: Log.Output.File is missing its path payload", loc,
                            R"(build it as Log.Output.File("path.log"))"};
                    }

                    file_path = choice.fields.front().as_string();
                } else {
                    throw RuntimeError{
                        "Log.set_output: unknown Log.Output variant", loc,
                        "use Log.Output.Stderr, Log.Output.Stdout, or Log.Output.File(path)"};
                }
            } else if (args[0].is_string()) {
                const auto& target = args[0].as_string();

                if (target == "stderr") {
                    stream_target = OutputTarget::Stderr;
                } else if (target == "stdout") {
                    stream_target = OutputTarget::Stdout;
                } else {
                    file_path = target;
                }
            } else {
                throw RuntimeError{"Log.set_output: expected a Log.Output or string", loc,
                                   "pass a Log.Output variant or a stream name / file path"};
            }

            // Stream target — close any open file and switch.
            if (stream_target.has_value()) {
                if (state.file_stream.is_open()) {
                    state.file_stream.close();
                }

                state.output = *stream_target;

                return make_success_value(Value{NullValue{}});
            }

            // File target — validate path before closing the old stream.
            std::filesystem::path safe;

            try {
                safe = validate_path(file_path, loc);
            } catch (const RuntimeError&) {
                return make_failure_value("Log.set_output: path rejected by security policy");
            }

            // Path is valid — now close old stream and open new one.
            if (state.file_stream.is_open()) {
                state.file_stream.close();
            }

            state.file_stream.open(safe, std::ios::app);

            if (!state.file_stream.is_open()) {
                return make_failure_value(
                    error_msg("Log", "set_output", std::format("cannot open '{}'", file_path)));
            }

            state.output = OutputTarget::File;

            return make_success_value(Value{NullValue{}});
        });
}

} // namespace luma
