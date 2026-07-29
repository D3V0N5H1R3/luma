#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

void register_result_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                               const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("collect", 1, "(arr: array<result<T>>)", R::result_any(), {p.array_any}),
            m.fn("error", 1, "(value: result<T>)", R::string_type(), {p.result_any}),
            m.fn("filter", 3, "(value: result<T>, f: func(T) -> boolean, error: string)",
                 R::result_any(), {p.result_any, p.func, p.string}),
            m.fn("flat_map", 2, "(value: result<T>, f: func(T) -> result<U>)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("flatten", 1, "(value: result<result<T>>)", R::result_any(), {p.result_any}),
            m.fn("is_failure", 1, "(value: result<T>)", R::boolean_type(), {p.result_any}),
            m.fn("is_success", 1, "(value: result<T>)", R::boolean_type(), {p.result_any}),
            m.fn("map", 2, "(value: result<T>, f: func(T) -> U)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("map_boolean", 2, "(value: result<T>, f: func(T) -> boolean)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("map_failure", 2, "(value: result<T>, f: func(string) -> string)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("map_integer", 2, "(value: result<T>, f: func(T) -> integer)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("map_number", 2, "(value: result<T>, f: func(T) -> number)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("map_string", 2, "(value: result<T>, f: func(T) -> string)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("or", 2, "(value: result<T>, fallback: result<T>)", R::result_any(),
                 {p.result_any, p.result_any}),
            m.fn("or_else", 2, "(value: result<T>, f: func(string) -> result<T>)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("recover", 2, "(value: result<T>, f: func(string) -> T)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("tap", 2, "(value: result<T>, f: func(T) -> void)", R::result_any(),
                 {p.result_any, p.func}),
            m.fn("to_optional", 1, "(value: result<T>)", R::optional_any(), {p.result_any}),
            m.fn("unwrap", 1, "(value: result<T>)", R::any_type(), {p.result_any}),
            m.fn("unwrap_or", 2, "(value: result<T>, default: T)", R::any_type(),
                 {p.result_any, p.any}),
            m.fn("zip", 2, "(a: result<T>, b: result<U>)", R::result_any(),
                 {p.result_any, p.result_any}),
            m.fn("bimap", 3,
                 "(value: result<T>, ok_fn: func(T) -> U, err_fn: func(string) -> string)",
                 R::result_any(), {p.result_any, p.func, p.func}),
            m.fn("fold", 3, "(value: result<T>, ok_fn: func(T) -> U, err_fn: func(string) -> U)",
                 R::any_type(), {p.result_any, p.func, p.func}),
            m.fn("error_code", 1, "(value: result<T>)", R::string_type(), {p.result_any}),
            m.fn("source_function", 1, "(value: result<T>)", R::string_type(), {p.result_any}),
        });
}

void register_optional_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                 const ParamShorthands& p) {
    append_specs(
        specs, {
                   m.fn("and", 2, "(a: optional<T>, b: optional<U>)", R::optional_any(),
                        {p.optional_any, p.optional_any}),
                   m.fn("expect", 2, "(value: optional<T>, message: string)", R::any_type(),
                        {p.optional_any, p.string}),
                   m.fn("filter", 2, "(value: optional<T>, f: func(T) -> boolean)",
                        R::optional_any(), {p.optional_any, p.func}),
                   m.fn("flat_map", 2, "(value: optional<T>, f: func(T) -> optional<U>)",
                        R::optional_any(), {p.optional_any, p.func}),
                   m.fn("flatten", 1, "(value: optional<optional<T>>)", R::optional_any(),
                        {p.optional_any}),
                   m.fn("is_none", 1, "(value: optional<T>)", R::boolean_type(), {p.optional_any}),
                   m.fn("is_some", 1, "(value: optional<T>)", R::boolean_type(), {p.optional_any}),
                   m.fn("map", 2, "(value: optional<T>, f: func(T) -> U)", R::optional_any(),
                        {p.optional_any, p.func}),
                   m.fn("or", 2, "(value: optional<T>, fallback: optional<T>)", R::optional_any(),
                        {p.optional_any, p.optional_any}),
                   m.fn("tap", 2, "(value: optional<T>, f: func(T) -> void)", R::optional_any(),
                        {p.optional_any, p.func}),
                   m.fn("to_result", 2, "(value: optional<T>, error: string)", R::result_any(),
                        {p.optional_any, p.string}),
                   m.fn("unwrap", 1, "(value: optional<T>)", R::any_type(), {p.optional_any}),
                   m.fn("unwrap_or", 2, "(value: optional<T>, default: T)", R::any_type(),
                        {p.optional_any, p.any}),
                   m.fn("zip", 2, "(a: optional<T>, b: optional<U>)", R::optional_any(),
                        {p.optional_any, p.optional_any}),
                   m.fn("and_then", 2, "(value: optional<T>, f: func(T) -> optional<U>)",
                        R::optional_any(), {p.optional_any, p.func}),
                   m.fn("contains", 2, "(value: optional<T>, expected: T)", R::boolean_type(),
                        {p.optional_any, p.any}),
                   m.fn("xor", 2, "(a: optional<T>, b: optional<T>)", R::optional_any(),
                        {p.optional_any, p.optional_any}),
               });
}

void register_reference_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                  const ParamShorthands& p) {
    append_specs(specs,
                 {
                     m.fn("get", 1, "(ref: reference<T>)", R::any_type(), {p.reference_any}),
                     m.fn("get_and_set", 2, "(ref: reference<T>, value: T)", R::any_type(),
                          {p.reference_any, p.any}),
                     m.fn("get_and_update", 2, "(ref: reference<T>, f: func(T) -> T)",
                          R::any_type(), {p.reference_any, p.func}),
                     m.fn("new", 1, "(value: T)", R::any_type(), {p.any}),
                     m.fn("set", 2, "(ref: reference<T>, value: T)", R::void_type(),
                          {p.reference_any, p.any}),
                     m.fn("swap", 2, "(a: reference<T>, b: reference<T>)", R::void_type(),
                          {p.reference_any, p.reference_any}),
                     m.fn("inspect", 1, "(ref: reference<T>)", R::string_type(), {p.reference_any}),
                     m.fn("equals", 2, "(a: reference<T>, b: reference<T>)", R::boolean_type(),
                          {p.reference_any, p.reference_any}),
                     m.fn("same", 2, "(a: reference<T>, b: reference<T>)", R::boolean_type(),
                          {p.reference_any, p.reference_any}),
                     m.fn("update", 2, "(ref: reference<T>, f: func(T) -> T)", R::void_type(),
                          {p.reference_any, p.func}),
                     m.fn("update_and_get", 2, "(ref: reference<T>, f: func(T) -> T)",
                          R::any_type(), {p.reference_any, p.func}),
                 });
}

void register_resource_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                 const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("using", 3, "(acquire: func() -> T, body: func(T) -> U, release: func(T) -> void)",
                 R::any_type(), {p.func, p.func, p.func}),
            m.fn("with", 3, "(resource: T, body: func(T) -> U, cleanup: func(T) -> void)",
                 R::any_type(), {p.any, p.func, p.func}),
        });
}

} // namespace luma::stdlib::detail
