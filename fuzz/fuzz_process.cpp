#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fuzz_harness.hpp"
#include "runtime/stdlib/system/process_module.hpp"

// LibFuzzer entry point for the Process module's command tokenizer
// (luma::tokenize_command, declared in core/runtime/stdlib/system/process_module.hpp).
//
// tokenize_command is the trust-boundary parser behind Process.run: it splits an
// untrusted command string into argv components, honouring double-quoted and
// single-quoted spans and backslash escapes, before the platform spawn path
// (CreateProcessA on Windows, fork + execvp on POSIX) hands the vector straight
// to the OS without a shell. Because the result drives process creation, the
// parser must never crash, read out of bounds or exhaust memory on hostile
// input; a mismatched quote raises luma::RuntimeError, which the shared harness
// treats as the expected outcome for malformed input. fuzz_vm only reaches this
// parser shallowly through sanitised string literals, so this direct target
// drives the quoting grammar far more deeply, mirroring fuzz_keyvaluestore and
// fuzz_csv.
//
// A round-trip oracle runs on top of the never-crash contract. Every argument
// tokenize_command yields is re-quoted into a canonical double-quoted form
// (the whole argument wrapped in '"' with each '"' and '\' backslash-escaped),
// the requoted arguments are rejoined with single spaces, and the result is
// tokenized again: it must reproduce the original argument vector exactly. The
// grammar guarantees this — inside a double-quoted span only '\' and '"' are
// special, every other byte (spaces, single quotes, tabs, newlines, NULs) is
// literal, and tokenize_command never emits an empty argument — so any
// divergence is a genuine tokenizer/quoting inconsistency.
namespace {

// Re-quote one argument into a form tokenize_command maps back to the same
// bytes: a single double-quoted span with '\' and '"' backslash-escaped.
[[nodiscard]] std::string requote_argument(const std::string& arg) {
    std::string quoted{"\""};

    for (const char c : arg) {
        if (c == '"' || c == '\\') {
            quoted += '\\';
        }
        quoted += c;
    }

    quoted += '"';

    return quoted;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            // ── Never-crash: arbitrary bytes are tokenized (an unclosed quote
            //    raises RuntimeError, which the harness treats as expected). ──
            const auto argv = luma::tokenize_command(input);
            luma::fuzz::do_not_optimize(argv.size());

            // ── Oracle: re-quoting the argv and re-tokenizing round-trips exactly.
            std::string requoted;

            for (std::size_t i{0}; i < argv.size(); ++i) {
                if (i != 0) {
                    requoted += ' ';
                }

                requoted += requote_argument(argv[i]);
            }

            const auto reparsed = luma::tokenize_command(requoted);

            if (reparsed != argv) {
                luma::fuzz::trap(); // re-quote -> tokenize is not a faithful round-trip.
            }
        });
}
