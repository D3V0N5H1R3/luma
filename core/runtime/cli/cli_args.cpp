#include "runtime/cli/cli_args.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <iostream>
#include <string_view>
#include <system_error>

#include "common/path_utils.hpp"

namespace {

// Valid optimization level range for -O flag (characters '0' through '2').
constexpr char k_min_optimize_level = '0';
constexpr char k_max_optimize_level = '2';

[[nodiscard]] bool matches_flag(const luma::FlagDescriptor& desc, std::string_view arg) {
    return desc.long_form == arg || (!desc.short_form.empty() && desc.short_form == arg);
}

// Try to parse an optimization level character. Returns the level (0–2) on
// success, or -1 if the character is not a valid level.
[[nodiscard]] constexpr int parse_optimize_char(char ch) {
    if (ch >= k_min_optimize_level && ch <= k_max_optimize_level) {
        return ch - '0';
    }
    return -1;
}

// Outcome of trying to consume the --optimize / -O flag.
enum class OptimizeParse {
    NotOptimize, // `arg` is not an optimisation flag.
    Consumed,    // Flag (and its value) consumed; args.optimize updated.
    Invalid,     // Flag present but its level is missing or invalid (error printed).
};

// Try to consume the --optimize / -O flag and its value. Handles both the
// combined form (-O2) and the separated form (-O 2 / --optimize 2).
//
// Before the .luma file, a missing or out-of-range level is a usage error
// rather than a warning: silently skipping it would let the bad value be taken
// as the file path (`-O 3 app.luma` would try to run a file named "3").  After
// the file, the argument might just as well be the program's own (`-Output`,
// `-O3` for a compiler the program drives), so only an unambiguous level —
// exactly -O0/-O1/-O2, or -O/--optimize followed by 0, 1, or 2 — is Luma's;
// anything else is reported as NotOptimize and forwarded to the program.
OptimizeParse try_consume_optimize(luma::ParsedArgs& args, std::string_view arg, int argc,
                                   char* argv[], int& i) {
    const bool after_file = !args.file_path.empty();

    // Combined form: -O0, -O1, -O2 (any other -O<suffix> is an invalid level).
    if (arg.size() > 2 && arg.starts_with("-O")) {
        const int level = arg.size() == 3 ? parse_optimize_char(arg[2]) : -1;

        if (level < 0) {
            if (after_file) {
                return OptimizeParse::NotOptimize;
            }

            std::cerr << "error: invalid optimization level '" << arg.substr(2)
                      << "'; expected 0, 1, or 2\n";
            return OptimizeParse::Invalid;
        }

        args.optimize = level;
        return OptimizeParse::Consumed;
    }

    if (arg != "--optimize" && arg != "-O") {
        return OptimizeParse::NotOptimize;
    }

    // Separated form: -O 2 or --optimize 2
    if (i + 1 >= argc) {
        if (after_file) {
            return OptimizeParse::NotOptimize;
        }

        std::cerr << "error: " << arg << " requires a level (0, 1, or 2)\n";
        return OptimizeParse::Invalid;
    }

    const std::string_view next{argv[i + 1]};
    const int level = next.size() == 1 ? parse_optimize_char(next[0]) : -1;

    if (level < 0) {
        if (after_file) {
            return OptimizeParse::NotOptimize;
        }

        std::cerr << "error: invalid optimization level '" << next << "'; expected 0, 1, or 2\n";
        return OptimizeParse::Invalid;
    }

    args.optimize = level;
    ++i;
    return OptimizeParse::Consumed;
}

} // namespace

