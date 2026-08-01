#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

// Statistics — core descriptive statistics over numeric arrays.
// Split out of Math so the four maths modules (Math, Calculus, LinearAlgebra,
// Statistics) each cover one cohesive domain.  The plain aggregate Math.sum
// stays in Math; the five core descriptive-statistics functions live here.
void register_statistics_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                   const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("mean", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("median", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("mode", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("standard_deviation", 1, "(values: array<number>)", R::result_number(),
                 {p.array_any}),
            m.fn("variance", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
        });
}

} // namespace luma::stdlib::detail
