#ifndef LUMA_FUZZ_FUZZ_GEN_FEATURES_HPP
#define LUMA_FUZZ_FUZZ_GEN_FEATURES_HPP

#include <string>

#include "fuzz_gen_vocab.hpp"

// Curated, type-correct language-feature emitters.
//
// Each emitter produces a self-contained, well-typed statement that survives
// type-checking and runs on the VM, exercising one language feature end to end
// against the fixed preludes (FuzzRec, the Fuzz* aliases, interfaces and the
// FuzzNs namespace) that generate_program emits once per program.  Both the
// chaos grammar (fuzz_gen_grammar.hpp) and the well-typed grammar
// (fuzz_gen_typed.hpp) dispatch to these.  Binding names carry an fdp-derived
// suffix to stay distinct from the shared identifier pool and from each other;
// a rare collision merely yields a type error (the program is dropped), never
// a crash.  See fuzz_source_generator.hpp for the Provider concept these
// templates require.

namespace luma::fuzz::gen {

// Record use: construct, mutate, access, and `with`.  Integer-typed so it
// reaches the VM, exercising MakeRecord / SetField / GetField / RecordWith.
template <typename Provider> [[nodiscard]] std::string generate_record_usage(Provider& fdp) {
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
    return "mutable FuzzRec " + name + " = " + ctor + "\n" + name + ".a = " + name + ".b\n" +
           "print((" + name + " with { a = " + a + " }).b)\n";
}

// Tuple use: construct, access, and destructure — MakeTuple, tuple field access
// (.0), and the destructuring unpack path.
template <typename Provider> [[nodiscard]] std::string generate_tuple_usage(Provider& fdp) {
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

// Type-alias use: chained, generic, function, and collection aliases.  Drives
// the type checker's alias registration and resolution, including the
// chained-alias cycle guard (FuzzChain -> FuzzAlias -> integer) and
// generic-alias type-argument binding (FuzzPair<T>).
template <typename Provider> [[nodiscard]] std::string generate_alias_usage(Provider& fdp) {
    const auto suffix = fresh_suffix(fdp);
    const auto a = generate_int_literal(fdp, -1000, 1000);
    const auto b = generate_int_literal(fdp, -1000, 1000);
    std::string out;
    out += "FuzzChain fc" + suffix + " = " + a + "\n";
    out += "print(fc" + suffix + ")\n";
    out += "FuzzPair<integer> fp" + suffix + " = (" + a + ", " + b + ")\n";
    out += "print(fp" + suffix + ".0)\n";
    out += "FuzzFn ff" + suffix + " = (integer n) -> n + 1\n";
    out += "print(ff" + suffix + "(" + a + "))\n";
    out += "FuzzList fl" + suffix + " = [" + a + ", " + b + "]\n";
    out += "print(Array.length(fl" + suffix + "))\n";
    return out;
}

// Interface use: pass a record where a plain and a generic interface are
// expected and read interface fields.  Drives structural interface
// satisfaction (check_structural_satisfaction, including the generic
// prefixed-binding save/restore path) and interface-typed parameters.
template <typename Provider> [[nodiscard]] std::string generate_interface_usage(Provider& fdp) {
    const auto suffix = fresh_suffix(fdp);
    const auto v = generate_int_literal(fdp, -1000, 1000);
    const auto flag = fdp.ConsumeBool() ? "true" : "false";
    std::string out;
    out += "FuzzImpl fi" + suffix + " = FuzzImpl { ival = " + v + ", flag = " + flag +
           ", extra = 0 }\n";
    out += "print(fuzz_use_iface(fi" + suffix + "))\n";
    out += "FuzzItem bi" + suffix + " = FuzzItem { item = " + v + " }\n";
    out += "print(fuzz_unbox(bi" + suffix + "))\n";
    return out;
}

enum class NamespaceKind {
    QualifiedCall,
    BareCall,
    QualifiedRecord,
    BareRecord,
    QualifiedChoice,
    BareChoice,
    Count
};

// Namespace use against the fixed `FuzzNs` namespace and its `use FuzzNs`
// wildcard import: namespace-qualified global lookup, use-imported bare
// aliases, qualified record creation, and namespace-qualified `match` arms.
template <typename Provider> [[nodiscard]] std::string generate_namespace_statement(Provider& fdp) {
    const auto suffix = fresh_suffix(fdp);
    const auto a = generate_int_literal(fdp, -1000, 1000);
    const auto b = generate_int_literal(fdp, -1000, 1000);

    switch (pick_kind<NamespaceKind>(fdp)) {
        case NamespaceKind::QualifiedCall: // Delegates to an internal helper.
            return "print(FuzzNs.fuzz_ns_add(" + a + ", " + b + "))\n";
        case NamespaceKind::BareCall:
            return "print(fuzz_ns_add(" + a + ", " + b + "))\n";
        case NamespaceKind::QualifiedRecord: {
            const auto name = "qnr" + suffix;
            return "FuzzNs.FuzzNsRec " + name + " = FuzzNs.FuzzNsRec { v = " + a + " }\n" +
                   "print(" + name + ".v)\n";
        }
        case NamespaceKind::BareRecord: {
            const auto name = "bnr" + suffix;
            return "FuzzNsRec " + name + " = FuzzNsRec { v = " + a + " }\n" + "print(" + name +
                   ".v)\n";
        }
        case NamespaceKind::QualifiedChoice: {
            const auto name = "qch" + suffix;
            const auto res = "qres" + suffix;
            return "FuzzNs.FuzzNsChoice " + name + " = FuzzNs.FuzzNsChoice.NsB(" + a + ")\n" +
                   "integer " + res + " = match " + name + " {\n" +
                   "case FuzzNs.FuzzNsChoice.NsA { 0 }\n" + "case FuzzNs.FuzzNsChoice.NsB(bound" +
                   suffix + ") { bound" + suffix + " }\n" + "}\n" + "print(" + res + ")\n";
        }
        case NamespaceKind::BareChoice: {
            const auto name = "bch" + suffix;
            const auto res = "bres" + suffix;
            return "FuzzNsChoice " + name + " = FuzzNsChoice.NsB(" + a + ")\n" + "integer " + res +
                   " = match " + name + " {\n" + "case FuzzNsChoice.NsA { 0 }\n" +
                   "case FuzzNsChoice.NsB(bound" + suffix + ") { bound" + suffix + " }\n" + "}\n" +
                   "print(" + res + ")\n";
        }
        case NamespaceKind::Count:
            break;
    }
    unreachable_kind();
}

} // namespace luma::fuzz::gen

#endif // LUMA_FUZZ_FUZZ_GEN_FEATURES_HPP
