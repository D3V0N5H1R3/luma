// Standard library test shared helpers.
//
// Convenience wrapper that imports shared_eval.hpp and test_framework.hpp,
// and provides a file-local eval() function in the global namespace.
// This avoids repeating the same boilerplate in every stdlib test file.
//
// Note (TS-4): Although three test helper headers exist (stdlib_test_helpers,
// type_checker_test_helpers, compiler_test_helpers), they serve distinct
// purposes and are not candidates for unification:
//   - stdlib_test_helpers wraps shared_eval for end-to-end stdlib tests.
//   - type_checker_test_helpers provides TypeChecker-specific passes/fails/check.
//   - compiler_test_helpers provides opcode-inspection utilities.

#ifndef LUMA_STDLIB_TEST_HELPERS_HPP
#define LUMA_STDLIB_TEST_HELPERS_HPP

#include <string>

#include "shared_eval.hpp"
#include "test_framework.hpp"

// Test-only convenience: this header is included exclusively by test
// translation units (never by any library or production header), so pulling
// the luma namespace into scope here keeps test bodies concise without leaking
// into shipped code.
using namespace luma;

[[maybe_unused]] static Value eval(const std::string& source) {
    return luma::test::eval(source);
}

// Test-only convenience: returns true when evaluating source raises a runtime
// error. Delegates to the shared eval_throws() (shared_eval.hpp) so the
// try/catch logic lives in one place. Shared by the VM test suites.
[[maybe_unused]] static bool throws_runtime(const std::string& source) {
    return luma::test::eval_throws(source);
}

// RAII guard that temporarily overrides a resource limit — or any assignable
// value reachable through an lvalue reference — and restores the original on
// destruction, even if an assertion throws in between.  A lowered cap therefore
// can never leak into a later test.  Templated on the limit's type so it works
// with any ResourceLimits field (std::size_t, std::int64_t, int, …).
//
// Class template argument deduction requires both constructor arguments to have
// the same type T, so when the field type differs from the replacement literal's
// type, cast the literal explicitly:
//   const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(8)};
template <typename T> class LimitGuard {
public:
    LimitGuard(T& target, T temp_value) : target_{target}, original_{target} {
        target_ = temp_value;
    }

    ~LimitGuard() noexcept {
        target_ = original_;
    }

    LimitGuard(const LimitGuard&) = delete;
    LimitGuard& operator=(const LimitGuard&) = delete;

private:
    T& target_;
    T original_;
};

// RAII helper for temporary files created through the Luma FileSystem module.
// The file is removed via FileSystem.delete when the guard goes out of scope —
// even if an assertion throws in between — so temp files never leak into the
// working tree. Two constructions are supported:
//   * LumaTempFile{name, contents} — writes contents via FileSystem.write_file.
//   * LumaTempFile{name}           — registers the name for cleanup only, for
//     files created by other means (write_lines, append_file, Hash.*_file, …).
class LumaTempFile {
public:
    explicit LumaTempFile(const std::string& name) : name_{name} {}

    LumaTempFile(const std::string& name, const std::string& contents) : name_{name} {
        (void)luma::test::eval("FileSystem.write_file(" + literal(name_) + ", " +
                               literal(contents) + ")");
    }

    ~LumaTempFile() noexcept {
        try {
            (void)luma::test::eval("FileSystem.delete(" + literal(name_) + ")");
        } catch (...) {
            // Best-effort cleanup: a failed delete must not escape the destructor.
        }
    }

    LumaTempFile(const LumaTempFile&) = delete;
    LumaTempFile& operator=(const LumaTempFile&) = delete;
    LumaTempFile(LumaTempFile&&) = delete;
    LumaTempFile& operator=(LumaTempFile&&) = delete;

    [[nodiscard]] const std::string& name() const {
        return name_;
    }

private:
    // Render a std::string as a Luma double-quoted string literal, escaping the
    // characters that would otherwise break the surrounding eval() source.
    [[nodiscard]] static std::string literal(const std::string& value) {
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

    std::string name_;
};

#endif // LUMA_STDLIB_TEST_HELPERS_HPP
