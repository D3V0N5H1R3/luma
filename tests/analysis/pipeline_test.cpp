// Pipeline unit tests.

#include <string>

#include "analysis/ast/declaration.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/pipeline/pipeline.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Test passes ───

// A pass that always succeeds.
class SuccessPass : public Pass {
public:
    [[nodiscard]] std::string name() const override {
        return "SuccessPass";
    }

    bool run(Program& /*program*/, PipelineResult& /*result*/) override {
        return true;
    }
};

// A pass that always fails (adds an error diagnostic).
class FailPass : public Pass {
public:
    [[nodiscard]] std::string name() const override {
        return "FailPass";
    }

    bool run(Program& /*program*/, PipelineResult& result) override {
        Diagnostic d;
        d.severity = Severity::Error;
        d.message = "intentional failure";
        result.diagnostics.push_back(d);

        return false;
    }
};

// A pass that adds a warning but succeeds.
class WarnPass : public Pass {
public:
    [[nodiscard]] std::string name() const override {
        return "WarnPass";
    }

    bool run(Program& /*program*/, PipelineResult& result) override {
        Diagnostic d;
        d.severity = Severity::Warning;
        d.message = "intentional warning";
        result.diagnostics.push_back(d);

        return true;
    }
};

// A pass that depends on FailPass yet runs after failure (mirrors the linter,
// which requires type-check but still runs to emit warnings).  Used to verify
// that a required pass which RAN but reported user errors still satisfies the
// dependency check, so no spurious "internal error" is emitted.
class DependentAfterFailurePass : public Pass {
public:
    [[nodiscard]] std::string name() const override {
        return "DependentAfterFailurePass";
    }

    bool run(Program& /*program*/, PipelineResult& /*result*/) override {
        return true;
    }

    [[nodiscard]] std::vector<std::string_view> required_passes() const override {
        return {"FailPass"};
    }

    [[nodiscard]] bool run_after_failure() const noexcept override {
        return true;
    }
};

// A counting pass to verify execution order.
static int pass_counter{0};

class CountingPass : public Pass {
public:
    explicit CountingPass(int expected_order) : expected_order_{expected_order} {}

    [[nodiscard]] std::string name() const override {
        return "CountingPass";
    }

    bool run(Program& /*program*/, PipelineResult& /*result*/) override {
        ++pass_counter;

        if (pass_counter != expected_order_) {
            throw std::runtime_error{"pass executed out of order"};
        }

        return true;
    }

private:
    int expected_order_;
};

// A pass that always throws, exercising the pipeline's exception safety net
// (which records a location-less "internal error" diagnostic).
class ThrowingPass : public Pass {
public:
    [[nodiscard]] std::string name() const override {
        return "ThrowingPass";
    }

    bool run(Program& /*program*/, PipelineResult& /*result*/) override {
        throw std::runtime_error{"boom"};
    }
};

// ─── Tests ───

static void test_empty_pipeline() {
    auto pipeline = Pipeline::builder().build();
    Program program;
    auto result = pipeline.run(program);

    ASSERT_FALSE(result.has_errors());
    ASSERT_EQ(result.diagnostics.size(), static_cast<std::size_t>(0));
}

static void test_single_success_pass() {
    auto pipeline = Pipeline::builder().add<SuccessPass>().build();
    Program program;
    auto result = pipeline.run(program);

    ASSERT_FALSE(result.has_errors());
}

static void test_single_fail_pass() {
    auto pipeline = Pipeline::builder().add<FailPass>().build();
    Program program;
    auto result = pipeline.run(program);

    ASSERT_TRUE(result.has_errors());
    ASSERT_EQ(result.error_count(), static_cast<std::size_t>(1));
}

static void test_fail_stops_pipeline() {
    // FailPass should stop before the second SuccessPass.
    auto pipeline =
        Pipeline::builder().add<SuccessPass>().add<FailPass>().add<SuccessPass>().build();

    Program program;
    auto result = pipeline.run(program);

    ASSERT_TRUE(result.has_errors());
    ASSERT_EQ(result.error_count(), static_cast<std::size_t>(1));
}

static void test_dependent_runs_after_required_failed() {
    // A pass that requires FailPass and runs after failure must NOT emit a
    // spurious "internal error": FailPass ran (it only reported user errors),
    // so the dependency is satisfied.  Regression test for the linter emitting
    // "internal error: pass 'lint' requires 'type-check'" — and an inflated
    // error count — on every program that fails type-checking.
    auto pipeline = Pipeline::builder().add<FailPass>().add<DependentAfterFailurePass>().build();

    Program program;
    auto result = pipeline.run(program);

    ASSERT_TRUE(result.has_errors());
    // Exactly one error (FailPass's), not an extra spurious internal error.
    ASSERT_EQ(result.error_count(), static_cast<std::size_t>(1));

    for (const auto& d : result.diagnostics) {
        ASSERT_TRUE(d.message.find("internal error") == std::string::npos);
    }
}

