#ifndef LUMA_PIPELINE_LINT_PASS_HPP
#define LUMA_PIPELINE_LINT_PASS_HPP

#include <string>
#include <string_view>
#include <vector>

#include "analysis/linter/linter.hpp"
#include "analysis/pipeline/pipeline.hpp"

namespace luma {

// Runs the linter.
// Lint warnings never stop the pipeline — this pass always succeeds.
class LintPass : public Pass {
public:
    [[nodiscard]] std::string name() const override {
        return std::string{pass_name::lint};
    }

    bool run(Program& program, PipelineResult& result) override {
        Linter linter;

        merge_diagnostics(result.diagnostics, linter.lint(program));

        return true;
    }

    [[nodiscard]] std::vector<std::string_view> required_passes() const override {
        return {pass_name::type_check};
    }

    [[nodiscard]] bool run_after_failure() const noexcept override {
        return true;
    }
};

} // namespace luma

#endif // LUMA_PIPELINE_LINT_PASS_HPP
