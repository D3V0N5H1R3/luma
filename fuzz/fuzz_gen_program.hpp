#ifndef LUMA_FUZZ_FUZZ_GEN_PROGRAM_HPP
#define LUMA_FUZZ_FUZZ_GEN_PROGRAM_HPP

#include <cstddef>
#include <string>

#include "fuzz_gen_grammar.hpp"
#include "fuzz_gen_typed.hpp"
#include "fuzz_gen_vocab.hpp"

// Declaration builders and whole-program assembly.
//
// generate_program picks one of two modes per input:
//   * well-typed — only the typed grammar (fuzz_gen_typed.hpp), well-typed
//     records, choices and functions, and the curated emitters, so the program
//     type-checks and reaches the compiler, optimiser, verifier and VM;
//   * chaos — the deliberately ill-typed grammar (fuzz_gen_grammar.hpp), which
//     stresses parser recovery and the type checker's error paths.
// Mixing the two in one program would let a single ill-typed statement reject
// all of it, which is why the mode is chosen up front.  Both modes share the
// fixed preludes (records, type aliases, interfaces, a namespace, and a
// concurrency helper) that the curated emitters rely on.  See
// fuzz_source_generator.hpp for the Provider concept these templates require.

namespace luma::fuzz::gen {

inline constexpr std::size_t k_min_structured_input = 8;
inline constexpr std::size_t k_max_structured_input = 4096;

// The fixed declarations every program starts with: the targets of the curated
// feature and stdlib emitters in fuzz_gen_features.hpp / fuzz_gen_stdlib.hpp.
// Every name is disjoint from the generated record, choice, function, and
// identifier pools.
[[nodiscard]] inline std::string generate_prelude() {
    std::string prelude;

    // A record with one required and one defaulted field (generate_record_usage).
    prelude += "record FuzzRec {\n    integer a,\n    integer b = 0\n}\n\n";

    // Simple, chained, collection, function, and generic type aliases
    // (generate_alias_usage).  FuzzChain -> FuzzAlias -> integer drives the
    // recursive-alias cycle guard; FuzzPair<T> generic-alias binding.
    prelude += "type FuzzAlias = integer\n"
               "type FuzzChain = FuzzAlias\n"
               "type FuzzList = array<integer>\n"
               "type FuzzFn = function(integer) -> integer\n"
               "type FuzzPair<T> = (T, T)\n\n";

    // A plain interface and a satisfying record with an extra field, plus a
    // generic interface and its satisfying record (generate_interface_usage).
    prelude += "interface FuzzIface {\n    integer ival,\n    boolean flag\n}\n\n"
               "record FuzzImpl {\n    integer ival,\n    boolean flag,\n    integer extra\n}\n\n"
               "function integer fuzz_use_iface(FuzzIface fi) {\n"
               "    if fi.flag {\n        return fi.ival\n    }\n    return 0 - fi.ival\n}\n\n"
               "interface FuzzBox<T> {\n    T item\n}\n\n"
               "record FuzzItem {\n    integer item\n}\n\n"
               "function<T> T fuzz_unbox(FuzzBox<T> b) {\n    return b.item\n}\n\n";

    // A namespace with a public function calling an internal helper, a public
    // record and choice, and a wildcard `use FuzzNs` import
    // (generate_namespace_statement): qualified and use-imported member access,
    // and internal-member access control from within the namespace.
    prelude += "namespace FuzzNs {\n"
               "    function integer fuzz_ns_add(integer a, integer b) {\n"
               "        return FuzzNs.fuzz_ns_secret(a) + b\n"
               "    }\n"
               "    internal function integer fuzz_ns_secret(integer n) {\n"
               "        return n + 1\n"
               "    }\n"
               "    record FuzzNsRec {\n        integer v\n    }\n"
               "    choice FuzzNsChoice {\n        NsA\n        NsB(integer x)\n    }\n"
               "}\n\n"
               "use FuzzNs\n\n";

    // The bounded task body of every spawn in generate_concurrency_statement,
    // so task_scope blocks always join and never deadlock or leak threads.
    prelude += "function integer fuzz_conc_work(integer n) {\n    return n + 1\n}\n\n";

    return prelude;
}

// ─── Chaos mode ─────────────────────────────────────────────────────────────

template <typename Provider> [[nodiscard]] std::string generate_chaos_function(Provider& fdp) {
    const auto name = generate_identifier(fdp);
    const int param_count = fdp.template ConsumeIntegralInRange<int>(0, 3);
    std::string params;
    bool defaults_started = false;
    for (int i = 0; i < param_count; ++i) {
        if (i > 0) {
            params += ", ";
        }
        // Optionally mark the parameter mutable to reach mutable-parameter
        // handling in the type checker, compiler, and VM.
        if (fdp.ConsumeBool()) {
            params += "mutable ";
        }
        // Optionally add an ownership qualifier so the type checker exercises
        // consuming (`unique`) and read-only (`borrow`) parameter handling at
        // call and pipe sites.
        switch (fdp.template ConsumeIntegralInRange<int>(0, 2)) {
            case 1:
                params += "unique ";
                break;
            case 2:
                params += "borrow ";
                break;
            default:
                break;
        }
        params += generate_type(fdp) + " " + generate_identifier(fdp);
        // Default values must be trailing, so once one is emitted every later
        // parameter also gets one. This reaches the compiler's default-argument
        // filling and the type checker's required-vs-optional arity logic.
        if (defaults_started || fdp.ConsumeBool()) {
            defaults_started = true;
            params += " = " + generate_literal(fdp);
        }
    }

    const auto ret_type = generate_type(fdp);
    std::string body;
    const int stmt_count = fdp.template ConsumeIntegralInRange<int>(1, 5);
    for (int i = 0; i < stmt_count; ++i) {
        body += "    " + generate_chaos_statement(fdp, 0);
    }
    // Every path must return a value for any non-void return type, so end with
    // a return rather than failing every function on "may fall through".
    body += "    return " + generate_expression(fdp, 0) + "\n";

    return "function " + ret_type + " " + name + "(" + params + ") {\n" + body + "}\n\n";
}

template <typename Provider> [[nodiscard]] std::string generate_chaos_record(Provider& fdp) {
    const auto name = "Record" + std::to_string(fdp.template ConsumeIntegralInRange<int>(0, 9));
    const int field_count = fdp.template ConsumeIntegralInRange<int>(1, 4);
    std::string fields;
    for (int i = 0; i < field_count; ++i) {
        if (i > 0) {
            fields += ",\n";
        }
        fields += "    " + generate_type(fdp) + " " + generate_identifier(fdp);
    }
    return "record " + name + " {\n" + fields + "\n}\n\n";
}

template <typename Provider> [[nodiscard]] std::string generate_chaos_choice(Provider& fdp) {
    const auto name = "Choice" + std::to_string(fdp.template ConsumeIntegralInRange<int>(0, 9));
    const auto variant_count =
        fdp.template ConsumeIntegralInRange<std::size_t>(1, k_variant_names.size());
    std::string variants;
    for (std::size_t i = 0; i < variant_count; ++i) {
        if (i > 0) {
            variants += "\n";
        }
        variants += std::string("    ") + k_variant_names[i];
        if (fdp.ConsumeBool()) {
            variants += "(" + generate_type(fdp) + " " + generate_identifier(fdp) + ")";
        }
    }
    return "choice " + name + " {\n" + variants + "\n}\n\n";
}

template <typename Provider> [[nodiscard]] std::string generate_chaos_program(Provider& fdp) {
    std::string program = generate_prelude();

    const int record_count = fdp.template ConsumeIntegralInRange<int>(0, 2);
    for (int i = 0; i < record_count; ++i) {
        program += generate_chaos_record(fdp);
    }

    const int choice_count = fdp.template ConsumeIntegralInRange<int>(0, 2);
    for (int i = 0; i < choice_count; ++i) {
        program += generate_chaos_choice(fdp);
    }

    const int func_count = fdp.template ConsumeIntegralInRange<int>(0, 3);
    for (int i = 0; i < func_count; ++i) {
        program += generate_chaos_function(fdp);
    }

    program += "@main\nfunction void main() {\n";
    const int stmt_count = fdp.template ConsumeIntegralInRange<int>(1, 10);
    for (int i = 0; i < stmt_count; ++i) {
        program += "    " + generate_chaos_statement(fdp, 0);
    }
    program += "}\n";

    return program;
}

// ─── Well-typed mode ────────────────────────────────────────────────────────

// A record `WRec<index>` with typed fields f0..fN, plus the statement that
// constructs it (every field initialised with a literal of its type) and reads
// each field back.
struct TypedDeclaration {
    std::string declaration;
    std::string usage;
};

template <typename Provider>
[[nodiscard]] TypedDeclaration generate_typed_record(Provider& fdp, int index) {
    const auto name = "WRec" + std::to_string(index);
    const auto binding = "wr" + std::to_string(index);
    const int field_count = fdp.template ConsumeIntegralInRange<int>(1, 4);

    std::string fields;
    std::string initialisers;
    std::string reads;
    for (int i = 0; i < field_count; ++i) {
        const auto type = pick_kind<ValueType>(fdp);
        const auto field = "f" + std::to_string(i);
        if (i > 0) {
            fields += ",\n";
            initialisers += ", ";
        }
        fields += "    " + type_name(type) + " " + field;
        initialisers += field + " = " + generate_typed_literal(fdp, type);
        reads += "print(" + binding + "." + field + ")\n";
    }

    return TypedDeclaration{.declaration = "record " + name + " {\n" + fields + "\n}\n\n",
                            .usage = name + " " + binding + " = " + name + " { " + initialisers +
                                     " }\n" + reads};
}

// A choice `WChoice<index>` whose variants optionally carry a typed payload,
// plus the statement that constructs one variant and matches every variant.
template <typename Provider>
[[nodiscard]] TypedDeclaration generate_typed_choice(Provider& fdp, int index) {
    const auto name = "WChoice" + std::to_string(index);
    const auto binding = "wc" + std::to_string(index);
    const auto variant_count =
        fdp.template ConsumeIntegralInRange<std::size_t>(1, k_variant_names.size());
    const auto chosen = fdp.template ConsumeIntegralInRange<std::size_t>(0, variant_count - 1);

    std::string variants;
    std::string arms;
    std::string constructed;
    for (std::size_t i = 0; i < variant_count; ++i) {
        const std::string variant = k_variant_names[i];
        const bool has_payload = fdp.ConsumeBool();
        const auto payload_type = pick_kind<ValueType>(fdp);

        variants += "    " + variant;
        arms += "case " + name + "." + variant;
        std::string value = name + "." + variant;
        if (has_payload) {
            variants += "(" + type_name(payload_type) + " payload)";
            arms += "(_p" + std::to_string(i) + ")";
            value += "(" + generate_typed_literal(fdp, payload_type) + ")";
        }
        variants += "\n";
        arms += " { " + std::to_string(i) + " }\n";
        if (i == chosen) {
            constructed = value;
        }
    }

    return TypedDeclaration{.declaration = "choice " + name + " {\n" + variants + "}\n\n",
                            .usage = name + " " + binding + " = " + constructed + "\n" +
                                     "print(match " + binding + " {\n" + arms + "})\n"};
}

// A function `wf<index>(integer a, integer b) -> integer` with a well-typed
// body, plus a statement calling it positionally or with named arguments.
template <typename Provider>
[[nodiscard]] TypedDeclaration generate_typed_function(Provider& fdp, int index) {
    const auto name = "wf" + std::to_string(index);
    std::string body;
    const int stmt_count = fdp.template ConsumeIntegralInRange<int>(1, 4);
    for (int i = 0; i < stmt_count; ++i) {
        body += "    " + generate_typed_statement(fdp, 1);
    }
    body += "    return a + b + " + generate_typed_expression(fdp, ValueType::Integer, 0) + "\n";

    const auto a = generate_int_literal(fdp, -1000, 1000);
    const auto b = generate_int_literal(fdp, -1000, 1000);
    const auto call =
        fdp.ConsumeBool() ? name + "(" + a + ", " + b + ")" : name + "(b: " + b + ", a: " + a + ")";

    return TypedDeclaration{.declaration = "function integer " + name +
                                           "(integer a, integer b) {\n" + body + "}\n\n",
                            .usage = "print(" + call + ")\n"};
}

template <typename Provider> [[nodiscard]] std::string generate_typed_program(Provider& fdp) {
    std::string declarations = generate_prelude();
    std::string usages;

    const auto add = [&](const TypedDeclaration& generated) {
        declarations += generated.declaration;
        usages += "    " + generated.usage;
    };

    const int record_count = fdp.template ConsumeIntegralInRange<int>(0, 2);
    for (int i = 0; i < record_count; ++i) {
        add(generate_typed_record(fdp, i));
    }

    const int choice_count = fdp.template ConsumeIntegralInRange<int>(0, 2);
    for (int i = 0; i < choice_count; ++i) {
        add(generate_typed_choice(fdp, i));
    }

    const int func_count = fdp.template ConsumeIntegralInRange<int>(0, 3);
    for (int i = 0; i < func_count; ++i) {
        add(generate_typed_function(fdp, i));
    }

    std::string main_body;
    const int stmt_count = fdp.template ConsumeIntegralInRange<int>(1, 10);
    for (int i = 0; i < stmt_count; ++i) {
        main_body += "    " + generate_typed_statement(fdp, 0);
    }

    return declarations + "@main\nfunction void main() {\n" + main_body + usages + "}\n";
}

// ─── Entry point ────────────────────────────────────────────────────────────

template <typename Provider> [[nodiscard]] std::string generate_program(Provider& fdp) {
    return fdp.ConsumeBool() ? generate_typed_program(fdp) : generate_chaos_program(fdp);
}

} // namespace luma::fuzz::gen

#endif // LUMA_FUZZ_FUZZ_GEN_PROGRAM_HPP