static void test_warnings_dont_stop() {
    auto pipeline = Pipeline::builder().add<WarnPass>().add<SuccessPass>().build();

    Program program;
    auto result = pipeline.run(program);

    ASSERT_FALSE(result.has_errors());
    ASSERT_TRUE(result.has_warnings());
    ASSERT_EQ(result.warning_count(), static_cast<std::size_t>(1));
}

static void test_execution_order() {
    pass_counter = 0;

    auto pipeline =
        Pipeline::builder().add<CountingPass>(1).add<CountingPass>(2).add<CountingPass>(3).build();

    Program program;
    auto result = pipeline.run(program);

    ASSERT_FALSE(result.has_errors());
    ASSERT_EQ(pass_counter, 3);
}

static void test_add_if_true() {
    auto pipeline = Pipeline::builder().add_if<WarnPass>(true).build();

    Program program;
    auto result = pipeline.run(program);

    ASSERT_TRUE(result.has_warnings());
}

static void test_add_if_false() {
    auto pipeline = Pipeline::builder().add_if<WarnPass>(false).build();

    Program program;
    auto result = pipeline.run(program);

    ASSERT_FALSE(result.has_warnings());
}

static void test_pipeline_result_counts() {
    PipelineResult result;

    Diagnostic e1;
    e1.severity = Severity::Error;
    result.diagnostics.push_back(e1);

    Diagnostic e2;
    e2.severity = Severity::Error;
    result.diagnostics.push_back(e2);

    Diagnostic w1;
    w1.severity = Severity::Warning;
    result.diagnostics.push_back(w1);

    ASSERT_EQ(result.error_count(), static_cast<std::size_t>(2));
    ASSERT_EQ(result.warning_count(), static_cast<std::size_t>(1));
    ASSERT_TRUE(result.has_errors());
    ASSERT_TRUE(result.has_warnings());
}

static void test_pipeline_result_no_errors() {
    const PipelineResult result;
    ASSERT_FALSE(result.has_errors());
    ASSERT_FALSE(result.has_warnings());
    ASSERT_EQ(result.error_count(), static_cast<std::size_t>(0));
    ASSERT_EQ(result.warning_count(), static_cast<std::size_t>(0));
}

static void test_pass_timings() {
    auto pipeline = Pipeline::builder().add<SuccessPass>().add<WarnPass>().build();

    Program program;
    auto result = pipeline.run(program);

    ASSERT_EQ(result.timings.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(result.timings[0].name, "SuccessPass");
    ASSERT_EQ(result.timings[1].name, "WarnPass");
    ASSERT_TRUE(result.timings[0].duration.count() >= 0);
    ASSERT_TRUE(result.timings[1].duration.count() >= 0);
}

static void test_internal_error_has_no_span() {
    // Regression (F1): a location-less internal-error diagnostic must carry no
    // source span.  A default-constructed SourceLocation is truthy, so the old
    // `if (loc)` guard wrongly attached a primary span at file_id 0 — which the
    // renderer then rendered as a spurious "[unable to retrieve source for
    // file_id 0]" line instead of a clean header-only message.
    auto pipeline = Pipeline::builder().add<ThrowingPass>().build();

    Program program;
    auto result = pipeline.run(program);

    ASSERT_TRUE(result.has_errors());
    ASSERT_EQ(result.error_count(), static_cast<std::size_t>(1));

    const auto& diagnostic = result.diagnostics.front();
    ASSERT_TRUE(diagnostic.spans.empty());
    ASSERT_TRUE(diagnostic.message.find("internal error") != std::string::npos);
}

int main() {
    RUN(test_empty_pipeline);
    RUN(test_single_success_pass);
    RUN(test_single_fail_pass);
    RUN(test_fail_stops_pipeline);
    RUN(test_dependent_runs_after_required_failed);
    RUN(test_warnings_dont_stop);
    RUN(test_execution_order);
    RUN(test_add_if_true);
    RUN(test_add_if_false);
    RUN(test_pipeline_result_counts);
    RUN(test_pipeline_result_no_errors);
    RUN(test_pass_timings);
    RUN(test_internal_error_has_no_span);
    return SUMMARY();
}
