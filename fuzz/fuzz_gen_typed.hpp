#ifndef LUMA_FUZZ_FUZZ_GEN_TYPED_HPP
#define LUMA_FUZZ_FUZZ_GEN_TYPED_HPP

#include <array>
#include <cstddef>
#include <string>

#include "fuzz_gen_features.hpp"
#include "fuzz_gen_stdlib.hpp"
#include "fuzz_gen_vocab.hpp"

// The well-typed expression and statement grammar.
//
// Every expression is generated for a requested static type and every
// statement is self-contained, so a program built only from this grammar
// type-checks and reaches the compiler, optimiser, verifier and VM — the
// stages a single ill-typed chaos statement would otherwise cut off.  Runtime
// failures are avoided by construction too: integer operands stay small enough
// that no product can overflow, divisors are non-zero literals, and loops have
// literal bounds.  See fuzz_source_generator.hpp for the Provider concept these
// templates require.

namespace luma::fuzz::gen {

// Expressions nest at most this deep.  With leaves bounded by ±1000, the
// deepest product has four factors (≤ 1e12), comfortably inside int64.
inline constexpr int k_max_typed_expression_depth = 2;
inline constexpr int k_max_typed_statement_depth = 3;

enum class ValueType {
    Integer,
    Number,
    Boolean,
    String,
    Count
};

inline constexpr std::array k_value_type_names = {"integer", "number", "boolean", "string"};

[[nodiscard]] inline std::string type_name(ValueType type) {
    return k_value_type_names[static_cast<std::size_t>(type)];
}

template <typename Provider>
[[nodiscard]] std::string generate_typed_expression(Provider& fdp, ValueType type, int depth);

// A literal of the requested type — the leaf of every typed expression.
template <typename Provider>
[[nodiscard]] std::string generate_typed_literal(Provider& fdp, ValueType type) {
    switch (type) {
        case ValueType::Integer:
            return generate_int_literal(fdp, -1000, 1000);
        case ValueType::Number:
            return generate_number_literal(fdp);
        case ValueType::Boolean:
            return fdp.ConsumeBool() ? "true" : "false";
        case ValueType::String:
            return generate_string_literal(fdp);
        case ValueType::Count:
            break;
    }
    unreachable_kind();
}

enum class IntegerKind {
    Literal,
    Arithmetic,
    FloorDivide,
    Modulo,
    IfElse,
    Match,
    Coalesce,
    StringLength,
    ArrayLength,
    Count
};

template <typename Provider>
[[nodiscard]] std::string generate_integer_expression(Provider& fdp, int depth) {
    const auto sub = [&](ValueType type) {
        return generate_typed_expression(fdp, type, depth + 1);
    };
    const auto divisor = [&] { return generate_int_literal(fdp, 1, 9); };

    switch (pick_kind<IntegerKind>(fdp)) {
        case IntegerKind::Literal:
            return generate_typed_literal(fdp, ValueType::Integer);
        case IntegerKind::Arithmetic: {
            const auto op = pick(fdp, std::array{" + ", " - ", " * "});
            return "(" + sub(ValueType::Integer) + op + sub(ValueType::Integer) + ")";
        }
        case IntegerKind::FloorDivide:
            return "(" + sub(ValueType::Integer) + " // " + divisor() + ")";
        case IntegerKind::Modulo:
            return "(" + sub(ValueType::Integer) + " % " + divisor() + ")";
        case IntegerKind::IfElse:
            return "(if " + sub(ValueType::Boolean) + " { " + sub(ValueType::Integer) +
                   " } else { " + sub(ValueType::Integer) + " })";
        case IntegerKind::Match: // Match patterns only accept non-negative literals.
            return "(match " + sub(ValueType::Integer) + " { case " +
                   generate_int_literal(fdp, 0, 3) + " { " + sub(ValueType::Integer) +
                   " } else { " + sub(ValueType::Integer) + " } })";
        case IntegerKind::Coalesce:
            return "(some(" + sub(ValueType::Integer) + ") ?? " + sub(ValueType::Integer) + ")";
        case IntegerKind::StringLength:
            return "(" + sub(ValueType::String) + " |> String.length())";
        case IntegerKind::ArrayLength:
            return "Array.length([" + sub(ValueType::Integer) + ", " + sub(ValueType::Integer) +
                   "])";
        case IntegerKind::Count:
            break;
    }
    unreachable_kind();
}

enum class NumberKind {
    Literal,
    Arithmetic,
    Divide,
    IfElse,
    Count
};

template <typename Provider>
[[nodiscard]] std::string generate_number_expression(Provider& fdp, int depth) {
    const auto sub = [&](ValueType type) {
        return generate_typed_expression(fdp, type, depth + 1);
    };

    switch (pick_kind<NumberKind>(fdp)) {
        case NumberKind::Literal:
            return generate_typed_literal(fdp, ValueType::Number);
        case NumberKind::Arithmetic: {
            const auto op = pick(fdp, std::array{" + ", " - ", " * "});
            return "(" + sub(ValueType::Number) + op + sub(ValueType::Number) + ")";
        }
        case NumberKind::Divide: // `/` always yields a number, even for integers.
            return "(" + sub(ValueType::Integer) + " / " + generate_int_literal(fdp, 1, 9) + ")";
        case NumberKind::IfElse:
            return "(if " + sub(ValueType::Boolean) + " { " + sub(ValueType::Number) +
                   " } else { " + sub(ValueType::Number) + " })";
        case NumberKind::Count:
            break;
    }
    unreachable_kind();
}

enum class BooleanKind {
    Literal,
    CompareIntegers,
    CompareNumbers,
    CompareStrings,
    Logical,
    Not,
    Count
};

template <typename Provider>
[[nodiscard]] std::string generate_boolean_expression(Provider& fdp, int depth) {
    const auto sub = [&](ValueType type) {
        return generate_typed_expression(fdp, type, depth + 1);
    };

    switch (pick_kind<BooleanKind>(fdp)) {
        case BooleanKind::Literal:
            return generate_typed_literal(fdp, ValueType::Boolean);
        case BooleanKind::CompareIntegers: {
            const auto op = pick(fdp, std::array{" == ", " != ", " < ", " > ", " <= ", " >= "});
            return "(" + sub(ValueType::Integer) + op + sub(ValueType::Integer) + ")";
        }
        case BooleanKind::CompareNumbers: { // Ordering only: `==` on floats is linted.
            const auto op = pick(fdp, std::array{" < ", " > ", " <= ", " >= "});
            return "(" + sub(ValueType::Number) + op + sub(ValueType::Number) + ")";
        }
        case BooleanKind::CompareStrings: {
            const auto op = pick(fdp, std::array{" == ", " != "});
            return "(" + sub(ValueType::String) + op + sub(ValueType::String) + ")";
        }
        case BooleanKind::Logical: {
            const auto op = pick(fdp, std::array{" && ", " || "});
            return "(" + sub(ValueType::Boolean) + op + sub(ValueType::Boolean) + ")";
        }
        case BooleanKind::Not:
            return "!(" + sub(ValueType::Boolean) + ")";
        case BooleanKind::Count:
            break;
    }
    unreachable_kind();
}

enum class StringKind {
    Literal,
    Interpolation,
    Concatenation,
    Transform,
    PipeTransform,
    Count
};

template <typename Provider>
[[nodiscard]] std::string generate_string_expression(Provider& fdp, int depth) {
    const auto sub = [&](ValueType type) {
        return generate_typed_expression(fdp, type, depth + 1);
    };
    constexpr std::array transforms = {"uppercase", "lowercase", "trim", "reverse"};

    switch (pick_kind<StringKind>(fdp)) {
        case StringKind::Literal:
            return generate_typed_literal(fdp, ValueType::String);
        case StringKind::Interpolation: {
            // Interpolate a non-string value so no quotes nest inside `${…}`.
            const auto inner = pick_kind<ValueType>(fdp);
            return "\"v=${" + sub(inner == ValueType::String ? ValueType::Integer : inner) + "}\"";
        }
        case StringKind::Concatenation:
            return "(" + sub(ValueType::String) + " + " + sub(ValueType::String) + ")";
        case StringKind::Transform:
            return "String." + std::string{pick(fdp, transforms)} + "(" + sub(ValueType::String) +
                   ")";
        case StringKind::PipeTransform:
            return "(" + sub(ValueType::String) + " |> String." +
                   std::string{pick(fdp, transforms)} + "())";
        case StringKind::Count:
            break;
    }
    unreachable_kind();
}

template <typename Provider>
[[nodiscard]] std::string generate_typed_expression(Provider& fdp, ValueType type, int depth) {
    if (depth >= k_max_typed_expression_depth || fdp.remaining_bytes() < 4) {
        return generate_typed_literal(fdp, type);
    }

    switch (type) {
        case ValueType::Integer:
            return generate_integer_expression(fdp, depth);
        case ValueType::Number:
            return generate_number_expression(fdp, depth);
        case ValueType::Boolean:
            return generate_boolean_expression(fdp, depth);
        case ValueType::String:
            return generate_string_expression(fdp, depth);
        case ValueType::Count:
            break;
    }
    unreachable_kind();
}

enum class TypedStatementKind {
    Declaration,
    MutableUpdate,
    Print,
    IfElse,
    ForLoop,
    WhileLoop,
    TryCatch,
    Decoder,
    RecordUsage,
    TupleUsage,
    AliasUsage,
    InterfaceUsage,
    NamespaceUsage,
    Concurrency,
    Converter,
    LinearAlgebra,
    Math,
    Count
};

template <typename Provider>
[[nodiscard]] std::string generate_typed_statement(Provider& fdp, int depth);

// A loop body: a statement optionally followed by a guarded break / continue,
// reaching the compiler's loop jump-patching and the VM's loop control.
template <typename Provider>
[[nodiscard]] std::string generate_typed_loop_body(Provider& fdp, int depth) {
    std::string body = generate_typed_statement(fdp, depth + 1);

    switch (fdp.template ConsumeIntegralInRange<int>(0, 3)) {
        case 0:
            body += "if " + generate_typed_expression(fdp, ValueType::Boolean, 0) + " { break }\n";
            break;
        case 1:
            body +=
                "if " + generate_typed_expression(fdp, ValueType::Boolean, 0) + " { continue }\n";
            break;
        default:
            break; // No loop-control statement on this body.
    }

    return body;
}

template <typename Provider>
[[nodiscard]] std::string generate_typed_statement(Provider& fdp, int depth) {
    if (depth > k_max_typed_statement_depth || fdp.remaining_bytes() < 8) {
        return "print(" + generate_typed_literal(fdp, ValueType::Integer) + ")\n";
    }

    switch (pick_kind<TypedStatementKind>(fdp)) {
        case TypedStatementKind::Declaration: {
            const auto type = pick_kind<ValueType>(fdp);
            const auto name = "v" + fresh_suffix(fdp);
            return type_name(type) + " " + name + " = " + generate_typed_expression(fdp, type, 0) +
                   "\nprint(" + name + ")\n";
        }
        case TypedStatementKind::MutableUpdate: {
            const auto name = "m" + fresh_suffix(fdp);
            return "mutable integer " + name + " = " +
                   generate_typed_expression(fdp, ValueType::Integer, 0) + "\n" + name + " = " +
                   name + " + " + generate_typed_expression(fdp, ValueType::Integer, 0) +
                   "\nprint(" + name + ")\n";
        }
        case TypedStatementKind::Print:
            return "print(" + generate_typed_expression(fdp, pick_kind<ValueType>(fdp), 0) + ")\n";
        case TypedStatementKind::IfElse:
            return "if " + generate_typed_expression(fdp, ValueType::Boolean, 0) + " {\n" +
                   generate_typed_statement(fdp, depth + 1) + "} else {\n" +
                   generate_typed_statement(fdp, depth + 1) + "}\n";
        case TypedStatementKind::ForLoop: {
            const auto name = "i" + fresh_suffix(fdp);
            const auto limit = generate_int_literal(fdp, 0, 20);
            return "for " + name + " in 0.." + limit + " {\nprint(" + name + ")\n" +
                   generate_typed_loop_body(fdp, depth) + "}\n";
        }
        case TypedStatementKind::WhileLoop: {
            // A counter incremented first keeps the loop bounded even when the
            // body continues early.
            const auto name = "w" + fresh_suffix(fdp);
            const auto limit = generate_int_literal(fdp, 0, 20);
            return "mutable integer " + name + " = 0\nwhile " + name + " < " + limit + " {\n" +
                   name + " = " + name + " + 1\n" + generate_typed_loop_body(fdp, depth) + "}\n";
        }
        case TypedStatementKind::TryCatch: {
            std::string out = "try {\n" + generate_typed_statement(fdp, depth + 1) +
                              "} catch(err) {\nprint(err)\n}";
            if (fdp.ConsumeBool()) {
                out += " finally {\n" + generate_typed_statement(fdp, depth + 1) + "}";
            }
            return out + "\n";
        }
        case TypedStatementKind::Decoder:
            return generate_decoder_statement(fdp);
        case TypedStatementKind::RecordUsage:
            return generate_record_usage(fdp);
        case TypedStatementKind::TupleUsage:
            return generate_tuple_usage(fdp);
        case TypedStatementKind::AliasUsage:
            return generate_alias_usage(fdp);
        case TypedStatementKind::InterfaceUsage:
            return generate_interface_usage(fdp);
        case TypedStatementKind::NamespaceUsage:
            return generate_namespace_statement(fdp);
        case TypedStatementKind::Concurrency:
            return generate_concurrency_statement(fdp);
        case TypedStatementKind::Converter:
            return generate_converter_statement(fdp);
        case TypedStatementKind::LinearAlgebra:
            return generate_linearalgebra_statement(fdp);
        case TypedStatementKind::Math:
            return generate_math_statement(fdp);
        case TypedStatementKind::Count:
            break;
    }
    unreachable_kind();
}

} // namespace luma::fuzz::gen

#endif // LUMA_FUZZ_FUZZ_GEN_TYPED_HPP
