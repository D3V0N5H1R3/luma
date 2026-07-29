#include "runtime/stdlib/system/process_module.hpp"

#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

#ifndef _WIN32
#include <unistd.h> // access(X_OK) — POSIX executable-bit test in find_executable
#endif

namespace luma {
namespace {

std::vector<std::string> program_args;

// Wraps a Process.ExitStatus variant name in a ChoiceValue.  The runtime short
// name "ExitStatus" matches how the type checker registers the choice from
// stdlib_type_arities.cpp; the variant names must match that declaration.
[[nodiscard]] Value make_exit_status_choice(std::string_view variant,
                                            std::optional<std::int64_t> code = std::nullopt) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "ExitStatus";
    cv->variant = std::string{variant};

    if (code) {
        cv->fields.emplace_back(*code);
    }

    return Value{std::move(cv)};
}

// Wraps a Process.Error variant name in a ChoiceValue.  The runtime short name
// "Error" matches how the type checker registers the choice from
// stdlib_type_arities.cpp; the four variant names must match that declaration.
[[nodiscard]] Value make_process_error_choice(std::string_view variant) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "Error";
    cv->variant = std::string{variant};

    return Value{std::move(cv)};
}

// Maps a POSIX-style launch errno (see CapturedOutput::launch_errno) to its
// Process.Error variant name.  ENOENT → NotFound (program not found), EACCES /
// EPERM → PermissionDenied, ENOEXEC → InvalidCommand (a file that is not a valid
// executable); anything else — including a generic spawn failure — is a
// LaunchFailed.  EINVAL is deliberately NOT mapped to InvalidCommand: it is the
// generic sentinel the Windows layer (launch_errno_from_win32) returns for an
// unclassified CreateProcess failure, so it must reach the LaunchFailed default
// rather than falsely claim the target is not an executable.
[[nodiscard]] std::string_view process_error_variant(int launch_errno) {
    switch (launch_errno) {
        case ENOENT:
            return "NotFound";
        case EACCES:
        case EPERM:
            return "PermissionDenied";
        case ENOEXEC:
            return "InvalidCommand";
        default:
            return "LaunchFailed";
    }
}

