#include "runtime/stdlib/io/console_module.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

// Whether `stream` is connected to an interactive terminal (TTY), used by
// Console.is_tty / is_interactive so a CLI can prompt-and-colour when
// interactive but stay quiet when piped or redirected.
[[nodiscard]] bool stream_is_tty(std::FILE* stream) {
#ifdef _WIN32
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

// Strips leading and trailing ASCII whitespace, shared by the typed-prompt
// parsers so surrounding spaces or a trailing carriage return never defeat the
// parse.
[[nodiscard]] std::string trim_ascii(std::string_view s) {
    const auto begin = s.find_first_not_of(" \t\r\n\f\v");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n\f\v");
    return std::string{s.substr(begin, end - begin + 1)};
}

// Writes a validated string argument to `stream`, reporting the outcome as a
// result<boolean>: success(true) while the stream stays healthy, or a failure
// result carrying a "<qualified_name>: write failed" message otherwise.  Shared
// by Console.write_to_stdout and Console.write_to_stderr, which differ only in
// their target stream and qualified name.
[[nodiscard]] Value write_string_to_stream(std::ostream& stream, std::string_view qualified_name,
                                           std::span<const Value> args, const SourceLocation& loc) {
    const std::string& text = expect_string(args[0], qualified_name, loc);

    stream << text;

    if (stream.fail()) {
        return make_failure_value(std::format("{}: write failed", qualified_name));
    }

    return make_success_value(Value{true});
}

} // namespace

