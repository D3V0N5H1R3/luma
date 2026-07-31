#pragma once

#include <string>

#include "fuzz_gen_stdlib.hpp"
#include "fuzz_gen_vocab.hpp"

// The recursive Luma expression and statement grammar.
//
// Deliberately generates shapes that are often ill-typed — its purpose is to
// stress the parser recovery, resolver, type checker, compiler and VM error
// paths.  generate_statement dispatches to the type-correct stdlib feature
// emitters in fuzz_gen_stdlib.hpp for the branches that must reach the VM.  See
// fuzz_source_generator.hpp for the Provider concept these templates require.

namespace luma::fuzz::gen {

inline constexpr int max_expression_depth = 5;
inline constexpr int max_statement_depth = 3;

// Build a guaranteed-valid Luma string literal from fuzzer bytes.  Luma only
// recognises the escapes \n \t \r \0 \\ \" and \$ and rejects raw control or
// non-ASCII bytes, so each character is either emitted as a safe escape, kept
// as printable ASCII, or dropped.  The decoded runtime string still carries the
// structural characters ({ } [ ] : , " \) that drive the data-format parsers.
template <typename Provider> [[nodiscard]] std::string generate_string_literal(Provider& fdp) {
    const auto raw = fdp.ConsumeRandomLengthString(32);
    std::string literal = "\"";
    for (const char ch : raw) {
        switch (ch) {
            case '"':
                literal += "\\\"";
                break;
            case '\\':
                literal += "\\\\";
                break;
            case '\n':
                literal += "\\n";
                break;
            case '\t':
                literal += "\\t";
                break;
            case '\r':
                literal += "\\r";
                break;
            case '$':
                literal += "\\$";
                break;
            default: {
                const auto byte = static_cast<unsigned char>(ch);
                if (byte >= 0x20U && byte < 0x7FU) {
                    literal += ch;
                }
                break;
            }
        }
    }
    literal += "\"";
    return literal;
}

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

template <typename Provider>
[[nodiscard]] std::string generate_expression(Provider& fdp, int depth) {
    if (depth > max_expression_depth || fdp.remaining_bytes() < 4) {
        return generate_literal(fdp);
    }

    constexpr int k_expression_kinds = 18; // Keep in sync with the switch arms below.
    switch (fdp.template ConsumeIntegralInRange<int>(0, k_expression_kinds - 1)) {
        case 0:
            return generate_literal(fdp);
        case 1:
            return generate_identifier(fdp);
        case 2:
            return generate_expression(fdp, depth + 1) + " " + generate_binop(fdp) + " " +
                   generate_expression(fdp, depth + 1);
        case 3:
            return "if " + generate_expression(fdp, depth + 1) + " { " +
                   generate_expression(fdp, depth + 1) + " } else { " +
                   generate_expression(fdp, depth + 1) + " }";
        case 4:
            return "[" + generate_expression(fdp, depth + 1) + ", " +
                   generate_expression(fdp, depth + 1) + "]";
        case 5:
            return "(" + generate_expression(fdp, depth + 1) + ")";
        case 6:
            return generate_identifier(fdp) + "(" + generate_expression(fdp, depth + 1) + ")";
        case 7: { // Lambda expression.
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
        case 8: // Pipe chain into a stdlib stage.
            return generate_expression(fdp, depth + 1) + " |> " +
                   std::string{pick(fdp, pipe_stages)};
        case 9: // String interpolation with an embedded expression.
            return "\"v=${" + generate_identifier(fdp) + " " + generate_binop(fdp) + " " +
                   generate_identifier(fdp) + "}\"";
        case 10: // Unary prefix operator. A space prevents adjacent operators
                 // (e.g. `- -x`) from lexing as `--`/`++`/etc.
            return generate_unaryop(fdp) + " " + generate_expression(fdp, depth + 1);
        case 11: { // Named-argument call. The callee and argument names are
                   // drawn from the shared identifier pool, so some calls bind
                   // cleanly while others surface the VM's unknown-named-argument
                   // path. A random mix of leading positional arguments and one
                   // or more trailing named arguments exercises the parser's
                   // argument-list split, the compiler (CallNamed), and the VM's
                   // combined positional + named binding — not just the single
                   // named-argument shape.
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
        case 12: { // Tuple literal (2–4 elements). Reaches the tuple-arity check
                   // in the type checker and the MakeTuple opcode in the compiler
                   // and VM whenever the surrounding program is type-correct.
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
        case 13: // Optional value: some(expr). Reaches the MakeSome opcode in the
                 // compiler and VM whenever the surrounding program type-checks.
            return "some(" + generate_expression(fdp, depth + 1) + ")";
        case 14: // Null-coalescing: a ?? b. Drives the NullCoalesce opcode and the
                 // optional-unwrap path; pairing some()/none with a fallback keeps
                 // many of these well-typed and reaches the VM.
            return generate_expression(fdp, depth + 1) + " ?? " +
                   generate_expression(fdp, depth + 1);
        case 15: // Optional chaining: ident?.field or ident?[index]. Exercises the
                 // GetFieldOpt / IndexGetOpt opcodes and the parser's postfix
                 // optional-access grammar. Type mismatches are tolerated — the
                 // program is dropped before the VM, never crashing.
            if (fdp.ConsumeBool()) {
                return generate_identifier(fdp) + "?." + generate_identifier(fdp);
            }
            return generate_identifier(fdp) + "?[" + generate_expression(fdp, depth + 1) + "]";
        case 16: // Error-pipe chain. Wrapping the left in success() keeps many of
                 // these well-typed so the VM's error-pipe opcodes (Dup, IsSuccess,
                 // Unwrap, EnsureSuccess) and the short-circuit jump are reached; the
                 // piped value becomes the stage call's implicit first argument.
            return "success(" + generate_expression(fdp, depth + 1) + ") !> " +
                   std::string{pick(fdp, pipe_stages)};
        default: // Match expression with a literal arm.
            return "match " + generate_expression(fdp, depth + 1) + " { case " +
                   generate_int_literal(fdp, 0, 1000) + " { " +
                   generate_expression(fdp, depth + 1) + " } else { " +
                   generate_expression(fdp, depth + 1) + " } }";
    }
}

template <typename Provider> [[nodiscard]] std::string generate_loop_body(Provider& fdp, int depth);

template <typename Provider>
[[nodiscard]] std::string generate_statement(Provider& fdp, int depth) {
    if (depth > max_statement_depth || fdp.remaining_bytes() < 8) {
        return generate_identifier(fdp) + " = " + generate_expression(fdp, 0) + "\n";
    }

    constexpr int k_statement_kinds = 19; // Keep in sync with the switch arms below.
    switch (fdp.template ConsumeIntegralInRange<int>(0, k_statement_kinds - 1)) {
        case 0: { // Variable declaration — optionally mutable and/or owned.
            std::string prefix;
            if (fdp.ConsumeBool()) {
                prefix += "mutable ";
            }
            // Ownership qualifiers require an explicit type annotation.  Emitting
            // them here reaches the type checker's linear/affine tracking:
            // consumption on read, borrow read-only enforcement, the
            // never-consumed warning, and the flow-sensitive ownership merge
            // across the control-flow statements this generator also produces.
            switch (fdp.template ConsumeIntegralInRange<int>(0, 2)) {
                case 1:
                    prefix += "unique " + generate_type(fdp) + " ";
                    break;
                case 2:
                    prefix += "borrow " + generate_type(fdp) + " ";
                    break;
                default:
                    break;
            }
            return prefix + generate_identifier(fdp) + " = " + generate_expression(fdp, 0) + "\n";
        }
        case 1: { // For loop
            const auto var = generate_identifier(fdp);
            const int limit = fdp.template ConsumeIntegralInRange<int>(1, 20);
            return "for " + var + " in 0.." + std::to_string(limit) + " {\n" +
                   generate_loop_body(fdp, depth) + "}\n";
        }
        case 2: { // If statement
            return "if " + generate_expression(fdp, 0) + " {\n" +
                   generate_statement(fdp, depth + 1) + "}\n";
        }
        case 3: { // If/else statement
            return "if " + generate_expression(fdp, 0) + " {\n" +
                   generate_statement(fdp, depth + 1) + "} else {\n" +
                   generate_statement(fdp, depth + 1) + "}\n";
        }
        case 4: { // While loop
            return "while " + generate_expression(fdp, 0) + " {\n" +
                   generate_loop_body(fdp, depth) + "}\n";
        }
        case 5: { // Print
            return "print(" + generate_expression(fdp, 0) + ")\n";
        }
        case 6: { // Typed variable
            return generate_type(fdp) + " " + generate_identifier(fdp) + " = " +
                   generate_expression(fdp, 0) + "\n";
        }
        case 7: { // Decode a generated string with a stdlib data-format parser.
            return "print(" + std::string{pick(fdp, decoders)} + "(" +
                   generate_string_literal(fdp) + "))\n";
        }
        case 8: { // Try / catch / finally — at least one of catch or finally is present.
            std::string out = "try {\n" + generate_statement(fdp, depth + 1) + "}";
            const bool with_catch = fdp.ConsumeBool();
            const bool with_finally = fdp.ConsumeBool();
            // The grammar requires a catch when no finally is present, so default
            // to emitting catch unless finally was selected on its own.
            if (with_catch || !with_finally) {
                out += " catch(err) {\n" + generate_statement(fdp, depth + 1) + "}";
            }
            if (with_finally) {
                out += " finally {\n" + generate_statement(fdp, depth + 1) + "}";
            }
            return out + "\n";
        }
        case 9: { // Type-correct record use: construct, mutate, access, and `with`.
            // Self-contained and integer-typed so it survives type-checking and
            // reaches the VM, exercising MakeRecord / SetField / GetField /
            // RecordWith. The fdp-derived suffix keeps the variable name distinct
            // from the shared identifier pool; a rare collision merely yields a
            // type error (the program is dropped), never a crash.
            const auto suffix = fresh_suffix(fdp);
            const auto a = generate_int_literal(fdp, -1000, 1000);
            std::string ctor = "FuzzRec { a = " + a;
            // Roughly half the time omit the defaulted field `b` to exercise the
            // compiler's default-value filling through to the VM.
            if (fdp.ConsumeBool()) {
                ctor += ", b = " + generate_int_literal(fdp, -1000, 1000);
            }
            ctor += " }";
            const auto name = "fr" + suffix;
            return "mutable FuzzRec " + name + " = " + ctor + "\n" + name + ".a = " + name +
                   ".b\n" + "print((" + name + " with { a = " + a + " }).b)\n";
        }
        case 10: { // Type-correct tuple use: construct, access, and destructure.
            // Self-contained and integer-typed so it survives type-checking and
            // reaches the VM, exercising MakeTuple, tuple field access (.0), and
            // the destructuring unpack path. The fdp-derived suffix keeps the
            // binding names distinct from the shared identifier pool; a rare
            // collision merely yields a type error (the program is dropped),
            // never a crash.
            const auto suffix = fresh_suffix(fdp);
            const auto a = generate_int_literal(fdp, -1000, 1000);
            const auto b = generate_int_literal(fdp, -1000, 1000);
            const auto name = "tup" + suffix;
            std::string out = "(integer, integer) " + name + " = (" + a + ", " + b + ")\n";
            out += "print(" + name + ".0)\n";
            out += "(integer x" + suffix + ", integer y" + suffix + ") = " + name + "\n";
            out += "print(x" + suffix + " + y" + suffix + ")\n";
            return out;
        }
        case 11: { // Type-correct type-alias use: chained, generic, function,
                   // and collection aliases declared once in generate_program.
                   // Self-contained and integer-typed so it survives
                   // type-checking and reaches the VM, exercising the type
                   // checker's alias registration and resolution (including the
                   // chained-alias cycle guard and generic-alias binding). The
                   // fdp-derived suffix keeps the binding names distinct from
                   // the shared identifier pool; a rare collision merely yields
                   // a type error (the program is dropped), never a crash.
            const auto suffix = fresh_suffix(fdp);
            const auto a = generate_int_literal(fdp, -1000, 1000);
            const auto b = generate_int_literal(fdp, -1000, 1000);
            std::string out;
            // Chained alias: FuzzChain -> FuzzAlias -> integer.
            out += "FuzzChain fc" + suffix + " = " + a + "\n";
            out += "print(fc" + suffix + ")\n";
            // Generic alias: FuzzPair<T> = (T, T).
            out += "FuzzPair<integer> fp" + suffix + " = (" + a + ", " + b + ")\n";
            out += "print(fp" + suffix + ".0)\n";
            // Function-type alias: FuzzFn = function(integer) -> integer.
            out += "FuzzFn ff" + suffix + " = (integer n) -> n + 1\n";
            out += "print(ff" + suffix + "(" + a + "))\n";
            // Collection alias: FuzzList = array<integer>.
            out += "FuzzList fl" + suffix + " = [" + a + ", " + b + "]\n";
            out += "print(Array.length(fl" + suffix + "))\n";
            return out;
        }
        case 12: { // Type-correct interface use: construct a record, pass it where
                   // an interface is expected (plain and generic), and read an
                   // interface field.  Self-contained and integer-typed so it
                   // survives type-checking and reaches the VM.  This drives the
                   // type checker's structural interface satisfaction
                   // (check_structural_satisfaction, including the generic
                   // prefixed-binding save/restore path) and the compiler/VM's
                   // handling of an interface-typed parameter and interface field
                   // access.  The fixed FuzzIface / FuzzImpl / FuzzBox / FuzzItem
                   // declarations are emitted once in generate_program.  The
                   // fdp-derived suffix keeps the binding names distinct from the
                   // shared identifier pool; a rare collision merely yields a type
                   // error (the program is dropped), never a crash.
            const auto suffix = fresh_suffix(fdp);
            const auto v = generate_int_literal(fdp, -1000, 1000);
            std::string out;
            const auto flag = fdp.ConsumeBool() ? "true" : "false";
            out += "FuzzImpl fi" + suffix + " = FuzzImpl { ival = " + v + ", flag = " + flag +
                   ", extra = 0 }\n";
            out += "print(fuzz_use_iface(fi" + suffix + "))\n";
            out += "FuzzItem bi" + suffix + " = FuzzItem { item = " + v + " }\n";
            out += "print(fuzz_unbox(bi" + suffix + "))\n";
            return out;
        }
        case 13: // Namespace-qualified and use-imported member access.
            return generate_namespace_statement(fdp);
        case 14: // Structured concurrency: task_scope / spawn / await / channels.
            return generate_concurrency_statement(fdp);
        case 15: { // Drive the Converter module's hand-written parsers and encoders.
            // Half the time parse a generated string — reaching the UTF-8
            // decoder in character_to_codepoint and the roman/base/numeric
            // scanners — otherwise format a generated integer, reaching the
            // base/roman/word/ordinal encoders and the UTF-8 codepoint encoder
            // together with their range guards.  Both forms are well-typed and
            // wrapped in print, so they survive type-checking and run end-to-end.
            if (fdp.ConsumeBool()) {
                return "print(" + std::string{pick(fdp, converter_string_parsers)} + "(" +
                       generate_string_literal(fdp) + "))\n";
            }
            const auto n =
                std::to_string(fdp.template ConsumeIntegralInRange<int>(-2000000000, 2000000000));
            return "print(" + std::string{pick(fdp, converter_int_formatters)} + "(" + n + "))\n";
        }
        case 16: // Type-correct LinearAlgebra usage: one well-typed operation.
            return generate_linearalgebra_statement(fdp);
        case 17: // Type-correct Math usage: one well-typed operation.
            return generate_math_statement(fdp);
        default: // Assignment
            return generate_identifier(fdp) + " = " + generate_expression(fdp, 0) + "\n";
    }
}

// Generate a loop body: a normal statement optionally followed by a guarded
// `break` or `continue`.  Emitting these only inside loop bodies keeps the
// program type-correct so the compiler's loop jump-patching and the VM's
// break/continue handling are reached, rather than being rejected up front as
// loop-control statements outside a loop.
template <typename Provider>
[[nodiscard]] std::string generate_loop_body(Provider& fdp, int depth) {
    std::string body = generate_statement(fdp, depth + 1);

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
