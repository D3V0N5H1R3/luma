#include "runtime/cli/cli_args.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string_view>

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

// Try to consume the --optimize / -O flag and its value. Handles both the
// combined form (-O2) and the separated form (-O 2 / --optimize 2). Returns true
// if `arg` was an optimize flag (and was consumed), false otherwise.
bool try_consume_optimize(luma::ParsedArgs& args, std::string_view arg, int argc, char* argv[],
                          int& i) {
    // Combined form: -O0, -O1, -O2
    if (arg.size() == 3 && arg.starts_with("-O")) {
        const int level = parse_optimize_char(arg[2]);
        if (level >= 0) {
            args.optimize = level;
            return true;
        }
    }

    // Separated form: -O 2 or --optimize 2
    if (arg == "--optimize" || arg == "-O") {
        if (i + 1 < argc) {
            const std::string_view next{argv[i + 1]};

            if (next.size() == 1) {
                const int level = parse_optimize_char(next[0]);
                if (level >= 0) {
                    args.optimize = level;
                    ++i;
                    return true;
                }
            }

            std::cerr << "warning: ignoring invalid optimization level '" << next
                      << "'; expected 0, 1, or 2\n";
        } else {
            std::cerr << "warning: --optimize requires a level (0, 1, or 2)\n";
        }
        return true;
    }

    return false;
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
    bool collecting_args{false};

    for (int i{1}; i < argc; ++i) {
        const std::string_view arg{argv[i]};

        if (collecting_args) {
            args.program_args.emplace_back(arg);

            continue;
        }

        // --optimize / -O (combined -O2 or separated -O 2) is parsed in one
        // place, before the flag table, so its value handling isn't split across
        // the descriptor lookup and the unknown-argument fallback.
        if (try_consume_optimize(args, arg, argc, argv, i)) {
            continue;
        }

        // Data-driven flag lookup.
        const auto it = std::ranges::find_if(k_flag_descriptors, [arg](const FlagDescriptor& desc) {
            return matches_flag(desc, arg);
        });

        if (it != k_flag_descriptors.end()) {
            if (it->apply.has_value()) {
                (*it->apply)(args);
            }

            continue;
        }

        if (arg == "pkg") {
            args.command = Command::Pkg;
            // Remaining args belong to the pkg subcommand.
            collecting_args = true;
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
            collecting_args = true;
        }
    }

    return args;
}

} // namespace luma
