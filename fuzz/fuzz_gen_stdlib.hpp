#ifndef LUMA_FUZZ_FUZZ_GEN_STDLIB_HPP
#define LUMA_FUZZ_FUZZ_GEN_STDLIB_HPP

#include <string>

#include "fuzz_gen_vocab.hpp"

// Curated, type-correct standard-library emitters.
//
// Each emitter produces a self-contained, well-typed statement that survives
// type-checking and runs on the VM under the sandboxed stdlib, threading the
// structured-concurrency, LinearAlgebra, Math/Statistics, Converter and Json
// features end to end.  Every function called here exists in the sandboxed
// stdlib: the type checker only *warns* about an unknown stdlib member, so a
// misspelt or removed name would type-check and then abort the whole program at
// run time — fuzz_generator_test pins this.  Both the chaos grammar
// (fuzz_gen_grammar.hpp) and the well-typed grammar (fuzz_gen_typed.hpp)
// dispatch to these.  See fuzz_source_generator.hpp for the Provider concept
// these templates require.

namespace luma::fuzz::gen {

enum class ConcurrencyKind {
    ScopeFanOut,
    SpawnAwait,
    UnboundedChannel,
    BufferedTryChannel,
    TaskAll,
    TimedChannel,
    SelectAndClosed,
    TaskRace,
    TaskAny,
    TaskSequence,
    TaskMap,
    TaskMapN,
    TaskFlatMap,
    TaskTimeout,
    TaskIsDone,
    TaskRetry,
    DrainReceiveAll,
    Count
};

// Generate a self-contained, type-correct concurrency statement so the
// structured fuzzer exercises the SpawnExpression / TaskScopeExpression /
// AwaitExpression AST nodes, their compiler opcodes, the TaskScope runtime, the
// thread pool, the Channel VM operations, and the Task combinator family
// (all / race / any / sequence / map / map_n / flat_map / timeout / is_done /
// retry) — paths that raw and other structured statements never reach.  Every
// branch is integer-typed and self-contained so it survives type-checking and
// runs on the VM.  All channel receives are preceded by a matching send on the
// same thread (channels created with Channel.new() are unbounded, so sends never
// block), and every spawned task runs the bounded `fuzz_conc_work` helper emitted
// once in generate_program — so no branch can deadlock or leak a thread
// (task_scope joins all children at block exit).  Task.timeout uses the same
// small bounded deadline as the channel timeout branches so it never stalls the
// fuzzer.
template <typename Provider>
[[nodiscard]] std::string generate_concurrency_statement(Provider& fdp) {
    const auto suffix = fresh_suffix(fdp);
    const auto a = generate_int_literal(fdp, -1000, 1000);
    const auto b = generate_int_literal(fdp, -1000, 1000);
    // Small, bounded timeout so the receive_timeout/send_timeout paths exercise
    // the condition-variable wait_for code without slowing the fuzzer.
    const auto ms = std::to_string(fdp.template ConsumeIntegralInRange<int>(0, 20));

    switch (pick_kind<ConcurrencyKind>(fdp)) {
        case ConcurrencyKind::ScopeFanOut: { // task_scope collecting results as array<integer>.
            const int task_count = fdp.template ConsumeIntegralInRange<int>(1, 4);
            std::string out = "array<integer> ts" + suffix + " = task_scope {\n";
            for (int i = 0; i < task_count; ++i) {
                out += "    spawn fuzz_conc_work(" + a + ")\n";
            }
            out += "}\n";
            return out + "print(Array.length(ts" + suffix + "))\n";
        }
        case ConcurrencyKind::SpawnAwait:
            return "task_scope {\n    task<integer> tk" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    integer av" + suffix + " = await tk" + suffix + "\n    print(av" +
                   suffix + ")\n}\n";
        case ConcurrencyKind::UnboundedChannel: // Send then receive on the same thread.
            return "channel<integer> ch" + suffix + " = Channel.new()\n    boolean _cs" + suffix +
                   " = Channel.send(ch" + suffix + ", " + a + ")\n    result<integer> cr" + suffix +
                   " = Channel.receive(ch" + suffix + ")\n    print(Result.unwrap_or(cr" + suffix +
                   ", 0))\n    Channel.close(ch" + suffix + ")\n";
        case ConcurrencyKind::BufferedTryChannel: { // The non-blocking try_* API.
            const int capacity = fdp.template ConsumeIntegralInRange<int>(1, 8);
            return "channel<integer> cb" + suffix + " = Channel.new_buffered(" +
                   std::to_string(capacity) + ")\n    boolean _bs" + suffix +
                   " = Channel.try_send(cb" + suffix + ", " + a + ")\n    result<integer> br" +
                   suffix + " = Channel.try_receive(cb" + suffix +
                   ")\n    print(Result.unwrap_or(br" + suffix + ", 0))\n";
        }
        case ConcurrencyKind::TaskAll:
            return "task_scope {\n    task<integer> p" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> q" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    array<integer> all" + suffix + " = Result.unwrap_or(Task.all([p" +
                   suffix + ", q" + suffix + "]), [])\n    print(Array.length(all" + suffix +
                   "))\n}\n";
        case ConcurrencyKind::TimedChannel: // send_timeout + receive_timeout, bounded wait.
            return "channel<integer> tc" + suffix + " = Channel.new_buffered(2)\n" +
                   "    result<boolean> tsr" + suffix + " = Channel.send_timeout(tc" + suffix +
                   ", " + a + ", " + ms + ")\n    result<integer> trr" + suffix +
                   " = Channel.receive_timeout(tc" + suffix + ", " + ms +
                   ")\n    print(Result.unwrap_or(tsr" + suffix +
                   ", false))\n    print(Result.unwrap_or(trr" + suffix + ", 0))\n" +
                   "    Channel.close(tc" + suffix + ")\n";
        case ConcurrencyKind::SelectAndClosed: // select, then the closed-channel error paths.
            return "channel<integer> sc" + suffix + " = Channel.new()\n    boolean _ss" + suffix +
                   " = Channel.send(sc" + suffix + ", " + a +
                   ")\n    result<(integer, integer)> sel" + suffix + " = Channel.select([sc" +
                   suffix + "])\n    print(Result.is_success(sel" + suffix +
                   "))\n    Channel.close(sc" + suffix + ")\n    boolean cls" + suffix +
                   " = Channel.send(sc" + suffix + ", " + b + ")\n    print(cls" + suffix +
                   ")\n    result<integer> clr" + suffix + " = Channel.try_receive(sc" + suffix +
                   ")\n    print(Result.is_failure(clr" + suffix + "))\n";
        case ConcurrencyKind::TaskRace: // First completed result.
            return "task_scope {\n    task<integer> rp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> rq" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    print(Result.unwrap_or(Task.race([rp" + suffix + ", rq" + suffix +
                   "]), 0))\n}\n";
        case ConcurrencyKind::TaskAny: // First successful result.
            return "task_scope {\n    task<integer> yp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> yq" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    print(Result.unwrap_or(Task.any([yp" + suffix + ", yq" + suffix +
                   "]), 0))\n}\n";
        case ConcurrencyKind::TaskSequence: // Results in order.
            return "task_scope {\n    task<integer> qp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> qq" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    array<integer> qr" + suffix + " = Result.unwrap_or(Task.sequence([qp" +
                   suffix + ", qq" + suffix + "]), [])\n    print(Array.length(qr" + suffix +
                   "))\n}\n";
        case ConcurrencyKind::TaskMap: // Transforms a single completed result.
            return "task_scope {\n    task<integer> mp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    print(Result.unwrap_or(Task.map(mp" + suffix + ", (integer mv" + suffix +
                   ") -> mv" + suffix + " + 1), 0))\n}\n";
        case ConcurrencyKind::TaskMapN: // Maps over every task result.
            return "task_scope {\n    task<integer> np" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> nq" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    array<integer> nr" + suffix + " = Result.unwrap_or(Task.map_n([np" +
                   suffix + ", nq" + suffix + "], (integer nv" + suffix + ") -> nv" + suffix +
                   " + 1), [])\n    print(Array.length(nr" + suffix + "))\n}\n";
        case ConcurrencyKind::TaskFlatMap: // Chains a completed result into another spawn.
            return "task_scope {\n    task<integer> fp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    print(Result.unwrap_or(Task.flat_map(fp" + suffix + ", (integer fv" +
                   suffix + ") -> spawn fuzz_conc_work(fv" + suffix + ")), 0))\n}\n";
        case ConcurrencyKind::TaskTimeout: // Awaits with a small bounded deadline.
            return "task_scope {\n    task<integer> tp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    print(Result.unwrap_or(Task.timeout(tp" + suffix + ", " + ms +
                   "), 0))\n}\n";
        case ConcurrencyKind::TaskIsDone: // Probes completion without consuming the task.
            return "task_scope {\n    task<integer> dp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    print(Result.unwrap_or(Task.is_done(dp" + suffix + "), false))\n}\n";
        case ConcurrencyKind::TaskRetry: { // Re-invokes a result-returning closure (no spawn).
            const auto attempts = std::to_string(fdp.template ConsumeIntegralInRange<int>(1, 4));
            return "print(Result.unwrap_or(Task.retry(" + attempts + ", () -> success(" + a +
                   ")), 0))\n";
        }
        case ConcurrencyKind::DrainReceiveAll: // Drained via receive_all after send + close.
            return "channel<integer> rc" + suffix + " = Channel.new_buffered(4)\n    boolean _rs" +
                   suffix + " = Channel.send(rc" + suffix + ", " + a + ")\n    Channel.close(rc" +
                   suffix + ")\n    array<integer> ra" + suffix + " = Channel.receive_all(rc" +
                   suffix + ")\n    print(Array.length(ra" + suffix + "))\n";
        case ConcurrencyKind::Count:
            break;
    }
    unreachable_kind();
}

enum class LinearAlgebraKind {
    Dot,
    Norm,
    Cross,
    Normalize,
    Add,
    Scale,
    Determinant,
    Trace,
    Inverse,
    Solve,
    Multiply,
    Transpose,
    Count
};

// Type-correct LinearAlgebra usage.  Builds small fixed-shape vector and matrix
// literals from fdp-chosen numbers and emits a single well-typed `print(...)`
// running one LinearAlgebra operation end-to-end.  Every branch is
// self-contained with inline numeric literals — no variable bindings — so it
// cannot collide with the shared identifier pool.  Result-returning operations
// are unwrapped (`Result.unwrap_or`) or probed (`Result.is_success`); direct
// numeric and array results are printed or measured.  This drives the module's
// to_vec / to_mat conversions, the dimension guards, and the LU-based
// determinant / trace / inverse / solve under the fuzzing resource limits.
template <typename Provider>
[[nodiscard]] std::string generate_linearalgebra_statement(Provider& fdp) {
    // Two 3-vectors for the vector operations, two 2x2 matrices for the matrix
    // operations, and a length-2 right-hand side for the linear solve.  All
    // five are built every call so fdp consumption is constant regardless of
    // the branch chosen below.
    const std::string va = "[" + generate_number_literal(fdp) + ", " +
                           generate_number_literal(fdp) + ", " + generate_number_literal(fdp) + "]";
    const std::string vb = "[" + generate_number_literal(fdp) + ", " +
                           generate_number_literal(fdp) + ", " + generate_number_literal(fdp) + "]";
    const std::string ma = "[[" + generate_number_literal(fdp) + ", " +
                           generate_number_literal(fdp) + "], [" + generate_number_literal(fdp) +
                           ", " + generate_number_literal(fdp) + "]]";
    const std::string mb = "[[" + generate_number_literal(fdp) + ", " +
                           generate_number_literal(fdp) + "], [" + generate_number_literal(fdp) +
                           ", " + generate_number_literal(fdp) + "]]";
    const std::string rhs =
        "[" + generate_number_literal(fdp) + ", " + generate_number_literal(fdp) + "]";

    switch (pick_kind<LinearAlgebraKind>(fdp)) {
        case LinearAlgebraKind::Dot:
            return "print(LinearAlgebra.dot(" + va + ", " + vb + ") |> Result.unwrap_or(0.0))\n";
        case LinearAlgebraKind::Norm:
            return "print(LinearAlgebra.norm(" + va + "))\n";
        case LinearAlgebraKind::Cross:
            return "print(Result.is_success(LinearAlgebra.cross(" + va + ", " + vb + ")))\n";
        case LinearAlgebraKind::Normalize:
            return "print(Result.is_success(LinearAlgebra.normalize(" + va + ")))\n";
        case LinearAlgebraKind::Add:
            return "print(Result.is_success(LinearAlgebra.add(" + va + ", " + vb + ")))\n";
        case LinearAlgebraKind::Scale:
            return "print(LinearAlgebra.dimension(LinearAlgebra.scale(" + va + ", 2.0)))\n";
        case LinearAlgebraKind::Determinant:
            return "print(LinearAlgebra.determinant(" + ma + ") |> Result.unwrap_or(0.0))\n";
        case LinearAlgebraKind::Trace:
            return "print(LinearAlgebra.trace(" + ma + ") |> Result.unwrap_or(0.0))\n";
        case LinearAlgebraKind::Inverse:
            return "print(Result.is_success(LinearAlgebra.inverse(" + ma + ")))\n";
        case LinearAlgebraKind::Solve:
            return "print(Result.is_success(LinearAlgebra.solve(" + ma + ", " + rhs + ")))\n";
        case LinearAlgebraKind::Multiply:
            return "print(Result.is_success(LinearAlgebra.multiply(" + ma + ", " + mb + ")))\n";
        case LinearAlgebraKind::Transpose:
            return "print(LinearAlgebra.rows(LinearAlgebra.transpose(" + ma + ")))\n";
        case LinearAlgebraKind::Count:
            break;
    }
    unreachable_kind();
}

enum class MathKind {
    SquareRoot,
    Power,
    LogE,
    LogBase,
    Factorial,
    Gcd,
    Lcm,
    IsPrime,
    Sine,
    Exponential,
    Clamp,
    Floor,
    Hypotenuse,
    Mean,
    Variance,
    Count
};

// Type-correct Math and Statistics usage.  Builds scalar, integer, and small
// numeric-array operands from fdp-chosen values and emits a single well-typed
// `print(...)` running one operation end-to-end.  Operands are deliberately
// allowed to be negative, zero, or large so the adversarial inputs reach the
// domain guards (square_root/log), overflow checks (factorial/gcd/lcm), and
// NaN/infinity result paths; result-returning operations are unwrapped
// (`Result.unwrap_or`) or probed (`Result.is_success`).
template <typename Provider> [[nodiscard]] std::string generate_math_statement(Provider& fdp) {
    // Two scalars, a clamp range, two integers, and a numeric array.  All are
    // built every call so fdp consumption is constant regardless of the branch.
    const std::string na = generate_number_literal(fdp);
    const std::string nb = generate_number_literal(fdp);
    const std::string lo = generate_number_literal(fdp);
    const std::string hi = generate_number_literal(fdp);
    // ia spans the factorial domain guard (negative and > 20) on both sides.
    const std::string ia = generate_int_literal(fdp, -3, 30);
    const std::string ib = generate_int_literal(fdp, -100000, 100000);
    const std::string arr = "[" + generate_number_literal(fdp) + ", " +
                            generate_number_literal(fdp) + ", " + generate_number_literal(fdp) +
                            "]";

    switch (pick_kind<MathKind>(fdp)) {
        case MathKind::SquareRoot:
            return "print(Math.square_root(" + na + ") |> Result.unwrap_or(0.0))\n";
        case MathKind::Power:
            return "print(Math.power(" + na + ", " + nb + ") |> Result.unwrap_or(0.0))\n";
        case MathKind::LogE:
            return "print(Result.is_success(Math.log_e(" + na + ")))\n";
        case MathKind::LogBase:
            return "print(Result.is_success(Math.log_base(" + na + ", " + nb + ")))\n";
        case MathKind::Factorial:
            return "print(Result.is_success(Math.factorial(" + ia + ")))\n";
        case MathKind::Gcd:
            return "print(Math.greatest_common_divisor(" + ia + ", " + ib +
                   ") |> Result.unwrap_or(0))\n";
        case MathKind::Lcm:
            return "print(Result.is_success(Math.least_common_multiple(" + ia + ", " + ib + ")))\n";
        case MathKind::IsPrime:
            return "print(Math.is_prime(" + ib + "))\n";
        case MathKind::Sine:
            return "print(Result.is_success(Math.sine(" + na + ")))\n";
        case MathKind::Exponential:
            return "print(Result.is_success(Math.exponential(" + na + ")))\n";
        case MathKind::Clamp: // lo > hi fails.
            return "print(Math.clamp(" + na + ", " + lo + ", " + hi +
                   ") |> Result.unwrap_or(0.0))\n";
        case MathKind::Floor: // Out-of-range guard.
            return "print(Result.is_success(Math.floor(" + na + ")))\n";
        case MathKind::Hypotenuse:
            return "print(Math.hypotenuse(" + na + ", " + nb + "))\n";
        case MathKind::Mean:
            return "print(Statistics.mean(" + arr + ") |> Result.unwrap_or(0.0))\n";
        case MathKind::Variance:
            return "print(Result.is_success(Statistics.variance(" + arr + ")))\n";
        case MathKind::Count:
            break;
    }
    unreachable_kind();
}

// Drive the Converter module's hand-written parsers and encoders.  Half the
// time parse a generated string — reaching the UTF-8 decoder in
// character_to_codepoint and the roman/base/numeric scanners — otherwise format
// a generated integer, reaching the base/roman/word/ordinal encoders and the
// UTF-8 codepoint encoder together with their range guards.
template <typename Provider> [[nodiscard]] std::string generate_converter_statement(Provider& fdp) {
    if (fdp.ConsumeBool()) {
        return "print(" + std::string{pick(fdp, k_converter_string_parsers)} + "(" +
               generate_string_literal(fdp) + "))\n";
    }
    const auto n =
        std::to_string(fdp.template ConsumeIntegralInRange<int>(-2000000000, 2000000000));
    return "print(" + std::string{pick(fdp, k_converter_int_formatters)} + "(" + n + "))\n";
}

// Decode a generated string with a sandbox-available data-format parser.
template <typename Provider> [[nodiscard]] std::string generate_decoder_statement(Provider& fdp) {
    return "print(" + std::string{pick(fdp, k_decoders)} + "(" + generate_string_literal(fdp) +
           "))\n";
}

} // namespace luma::fuzz::gen

#endif // LUMA_FUZZ_FUZZ_GEN_STDLIB_HPP
