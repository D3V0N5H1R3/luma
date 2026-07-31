#pragma once

#include <string>

#include "fuzz_gen_vocab.hpp"

// Curated, type-correct standard-library feature emitters.
//
// Unlike the deliberately-ill-typed grammar in fuzz_gen_grammar.hpp, each emitter
// here produces a self-contained, well-typed statement that survives type-checking
// and runs on the VM, threading the namespace, structured-concurrency,
// LinearAlgebra and Math features end to end.  generate_statement dispatches to
// these.  See fuzz_source_generator.hpp for the Provider concept these templates
// require.

namespace luma::fuzz::gen {

// Generate a statement that exercises the namespace machinery.  The fixed
// `FuzzNs` namespace and its `use FuzzNs` wildcard import are emitted once in
// generate_program, so both qualified (`FuzzNs.x`) and use-imported bare (`x`)
// access are valid here.  Every branch is self-contained and integer-typed so
// it survives type-checking and reaches the VM, exercising namespace-qualified
// global lookup, use-imported bare aliases, qualified record creation, and
// namespace-qualified `match` arms.
template <typename Provider> [[nodiscard]] std::string generate_namespace_statement(Provider& fdp) {
    const auto suffix = fresh_suffix(fdp);
    const auto a = generate_int_literal(fdp, -1000, 1000);
    const auto b = generate_int_literal(fdp, -1000, 1000);

    switch (fdp.template ConsumeIntegralInRange<int>(0, 5)) {
        case 0: // Qualified function call (delegates to an internal helper).
            return "print(FuzzNs.fuzz_ns_add(" + a + ", " + b + "))\n";
        case 1: // Bare, use-imported function call.
            return "print(fuzz_ns_add(" + a + ", " + b + "))\n";
        case 2: { // Qualified record creation and field read.
            const auto name = "qnr" + suffix;
            return "FuzzNs.FuzzNsRec " + name + " = FuzzNs.FuzzNsRec { v = " + a + " }\n" +
                   "print(" + name + ".v)\n";
        }
        case 3: { // Bare, use-imported record creation and field read.
            const auto name = "bnr" + suffix;
            return "FuzzNsRec " + name + " = FuzzNsRec { v = " + a + " }\n" + "print(" + name +
                   ".v)\n";
        }
        case 4: { // Qualified choice value and namespace-qualified match arms.
            const auto name = "qch" + suffix;
            const auto res = "qres" + suffix;
            return "FuzzNs.FuzzNsChoice " + name + " = FuzzNs.FuzzNsChoice.NsB(" + a + ")\n" +
                   "integer " + res + " = match " + name + " {\n" +
                   "case FuzzNs.FuzzNsChoice.NsA { 0 }\n" + "case FuzzNs.FuzzNsChoice.NsB(bound" +
                   suffix + ") { bound" + suffix + " }\n" + "}\n" + "print(" + res + ")\n";
        }
        default: { // Bare, use-imported choice value and bare match arms.
            const auto name = "bch" + suffix;
            const auto res = "bres" + suffix;
            return "FuzzNsChoice " + name + " = FuzzNsChoice.NsB(" + a + ")\n" + "integer " + res +
                   " = match " + name + " {\n" + "case FuzzNsChoice.NsA { 0 }\n" +
                   "case FuzzNsChoice.NsB(bound" + suffix + ") { bound" + suffix + " }\n" + "}\n" +
                   "print(" + res + ")\n";
        }
    }
}

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
// fuzzer.  The fdp-derived suffix keeps binding names distinct from the shared
// identifier pool; a rare collision merely yields a type error (the program is
// dropped), never a crash.
template <typename Provider>
[[nodiscard]] std::string generate_concurrency_statement(Provider& fdp) {
    const auto suffix = fresh_suffix(fdp);
    const auto a = generate_int_literal(fdp, -1000, 1000);
    const auto b = generate_int_literal(fdp, -1000, 1000);
    // Small, bounded timeout so the receive_timeout/send_timeout paths exercise
    // the condition-variable wait_for code without slowing the fuzzer.
    const auto ms = std::to_string(fdp.template ConsumeIntegralInRange<int>(0, 20));

    constexpr int k_concurrency_kinds = 17; // Keep in sync with the switch arms below.
    switch (fdp.template ConsumeIntegralInRange<int>(0, k_concurrency_kinds - 1)) {
        case 0: { // task_scope fan-out collecting results as array<integer>.
            const int task_count = fdp.template ConsumeIntegralInRange<int>(1, 4);
            std::string out = "array<integer> ts" + suffix + " = task_scope {\n";
            for (int i = 0; i < task_count; ++i) {
                out += "    spawn fuzz_conc_work(" + a + ")\n";
            }
            out += "}\n";
            return out + "print(Array.length(ts" + suffix + "))\n";
        }
        case 1: // spawn + await a single task inside a scope.
            return "task_scope {\n    task<integer> tk" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    integer av" + suffix + " = await tk" + suffix + "\n    print(av" +
                   suffix + ")\n}\n";
        case 2: // Unbounded channel: send then receive on the same thread.
            return "channel<integer> ch" + suffix + " = Channel.new()\n    boolean _cs" + suffix +
                   " = Channel.send(ch" + suffix + ", " + a + ")\n    result<integer> cr" + suffix +
                   " = Channel.receive(ch" + suffix + ")\n    print(Result.unwrap_or(cr" + suffix +
                   ", 0))\n    Channel.close(ch" + suffix + ")\n";
        case 3: { // Buffered channel exercised with the non-blocking try_* API.
            const int capacity = fdp.template ConsumeIntegralInRange<int>(1, 8);
            return "channel<integer> cb" + suffix + " = Channel.new_buffered(" +
                   std::to_string(capacity) + ")\n    boolean _bs" + suffix +
                   " = Channel.try_send(cb" + suffix + ", " + a + ")\n    result<integer> br" +
                   suffix + " = Channel.try_receive(cb" + suffix +
                   ")\n    print(Result.unwrap_or(br" + suffix + ", 0))\n";
        }
        case 4: // Task combinator (Task.all) over a small task array in a scope.
            return "task_scope {\n    task<integer> p" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> q" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    array<integer> all" + suffix + " = Result.unwrap_or(Task.all([p" +
                   suffix + ", q" + suffix + "]), [])\n    print(Array.length(all" + suffix +
                   "))\n}\n";
        case 5: // Buffered channel timed send/receive — exercises send_timeout +
            // receive_timeout result handling with a bounded wait.
            return "channel<integer> tc" + suffix + " = Channel.new_buffered(2)\n" +
                   "    result<boolean> tsr" + suffix + " = Channel.send_timeout(tc" + suffix +
                   ", " + a + ", " + ms + ")\n    result<integer> trr" + suffix +
                   " = Channel.receive_timeout(tc" + suffix + ", " + ms +
                   ")\n    print(Result.unwrap_or(tsr" + suffix +
                   ", false))\n    print(Result.unwrap_or(trr" + suffix + ", 0))\n" +
                   "    Channel.close(tc" + suffix + ")\n";
        case 6: // select on a ready channel, then the closed-channel error paths:
            // send on a closed channel returns false; try_receive fails.
            return "channel<integer> sc" + suffix + " = Channel.new()\n    boolean _ss" + suffix +
                   " = Channel.send(sc" + suffix + ", " + a +
                   ")\n    result<(integer, integer)> sel" + suffix + " = Channel.select([sc" +
                   suffix + "])\n    print(Result.is_success(sel" + suffix +
                   "))\n    Channel.close(sc" + suffix + ")\n    boolean cls" + suffix +
                   " = Channel.send(sc" + suffix + ", " + b + ")\n    print(cls" + suffix +
                   ")\n    result<integer> clr" + suffix + " = Channel.try_receive(sc" + suffix +
                   ")\n    print(Result.is_failure(clr" + suffix + "))\n";
        case 7: // Task.race over two tasks → first completed result.
            return "task_scope {\n    task<integer> rp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> rq" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    print(Result.unwrap_or(Task.race([rp" + suffix + ", rq" + suffix +
                   "]), 0))\n}\n";
        case 8: // Task.any over two tasks → first successful result.
            return "task_scope {\n    task<integer> yp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> yq" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    print(Result.unwrap_or(Task.any([yp" + suffix + ", yq" + suffix +
                   "]), 0))\n}\n";
        case 9: // Task.sequence over two tasks → results in order.
            return "task_scope {\n    task<integer> qp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> qq" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    array<integer> qr" + suffix + " = Result.unwrap_or(Task.sequence([qp" +
                   suffix + ", qq" + suffix + "]), [])\n    print(Array.length(qr" + suffix +
                   "))\n}\n";
        case 10: // Task.map transforms a single completed result.
            return "task_scope {\n    task<integer> mp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    print(Result.unwrap_or(Task.map(mp" + suffix + ", (integer mv" + suffix +
                   ") -> mv" + suffix + " + 1), 0))\n}\n";
        case 11: // Task.map_n maps over every task result.
            return "task_scope {\n    task<integer> np" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    task<integer> nq" + suffix + " = spawn fuzz_conc_work(" + b +
                   ")\n    array<integer> nr" + suffix + " = Result.unwrap_or(Task.map_n([np" +
                   suffix + ", nq" + suffix + "], (integer nv" + suffix + ") -> nv" + suffix +
                   " + 1), [])\n    print(Array.length(nr" + suffix + "))\n}\n";
        case 12: // Task.flat_map chains a completed result into another spawn.
            return "task_scope {\n    task<integer> fp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    print(Result.unwrap_or(Task.flat_map(fp" + suffix + ", (integer fv" +
                   suffix + ") -> spawn fuzz_conc_work(fv" + suffix + ")), 0))\n}\n";
        case 13: // Task.timeout awaits a task with a small bounded deadline.
            return "task_scope {\n    task<integer> tp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    print(Result.unwrap_or(Task.timeout(tp" + suffix + ", " + ms +
                   "), 0))\n}\n";
        case 14: // Task.is_done probes completion without consuming the task.
            return "task_scope {\n    task<integer> dp" + suffix + " = spawn fuzz_conc_work(" + a +
                   ")\n    print(Result.unwrap_or(Task.is_done(dp" + suffix + "), false))\n}\n";
        case 15: { // Task.retry re-invokes a result-returning closure (no spawn).
            const auto attempts = std::to_string(fdp.template ConsumeIntegralInRange<int>(1, 4));
            return "print(Result.unwrap_or(Task.retry(" + attempts + ", () -> success(" + a +
                   ")), 0))\n";
        }
        default: // Buffered channel drained via receive_all after sends + close.
            return "channel<integer> rc" + suffix + " = Channel.new_buffered(4)\n    boolean _rs" +
                   suffix + " = Channel.send(rc" + suffix + ", " + a + ")\n    Channel.close(rc" +
                   suffix + ")\n    array<integer> ra" + suffix + " = Channel.receive_all(rc" +
                   suffix + ")\n    print(Array.length(ra" + suffix + "))\n";
    }
}

// Type-correct LinearAlgebra usage (case 16 in generate_statement).  Builds
// small fixed-shape vector and matrix literals from fdp-chosen numbers and
// emits a single well-typed `print(...)` running one LinearAlgebra operation
// end-to-end.  Every branch is self-contained with inline numeric literals —
// no variable bindings — so it cannot collide with the shared identifier pool
// and always survives type-checking to reach the compiler and VM.  Result-
// returning operations are unwrapped (`Result.unwrap_or`) or probed
// (`Result.is_success`); direct numeric and array results are printed or
// measured so the emitted statement is always type-correct.  This drives the
// module's to_vec / to_mat conversions, the dimension guards, and the LU-based
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

    constexpr int k_linearalgebra_kinds = 12; // Keep in sync with the switch arms below.
    switch (fdp.template ConsumeIntegralInRange<int>(0, k_linearalgebra_kinds - 1)) {
        case 0: // Dot product (result<number>) — unwrap to a number.
            return "print(LinearAlgebra.dot(" + va + ", " + vb + ") |> Result.unwrap_or(0.0))\n";
        case 1: // Vector norm — direct number.
            return "print(LinearAlgebra.norm(" + va + "))\n";
        case 2: // Cross product (result<vector>) — probe success.
            return "print(Result.is_success(LinearAlgebra.cross(" + va + ", " + vb + ")))\n";
        case 3: // Normalisation (result<vector>) — probe success.
            return "print(Result.is_success(LinearAlgebra.normalize(" + va + ")))\n";
        case 4: // Vector addition (result<vector>) — probe success.
            return "print(Result.is_success(LinearAlgebra.add(" + va + ", " + vb + ")))\n";
        case 5: // Vector scale (direct array) — measure its dimension.
            return "print(LinearAlgebra.dimension(LinearAlgebra.scale(" + va + ", 2.0)))\n";
        case 6: // Determinant (result<number>) — unwrap to a number.
            return "print(LinearAlgebra.determinant(" + ma + ") |> Result.unwrap_or(0.0))\n";
        case 7: // Trace (result<number>) — unwrap to a number.
            return "print(LinearAlgebra.trace(" + ma + ") |> Result.unwrap_or(0.0))\n";
        case 8: // Matrix inverse (result<matrix>) — probe success.
            return "print(Result.is_success(LinearAlgebra.inverse(" + ma + ")))\n";
        case 9: // Linear solve (result<vector>) — probe success.
            return "print(Result.is_success(LinearAlgebra.solve(" + ma + ", " + rhs + ")))\n";
        case 10: // Matrix multiply (result<matrix>) — probe success.
            return "print(Result.is_success(LinearAlgebra.multiply(" + ma + ", " + mb + ")))\n";
        default: // Transpose (direct matrix) — measure its row count.
            return "print(LinearAlgebra.rows(LinearAlgebra.transpose(" + ma + ")))\n";
    }
}

// Type-correct Math usage (case 17 in generate_statement).  Builds scalar,
// integer, and small numeric-array operands from fdp-chosen values and emits a
// single well-typed `print(...)` running one Math operation end-to-end.  Like
// the LinearAlgebra generator, every branch is self-contained with inline
// literals — no variable bindings — so it cannot collide with the shared
// identifier pool and always survives type-checking to reach the compiler and
// VM.  Operands are deliberately allowed to be negative, zero, or large so the
// adversarial inputs reach the domain guards (square_root/log/arc_sine),
// overflow checks (factorial/absolute/gcd/lcm), and NaN/infinity result paths;
// result-returning operations are unwrapped (`Result.unwrap_or`) or probed
// (`Result.is_success`) and direct values are printed so the statement is
// always type-correct.
template <typename Provider> [[nodiscard]] std::string generate_math_statement(Provider& fdp) {
    // Two scalars, a clamp range, two integers, and two equal-length numeric
    // arrays.  All are built every call so fdp consumption is constant
    // regardless of the branch chosen below.
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
    const std::string brr = "[" + generate_number_literal(fdp) + ", " +
                            generate_number_literal(fdp) + ", " + generate_number_literal(fdp) +
                            "]";

    constexpr int k_math_kinds = 15; // Keep in sync with the switch arms below.
    switch (fdp.template ConsumeIntegralInRange<int>(0, k_math_kinds - 1)) {
        case 0: // Square root (result<number>) — domain guard on negatives.
            return "print(Math.square_root(" + na + ") |> Result.unwrap_or(0.0))\n";
        case 1: // Power (result<number>) — non-real results fail.
            return "print(Math.power(" + na + ", " + nb + ") |> Result.unwrap_or(0.0))\n";
        case 2: // Natural log (result<number>) — non-positive guard.
            return "print(Result.is_success(Math.log_e(" + na + ")))\n";
        case 3: // Arbitrary-base log (result<number>) — base/value guards.
            return "print(Result.is_success(Math.log(" + na + ", " + nb + ")))\n";
        case 4: // Factorial (result<integer>) — negative and overflow guards.
            return "print(Result.is_success(Math.factorial(" + ia + ")))\n";
        case 5: // Greatest common divisor (result<integer>) — INT64_MIN guard.
            return "print(Math.greatest_common_divisor(" + ia + ", " + ib +
                   ") |> Result.unwrap_or(0))\n";
        case 6: // Least common multiple (result<integer>) — overflow guard.
            return "print(Result.is_success(Math.least_common_multiple(" + ia + ", " + ib + ")))\n";
        case 7: // Primality test (boolean) — direct value.
            return "print(Math.is_prime(" + ib + "))\n";
        case 8: // Trigonometry (result<number>) — infinity/NaN argument paths.
            return "print(Result.is_success(Math.sine(" + na + ")))\n";
        case 9: // Exponential (result<number>) — overflow to infinity fails.
            return "print(Result.is_success(Math.exponential(" + na + ")))\n";
        case 10: // Clamp (result<number>) — lo > hi fails.
            return "print(Math.clamp(" + na + ", " + lo + ", " + hi +
                   ") |> Result.unwrap_or(0.0))\n";
        case 11: // Rounding family (result<integer>) — out-of-range guard.
            return "print(Result.is_success(Math.floor(" + na + ")))\n";
        case 12: // Hypotenuse (direct number) — Euclidean distance.
            return "print(Math.hypot(" + na + ", " + nb + "))\n";
        case 13: // Statistics over a numeric array (result<number>).
            return "print(Statistics.mean(" + arr + ") |> Result.unwrap_or(0.0))\n";
        default: // Correlation of two equal-length arrays (result<number>).
            return "print(Result.is_success(Statistics.correlation(" + arr + ", " + brr + ")))\n";
    }
}

} // namespace luma::fuzz::gen
