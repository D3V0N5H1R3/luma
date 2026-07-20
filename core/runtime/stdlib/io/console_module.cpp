#include "runtime/stdlib/io/console_module.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <iostream>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

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
        });
}

} // namespace luma