namespace luma {

bool is_known_flag(std::string_view flag) {
    return std::ranges::any_of(k_flag_descriptors, [flag](const FlagDescriptor& desc) {
        return matches_flag(desc, flag);
    });
}

std::string_view suggest_flag(std::string_view unknown) noexcept {
    static constexpr std::size_t k_flag_suggestion_threshold = 3;

    std::string_view best{};
    std::size_t best_dist{k_flag_suggestion_threshold};

    for (const auto& desc : k_flag_descriptors) {
        const auto dist = levenshtein_distance(unknown, desc.long_form);

        if (dist < best_dist) {
            best_dist = dist;
            best = desc.long_form;
        }

        if (!desc.short_form.empty()) {
            const auto short_dist = levenshtein_distance(unknown, desc.short_form);

            if (short_dist < best_dist) {
                best_dist = short_dist;
                best = desc.short_form;
            }
        }
    }

    return best;
}

std::optional<ParsedArgs> parse_args(int argc, char* argv[]) {
    ParsedArgs args;
    // Set after a literal `--` (and for the `init`/`pkg` subcommand): every
    // remaining argument is forwarded verbatim, even ones that look like flags.
    bool forward_rest{false};

    for (int i{1}; i < argc; ++i) {
        const std::string_view arg{argv[i]};

        if (forward_rest) {
            args.program_args.emplace_back(arg);

            continue;
        }

        if (arg == "--") {
            forward_rest = true;

            continue;
        }

        // Luma's own flags are honoured anywhere on the command line, before
        // or after the .luma file, so `luma app.luma --test` tests the file.
        //
        // --optimize / -O (combined -O2 or separated -O 2) is parsed in one
        // place, before the flag table, so its value handling isn't split across
        // the descriptor lookup and the unknown-argument fallback.
        switch (try_consume_optimize(args, arg, argc, argv, i)) {
            case OptimizeParse::Consumed:
                continue;
            case OptimizeParse::Invalid:
                print_usage();
                return std::nullopt;
            case OptimizeParse::NotOptimize:
                break;
        }

        // Data-driven flag lookup.  The --optimize entry has no apply callback
        // (try_consume_optimize owns it) and is skipped: an -O it declined
        // after the file belongs to the program, not to a no-op match here.
        const auto it = std::ranges::find_if(k_flag_descriptors, [arg](const FlagDescriptor& desc) {
            return desc.apply.has_value() && matches_flag(desc, arg);
        });

        if (it != k_flag_descriptors.end()) {
            (*it->apply)(args);

            continue;
        }

        if (!args.file_path.empty()) {
            // Anything else after the file belongs to the program.
            args.program_args.emplace_back(arg);
        } else if (arg == "init" || arg == "pkg") {
            args.command = Command::Pkg;
            // Remaining args belong to the init subcommand.
            forward_rest = true;
        } else if (arg.starts_with('-')) {
            std::cerr << "error: unknown option '" << arg << "'";

            const auto suggestion = suggest_flag(arg);

            if (!suggestion.empty()) {
                std::cerr << "; did you mean '" << suggestion << "'?";
            }

            std::cerr << "\n";

            print_usage();

            return std::nullopt;
        } else {
            // First non-flag argument is the file path.
            args.file_path = std::string{arg};
        }
    }

    return args;
}

std::optional<std::string> validate_file_argument(const ParsedArgs& args) {
    const bool needs_file = args.command == Command::Test || args.command == Command::Check;
    const bool runs_file = args.command == Command::Run && !args.file_path.empty();

    if (args.file_path.empty()) {
        if (needs_file) {
            return std::format("{} requires a .luma file",
                               args.command == Command::Test ? "--test" : "--check");
        }

        if (args.command == Command::Run && !args.program_args.empty()) {
            return std::string{"program arguments given without a .luma file"};
        }

        return std::nullopt;
    }

    if (!needs_file && !runs_file) {
        return std::nullopt;
    }

    if (!has_luma_extension(args.file_path)) {
        return std::format("file must have a '{}' extension: '{}'", luma_extension, args.file_path);
    }

    // main() hands parse_args the arguments as UTF-8 (on Windows it re-reads
    // the UTF-16 command line), so decode the path as UTF-8.
    const auto path = try_path_from_utf8(args.file_path);

    if (!path) {
        return std::format("file path is not valid UTF-8: '{}'", args.file_path);
    }

    std::error_code ec;

    if (!std::filesystem::exists(*path, ec)) {
        return std::format("file not found: '{}'", args.file_path);
    }

    if (!std::filesystem::is_regular_file(*path, ec)) {
        return std::format("not a regular file: '{}'", args.file_path);
    }

    return std::nullopt;
}

} // namespace luma
