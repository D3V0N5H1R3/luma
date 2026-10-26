#ifndef LUMA_CLI_CLI_ARGS_HPP
#define LUMA_CLI_CLI_ARGS_HPP

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/string_utils.hpp"
#include "runtime/cli/cli.hpp"

namespace luma {

// Forward declaration for FlagDescriptor.
struct ParsedArgs;

/// The command the user requested via CLI flags or subcommands.
enum class Command {
    Run,     ///< Default: run a Luma source file.
    Help,    ///< Print usage information.
    Version, ///< Print the version number.
    Repl,    ///< Start the interactive REPL.
    Eval,    ///< Evaluate a program read from standard input.
    Test,    ///< Run test annotations in a file.
    Check,   ///< Type-check without running.
    Pkg,     ///< Project initialisation subcommand (luma init).
};

/// Describes a single CLI flag with its long form, optional short form,
/// description for help text, and an optional function that applies the flag to ParsedArgs.
struct FlagDescriptor {
    std::string_view long_form;                            ///< e.g. "--help"
    std::string_view short_form;                           ///< e.g. "-h" (empty if none)
    std::string_view description;                          ///< Help text shown in usage output.
    std::optional<std::function<void(ParsedArgs&)>> apply; ///< Applies this flag to ParsedArgs.
};

// Parsed command-line arguments.
struct ParsedArgs {
    Command command{Command::Run};
    bool strict{false};
    bool sandbox{false};
    bool verify{false};
    int optimize{1};

    std::string file_path{};
    std::vector<std::string> program_args{};

    // Convert parsed arguments to RunOptions for program execution.
    [[nodiscard]] RunOptions to_run_options() const {
        return {
            .args = program_args,
            .strict = strict,
            .sandbox = sandbox,
            .optimize_level = static_cast<OptimizationLevel>(optimize),
            .verify = verify,
        };
    }
};

/// Single source of truth for all CLI flags.
/// Order determines help text display order.
inline const std::array k_flag_descriptors = std::to_array<FlagDescriptor>({
    {.long_form = "--help",
     .short_form = "-h",
     .description = "Print this help message and exit",
     .apply = [](ParsedArgs& a) { a.command = Command::Help; }},
    {.long_form = "--version",
     .short_form = "-v",
     .description = "Print the version number and exit",
     .apply = [](ParsedArgs& a) { a.command = Command::Version; }},
    {.long_form = "--repl",
     .short_form = "-r",
     .description = "Start the interactive REPL",
     .apply = [](ParsedArgs& a) { a.command = Command::Repl; }},
    {.long_form = "--test",
     .short_form = "-t",
     .description = "Run test annotations in the file",
     .apply = [](ParsedArgs& a) { a.command = Command::Test; }},
    {.long_form = "--eval",
     .short_form = "-e",
     .description = "Evaluate a program read from standard input",
     .apply = [](ParsedArgs& a) { a.command = Command::Eval; }},
    {.long_form = "--check",
     .short_form = "-c",
     .description = "Type-check the file without running it",
     .apply = [](ParsedArgs& a) { a.command = Command::Check; }},
    {.long_form = "--strict",
     .short_form = "-s",
     .description = "Treat warnings as errors",
     .apply = [](ParsedArgs& a) { a.strict = true; }},
    {.long_form = "--box",
     .short_form = "-b",
     .description = "Run in sandbox mode (restricted I/O)",
     .apply = [](ParsedArgs& a) { a.sandbox = true; }},
    // The optimisation level (-O2 or -O 2) is consumed by try_consume_optimize()
    // before the flag table is consulted, so this entry needs no apply callback;
    // it exists only for help text and flag suggestions.
    {.long_form = "--optimize",
     .short_form = "-O",
     .description = "Enable bytecode optimisation",
     .apply = std::nullopt},
    {.long_form = "--verify",
     .short_form = "",
     .description = "Verify bytecode after compilation",
     .apply = [](ParsedArgs& a) { a.verify = true; }},
});

/// Returns true if the given string matches any known flag (long or short form).
[[nodiscard]] bool is_known_flag(std::string_view flag);

// Suggest the closest known CLI flag for a mistyped flag.
// Returns an empty view if no close match exists (distance > 2).
[[nodiscard]] std::string_view suggest_flag(std::string_view unknown) noexcept;

// Parse command-line arguments into a ParsedArgs struct.
// Returns a ParsedArgs on success, or prints an error and returns std::nullopt.
//
// Luma's own flags are recognised anywhere — before or after the .luma file.
// After the file, any other argument is forwarded to the program
// (Process.get_arguments()); a literal `--` forwards everything after it
// verbatim, including arguments that look like Luma flags.  Before the file,
// an unknown `-x` option or a bad optimisation level is a usage error (an
// unknown option with a "did you mean" suggestion).  After the file only a
// valid level (-O0/-O1/-O2, or -O/--optimize followed by 0, 1, or 2) is Luma's;
// any other `-O...` argument, such as `-O3` or `-Output`, is the program's.
[[nodiscard]] std::optional<ParsedArgs> parse_args(int argc, char* argv[]);

// Validate the source-file argument of a parsed command line before dispatch:
// --test and --check require a file, and a file to run, test, or check must
// have the .luma extension and exist as a regular file.  Returns the error
// message for a usage error (exit code 5), or std::nullopt when valid.
[[nodiscard]] std::optional<std::string> validate_file_argument(const ParsedArgs& args);

} // namespace luma

#endif // LUMA_CLI_CLI_ARGS_HPP
