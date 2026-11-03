// Fuzz-generator integration tests — pin the contract of the structured
// fuzzer's source generator (fuzz/fuzz_source_generator.hpp).
//
// The well-typed half of the generator exists to reach the compiler, optimiser,
// verifier and VM, which only happens when the *whole* program is valid: one
// misspelt stdlib call, reserved-word identifier or ill-typed statement
// silently drops the program before the back end — and the fuzzer cannot tell.
// These tests drive the generator with a deterministic provider and require
// every well-typed program (and every statement kind in isolation) to
// type-check without unknown-stdlib warnings, compile through the production
// passes at the highest optimisation level, pass the bytecode verifier, and run
// to completion under the sandboxed stdlib.  Chaos programs only need to pass
// through the front end without an exception.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include "analysis/pipeline/pipeline.hpp"
#include "analysis/pipeline/type_checker_pass.hpp"
#include "analysis/types/type_checker.hpp"
#include "fuzz_source_generator.hpp"
#include "runtime/compiler/compile_result.hpp"
#include "runtime/compiler/compiler_pass.hpp"
#include "runtime/compiler/optimizer.hpp"
#include "runtime/compiler/optimizer_pass.hpp"
#include "runtime/compiler/verifier_pass.hpp"
#include "runtime/interpreter/environment.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"
#include "shared_eval.hpp"
#include "test_framework.hpp"

using luma::CompileArtifact;
using luma::CompilerPass;
using luma::Optimizer;
using luma::OptimizerPass;
using luma::Pipeline;
using luma::TypeChecker;
using luma::TypeCheckerPass;
using luma::VerifierPass;
using luma::VM;
using luma::test::CapturedStream;
using luma::test::lex_and_parse;
using luma::test::make_sandboxed_std_env;

namespace gen = luma::fuzz::gen;

namespace {

// A deterministic stand-in for LibFuzzer's FuzzedDataProvider with the same
// byte-consumption semantics for the subset the generator uses: integers are
// read from the end of the buffer, strings from the front, and an exhausted
// buffer yields each range's minimum.
class ByteProvider {
public:
    explicit ByteProvider(std::vector<std::uint8_t> bytes) : bytes_{std::move(bytes)} {}

    template <typename T> [[nodiscard]] T ConsumeIntegralInRange(T min, T max) {
        using Unsigned = std::make_unsigned_t<T>;
        const auto range = static_cast<std::uint64_t>(
            static_cast<Unsigned>(static_cast<Unsigned>(max) - static_cast<Unsigned>(min)));
        std::uint64_t result{0};
        std::size_t offset{0};
        while (offset < sizeof(T) * 8 && (range >> offset) > 0 && back_ > front_) {
            --back_;
            result = (result << 8) | bytes_[back_];
            offset += 8;
        }
        if (range != std::numeric_limits<std::uint64_t>::max()) {
            result %= range + 1;
        }
        return static_cast<T>(static_cast<Unsigned>(static_cast<Unsigned>(min) + result));
    }

    [[nodiscard]] bool ConsumeBool() {
        return (ConsumeIntegralInRange<std::uint8_t>(0, 255) & 1U) != 0;
    }

    template <typename T> [[nodiscard]] T ConsumeFloatingPointInRange(T min, T max) {
        const auto raw =
            ConsumeIntegralInRange<std::uint32_t>(0, std::numeric_limits<std::uint32_t>::max());
        return min + (max - min) * (static_cast<T>(raw) /
                                    static_cast<T>(std::numeric_limits<std::uint32_t>::max()));
    }

    [[nodiscard]] std::string ConsumeRandomLengthString(std::size_t max_length) {
        std::string out;
        while (out.size() < max_length && back_ > front_) {
            char next = static_cast<char>(bytes_[front_++]);
            if (next == '\\' && back_ > front_) {
                next = static_cast<char>(bytes_[front_++]);
                if (next != '\\') {
                    break;
                }
            }
            out += next;
        }
        return out;
    }

