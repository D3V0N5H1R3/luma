#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// shellapi.h must follow windows.h.
#include <shellapi.h>
#endif

#include "common/platform_utils.hpp"
#include "common/resource_limits.hpp"
#include "common/terminal.hpp"
#include "common/version.hpp"
#include "runtime/cli/cli.hpp"
#include "runtime/cli/cli_args.hpp"
#include "runtime/cli/cli_commands.hpp"
#include "runtime/interpreter/control_flow.hpp"
#include "runtime/repl/repl.hpp"

namespace {

// The command-line arguments as UTF-8, the encoding every path and string in
// the interpreter uses.  On Windows the narrow `argv` is in the ANSI code page
// (a file name outside it arrives as '?'), so the command line is re-read as
// UTF-16 and converted; elsewhere `argv` is already UTF-8.
[[nodiscard]] std::vector<std::string> utf8_arguments(int argc, char* argv[]) {
#ifdef _WIN32
    int wide_argc{0};
    const std::unique_ptr<LPWSTR, decltype(&LocalFree)> wide_argv{
        CommandLineToArgvW(GetCommandLineW(), &wide_argc), &LocalFree};

    if (wide_argv) {
        std::vector<std::string> args;
        args.reserve(static_cast<std::size_t>(wide_argc));

        for (int i{0}; i < wide_argc; ++i) {
            args.push_back(luma::wstring_to_utf8(wide_argv.get()[i]));
        }

        return args;
    }
#endif
    return {argv, argv + argc};
}

} // namespace

// The function-try-block catches every exception from the body; the only
// residual throw is fatal-path logging in the catch handlers, where terminating
// is acceptable, so the check is silenced rather than wrapping std::cerr.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char* argv[]) try {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    luma::term::enable_ansi_escapes();
    luma::ResourceLimits::init_from_env();

    if (argc < 2) {
        luma::run_repl();

        return luma::exit_code::success;
    }

    // parse_args takes a mutable char* array; back it with UTF-8 strings that
    // outlive the parsed result.
    auto arg_storage = utf8_arguments(argc, argv);
    std::vector<char*> utf8_argv;
    utf8_argv.reserve(arg_storage.size() + 1);

    for (auto& arg : arg_storage) {
        utf8_argv.push_back(arg.data());
    }

    utf8_argv.push_back(nullptr);

    auto parsed = luma::parse_args(static_cast<int>(arg_storage.size()), utf8_argv.data());

    if (!parsed) {
        return luma::exit_code::usage_error;
    }

    const auto& args = *parsed;

    // Validate the source-file argument up front so a missing file, a wrong
    // extension, or a nonexistent path is a usage error (exit 5) rather than a
    // runtime failure surfacing from deep inside the loader.
    if (const auto error = luma::validate_file_argument(args)) {
        std::cerr << "error: " << *error << "\n";
        return luma::exit_code::usage_error;
    }

    switch (args.command) {
        case luma::Command::Help:
            luma::print_usage();
            return luma::exit_code::success;

        case luma::Command::Version:
            std::cout << "Luma " << luma::luma_version << "\n";
            return luma::exit_code::success;

        case luma::Command::Repl:
            luma::run_repl(args.sandbox);
            return luma::exit_code::success;

        case luma::Command::Eval:
            return luma::run_eval(args.sandbox);

        case luma::Command::Pkg:
            return luma::run_pkg_command(args);

        case luma::Command::Test:
            return luma::run_tests_file(args.file_path, args.strict, args.sandbox);

        case luma::Command::Check:
            return luma::check_file(args.file_path, args.strict);

        case luma::Command::Run:
            if (args.file_path.empty()) {
                luma::run_repl(args.sandbox);
                return luma::exit_code::success;
            }
            return luma::run_file(args.file_path, args.to_run_options());
    }

    return luma::exit_code::usage_error;
} catch (const luma::ExitSignal& e) {
    return e.code;
} catch (const std::exception& e) {
    std::cerr << "luma: fatal error: " << e.what() << "\n";
    return luma::exit_code::runtime_error;
} catch (...) {
    std::cerr << "luma: fatal error: unknown exception\n";
    return luma::exit_code::runtime_error;
}
