#ifndef LUMA_CLI_CLI_HPP
#define LUMA_CLI_CLI_HPP

#include <string>
#include <string_view>
#include <vector>

#include "common/path_utils.hpp"
#include "common/version.hpp"
#include "runtime/compiler/compilation_cache.hpp"
#include "runtime/compiler/compilation_pipeline.hpp"
#include "runtime/compiler/compiler_config.hpp"

namespace luma {

// Execution options (parsed from CLI flags).
struct RunOptions {
    std::vector<std::string> args;
    bool strict{false};
    bool sandbox{false};
    OptimizationLevel optimize_level{OptimizationLevel::Full};
    bool verify{false};

    // Convert to a CompilerProfile for the compilation pipeline.
    [[nodiscard]] CompilerProfile to_compiler_profile() const {
        return {
            .optimize_level = optimize_level,
            .strict = strict,
            .verify = verify,
        };
    }
};

void print_usage();
// Run a Luma source file with the given options.
[[nodiscard]] int run_file(const std::string& path, const RunOptions& options);

// Full overload with explicit compilation cache control.
[[nodiscard]] int run_file(const std::string& path, const RunOptions& options,
                           CompilationCache& cache);
[[nodiscard]] int run_tests_file(const std::string& path, bool strict = false,
                                 bool sandbox = false);
[[nodiscard]] int check_file(const std::string& path, bool strict = false);

} // namespace luma

#endif // LUMA_CLI_CLI_HPP