    [[nodiscard]] std::size_t remaining_bytes() const {
        return back_ - front_;
    }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t front_{0};
    std::size_t back_{bytes_.size()};
};

constexpr int k_program_count = 120;
constexpr int k_samples_per_statement_kind = 12;

[[nodiscard]] std::vector<std::uint8_t> random_bytes(std::mt19937_64& rng, std::size_t size) {
    std::uniform_int_distribution<int> byte{0, 255};
    std::vector<std::uint8_t> bytes(size);
    std::ranges::generate(bytes, [&] { return static_cast<std::uint8_t>(byte(rng)); });
    return bytes;
}

// Why a generated program failed to reach a clean run, or nullopt on success.
[[nodiscard]] std::optional<std::string> run_through_back_end(const std::string& source) {
    auto program = lex_and_parse(source);

    TypeChecker checker;
    if (!checker.check(program, /*require_main=*/true).empty()) {
        return "type error";
    }
    // The type checker only warns about an unknown stdlib member, so a misspelt
    // or removed call would otherwise slip through and abort the program at run
    // time.
    const auto& warnings = checker.get_warnings();
    const auto unknown_stdlib = std::ranges::find_if(warnings, [](const auto& warning) {
        return warning.message.find("unknown stdlib function") != std::string::npos;
    });
    if (unknown_stdlib != warnings.end()) {
        return unknown_stdlib->message;
    }

    std::optional<CompileArtifact> artifact;
    auto pipeline = Pipeline::builder()
                        .add<TypeCheckerPass>(/*require_main=*/true)
                        .add<CompilerPass>(artifact)
                        .add<OptimizerPass>(artifact, Optimizer::k_max_level)
                        .add<VerifierPass>(artifact)
                        .build();
    if (pipeline.run(program).has_errors() || !artifact) {
        return "compile or verify error";
    }

    const auto env = make_sandboxed_std_env();
    const CapturedStream silence{std::cout};
    try {
        VM vm{env};
        vm.execute(artifact->functions, artifact->top_level);
    } catch (const std::exception& error) {
        return std::string{"runtime error: "} + error.what();
    }
    return std::nullopt;
}

// Report a failing program in full so the generator defect is easy to spot.
[[nodiscard]] bool runs_cleanly(const std::string& source) {
    const auto failure = run_through_back_end(source);
    if (failure) {
        std::cerr << "generated program failed (" << *failure << "):\n" << source << '\n';
    }
    return !failure.has_value();
}

} // namespace

LUMA_TEST(well_typed_programs_run_end_to_end) {
    std::mt19937_64 rng{0x5EED};
    std::uniform_int_distribution<std::size_t> size{gen::k_min_structured_input,
                                                    gen::k_max_structured_input};

    for (int i = 0; i < k_program_count; ++i) {
        ByteProvider fdp{random_bytes(rng, size(rng))};
        ASSERT_TRUE(runs_cleanly(gen::generate_typed_program(fdp)));
    }
}

LUMA_TEST(every_well_typed_statement_kind_runs_in_isolation) {
    std::mt19937_64 rng{0xC0FFEE};
    const auto prelude = gen::generate_prelude();
    constexpr auto kind_count = static_cast<int>(gen::TypedStatementKind::Count);

    for (int kind = 0; kind < kind_count; ++kind) {
        for (int i = 0; i < k_samples_per_statement_kind; ++i) {
            // generate_typed_statement draws its kind from the last byte.
            auto bytes = random_bytes(rng, 512);
            bytes.back() = static_cast<std::uint8_t>(kind);
            ByteProvider fdp{std::move(bytes)};
            ASSERT_TRUE(runs_cleanly(prelude + "@main\nfunction void main() {\n" +
                                     gen::generate_typed_statement(fdp, 0) + "}\n"));
        }
    }
}

// Chaos programs must pass through lexing, parsing and type checking without an
// exception, and, being deliberately ill-typed, must actually drive the type
// checker into its error paths rather than all being accepted.
LUMA_TEST(chaos_programs_reach_type_checker_error_paths) {
    std::mt19937_64 rng{0xBADC0DE};
    std::uniform_int_distribution<std::size_t> size{gen::k_min_structured_input,
                                                    gen::k_max_structured_input};
    int rejected_count = 0;

    for (int i = 0; i < k_program_count; ++i) {
        ByteProvider fdp{random_bytes(rng, size(rng))};
        const auto program = lex_and_parse(gen::generate_chaos_program(fdp));
        TypeChecker checker;

        if (!checker.check(program, /*require_main=*/true).empty()) {
            ++rejected_count;
        }
    }

    ASSERT_GT(rejected_count, 0);
}

LUMA_TEST(generation_is_deterministic) {
    std::mt19937_64 rng{42};
    const auto bytes = random_bytes(rng, 1024);

    ByteProvider first{bytes};
    ByteProvider second{bytes};
    ASSERT_EQ(gen::generate_program(first), gen::generate_program(second));
}

int main() {
    LUMA_RUN_ALL();
}
