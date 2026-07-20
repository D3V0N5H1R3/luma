#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

void register_task_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("all", 1, "(tasks: array<task<T>>)", R::result_array_any(), {p.array_any}),
            m.fn("any", 1, "(tasks: array<task<T>>)", R::result_any(), {p.array_any}),
            m.fn("cancel", 1, "(t: task<T>)", R::boolean_type(), {p.task_any}),
            m.fn("delay", 1, "(ms: integer)", R::void_type(), {p.integer}),
            m.fn("flat_map", 2, "(t: task<T>, f: func(T) -> task<U>)", R::result_any(),
                 {p.task_any, p.func}),
            m.fn("is_cancelled", 1, "(t: task<T>)", R::boolean_type(), {p.task_any}),
            m.fn("is_done", 1, "(t: task<T>)", R::result_boolean(), {p.task_any}),
            m.fn("map", 2, "(t: task<T>, f: func(T) -> U)", R::result_any(), {p.task_any, p.func}),
            m.fn("map_n", 2, "(tasks: array<task<T>>, f: func(T) -> U)", R::result_array_any(),
                 {p.array_any, p.func}),
            m.fn("race", 1, "(tasks: array<task<T>>)", R::result_any(), {p.array_any}),
            m.fn("retry", 2, "(max_attempts: integer, f: func() -> result<T>)", R::result_any(),
                 {p.integer, p.func}),
            m.fn("sequence", 1, "(tasks: array<task<T>>)", R::result_array_any(), {p.array_any}),
            m.fn("timeout", 2, "(t: task<T>, ms: integer)", R::result_any(),
                 {p.task_any, p.integer}),
        });
}

} // namespace luma::stdlib::detail
