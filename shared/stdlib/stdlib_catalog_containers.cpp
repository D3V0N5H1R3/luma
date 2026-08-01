#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

void register_set_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                            const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("add", 2, "(s: set, value: T)", named::set(), {p.set, p.any}),
            m.fn("all", 2, "(s: set, f: func(T) -> boolean)", R::result_boolean(), {p.set, p.func}),
            m.fn("any", 2, "(s: set, f: func(T) -> boolean)", R::result_boolean(), {p.set, p.func}),
            m.fn("concat", 2, "(a: set, b: set)", named::set(), {p.set, p.set}),
            m.fn("contains", 2, "(s: set, value: T)", R::boolean_type(), {p.set, p.any}),
            m.fn("count", 2, "(s: set, f: func(T) -> boolean)", R::result_integer(),
                 {p.set, p.func}),
            m.fn("difference", 2, "(a: set, b: set)", named::set(), {p.set, p.set}),
            m.fn("equals", 2, "(a: set, b: set)", R::boolean_type(), {p.set, p.set}),
            m.fn("each", 2, "(s: set, f: func(T) -> none)", R::result(R::none_type()),
                 {p.set, p.func}),
            m.fn("filter", 2, "(s: set, f: func(T) -> boolean)", R::result(named::set()),
                 {p.set, p.func}),
            m.fn("find", 2, "(s: set, f: func(T) -> boolean)", R::result_any(), {p.set, p.func}),
            m.fn("from_array", 1, "(arr: array<T>)", named::set(), {p.array_any}),
            m.fn("map", 2, "(s: set, f: func(T) -> U)", R::result(named::set()), {p.set, p.func}),
            m.fn("intersection", 2, "(a: set, b: set)", named::set(), {p.set, p.set}),
            m.fn("is_disjoint", 2, "(a: set, b: set)", R::boolean_type(), {p.set, p.set}),
            m.fn("is_empty", 1, "(s: set)", R::boolean_type(), {p.set}),
            m.fn("is_subset", 2, "(a: set, b: set)", R::boolean_type(), {p.set, p.set}),
            m.fn("is_superset", 2, "(a: set, b: set)", R::boolean_type(), {p.set, p.set}),
            m.fn("length", 1, "(s: set)", R::integer_type(), {p.set}),
            m.fn("new", 0, "()", named::set(), {}),
            m.fn("partition", 2, "(s: set, f: func(T) -> boolean)",
                 R::result(R::tuple({named::set(), named::set()})), {p.set, p.func}),
            m.fn("reduce", 3, "(s: set, initial: U, f: func(U, T) -> U)", R::result_any(),
                 {p.set, p.any, p.func}),
            m.fn("remove", 2, "(s: set, value: T)", named::set(), {p.set, p.any}),
            m.fn("symmetric_difference", 2, "(a: set, b: set)", named::set(), {p.set, p.set}),
            m.fn("to_array", 1, "(s: set)", R::array_any(), {p.set}),
            m.fn("union", 2, "(a: set, b: set)", named::set(), {p.set, p.set}),
        });
}

