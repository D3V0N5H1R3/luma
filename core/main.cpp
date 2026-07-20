#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "common/resource_limits.hpp"
#include "common/version.hpp"
#include "runtime/cli/cli.hpp"
#include "runtime/cli/cli_args.hpp"
#include "runtime/cli/cli_commands.hpp"
#include "runtime/cli/terminal.hpp"
#include "runtime/interpreter/control_flow.hpp"
#include "runtime/repl/repl.hpp"

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

    auto parsed = luma::parse_args(argc, argv);

    if (!parsed) {
        return luma::exit_code::usage_error;
    }

    const auto& args = *parsed;

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
