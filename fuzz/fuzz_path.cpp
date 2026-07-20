#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "common/path_utils.hpp"
#include "fuzz_harness.hpp"
#include "runtime/stdlib/common/path_validator.hpp"

// LibFuzzer entry point for the FileSystem path-validation trust boundary.
//
// validate_path (core/runtime/stdlib/common/path_validator.hpp) is the hand-written
// security check that every FileSystem.* function runs against untrusted path
// strings supplied by a Luma program: it resolves the path against the current
// working directory and rejects anything that escapes it.  The lower-level
// security primitives in core/common/path_utils.hpp (has_directory_traversal,
// escapes_root, check_extension and the combined validate_path overload behind
// the include resolver) sit behind the same boundary.  Arbitrary bytes
// interpreted as a path must never crash these routines, read out of bounds, or
// exhaust memory.  Rejected paths raise luma::RuntimeError, which the shared
// harness treats as the expected outcome for hostile input; std::filesystem may
// also raise filesystem_error for syntactically invalid paths, which is
// tolerated as a standard exception.
//
// One oracle runs on top of the never-crash contract:
//   * Agreement: when path_validator::validate_path accepts a path (returns a
//     resolved, in-sandbox path), that resolved path must NOT be reported as
//     escaping the working directory by path_utils::canonical_escapes_root.
//     The two independent security checks must agree on what stays inside the
//     sandbox; any disagreement is a genuine boundary defect.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            namespace fs = std::filesystem;

            // ── path_utils.hpp primitives: arbitrary bytes must never crash. ──
            const fs::path candidate{input};
            luma::fuzz::do_not_optimize(luma::has_directory_traversal(candidate));
            luma::fuzz::do_not_optimize(luma::is_symlink(candidate));
            luma::fuzz::do_not_optimize(luma::is_symlink_or_contains_symlinks(candidate));
            luma::fuzz::do_not_optimize(luma::has_luma_extension(input));
            luma::fuzz::do_not_optimize(luma::check_extension(input, ".luma"));

            // The process working directory is invariant for the fuzzer's
            // lifetime — libFuzzer never chdir's and validate_path is read-only —
            // so resolve it once instead of paying a getcwd + repeated stat walk
            // (weakly_canonical) on every iteration.
            static const fs::path cwd = fs::weakly_canonical(fs::current_path());
            luma::fuzz::do_not_optimize(luma::escapes_root(candidate, cwd));

            // The combined validator exercises the extension + traversal + symlink +
            // root-escape branches in one call (the path the include resolver uses).
            const auto combined = luma::validate_path(input, std::string_view{}, cwd);
            luma::fuzz::do_not_optimize(combined.is_secure);

            // ── path_validator.hpp sandbox boundary. ──
            // A rejected path throws RuntimeError (caught by the harness); an
            // accepted path returns its resolved, canonical location.
            const luma::SourceLocation loc{};
            const auto resolved = luma::validate_path(input, loc);

            // ── Oracle: an accepted path must not escape the working directory. ──
            if (luma::canonical_escapes_root(resolved, cwd)) {
                luma::fuzz::trap(); // validator accepted a path that escapes the sandbox.
            }
        });
}