void register_queue_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p) {
    append_specs(
        specs, {
                   m.fn("concat", 2, "(a: queue, b: queue)", named::queue(), {p.queue, p.queue}),
                   m.fn("all", 2, "(q: queue, f: func(T) -> boolean)", R::result_boolean(),
                        {p.queue, p.func}),
                   m.fn("any", 2, "(q: queue, f: func(T) -> boolean)", R::result_boolean(),
                        {p.queue, p.func}),
                   m.fn("contains", 2, "(q: queue, value: T)", R::boolean_type(), {p.queue, p.any}),
                   m.fn("count", 2, "(q: queue, f: func(T) -> boolean)", R::result_integer(),
                        {p.queue, p.func}),
                   m.fn("dequeue", 1, "(q: queue)", R::result_any(), {p.queue}),
                   m.fn("each", 2, "(q: queue, f: func(T) -> none)", R::result(R::none_type()),
                        {p.queue, p.func}),
                   m.fn("enqueue", 2, "(q: queue, value: T)", named::queue(), {p.queue, p.any}),
                   m.fn("filter", 2, "(q: queue, f: func(T) -> boolean)", R::result(named::queue()),
                        {p.queue, p.func}),
                   m.fn("find", 2, "(q: queue, f: func(T) -> boolean)", R::result_any(),
                        {p.queue, p.func}),
                   m.fn("from_array", 1, "(arr: array<T>)", named::queue(), {p.array_any}),
                   m.fn("is_empty", 1, "(q: queue)", R::boolean_type(), {p.queue}),
                   m.fn("length", 1, "(q: queue)", R::integer_type(), {p.queue}),
                   m.fn("map", 2, "(q: queue, f: func(T) -> U)", R::result(named::queue()),
                        {p.queue, p.func}),
                   m.fn("new", 0, "()", named::queue(), {}),
                   m.fn("partition", 2, "(q: queue, f: func(T) -> boolean)",
                        R::result(R::tuple({named::queue(), named::queue()})), {p.queue, p.func}),
                   m.fn("peek", 1, "(q: queue)", R::result_any(), {p.queue}),
                   m.fn("reduce", 3, "(q: queue, initial: U, f: func(U, T) -> U)", R::result_any(),
                        {p.queue, p.any, p.func}),
                   m.fn("reverse", 1, "(q: queue)", named::queue(), {p.queue}),
                   m.fn("to_array", 1, "(q: queue)", R::array_any(), {p.queue}),
               });
}

void register_stack_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p) {
    append_specs(
        specs, {
                   m.fn("all", 2, "(s: stack, f: func(T) -> boolean)", R::result_boolean(),
                        {p.stack, p.func}),
                   m.fn("any", 2, "(s: stack, f: func(T) -> boolean)", R::result_boolean(),
                        {p.stack, p.func}),
                   m.fn("concat", 2, "(a: stack, b: stack)", named::stack(), {p.stack, p.stack}),
                   m.fn("contains", 2, "(s: stack, value: T)", R::boolean_type(), {p.stack, p.any}),
                   m.fn("count", 2, "(s: stack, f: func(T) -> boolean)", R::result_integer(),
                        {p.stack, p.func}),
                   m.fn("each", 2, "(s: stack, f: func(T) -> none)", R::result(R::none_type()),
                        {p.stack, p.func}),
                   m.fn("filter", 2, "(s: stack, f: func(T) -> boolean)", R::result(named::stack()),
                        {p.stack, p.func}),
                   m.fn("find", 2, "(s: stack, f: func(T) -> boolean)", R::result_any(),
                        {p.stack, p.func}),
                   m.fn("from_array", 1, "(arr: array<T>)", named::stack(), {p.array_any}),
                   m.fn("is_empty", 1, "(s: stack)", R::boolean_type(), {p.stack}),
                   m.fn("length", 1, "(s: stack)", R::integer_type(), {p.stack}),
                   m.fn("map", 2, "(s: stack, f: func(T) -> U)", R::result(named::stack()),
                        {p.stack, p.func}),
                   m.fn("new", 0, "()", named::stack(), {}),
                   m.fn("partition", 2, "(s: stack, f: func(T) -> boolean)",
                        R::result(R::tuple({named::stack(), named::stack()})), {p.stack, p.func}),
                   m.fn("peek", 1, "(s: stack)", R::result_any(), {p.stack}),
                   m.fn("pop", 1, "(s: stack)", R::result_any(), {p.stack}),
                   m.fn("pop_while", 2, "(s: stack, f: func(T) -> boolean)",
                        R::result(R::tuple({R::array_any(), named::stack()})), {p.stack, p.func}),
                   m.fn("push", 2, "(s: stack, value: T)", named::stack(), {p.stack, p.any}),
                   m.fn("reduce", 3, "(s: stack, initial: U, f: func(U, T) -> U)", R::result_any(),
                        {p.stack, p.any, p.func}),
                   m.fn("reverse", 1, "(s: stack)", named::stack(), {p.stack}),
                   m.fn("to_array", 1, "(s: stack)", R::array_any(), {p.stack}),
               });
}

} // namespace luma::stdlib::detail