// Maps a Process.Signal choice variant name to the platform SignalKind consumed
// by platform_process::send_signal.  The four variant names must match the
// Process.Signal choice declared in stdlib_type_arities.cpp exactly.
[[nodiscard]] std::optional<platform_process::SignalKind>
signal_kind_from_variant(std::string_view variant) {
    if (variant == "Terminate") {
        return platform_process::SignalKind::Terminate;
    }
    if (variant == "Kill") {
        return platform_process::SignalKind::Kill;
    }
    if (variant == "Interrupt") {
        return platform_process::SignalKind::Interrupt;
    }
    if (variant == "Hangup") {
        return platform_process::SignalKind::Hangup;
    }
    return std::nullopt;
}

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
        // Process.signal(pid, signal) -> result<boolean>
        // Sends a portable termination request to another process.  On POSIX the
        // Process.Signal variant maps to SIGTERM / SIGKILL / SIGINT / SIGHUP; on
        // Windows the mapping is lossy (see platform_process::SignalKind).  Returns
        // success(true) when the OS accepted the request, or a failure result when
        // it did not (e.g. no such process, or insufficient permission).
        .func("signal", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto pid = expect_integer(args[0], "Process.signal", loc);

            if (!args[1].is_choice()) {
                throw RuntimeError{"Process.signal: expected a Process.Signal choice", loc,
                                   "pass a Process.Signal variant, e.g. Process.Signal.Terminate"};
            }

            const auto& variant = args[1].as_choice()->variant;
            const auto kind = signal_kind_from_variant(variant);

            if (!kind) {
                throw RuntimeError{
                    std::format("Process.signal: unknown signal 'Process.Signal.{}'", variant), loc,
                    "use a Process.Signal variant: Terminate, Kill, Interrupt, Hangup"};
            }

            if (pid <= 0) {
                return make_failure_value(
                    error_msg("Process", "signal", "pid must be a positive process id"));
            }

            if (!platform_process::send_signal(pid, *kind)) {
                return make_failure_value(error_msg(
                    "Process", "signal", std::format("could not signal process {}", pid)));
            }

            return make_success_value(Value{true});
        })
        // Process.exit_status(output) -> Process.ExitStatus
        // Classifies a Process.CommandOutput's exit_code sign convention into an
        // exhaustive, match-able type: 0 = Success, a positive code = Failed(code)
        // (the process ran and exited non-zero), a negative code = LaunchFailed
        // (the process never ran — see platform_process::CapturedOutput). Pure
        // classifier: it never re-runs or re-captures anything, it only inspects
        // the exit_code field already present on the record.
        .func("exit_status", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record()) {
                throw RuntimeError{"Process.exit_status: expected a Process.CommandOutput record",
                                   loc,
                                   "pass the record returned by Process.execute / run_command"};
            }

            const auto& rec = args[0].as_record();
            const Value* exit_code_field = rec->find_field("exit_code");

            if (exit_code_field == nullptr || !exit_code_field->is_integer()) {
                throw RuntimeError{"Process.exit_status: expected a Process.CommandOutput record",
                                   loc,
                                   "pass the record returned by Process.execute / run_command"};
            }

            const auto exit_code = exit_code_field->as_integer();

            if (exit_code == 0) {
                return make_exit_status_choice("Success");
            }

            if (exit_code < 0) {
                return make_exit_status_choice("LaunchFailed");
            }

            return make_exit_status_choice("Failed", exit_code);
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
        // Process.find_executable(name) -> optional<string>
        // Scans PATH (honouring PATHEXT on Windows) for an executable, returning
        // its absolute path or none.  A read-only query — it never launches
        // anything — so it complements Process.Error.NotFound.
        .func("find_executable", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& name = expect_string(args[0], "Process.find_executable", loc);

            if (name.empty()) {
                return Value{NullValue{}};
            }

            namespace fs = std::filesystem;

#ifdef _WIN32
            constexpr char k_path_separator = ';';
            const bool has_dir = name.find_first_of("/\\") != std::string::npos;

            // Candidate extensions from PATHEXT, plus an empty one so a name that
            // already carries its extension still matches.
            std::vector<std::string> extensions{""};
            {
                const auto pathext =
                    safe_getenv("PATHEXT").value_or(".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WSF");
                std::size_t start{0};

                while (start <= pathext.size()) {
                    const auto end = pathext.find(';', start);
                    auto ext = pathext.substr(start, end == std::string::npos ? std::string::npos
                                                                              : end - start);

                    if (!ext.empty()) {
                        extensions.push_back(std::move(ext));
                    }

                    if (end == std::string::npos) {
                        break;
                    }

                    start = end + 1;
                }
            }
#else
            constexpr char k_path_separator = ':';
            const bool has_dir = name.find('/') != std::string::npos;
            const std::vector<std::string> extensions{""};
#endif

            // Return the absolute path of `dir/name` (with each candidate
            // extension) when it names an existing regular file that the process
            // can execute, else nullopt.  On Windows executability is expressed
            // by the PATHEXT extension list; on POSIX it is the X_OK permission
            // bit, so a non-executable regular file does not shadow a real
            // binary that appears later on PATH (matching `which` / execvp).
            const auto probe = [&](const fs::path& dir) -> std::optional<std::string> {
                for (const auto& ext : extensions) {
                    fs::path candidate = dir / (name + ext);
                    std::error_code ec;

                    if (!fs::is_regular_file(candidate, ec)) {
                        continue;
                    }

#ifndef _WIN32
                    if (::access(candidate.c_str(), X_OK) != 0) {
                        continue;
                    }
#endif

                    auto resolved = fs::weakly_canonical(candidate, ec);

                    return (ec ? fs::absolute(candidate, ec) : resolved).string();
                }

                return std::nullopt;
            };

            // A name that already contains a directory component is resolved
            // relative to the working directory, not searched on PATH.
            if (has_dir) {
                if (auto found = probe(fs::current_path())) {
                    return Value{std::move(*found)};
                }

                // The name may itself be an absolute/relative path prefix.
                if (auto found = probe(fs::path{})) {
                    return Value{std::move(*found)};
                }

                return Value{NullValue{}};
            }

            const auto path_env = safe_getenv("PATH");

            if (!path_env) {
                return Value{NullValue{}};
            }

            std::size_t start{0};

            while (start <= path_env->size()) {
                const auto end = path_env->find(k_path_separator, start);
                const auto dir = path_env->substr(
                    start, end == std::string::npos ? std::string::npos : end - start);

                if (!dir.empty()) {
                    if (auto found = probe(fs::path{dir})) {
                        return Value{std::move(*found)};
                    }
                }

                if (end == std::string::npos) {
                    break;
                }

                start = end + 1;
            }

            return Value{NullValue{}};
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
        .func("execute", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& cmd = expect_string(args[0], "Process.execute", loc);

            if (cmd.empty()) {
                return make_failure_value("command string must not be empty");
            }

            try {
                auto captured = platform_process::execute_command_captured(cmd);

                if (captured.exit_code < 0) {
                    return make_failure_value("failed to execute command");
                }

                auto rec = std::make_shared<RecordValue>();
                rec->type_name = "CommandOutput";
                rec->fields.emplace_back("exit_code",
                                         Value{static_cast<std::int64_t>(captured.exit_code)});
                rec->fields.emplace_back("standard_output",
                                         Value{std::move(captured.standard_output)});
                rec->fields.emplace_back("standard_error",
                                         Value{std::move(captured.standard_error)});
                rec->fields.emplace_back("success", Value{captured.exit_code == 0});

                return make_success_value(Value{std::move(rec)});
            } catch (const RuntimeError& e) {
                return failure_from_exception(e);
            }
        })
        .func("command", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& program = expect_string(args[0], "Process.command", loc);

            if (!args[1].is_array()) {
                throw RuntimeError{"Process.command: arguments must be an array of strings", loc,
                                   "pass an array<string> of arguments"};
            }

            auto arguments = std::make_shared<ArrayValue>();

            // Copy and validate each argument so the record only ever holds
            // strings — Process.run_command relies on that when building argv.
            for (const auto& element : *args[1].as_array()->elements) {
                if (!element.is_string()) {
                    throw RuntimeError{"Process.command: every argument must be a string", loc,
                                       "pass an array<string> of arguments"};
                }

                arguments->elements->emplace_back(element);
            }

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "Command";
            rec->fields.emplace_back("program", Value{program});
            rec->fields.emplace_back("arguments", Value{std::move(arguments)});

            return Value{std::move(rec)};
        })
        .func("run_command", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record()) {
                throw RuntimeError{"Process.run_command: expected a Process.Command record", loc,
                                   "build one with Process.command(program, arguments)"};
            }

            const auto& rec = args[0].as_record();
            const Value* program = rec->find_field("program");
            const Value* arguments = rec->find_field("arguments");

            if (program == nullptr || !program->is_string() || arguments == nullptr ||
                !arguments->is_array()) {
                throw RuntimeError{"Process.run_command: expected a Process.Command record", loc,
                                   "build one with Process.command(program, arguments)"};
            }

            if (program->as_string().empty()) {
                return make_failure_value("program must not be empty");
            }

            // Build the argv vector verbatim (argv[0] is the program): no shell,
            // no tokenization, so metacharacters in any argument are inert.
            std::vector<std::string> argv;
            argv.reserve(arguments->as_array()->elements->size() + 1);
            argv.push_back(program->as_string());

            for (const auto& element : *arguments->as_array()->elements) {
                if (!element.is_string()) {
                    return make_failure_value("every argument must be a string");
                }

                argv.push_back(element.as_string());
            }

            try {
                auto captured = platform_process::execute_argv_captured(std::move(argv));

                if (captured.exit_code < 0) {
                    return make_failure_value("failed to execute command");
                }

                auto output = std::make_shared<RecordValue>();
                output->type_name = "CommandOutput";
                output->fields.emplace_back("exit_code",
                                            Value{static_cast<std::int64_t>(captured.exit_code)});
                output->fields.emplace_back("standard_output",
                                            Value{std::move(captured.standard_output)});
                output->fields.emplace_back("standard_error",
                                            Value{std::move(captured.standard_error)});
                output->fields.emplace_back("success", Value{captured.exit_code == 0});

                return make_success_value(Value{std::move(output)});
            } catch (const RuntimeError& e) {
                return failure_from_exception(e);
            }
        })
        // Process.run_command_typed(command) -> result<Process.CommandOutput, Process.Error>
        // Opt-in typed-error variant of run_command: a *launch* failure is surfaced
        // as a Process.Error choice (NotFound / PermissionDenied / InvalidCommand /
        // LaunchFailed) rather than an opaque string, so a program can distinguish
        // "the program isn't installed" from "it ran and exited non-zero" (the latter
        // is still reported as a successful result carrying the CommandOutput, whose
        // exit_code Process.exit_status classifies).  Mirrors FileSystem.read_file_typed;
        // the string-error run_command is left untouched.
        .func("run_command_typed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record()) {
                throw RuntimeError{"Process.run_command_typed: expected a Process.Command record",
                                   loc, "build one with Process.command(program, arguments)"};
            }

            const auto& rec = args[0].as_record();
            const Value* program = rec->find_field("program");
            const Value* arguments = rec->find_field("arguments");

            if (program == nullptr || !program->is_string() || arguments == nullptr ||
                !arguments->is_array()) {
                throw RuntimeError{"Process.run_command_typed: expected a Process.Command record",
                                   loc, "build one with Process.command(program, arguments)"};
            }

            // An empty program name or a non-string argument is a malformed
            // command — a typed InvalidCommand rather than an attempted launch.
            if (program->as_string().empty()) {
                return Value{ResultValue::failure(make_process_error_choice("InvalidCommand"))};
            }

            std::vector<std::string> argv;
            argv.reserve(arguments->as_array()->elements->size() + 1);
            argv.push_back(program->as_string());

            for (const auto& element : *arguments->as_array()->elements) {
                if (!element.is_string()) {
                    return Value{ResultValue::failure(make_process_error_choice("InvalidCommand"))};
                }

                argv.push_back(element.as_string());
            }

            try {
                auto captured = platform_process::execute_argv_captured(std::move(argv));

                if (captured.exit_code < 0) {
                    // A negative exit code means the process never ran; classify
                    // the launch failure from the captured errno.
                    return Value{ResultValue::failure(
                        make_process_error_choice(process_error_variant(captured.launch_errno)))};
                }

                auto output = std::make_shared<RecordValue>();
                output->type_name = "CommandOutput";
                output->fields.emplace_back("exit_code",
                                            Value{static_cast<std::int64_t>(captured.exit_code)});
                output->fields.emplace_back("standard_output",
                                            Value{std::move(captured.standard_output)});
                output->fields.emplace_back("standard_error",
                                            Value{std::move(captured.standard_error)});
                output->fields.emplace_back("success", Value{captured.exit_code == 0});

                return make_success_value(Value{std::move(output)});
            } catch (const RuntimeError&) {
                // A thrown RuntimeError here is a launch-level failure (e.g. a
                // mismatched-quote tokenization error never happens on the argv
                // path); report it as a generic typed LaunchFailed.
                return Value{ResultValue::failure(make_process_error_choice("LaunchFailed"))};
            }
        })
        // Process.run_command_timeout(command, timeout_ms) -> result<Process.CommandOutput>
        // Timeout-bounded variant of run_command: runs the shell-free Command and,
        // if it has not exited within timeout_ms milliseconds, forcibly kills it
        // (SIGKILL on POSIX, TerminateProcess on Windows) and returns a failure.
        // This is the safety affordance that prevents a hung or runaway child (a
        // stuck network call, a program awaiting input, an infinite loop) from
        // freezing the Luma program forever.  A command that exits within the
        // deadline returns the same result<Process.CommandOutput> as run_command
        // (a negative exit code — surfaced as failure — signals a launch failure).
        .func("run_command_timeout", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record()) {
                throw RuntimeError{"Process.run_command_timeout: expected a Process.Command record",
                                   loc, "build one with Process.command(program, arguments)"};
            }

            const auto& rec = args[0].as_record();
            const Value* program = rec->find_field("program");
            const Value* arguments = rec->find_field("arguments");

            if (program == nullptr || !program->is_string() || arguments == nullptr ||
                !arguments->is_array()) {
                throw RuntimeError{"Process.run_command_timeout: expected a Process.Command record",
                                   loc, "build one with Process.command(program, arguments)"};
            }

            const auto timeout_ms = expect_integer(args[1], "Process.run_command_timeout", loc);

            if (timeout_ms <= 0) {
                return make_failure_value(error_msg("Process", "run_command_timeout",
                                                    "timeout_ms must be a positive number of "
                                                    "milliseconds"));
            }

            if (program->as_string().empty()) {
                return make_failure_value("program must not be empty");
            }

            // Build the argv vector verbatim (argv[0] is the program): no shell,
            // no tokenization, so metacharacters in any argument are inert.
            std::vector<std::string> argv;
            argv.reserve(arguments->as_array()->elements->size() + 1);
            argv.push_back(program->as_string());

            for (const auto& element : *arguments->as_array()->elements) {
                if (!element.is_string()) {
                    return make_failure_value("every argument must be a string");
                }

                argv.push_back(element.as_string());
            }

            try {
                auto captured =
                    platform_process::execute_argv_captured_timeout(std::move(argv), timeout_ms);

                if (captured.timed_out) {
                    return make_failure_value(
                        error_msg("Process", "run_command_timeout",
                                  std::format("command timed out after {} ms", timeout_ms)));
                }

                if (captured.exit_code < 0) {
                    return make_failure_value("failed to execute command");
                }

                auto output = std::make_shared<RecordValue>();
                output->type_name = "CommandOutput";
                output->fields.emplace_back("exit_code",
                                            Value{static_cast<std::int64_t>(captured.exit_code)});
                output->fields.emplace_back("standard_output",
                                            Value{std::move(captured.standard_output)});
                output->fields.emplace_back("standard_error",
                                            Value{std::move(captured.standard_error)});
                output->fields.emplace_back("success", Value{captured.exit_code == 0});

                return make_success_value(Value{std::move(output)});
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