void register_console_ns(const EnvPtr& env) {
    ModuleBuilder{"Console", env}
        .func("prompt", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& prompt_text = expect_string(args[0], "Console.prompt", loc);

            std::cout << prompt_text << std::flush;

            std::string line{};

            if (!std::getline(std::cin, line)) {
                return make_failure_value(
                    error_msg("Console", "prompt", "end of input or read error"));
            }

            if (line.size() > ResourceLimits::max_string_size) {
                return make_failure_value(
                    error_msg("Console", "prompt", "input line exceeds maximum size"));
            }

            return make_success_value(Value{std::move(line)});
        })
        .func("read_from_stdin", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            std::string content{};

            // Read buffer size for Console.read_from_stdin(): page-aligned for efficient OS I/O.
            constexpr std::size_t k_stdin_read_buffer_size{4096};

            std::array<char, k_stdin_read_buffer_size> buffer{};

            while (std::cin.read(buffer.data(), k_stdin_read_buffer_size) ||
                   std::cin.gcount() > 0) {
                const auto raw_count = std::cin.gcount();
                const auto n = static_cast<std::size_t>(raw_count > 0 ? raw_count : 0);

                if (content.size() + n > ResourceLimits::max_string_size) {
                    return make_failure_value(error_msg("Console", "read_from_stdin",
                                                        "input exceeds maximum string size"));
                }

                content.append(buffer.data(), n);

                if (std::cin.eof()) {
                    break;
                }
            }

            if (std::cin.bad()) {
                return make_failure_value(
                    error_msg("Console", "read_from_stdin", "I/O error while reading stdin"));
            }

            return make_success_value(Value{std::move(content)});
        })
        .func("write_to_stderr", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return write_string_to_stream(std::cerr, "Console.write_to_stderr", args, loc);
        })
        .func("write_to_stdout", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return write_string_to_stream(std::cout, "Console.write_to_stdout", args, loc);
        })
        .func("read_line", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            // Reads a single line from stdin (newline stripped), the basic
            // interactive-loop primitive between prompt (prints first) and
            // read_from_stdin (reads everything).
            std::string line{};

            if (!std::getline(std::cin, line)) {
                return make_failure_value(
                    error_msg("Console", "read_line", "end of input or read error"));
            }

            if (line.size() > ResourceLimits::max_string_size) {
                return make_failure_value(
                    error_msg("Console", "read_line", "input line exceeds maximum size"));
            }

            return make_success_value(Value{std::move(line)});
        })
        .func("read_lines", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            // Reads all of stdin split into lines, mirroring FileSystem.read_lines.
            auto arr = std::make_shared<ArrayValue>();

            std::string line{};
            std::size_t total_bytes = 0;

            while (std::getline(std::cin, line)) {
                total_bytes += line.size();

                if (total_bytes > ResourceLimits::max_string_size) {
                    return make_failure_value(
                        error_msg("Console", "read_lines", "input exceeds maximum string size"));
                }

                if (arr->elements->size() >= ResourceLimits::max_array_size) {
                    return make_failure_value(
                        error_msg("Console", "read_lines", "input has too many lines"));
                }

                arr->elements->emplace_back(line);
            }

            if (std::cin.bad()) {
                return make_failure_value(
                    error_msg("Console", "read_lines", "I/O error while reading stdin"));
            }

            return make_success_value(Value{std::move(arr)});
        })
        .func("flush", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            // Forces buffered stdout out so a newline-less prompt appears before a
            // blocking read.
            std::cout.flush();

            if (std::cout.fail()) {
                return make_failure_value(error_msg("Console", "flush", "flush failed"));
            }

            return make_success_value(Value{true});
        })
        .func("is_tty", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return Value{stream_is_tty(stdout)};
        })
        .func("is_interactive", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return Value{stream_is_tty(stdin)};
        })
        .func("prompt_integer", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& prompt_text = expect_string(args[0], "Console.prompt_integer", loc);

            std::cout << prompt_text << std::flush;

            std::string line{};

            if (!std::getline(std::cin, line)) {
                return make_failure_value(
                    error_msg("Console", "prompt_integer", "end of input or read error"));
            }

            const std::string trimmed = trim_ascii(line);

            try {
                std::size_t pos = 0;
                const auto value = std::stoll(trimmed, &pos);

                if (pos == trimmed.size() && !trimmed.empty()) {
                    return make_success_value(Value{static_cast<std::int64_t>(value)});
                }
            } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
                // Fall through to the failure result below.
            }

            return make_failure_value(error_msg(
                "Console", "prompt_integer", std::format("'{}' is not a whole number", trimmed)));
        })
        .func("prompt_number", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& prompt_text = expect_string(args[0], "Console.prompt_number", loc);

            std::cout << prompt_text << std::flush;

            std::string line{};

            if (!std::getline(std::cin, line)) {
                return make_failure_value(
                    error_msg("Console", "prompt_number", "end of input or read error"));
            }

            const std::string trimmed = trim_ascii(line);

            try {
                std::size_t pos = 0;
                const auto value = std::stod(trimmed, &pos);

                if (pos == trimmed.size() && !trimmed.empty()) {
                    return make_success_value(Value{value});
                }
            } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
                // Fall through to the failure result below.
            }

            return make_failure_value(error_msg("Console", "prompt_number",
                                                std::format("'{}' is not a number", trimmed)));
        })
        .func("confirm", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& prompt_text = expect_string(args[0], "Console.confirm", loc);

            std::cout << prompt_text << std::flush;

            std::string line{};

            if (!std::getline(std::cin, line)) {
                return make_failure_value(
                    error_msg("Console", "confirm", "end of input or read error"));
            }

            std::string answer = trim_ascii(line);
            std::ranges::transform(answer, answer.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (answer == "y" || answer == "yes") {
                return make_success_value(Value{true});
            }

            if (answer == "n" || answer == "no") {
                return make_success_value(Value{false});
            }

            return make_failure_value(error_msg(
                "Console", "confirm", std::format("'{}' is not yes or no", trim_ascii(line))));
        })
        .func("prompt_with_default", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& prompt_text =
                expect_string(args[0], "Console.prompt_with_default", loc);
            const std::string& default_value =
                expect_string(args[1], "Console.prompt_with_default", loc);

            std::cout << prompt_text << std::flush;

            std::string line{};

            if (!std::getline(std::cin, line)) {
                return make_failure_value(
                    error_msg("Console", "prompt_with_default", "end of input or read error"));
            }

            if (line.size() > ResourceLimits::max_string_size) {
                return make_failure_value(
                    error_msg("Console", "prompt_with_default", "input line exceeds maximum size"));
            }

            // An empty line accepts the default; otherwise return the entered text.
            if (line.empty()) {
                return make_success_value(Value{default_value});
            }

            return make_success_value(Value{std::move(line)});
        });
}

} // namespace luma
