#ifndef LUMA_FUZZ_FUZZ_GEN_GRAMMAR_HPP
#define LUMA_FUZZ_FUZZ_GEN_GRAMMAR_HPP

#include <string>

#include "fuzz_gen_features.hpp"
#include "fuzz_gen_stdlib.hpp"
#include "fuzz_gen_vocab.hpp"

// The recursive "chaos" expression and statement grammar.
//
// Deliberately generates shapes that are often ill-typed — its purpose is to
// stress parser recovery and the type checker's error paths, where a single
// ill-typed statement rejects the whole program.  The well-typed grammar in
// fuzz_gen_typed.hpp is the one that reliably reaches the compiler and VM.
// generate_chaos_statement still dispatches to the type-correct feature and
// stdlib emitters for some branches, so a lucky chaos program also runs.  See
// fuzz_source_generator.hpp for the Provider concept these templates require.

namespace luma::fuzz::gen {

inline constexpr int k_max_expression_depth = 5;
inline constexpr int k_max_statement_depth = 3;

template <typename Provider> [[nodiscard]] std::string generate_literal(Provider& fdp) {
    switch (fdp.template ConsumeIntegralInRange<int>(0, 4)) {
        case 0:
            return generate_int_literal(fdp, -1000, 1000);
        case 1:
            return std::to_string(fdp.template ConsumeFloatingPointInRange<double>(-100.0, 100.0));
        case 2:
            return generate_string_literal(fdp);
        case 3:
            return fdp.ConsumeBool() ? "true" : "false";
        default:
            return "0";
    }
}

enum class ChaosExpressionKind {
    Literal,
    Identifier,
    Binary,
    IfElse,
    ArrayLiteral,
    Parenthesised,
    Call,
    Lambda,
    Pipe,
    Interpolation,
    Unary,
    NamedCall,
    Tuple,
    Some,
    NullCoalesce,
    OptionalChain,
    ErrorPipe,
    Match,
    Count
};

template <typename Provider>
[[nodiscard]] std::string generate_expression(Provider& fdp, int depth) {
    if (depth > k_max_expression_depth || fdp.remaining_bytes() < 4) {
        return generate_literal(fdp);
    }

    switch (pick_kind<ChaosExpressionKind>(fdp)) {
        case ChaosExpressionKind::Literal:
            return generate_literal(fdp);
        case ChaosExpressionKind::Identifier:
            return generate_identifier(fdp);
        case ChaosExpressionKind::Binary:
            return generate_expression(fdp, depth + 1) + " " + generate_binop(fdp) + " " +
                   generate_expression(fdp, depth + 1);
        case ChaosExpressionKind::IfElse:
            return "if " + generate_expression(fdp, depth + 1) + " { " +
                   generate_expression(fdp, depth + 1) + " } else { " +
                   generate_expression(fdp, depth + 1) + " }";
        case ChaosExpressionKind::ArrayLiteral:
            return "[" + generate_expression(fdp, depth + 1) + ", " +
                   generate_expression(fdp, depth + 1) + "]";
        case ChaosExpressionKind::Parenthesised:
            return "(" + generate_expression(fdp, depth + 1) + ")";
        case ChaosExpressionKind::Call:
            return generate_identifier(fdp) + "(" + generate_expression(fdp, depth + 1) + ")";
        case ChaosExpressionKind::Lambda: {
            const int param_count = fdp.template ConsumeIntegralInRange<int>(0, 2);
            std::string params;
            for (int i = 0; i < param_count; ++i) {
                if (i > 0) {
                    params += ", ";
                }
                params += generate_type(fdp) + " " + generate_identifier(fdp);
            }
            // Roughly half the time emit a block body (`-> { return expr }`) to
            // exercise the statement-bodied lambda path through the parser,
            // compiler (block scope + MakeClosure) and VM; otherwise an
            // expression body.
            if (fdp.ConsumeBool()) {
                return "(" + params + ") -> { return " + generate_expression(fdp, depth + 1) + " }";
            }
            return "(" + params + ") -> " + generate_expression(fdp, depth + 1);
        }
        case ChaosExpressionKind::Pipe:
            return generate_expression(fdp, depth + 1) + " |> " +
                   std::string{pick(fdp, k_pipe_stages)};
        case ChaosExpressionKind::Interpolation:
            return "\"v=${" + generate_identifier(fdp) + " " + generate_binop(fdp) + " " +
                   generate_identifier(fdp) + "}\"";
        case ChaosExpressionKind::Unary: // A space keeps `- -x` from lexing as `--`.
            return generate_unaryop(fdp) + " " + generate_expression(fdp, depth + 1);
        case ChaosExpressionKind::NamedCall: {
            // A random mix of leading positional arguments and one or more
            // trailing named arguments exercises the parser's argument-list
            // split, the compiler (CallNamed), and the VM's combined positional
            // + named binding, including the unknown-named-argument path.
            const int positional_count = fdp.template ConsumeIntegralInRange<int>(0, 2);
            const int named_count = fdp.template ConsumeIntegralInRange<int>(1, 2);
            std::string call = generate_identifier(fdp) + "(";
            bool first = true;
            for (int i = 0; i < positional_count; ++i) {
                if (!first) {
                    call += ", ";
                }
                call += generate_expression(fdp, depth + 1);
                first = false;
            }
            for (int i = 0; i < named_count; ++i) {
                if (!first) {
                    call += ", ";
                }
                call += generate_identifier(fdp) + ": " + generate_expression(fdp, depth + 1);
                first = false;
            }
            return call + ")";
        }
        case ChaosExpressionKind::Tuple: { // 2–4 elements: the tuple-arity check.
            const int elem_count = fdp.template ConsumeIntegralInRange<int>(2, 4);
            std::string out = "(";
            for (int i = 0; i < elem_count; ++i) {
                if (i > 0) {
                    out += ", ";
                }
                out += generate_expression(fdp, depth + 1);
            }
            return out + ")";
        }
        case ChaosExpressionKind::Some:
            return "some(" + generate_expression(fdp, depth + 1) + ")";
        case ChaosExpressionKind::NullCoalesce:
            return generate_expression(fdp, depth + 1) + " ?? " +
                   generate_expression(fdp, depth + 1);
        case ChaosExpressionKind::OptionalChain: // GetFieldOpt / IndexGetOpt.
            if (fdp.ConsumeBool()) {
                return generate_identifier(fdp) + "?." + generate_identifier(fdp);
            }
            return generate_identifier(fdp) + "?[" + generate_expression(fdp, depth + 1) + "]";
        case ChaosExpressionKind::ErrorPipe:
            return "success(" + generate_expression(fdp, depth + 1) + ") !> " +
                   std::string{pick(fdp, k_pipe_stages)};
        case ChaosExpressionKind::Match:
            return "match " + generate_expression(fdp, depth + 1) + " { case " +
                   generate_int_literal(fdp, 0, 1000) + " { " +
                   generate_expression(fdp, depth + 1) + " } else { " +
                   generate_expression(fdp, depth + 1) + " } }";
        case ChaosExpressionKind::Count:
            break;
    }
    unreachable_kind();
}

template <typename Provider>
[[nodiscard]] std::string generate_chaos_loop_body(Provider& fdp, int depth);

enum class ChaosStatementKind {
    Declaration,
    ForLoop,
    If,
    IfElse,
    WhileLoop,
    Print,
    TypedDeclaration,
    Decoder,
    TryCatch,
    RecordUsage,
    TupleUsage,
    AliasUsage,
    InterfaceUsage,
    NamespaceUsage,
    Concurrency,
    Converter,
    LinearAlgebra,
    Math,
    Assignment,
    Count
};

template <typename Provider>
[[nodiscard]] std::string generate_chaos_statement(Provider& fdp, int depth) {
    if (depth > k_max_statement_depth || fdp.remaining_bytes() < 8) {
        return generate_identifier(fdp) + " = " + generate_expression(fdp, 0) + "\n";
    }

    switch (pick_kind<ChaosStatementKind>(fdp)) {
        case ChaosStatementKind::Declaration: {
            // Luma declarations are always type-first, so a type is always
            // emitted; it is optionally preceded by `mutable` and an ownership
            // qualifier.  The qualifiers reach the type checker's linear/affine
            // tracking: consumption on read, borrow read-only enforcement, the
            // never-consumed warning, and the flow-sensitive ownership merge
            // across the control-flow statements this generator also produces.
            std::string prefix = fdp.ConsumeBool() ? "mutable " : "";
            switch (fdp.template ConsumeIntegralInRange<int>(0, 2)) {
                case 1:
                    prefix += "unique ";
                    break;
                case 2:
                    prefix += "borrow ";
                    break;
                default:
                    break;
            }
            return prefix + generate_type(fdp) + " " + generate_identifier(fdp) + " = " +
                   generate_expression(fdp, 0) + "\n";
        }
        case ChaosStatementKind::ForLoop: {
            const auto var = generate_identifier(fdp);
            const int limit = fdp.template ConsumeIntegralInRange<int>(1, 20);
            return "for " + var + " in 0.." + std::to_string(limit) + " {\n" +
                   generate_chaos_loop_body(fdp, depth) + "}\n";
        }
        case ChaosStatementKind::If:
            return "if " + generate_expression(fdp, 0) + " {\n" +
                   generate_chaos_statement(fdp, depth + 1) + "}\n";
        case ChaosStatementKind::IfElse:
            return "if " + generate_expression(fdp, 0) + " {\n" +
                   generate_chaos_statement(fdp, depth + 1) + "} else {\n" +
                   generate_chaos_statement(fdp, depth + 1) + "}\n";
        case ChaosStatementKind::WhileLoop:
            return "while " + generate_expression(fdp, 0) + " {\n" +
                   generate_chaos_loop_body(fdp, depth) + "}\n";
        case ChaosStatementKind::Print:
            return "print(" + generate_expression(fdp, 0) + ")\n";
        case ChaosStatementKind::TypedDeclaration:
            return generate_type(fdp) + " " + generate_identifier(fdp) + " = " +
                   generate_expression(fdp, 0) + "\n";
        case ChaosStatementKind::Decoder:
            return generate_decoder_statement(fdp);
        case ChaosStatementKind::TryCatch: {
            std::string out = "try {\n" + generate_chaos_statement(fdp, depth + 1) + "}";
            const bool with_catch = fdp.ConsumeBool();
            const bool with_finally = fdp.ConsumeBool();
            // The grammar requires a catch when no finally is present, so default
            // to emitting catch unless finally was selected on its own.
            if (with_catch || !with_finally) {
                out += " catch(err) {\n" + generate_chaos_statement(fdp, depth + 1) + "}";
            }
            if (with_finally) {
                out += " finally {\n" + generate_chaos_statement(fdp, depth + 1) + "}";
            }
            return out + "\n";
        }
        case ChaosStatementKind::RecordUsage:
            return generate_record_usage(fdp);
        case ChaosStatementKind::TupleUsage:
            return generate_tuple_usage(fdp);
        case ChaosStatementKind::AliasUsage:
            return generate_alias_usage(fdp);
        case ChaosStatementKind::InterfaceUsage:
            return generate_interface_usage(fdp);
        case ChaosStatementKind::NamespaceUsage:
            return generate_namespace_statement(fdp);
        case ChaosStatementKind::Concurrency:
            return generate_concurrency_statement(fdp);
        case ChaosStatementKind::Converter:
            return generate_converter_statement(fdp);
        case ChaosStatementKind::LinearAlgebra:
            return generate_linearalgebra_statement(fdp);
        case ChaosStatementKind::Math:
            return generate_math_statement(fdp);
        case ChaosStatementKind::Assignment:
            return generate_identifier(fdp) + " = " + generate_expression(fdp, 0) + "\n";
        case ChaosStatementKind::Count:
            break;
    }
    unreachable_kind();
}

// Generate a loop body: a normal statement optionally followed by a guarded
// `break` or `continue`.  Emitting these only inside loop bodies keeps them
// past the "loop control outside a loop" check, so the compiler's loop
// jump-patching and the VM's break/continue handling are reachable.
template <typename Provider>
[[nodiscard]] std::string generate_chaos_loop_body(Provider& fdp, int depth) {
    std::string body = generate_chaos_statement(fdp, depth + 1);

    switch (fdp.template ConsumeIntegralInRange<int>(0, 3)) {
        case 0:
            body += "if " + generate_expression(fdp, 0) + " { break }\n";
            break;
        case 1:
            body += "if " + generate_expression(fdp, 0) + " { continue }\n";
            break;
        default:
            break; // No loop-control statement on this body.
    }

    return body;
}

} // namespace luma::fuzz::gen

#endif // LUMA_FUZZ_FUZZ_GEN_GRAMMAR_HPP
