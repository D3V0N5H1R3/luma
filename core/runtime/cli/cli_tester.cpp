#include "runtime/cli/cli.hpp"
#include "runtime/cli/cli_internal.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"

namespace luma {

int run_tests_file(const std::string& path, bool strict, bool sandbox) {
    return with_error_handling([&path, strict, sandbox](SourceManager& source_manager) {
        const CompilerProfile config{
            .optimize_level = OptimizationLevel::Peephole,
            .strict = strict,
            .require_main = false,
        };

        auto outcome = compile_program(path, source_manager, config);

        if (!outcome.success) {
            return outcome.exit_code;
        }

        const auto* compile_result = extract_compile_result(outcome.artifact);
        const auto global_env = Environment::create();
        register_all(global_env, sandbox);

        VM vm{global_env};
        const bool all_passed =
            vm.execute_tests(compile_result->functions, compile_result->top_level);

        return all_passed ? exit_code::success : exit_code::runtime_error;
    });
}

} // namespace luma
