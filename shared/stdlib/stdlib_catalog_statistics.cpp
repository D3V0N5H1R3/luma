#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

// Statistics — descriptive and inferential statistics over numeric arrays.
// Split out of Math so the four maths modules (Math, Calculus, LinearAlgebra,
// Statistics) each cover one cohesive domain.  The plain aggregate Math.sum
// stays in Math; every dataset-summarising function lives here.
void register_statistics_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                   const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("correlation", 2, "(xs: array<number>, ys: array<number>)", R::result_number(),
                 {p.array_any, p.array_any}),
            m.fn("five_number_summary", 1, "(values: array<number>)",
                 R::result(named::five_number_summary()), {p.array_any}),
            m.fn("histogram", 2, "(values: array<number>, bins: integer)",
                 R::result(named::histogram()), {p.array_any, p.integer}),
            m.fn("linear_fit", 2, "(xs: array<number>, ys: array<number>)",
                 R::result(named::line_fit()), {p.array_any, p.array_any}),
            m.fn("mean", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("median", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("mode", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("percentile", 2, "(values: array<number>, p: number)", R::result_number(),
                 {p.array_any, p.number}),
            m.fn("standard_deviation", 1, "(values: array<number>)", R::result_number(),
                 {p.array_any}),
            m.fn("summarize", 1, "(values: array<number>)", R::result(named::summary()),
                 {p.array_any}),
            m.fn("variance", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
        });
}

} // namespace luma::stdlib::detail
