// Standard library test shared helpers.
//
// Imports shared_eval.hpp (eval(), the ASSERT_EVAL_* macros) and
// test_framework.hpp, and adds the stdlib-specific LumaTempFile guard.
// LimitGuard lives in test_framework.hpp so front-end suites can use it too.
//
// Note (TS-4): Although three test helper headers exist (stdlib_test_helpers,
// type_checker_test_helpers, compiler_test_helpers), they serve distinct
// purposes and are not candidates for unification:
//   - stdlib_test_helpers wraps shared_eval for end-to-end stdlib tests.
//   - type_checker_test_helpers provides TypeChecker-specific passes/fails/check.
//   - compiler_test_helpers provides compilation and opcode-inspection utilities.

#ifndef LUMA_TESTS_RUNTIME_STDLIB_TEST_HELPERS_HPP
#define LUMA_TESTS_RUNTIME_STDLIB_TEST_HELPERS_HPP

#include <stdexcept>
#include <string>

#include "shared_eval.hpp"
#include "test_framework.hpp"

namespace luma::test {

// Render a std::string as a Luma double-quoted string literal, escaping the
// characters that would otherwise break the surrounding eval() source or start
// an interpolation.
[[nodiscard]] inline std::string luma_string_literal(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');

    for (const char c : value) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '$':
                out += "\\$";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }

    out.push_back('"');
    return out;
}

// RAII helper for temporary files created through the Luma FileSystem module.
// The file is removed via FileSystem.delete when the guard goes out of scope —
// even if an assertion throws in between — so temp files never leak into the
// working tree. Two constructions are supported:
//   * LumaTempFile{name, contents} — writes contents via FileSystem.write_file
//     and throws if the write fails.
//   * LumaTempFile{name}           — registers the name for cleanup only, for
//     files created by other means (write_lines, append_file, Hash.*_file, …).
class LumaTempFile {
public:
    explicit LumaTempFile(const std::string& name) : name_{name} {}

    LumaTempFile(const std::string& name, const std::string& contents) : name_{name} {
        const auto written = eval("FileSystem.write_file(" + luma_string_literal(name_) + ", " +
                                  luma_string_literal(contents) + ")");

        if (!is_success_result(written)) {
            throw std::runtime_error{"LumaTempFile: cannot write " + name_ + ": " +
                                     written.to_string()};
        }
    }

    LumaTempFile(const LumaTempFile&) = delete;
    LumaTempFile& operator=(const LumaTempFile&) = delete;
    LumaTempFile(LumaTempFile&&) = delete;
    LumaTempFile& operator=(LumaTempFile&&) = delete;

    // Best-effort cleanup: a destructor cannot report a failed delete, and
    // FileSystem.delete reports a missing file as a failure result (not an
    // exception), which is expected when the test already removed it.
    ~LumaTempFile() noexcept {
        try {
            (void)eval("FileSystem.delete(" + luma_string_literal(name_) + ")");
        } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
            // An exception must not escape a destructor.
        }
    }

    [[nodiscard]] const std::string& name() const {
        return name_;
    }

private:
    std::string name_;
};

} // namespace luma::test

#endif // LUMA_TESTS_RUNTIME_STDLIB_TEST_HELPERS_HPP
