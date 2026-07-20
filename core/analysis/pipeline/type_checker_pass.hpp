#ifndef LUMA_PIPELINE_TYPE_CHECKER_PASS_HPP
#define LUMA_PIPELINE_TYPE_CHECKER_PASS_HPP

#include <string>
#include <utility>

#include "analysis/pipeline/pipeline.hpp"
#include "analysis/types/type_checker.hpp"

namespace luma {

// Runs the static type checker.
// Appends type errors and warnings to the pipeline result.
class TypeCheckerPass : public Pass {
public:
    explicit TypeCheckerPass(bool require_main = true) : require_main_{require_main} {}

    [[nodiscard]] std::string name() const override {
        return std::string{pass_name::type_check};
    }

    bool run(Program& program, PipelineResult& result) override {
        TypeChecker checker;

        auto errors = checker.check(program, require_main_);
        const bool ok = errors.empty();

        merge_diagnostics(result.diagnostics, std::move(errors));
        merge_diagnostics(result.diagnostics, checker.get_warnings());

        return ok;
    }

private:
    bool require_main_;
};

} // namespace luma

#endif // LUMA_PIPELINE_TYPE_CHECKER_PASS_HPP
