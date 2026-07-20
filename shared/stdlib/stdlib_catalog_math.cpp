#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

void register_math_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p) {
    append_specs(
        specs,
        {
            // Some Math functions document "integer | number" in the params string
            // because both types are accepted by the user. The param_types use p.number
            // (number) because the type checker implicitly coerces integer to number.
            m.fn("absolute", 1, "(value: integer | number)", R::result_number(), {p.number}),
            m.variadic_fn("approximately_equal", 2, "(a: number, b: number, epsilon?: number)",
                          R::boolean_type(), {p.number, p.number}),
            m.fn("arc_cosine", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("arc_sine", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("arc_tangent", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("ceil", 1, "(value: number)", R::result_integer(), {p.number}),
            m.fn("clamp", 3, "(value: number, min: number, max: number)", R::result_number(),
                 {p.number, p.number, p.number}),
            m.fn("correlation", 2, "(xs: array<number>, ys: array<number>)", R::result_number(),
                 {p.array_any, p.array_any}),
            m.fn("cosine", 1, "(angle: number)", R::result_number(), {p.number}),
            m.fn("degrees", 1, "(radians: number)", R::number_type(), {p.number}),
            m.constant("e", R::number_type()),
            m.fn("exponential", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("factorial", 1, "(value: integer)", R::result_integer(), {p.integer}),
            m.fn("floor", 1, "(value: number)", R::result_integer(), {p.number}),
            m.fn("greatest_common_divisor", 2, "(a: integer, b: integer)", R::result_integer(),
                 {p.integer, p.integer}),
            m.constant("infinity", R::number_type()),
            m.fn("is_infinite", 1, "(value: number)", R::boolean_type(), {p.number}),
            m.fn("is_not_a_number", 1, "(value: number)", R::boolean_type(), {p.number}),
            m.fn("is_prime", 1, "(value: integer)", R::boolean_type(), {p.integer}),
            m.fn("least_common_multiple", 2, "(a: integer, b: integer)", R::result_integer(),
                 {p.integer, p.integer}),
            m.fn("lerp", 3, "(a: number, b: number, t: number)", R::result_number(),
                 {p.number, p.number, p.number}),
            m.fn("log_10", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("log_2", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("log_e", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("max", 2, "(a: number, b: number)", R::number_type(), {p.number, p.number}),
            m.fn("mean", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("median", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("min", 2, "(a: number, b: number)", R::number_type(), {p.number, p.number}),
            m.fn("mode", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("percentile", 2, "(values: array<number>, p: number)", R::result_number(),
                 {p.array_any, p.number}),
            m.constant("pi", R::number_type()),
            m.fn("power", 2, "(base: number, exponent: number)", R::result_number(),
                 {p.number, p.number}),
            m.fn("radians", 1, "(degrees: number)", R::number_type(), {p.number}),
            m.fn(
                "remap", 5,
                "(value: number, in_min: number, in_max: number, out_min: number, out_max: number)",
                R::result_number(), {p.number, p.number, p.number, p.number, p.number}),
            m.fn("remainder", 2, "(a: integer | number, b: integer | number)", R::result_number(),
                 {p.number, p.number}),
            m.fn("round", 1, "(value: number)", R::result_integer(), {p.number}),
            m.fn("sign", 1, "(value: integer | number)", R::integer_type(), {p.number}),
            m.fn("sine", 1, "(angle: number)", R::result_number(), {p.number}),
            m.fn("smooth_step", 3, "(edge0: number, edge1: number, x: number)", R::result_number(),
                 {p.number, p.number, p.number}),
            m.fn("square_root", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("standard_deviation", 1, "(values: array<number>)", R::result_number(),
                 {p.array_any}),
            m.fn("sum", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("tangent", 1, "(angle: number)", R::result_number(), {p.number}),
            m.constant("tau", R::number_type()),
            m.fn("truncate", 1, "(value: number)", R::result_integer(), {p.number}),
            m.fn("variance", 1, "(values: array<number>)", R::result_number(), {p.array_any}),
            m.fn("atan2", 2, "(y: number, x: number)", R::result_number(), {p.number, p.number}),
            m.fn("hypot", 2, "(x: number, y: number)", R::number_type(), {p.number, p.number}),
            m.fn("log", 2, "(base: number, value: number)", R::result_number(),
                 {p.number, p.number}),
            m.fn("cube_root", 1, "(value: number)", R::number_type(), {p.number}),
            m.fn("hyperbolic_sine", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("hyperbolic_cosine", 1, "(value: number)", R::result_number(), {p.number}),
            m.fn("hyperbolic_tangent", 1, "(value: number)", R::number_type(), {p.number}),
        });
}

void register_converter_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                  const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("character_to_codepoint", 1, "(char: string)", R::result_integer(), {p.string}),
            m.fn("codepoint_to_character", 1, "(code: integer)", R::result_string(), {p.integer}),
            m.fn("from_binary", 1, "(value: string)", R::result_integer(), {p.string}),
            m.fn("from_hexadecimal", 1, "(value: string)", R::result_integer(), {p.string}),
            m.fn("from_roman", 1, "(value: string)", R::result_integer(), {p.string}),
            m.fn("number_to_words", 1, "(value: integer)", R::string_type(), {p.integer}),
            m.fn("ordinal", 1, "(value: integer)", R::string_type(), {p.integer}),
            m.fn("to_binary", 1, "(value: integer)", R::string_type(), {p.integer}),
            m.fn("to_boolean", 1, "(value: string)", R::result_boolean(), {p.string}),
            m.fn("to_hexadecimal", 1, "(value: integer)", R::string_type(), {p.integer}),
            m.fn("to_integer", 1, "(value: string | number)", R::result_integer(), {p.any}),
            m.fn("to_number", 1, "(value: string | integer)", R::result_number(), {p.any}),
            m.fn("to_roman", 1, "(value: integer)", R::result_string(), {p.integer}),
            m.fn("to_string", 1, "(value: T)", R::string_type(), {p.any}),
        });
}

void register_random_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                               const ParamShorthands& p) {
    append_specs(
        specs, {
                   m.fn("choice", 1, "(arr: array<T>)", R::result_any(), {p.any}),
                   m.fn("generate_boolean", 0, "()", R::boolean_type(), {}),
                   m.fn("generate_integer", 2, "(min: integer, max: integer)", R::result_integer(),
                        {p.integer, p.integer}),
                   m.fn("generate_number", 0, "()", R::number_type(), {}),
                   m.fn("generate_string", 1, "(length: integer)", R::result_string(), {p.integer}),
                   m.fn("sample", 2, "(arr: array<T>, count: integer)", R::result_array_any(),
                        {p.any, p.integer}),
                   m.fn("shuffle", 1, "(arr: array<T>)", R::array_any(), {p.any}),
                   m.fn("generate_uuid", 0, "()", R::string_type(), {}),
                   m.fn("secure_boolean", 0, "()", R::result_boolean(), {}),
                   m.fn("secure_integer", 2, "(min: integer, max: integer)", R::result_integer(),
                        {p.integer, p.integer}),
                   m.fn("secure_number", 0, "()", R::result_number(), {}),
                   m.fn("secure_string", 1, "(length: integer)", R::result_string(), {p.integer}),
                   m.fn("secure_uuid", 0, "()", R::result_string(), {}),
               });
}

} // namespace luma::stdlib::detail
