#pragma once

#include <cstddef>
#include <string>

#include "fuzz_gen_grammar.hpp"

// Declaration builders and whole-program assembly.
//
// generate_function / generate_record / generate_choice build top-level
// declarations from the vocabulary and grammar, and generate_program stitches
// them together with the fixed preludes (records, type aliases, interfaces, a
// namespace, and a concurrency helper) that make the type-correct usage statements
// in the grammar and stdlib emitters valid.  See fuzz_source_generator.hpp for the
// Provider concept these templates require.

namespace luma::fuzz::gen {

inline constexpr std::size_t min_structured_input = 8;
inline constexpr std::size_t max_structured_input = 4096;

template <typename Provider> [[nodiscard]] std::string generate_function(Provider& fdp) {
    const auto name = generate_identifier(fdp);
    const int param_count = fdp.template ConsumeIntegralInRange<int>(0, 3);
    std::string params;
    bool defaults_started = false;
    for (int i = 0; i < param_count; ++i) {
        if (i > 0) {
            params += ", ";
        }
        // Optionally mark the parameter mutable to reach mutable-parameter
        // handling in the resolver, compiler, and VM.
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
        body += "    " + generate_statement(fdp, 0);
    }

    return "function " + ret_type + " " + name + "(" + params + ") {\n" + body + "}\n\n";
}

template <typename Provider> [[nodiscard]] std::string generate_record(Provider& fdp) {
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

template <typename Provider> [[nodiscard]] std::string generate_choice(Provider& fdp) {
    const auto name = "Choice" + std::to_string(fdp.template ConsumeIntegralInRange<int>(0, 9));
    const int variant_count =
        fdp.template ConsumeIntegralInRange<int>(1, static_cast<int>(variant_names.size()));
    std::string variants;
    for (int i = 0; i < variant_count; ++i) {
        if (i > 0) {
            variants += "\n";
        }
        // Modulo keeps the index in range regardless of variant_count, so the
        // loop bound and the array size can change independently without risk
        // of an out-of-bounds access.
        variants +=
            std::string("    ") + variant_names[static_cast<std::size_t>(i) % variant_names.size()];
        if (fdp.ConsumeBool()) {
            variants += "(" + generate_type(fdp) + " " + generate_identifier(fdp) + ")";
        }
    }
    return "choice " + name + " {\n" + variants + "\n}\n\n";
}

template <typename Provider> [[nodiscard]] std::string generate_program(Provider& fdp) {
    std::string program;

    // A fixed record with one required and one defaulted field, emitted in
    // every program so that the record-usage statement (case 9 in
    // generate_statement) can construct, mutate, read, and copy-with a record
    // as type-correct code that survives type-checking and reaches the compiler
    // and VM — exercising MakeRecord, GetField, SetField, and RecordWith.
    program += "record FuzzRec {\n    integer a,\n    integer b = 0\n}\n\n";

    // A fixed set of type aliases — simple, chained, collection, function, and
    // generic — emitted in every program so the type-alias usage statement
    // (case 11 in generate_statement) is type-correct and reaches the VM.  This
    // structurally exercises the type checker's alias registration and
    // resolution, including chained resolution (FuzzChain -> FuzzAlias ->
    // integer) which drives the recursive-alias cycle guard, and generic-alias
    // type-argument binding (FuzzPair<T>).  Type aliases are erased before
    // runtime, so the names never collide with the generated record, choice,
    // function, or identifier pools.
    program += "type FuzzAlias = integer\n"
               "type FuzzChain = FuzzAlias\n"
               "type FuzzList = array<integer>\n"
               "type FuzzFn = function(integer) -> integer\n"
               "type FuzzPair<T> = (T, T)\n\n";

    // A fixed plain interface and a satisfying record, plus a generic interface
    // and its satisfying record, emitted in every program so the interface-usage
    // statement (case 12 in generate_statement) is type-correct and reaches the
    // VM.  FuzzImpl has an extra field to exercise the "record may have more
    // fields than the interface requires" satisfaction path; fuzz_use_iface and
    // fuzz_unbox read interface fields.  This structurally drives the type
    // checker's structural interface satisfaction (record-vs-interface, plus the
    // generic prefixed-binding save/restore in check_structural_satisfaction)
    // and the compiler/VM's handling of interface-typed parameters.  Interfaces
    // are compile-time only, so the names never collide with the generated
    // record, choice, function, or identifier pools at runtime.
    program += "interface FuzzIface {\n    integer ival,\n    boolean flag\n}\n\n"
               "record FuzzImpl {\n    integer ival,\n    boolean flag,\n    integer extra\n}\n\n"
               "function integer fuzz_use_iface(FuzzIface fi) {\n"
               "    if fi.flag {\n        return fi.ival\n    }\n    return 0 - fi.ival\n}\n\n"
               "interface FuzzBox<T> {\n    T item\n}\n\n"
               "record FuzzItem {\n    integer item\n}\n\n"
               "function<T> T fuzz_unbox(FuzzBox<T> b) {\n    return b.item\n}\n\n";

    // A fixed namespace with a public function that calls an internal helper, an
    // internal function, a public record, and a public choice, followed by a
    // wildcard `use FuzzNs` import — emitted in every program so the
    // namespace-usage statement (case 13 in generate_statement) can exercise
    // both qualified (`FuzzNs.x`) and use-imported bare (`x`) member access as
    // type-correct code that reaches the compiler and VM.  Declaring the
    // namespace structurally drives the type checker's namespace registration,
    // internal-member access control (the internal helper is reached from within
    // the namespace via a qualified call — the allowed path), and the compiler's
    // qualified-global emission plus `use` alias emission.  Internal members are
    // skipped by the wildcard import, and the public member names are disjoint
    // from the generated record, choice, function, and identifier pools, so no
    // name collisions occur.
    program += "namespace FuzzNs {\n"
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

    // A fixed integer→integer helper used by every spawned task in the
    // concurrency-usage statement (case 14 in generate_statement).  Keeping the
    // task body in a named top-level function means each `spawn fuzz_conc_work(n)`
    // is type-correct and bounded (it returns immediately), so task_scope blocks
    // always join and never deadlock or leak threads.  The name is disjoint from
    // the generated record, choice, function, and identifier pools.
    program += "function integer fuzz_conc_work(integer n) {\n    return n + 1\n}\n\n";

    const int record_count = fdp.template ConsumeIntegralInRange<int>(0, 2);
    for (int i = 0; i < record_count; ++i) {
        program += generate_record(fdp);
    }

    const int choice_count = fdp.template ConsumeIntegralInRange<int>(0, 2);
    for (int i = 0; i < choice_count; ++i) {
        program += generate_choice(fdp);
    }

    const int func_count = fdp.template ConsumeIntegralInRange<int>(0, 3);
    for (int i = 0; i < func_count; ++i) {
        program += generate_function(fdp);
    }

    program += "@main\nfunction void main() {\n";
    const int stmt_count = fdp.template ConsumeIntegralInRange<int>(1, 10);
    for (int i = 0; i < stmt_count; ++i) {
        program += "    " + generate_statement(fdp, 0);
    }
    program += "}\n";

    return program;
}

} // namespace luma::fuzz::gen
