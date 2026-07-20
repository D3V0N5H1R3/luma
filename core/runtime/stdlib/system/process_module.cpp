#include "runtime/stdlib/system/process_module.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/platform_utils.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/control_flow.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/stdlib_error_helpers.hpp"
#include "runtime/stdlib/system/platform_process.hpp"

namespace luma {
namespace {

std::vector<std::string> program_args;

} // namespace

void set_program_args(std::vector<std::string> args) {
    program_args = std::move(args);
}

std::vector<std::string> tokenize_command(std::string_view cmd) {
    std::vector<std::string> args{};
    std::string current{};
    bool in_double_quote{false};
    bool in_single_quote{false};

    for (std::size_t i{0}; i < cmd.size(); ++i) {
        const char c = cmd[i];

        if (c == '\\' && !in_single_quote && i + 1 < cmd.size()) {
            current += cmd[++i];
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if ((std::isspace(static_cast<unsigned char>(c)) != 0) && !in_double_quote &&
                   !in_single_quote) {
            if (!current.empty()) {
                args.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        args.push_back(std::move(current));
    }

    if (in_double_quote || in_single_quote) {
        throw RuntimeError{"Process.run: unclosed quote in command string",
                           {},
                           "the command string has mismatched quotes"};
    }

    return args;
}

void register_process_ns(const EnvPtr& env) {
    ModuleBuilder{"Process", env}
        .func("get_arguments", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            auto arr = std::make_shared<ArrayValue>();
            arr->elements->reserve(program_args.size());

            for (const auto& arg : program_args) {
                arr->elements->emplace_back(arg);
            }

            return Value{std::move(arr)};
        })
        .func("exit", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto code = expect_integer(args[0], "Process.exit", loc);
            if (code < std::numeric_limits<int>::min() || code > std::numeric_limits<int>::max()) {
                throw RuntimeError{"Process.exit: exit code out of range", loc,
                                   "pass an exit code that fits in a 32-bit integer"};
            }

            throw ExitSignal{static_cast<int>(code)};
        })
        .func("get_environment_variable", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& name = expect_string(args[0], "Process.get_environment_variable", loc);

            auto val = safe_getenv(name.c_str());

            if (val) {
                return make_success_value(Value{std::move(*val)});
            }

            return make_failure_value("environment variable not set");
        })
        .func("current_directory", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return wrap_result_operation("Process", "current_directory", [&]() -> Value {
                return make_success_value(Value{std::filesystem::current_path().string()});
            });
        })
        .func("has_environment_variable", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& name = expect_string(args[0], "Process.has_environment_variable", loc);

            return Value{safe_getenv(name.c_str()).has_value()};
        })
        .func("set_environment_variable", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            constexpr std::string_view fn_name = "Process.set_environment_variable";
            const auto& name = expect_string(args[0], fn_name, loc);
            const auto& value = expect_string(args[1], fn_name, loc);

            if (name.empty()) {
                return make_failure_value("environment variable name must not be empty");
            }

            if (name.find('=') != std::string::npos || name.find('\0') != std::string::npos) {
                return make_failure_value("environment variable name contains "
                                          "invalid characters ('=' or null)");
            }

            if (name.size() > ResourceLimits::max_env_size ||
                value.size() > ResourceLimits::max_env_size) {
                return make_failure_value("environment variable name or value "
                                          "exceeds maximum size");
            }

            const int rc = platform_process::set_environment_variable(name, value);

            if (rc == 0) {
                return make_success_value(Value{NullValue{}});
            }

            return make_failure_value("failed to set environment variable");
        })
        .func("run", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& cmd = expect_string(args[0], "Process.run", loc);

            if (cmd.empty()) {
                return make_failure_value("command string must not be empty");
            }

            try {
                const auto [exit_code, output] = platform_process::execute_command(cmd);

                if (exit_code < 0) {
                    return make_failure_value("failed to execute command");
                }

                auto rec = std::make_shared<RecordValue>();
                rec->type_name = "ProcessResult";
                rec->fields.emplace_back("exit_code", Value{static_cast<std::int64_t>(exit_code)});
                rec->fields.emplace_back("output", Value{output});

                return make_success_value(Value{std::move(rec)});
            } catch (const RuntimeError& e) {
                return failure_from_exception(e);
            }
        })
        .func("get_process_id", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return Value{platform_process::current_process_id()};
        })
        .func("get_all_environment_variables", 0)
        .raw_body(
            [](std::span<const Value> /*args*/, [[maybe_unused]] SourceLocation loc) -> Value {
                auto dict = std::make_shared<DictionaryValue>();
                // Pre-build the empty hash index so each set() below is O(1),
                // keeping the build O(n) rather than O(n^2).
                dict->rebuild_index();

                auto entries = platform_process::all_environment_variables();

                if (!entries) {
                    throw RuntimeError{"Process.get_all_environment_variables: "
                                       "failed to read environment variables",
                                       loc};
                }

                for (auto& [name, value] : *entries) {
                    dict->set(name, Value{std::move(value)});
                }

                return Value{std::move(dict)};
            });
}

} // namespace luma
