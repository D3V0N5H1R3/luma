#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

void register_task_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("all", 1, "(tasks: array<task<T>>)", R::result_array_any(), {p.array_any}),
            m.fn("all_settled", 1, "(tasks: array<task<T>>)", R::array(R::result_any()),
                 {R::array(p.task_any)}),
            m.fn("any", 1, "(tasks: array<task<T>>)", R::result_any(), {p.array_any}),
            m.fn("cancel", 1, "(task: task<T>)", R::boolean_type(), {p.task_any}),
            m.fn("completed", 1, "(value: T)", R::task_any(), {p.any}),
            m.fn("delay", 1, "(milliseconds: integer)", R::void_type(), {p.integer}),
            m.fn("failed", 1, "(message: string)", R::task_any(), {p.string}),
            m.fn("flat_map", 2, "(task: task<T>, f: func(T) -> task<U>)", R::result_any(),
                 {p.task_any, p.func}),
            m.fn("is_cancelled", 1, "(task: task<T>)", R::boolean_type(), {p.task_any}),
            m.fn("is_done", 1, "(task: task<T>)", R::result_boolean(), {p.task_any}),
            m.fn("map", 2, "(task: task<T>, f: func(T) -> U)", R::result_any(),
                 {p.task_any, p.func}),
            m.fn("map_n", 2, "(tasks: array<task<T>>, f: func(T) -> U)", R::result_array_any(),
                 {p.array_any, p.func}),
            m.fn("race", 1, "(tasks: array<task<T>>)", R::result_any(), {p.array_any}),
            m.fn("retry", 2, "(maximum_attempts: integer, f: func() -> result<T>)", R::result_any(),
                 {p.integer, p.func}),
            m.fn("retry_with_backoff", 3,
                 "(n: integer, base_delay_milliseconds: integer, function: function() -> T)",
                 R::result_any(), {p.integer, p.integer, p.func}),
            m.fn("sequence", 1, "(tasks: array<task<T>>)", R::result_array_any(), {p.array_any}),
            m.fn("timeout", 2, "(task: task<T>, milliseconds: integer)", R::result_any(),
                 {p.task_any, p.integer}),
            m.fn("timeout_or", 3, "(task: task<T>, milliseconds: integer, default: T)",
                 R::any_type(), {p.task_any, p.integer, p.any}),
        });
}

} // namespace luma::stdlib::detail
