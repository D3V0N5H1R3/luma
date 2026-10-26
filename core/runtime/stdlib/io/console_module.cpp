#include "runtime/stdlib/io/console_module.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <format>
#include <iostream>
#include <istream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "common/result.hpp"
#include "common/string_utils.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/platform_terminal.hpp"

namespace luma {

namespace {

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

// Prints `prompt_text` (flushed, so it appears before the blocking read) and then
// reads one bounded line from stdin for Console.<function>.  Returns the line,
// or the function's failure result on end of input / a read error / an
// oversized line.  The single read path behind every line-oriented Console
// function, so the size bound cannot be forgotten by one of them.
[[nodiscard]] Result<std::string, Value>
read_console_line(std::string_view function, std::optional<std::string_view> prompt_text) {
    if (prompt_text) {
        std::cout << *prompt_text << std::flush;
    }

    std::string line{};

    switch (console_detail::read_bounded_line(std::cin, ResourceLimits::max_string_size, line)) {
        case console_detail::LineStatus::Ok:
            return Result<std::string, Value>::ok(std::move(line));
        case console_detail::LineStatus::EndOfInput:
            return Result<std::string, Value>::err(
                make_failure_value(error_msg("Console", function, "end of input or read error")));
        case console_detail::LineStatus::TooLong:
            break;
    }

    return Result<std::string, Value>::err(
        make_failure_value(error_msg("Console", function, "input line exceeds maximum size")));
}

} // namespace

namespace console_detail {

LineStatus read_bounded_line(std::istream& in, std::size_t limit, std::string& line) {
    using traits = std::char_traits<char>;

    line.clear();

    // Same entry protocol as std::getline: a sentry that does not skip
    // whitespace, then raw streambuf access.
    const std::istream::sentry guard{in, true};

    if (!guard) {
        return LineStatus::EndOfInput;
    }

    std::streambuf* const buffer = in.rdbuf();
    bool extracted{false};

    while (true) {
        const traits::int_type next = buffer->sbumpc();

        if (traits::eq_int_type(next, traits::eof())) {
            // Like std::getline: a final unterminated line is still a line; only
            // an empty extraction at end of input is a failure.
            in.setstate(extracted ? std::ios::eofbit : (std::ios::eofbit | std::ios::failbit));

            return extracted ? LineStatus::Ok : LineStatus::EndOfInput;
        }

        extracted = true;

        const char ch = traits::to_char_type(next);

        if (ch == '\n') {
            return LineStatus::Ok;
        }

        if (line.size() >= limit) {
            // Stop buffering at the bound and discard (without storing) the rest
            // of the oversized line, so the next read starts on a fresh line.
            in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            return LineStatus::TooLong;
        }

        line.push_back(ch);
    }
}

} // namespace console_detail

void register_console_ns(const EnvPtr& env) {
    ModuleBuilder{"Console", env}
        .func("prompt", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& prompt_text = expect_string(args[0], "Console.prompt", loc);

            auto line = read_console_line("prompt", prompt_text);

            if (!line.is_ok()) {
                return std::move(line).error();
            }

            return make_success_value(Value{std::move(line).value()});
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
            auto line = read_console_line("read_line", std::nullopt);

            if (!line.is_ok()) {
                return std::move(line).error();
            }

            return make_success_value(Value{std::move(line).value()});
        })
        .func("read_lines", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            // Reads all of stdin split into lines, mirroring FileSystem.read_lines.
            auto arr = std::make_shared<ArrayValue>();

            std::string line{};
            std::size_t total_bytes = 0;

            while (true) {
                // Bound each line by the budget that remains, so a single huge
                // newline-free line can never be buffered past the limit.
                const auto status = console_detail::read_bounded_line(
                    std::cin, ResourceLimits::max_string_size - total_bytes, line);

                if (status == console_detail::LineStatus::EndOfInput) {
                    break;
                }

                if (status == console_detail::LineStatus::TooLong) {
                    return make_failure_value(
                        error_msg("Console", "read_lines", "input exceeds maximum string size"));
                }

                total_bytes += line.size();

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
            return Value{platform_terminal::stdout_is_terminal()};
        })
        .func("is_interactive", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return Value{platform_terminal::stdin_is_terminal()};
        })
        .func("prompt_integer", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& prompt_text = expect_string(args[0], "Console.prompt_integer", loc);

            auto line = read_console_line("prompt_integer", prompt_text);

            if (!line.is_ok()) {
                return std::move(line).error();
            }

            const std::string trimmed = trim(line.value());

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

            auto line = read_console_line("prompt_number", prompt_text);

            if (!line.is_ok()) {
                return std::move(line).error();
            }

            const std::string trimmed = trim(line.value());

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

            auto line = read_console_line("confirm", prompt_text);

            if (!line.is_ok()) {
                return std::move(line).error();
            }

            const std::string trimmed = trim(line.value());
            std::string answer = trimmed;
            std::ranges::transform(answer, answer.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (answer == "y" || answer == "yes") {
                return make_success_value(Value{true});
            }

            if (answer == "n" || answer == "no") {
                return make_success_value(Value{false});
            }

            return make_failure_value(
                error_msg("Console", "confirm", std::format("'{}' is not yes or no", trimmed)));
        })
        .func("prompt_with_default", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& prompt_text =
                expect_string(args[0], "Console.prompt_with_default", loc);
            const std::string& default_value =
                expect_string(args[1], "Console.prompt_with_default", loc);

            auto line = read_console_line("prompt_with_default", prompt_text);

            if (!line.is_ok()) {
                return std::move(line).error();
            }

            // An empty line accepts the default; otherwise return the entered text.
            if (line.value().empty()) {
                return make_success_value(Value{default_value});
            }

            return make_success_value(Value{std::move(line).value()});
        });
}

} // namespace luma
