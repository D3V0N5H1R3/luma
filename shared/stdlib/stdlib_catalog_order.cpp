#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

// Order — comparison helpers built around the top-level Ordering choice
// (Less / Equal / Greater).  A match over Ordering is exhaustive and
// self-documenting, and the to_number / from_number bridges keep it
// interoperable with the existing numeric comparator that Array.sort expects.
void register_order_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("of", 2, "(a: any, b: any)", named::ordering(), {p.any, p.any}),
            m.fn("reverse", 1, "(o: Ordering)", named::ordering(), {p.any}),
            m.fn("then", 2, "(first: Ordering, second: Ordering)", named::ordering(),
                 {p.any, p.any}),
            m.fn("to_number", 1, "(o: Ordering)", R::number_type(), {p.any}),
            m.fn("from_number", 1, "(n: number)", named::ordering(), {p.number}),
        });
}

} // namespace luma::stdlib::detail
