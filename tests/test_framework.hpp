// test_framework.hpp — Shared micro-test framework for Luma C++ unit tests.
//
// Usage:
//   #include "test_framework.hpp"
//   void test_something() { ASSERT_EQ(1, 1); }
//   int main() { RUN(test_something); return SUMMARY(); }
//
// Every helper lives in namespace luma::test. The assertion macros evaluate each
// argument exactly once (they forward to function templates), so an argument
// with side effects — such as evaluating a Luma program — runs a single time.

#ifndef LUMA_TEST_FRAMEWORK_HPP
#define LUMA_TEST_FRAMEWORK_HPP

#include <atomic>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace luma::test {

// ─── Test runner ───

inline std::atomic<int> tests_run{0};
inline std::atomic<int> tests_passed{0};

// Run one test and report it. This is the top-level reporter: every exception
// escaping a test — including a non-std one — is recorded as a failure.
inline void run_test(const char* name, const std::function<void()>& fn) noexcept {
    ++tests_run;

    try {
        fn();

        ++tests_passed;

        std::cout << "[PASS] " << name << "\n";
    } catch (const std::exception& e) {
        std::cout << "[FAIL] " << name << " \xe2\x80\x94 " << e.what() << "\n";
    } catch (...) {
        std::cout << "[FAIL] " << name << " \xe2\x80\x94 unknown exception\n";
    }
}

inline int summary() noexcept {
    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return (tests_passed == tests_run) ? 0 : 1;
}

// Print a test-suite banner.  Marked noexcept so it can be the opening statement
// of main() without making main() itself capable of throwing.
inline void print_suite_header(std::string_view title) noexcept {
    std::cout << "=== " << title << " ===\n\n";
}

// ─── Assertion support ───

// Where an assertion was written; captured by the ASSERT_* macros.
struct AssertionSite {
    const char* file;
    int line;
    const char* function;
};

[[noreturn]] inline void throw_assertion_error(const std::string& msg) {
    throw std::runtime_error{msg};
}

[[noreturn]] inline void fail_at(const AssertionSite& site, std::string_view detail) {
    std::ostringstream oss;
    oss << site.file << ":" << site.line << " in " << site.function << ": " << detail;
    throw_assertion_error(oss.str());
}

// Types that already have an operator<< (std::string, std::filesystem::path, …).
template <typename T>
concept Streamable = requires(std::ostream& out, const T& value) { out << value; };

template <typename T> inline constexpr bool is_chrono_duration_v = false;

template <typename Rep, typename Period>
inline constexpr bool is_chrono_duration_v<std::chrono::duration<Rep, Period>> = true;

template <typename T> inline constexpr bool is_chrono_time_point_v = false;

template <typename Clock, typename Duration>
inline constexpr bool is_chrono_time_point_v<std::chrono::time_point<Clock, Duration>> = true;

// 128-bit integers have no operator<< (libc++ uses __int128 as the tick type of
// std::filesystem::file_time_type), so they are printed digit by digit.
// __extension__ keeps -Wpedantic quiet about the non-standard type names.
#ifdef __SIZEOF_INT128__
__extension__ typedef __int128 Int128;
__extension__ typedef unsigned __int128 UInt128;

template <typename T>
inline constexpr bool is_int128_v =
    std::is_same_v<std::remove_cv_t<T>, Int128> || std::is_same_v<std::remove_cv_t<T>, UInt128>;

template <typename T>
inline constexpr bool is_unsigned_int128_v = std::is_same_v<std::remove_cv_t<T>, UInt128>;
#else
template <typename T> inline constexpr bool is_int128_v = false;

template <typename T> inline constexpr bool is_unsigned_int128_v = false;
#endif

template <typename T> std::string int128_to_string(T value) {
    bool negative = false;

    if constexpr (!is_unsigned_int128_v<T>) {
        negative = value < 0;
    }

    std::string digits;

    do {
        const auto digit = static_cast<int>(value % 10);
        digits.push_back(static_cast<char>('0' + (negative ? -digit : digit)));
        value /= 10;
    } while (value != 0);

    if (negative) {
        digits.push_back('-');
    }

    return {digits.rbegin(), digits.rend()};
}

// A chrono tick count, by value, in a form that can be streamed.
template <typename Rep> auto printable_count(Rep count) {
    if constexpr (is_int128_v<Rep>) {
        return int128_to_string(count);
    } else {
        return count;
    }
}

// Format values for assertion messages. Enums print their underlying integer;
// narrow character types print as int so byte/char assertions show readable
// numbers instead of raw (possibly control) characters; std::chrono durations
// and time points print their tick count (standard libraries differ in which
// chrono types have an operator<<, e.g. file_time_type on older libstdc++);
// ranges without their own operator<< (such as std::vector) print as
// "[a, b, c]"; everything else is passed through by reference (so move-only
// values such as std::unique_ptr print too) and streamed with operator<<.
template <typename T> decltype(auto) to_printable(const T& value) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<std::underlying_type_t<T>>(value);
    } else if constexpr (std::is_same_v<T, char> || std::is_same_v<T, signed char> ||
                         std::is_same_v<T, unsigned char>) {
        return static_cast<int>(value);
    } else if constexpr (is_chrono_duration_v<T>) {
        return printable_count(value.count());
    } else if constexpr (is_chrono_time_point_v<T>) {
        return printable_count(value.time_since_epoch().count());
    } else if constexpr (is_int128_v<T>) {
        return int128_to_string(value);
    } else if constexpr (std::ranges::input_range<T> && !Streamable<T>) {
        std::ostringstream text;
        text << "[";
        std::string_view separator;

        for (const auto& element : value) {
            text << separator << to_printable(element);
            separator = ", ";
        }

        text << "]";
        return text.str();
    } else {
        return (value);
    }
}

// Integer types that the std::cmp_* functions accept (bool and the character
// types are excluded by the standard).
template <typename T>
concept SafeComparableInteger =
    std::integral<T> && !std::same_as<T, bool> && !std::same_as<T, char> &&
    !std::same_as<T, wchar_t> && !std::same_as<T, char8_t> && !std::same_as<T, char16_t> &&
    !std::same_as<T, char32_t>;

// Compare with the std::cmp_* functions when both sides are integers, so that a
// signed/unsigned pair compares by mathematical value (-1 never equals SIZE_MAX).
template <typename Lhs, typename Rhs>
[[nodiscard]] constexpr bool values_equal(const Lhs& lhs, const Rhs& rhs) {
    if constexpr (SafeComparableInteger<Lhs> && SafeComparableInteger<Rhs>) {
        return std::cmp_equal(lhs, rhs);
    } else {
        return static_cast<bool>(lhs == rhs);
    }
}

template <typename Lhs, typename Rhs>
[[nodiscard]] constexpr bool values_less(const Lhs& lhs, const Rhs& rhs) {
    if constexpr (SafeComparableInteger<Lhs> && SafeComparableInteger<Rhs>) {
        return std::cmp_less(lhs, rhs);
    } else {
        return static_cast<bool>(lhs < rhs);
    }
}

template <typename Lhs, typename Rhs>
[[nodiscard]] constexpr bool values_less_equal(const Lhs& lhs, const Rhs& rhs) {
    if constexpr (SafeComparableInteger<Lhs> && SafeComparableInteger<Rhs>) {
        return std::cmp_less_equal(lhs, rhs);
    } else {
        return static_cast<bool>(lhs <= rhs);
    }
}

// The binary comparisons the ASSERT_* macros check.
enum class Comparison {
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
};

// Only the requested operator is instantiated, so ASSERT_EQ(pointer, nullptr)
// compiles even though `pointer < nullptr` would not.
template <Comparison comparison, typename Lhs, typename Rhs>
[[nodiscard]] constexpr bool holds(const Lhs& lhs, const Rhs& rhs) {
    if constexpr (comparison == Comparison::equal) {
        return values_equal(lhs, rhs);
    } else if constexpr (comparison == Comparison::not_equal) {
        return !values_equal(lhs, rhs);
    } else if constexpr (comparison == Comparison::less) {
        return values_less(lhs, rhs);
    } else if constexpr (comparison == Comparison::less_equal) {
        return values_less_equal(lhs, rhs);
    } else if constexpr (comparison == Comparison::greater) {
        return values_less(rhs, lhs);
    } else {
        return values_less_equal(rhs, lhs);
    }
}

// The operator shown in a failure message: the relation that was observed
// instead of the asserted one.
[[nodiscard]] constexpr std::string_view violated_relation(Comparison comparison) noexcept {
    switch (comparison) {
        case Comparison::equal:
            return "!=";
        case Comparison::not_equal:
            return "==";
        case Comparison::less:
            return ">=";
        case Comparison::less_equal:
            return ">";
        case Comparison::greater:
            return "<=";
        case Comparison::greater_equal:
            return "<";
    }

    return "?";
}

template <Comparison comparison, typename Lhs, typename Rhs>
void assert_comparison(const Lhs& lhs, const Rhs& rhs, std::string_view macro_name,
                       const AssertionSite& site) {
    if (holds<comparison>(lhs, rhs)) {
        return;
    }

    std::ostringstream detail;
    detail << macro_name << " failed: " << to_printable(lhs) << " " << violated_relation(comparison)
           << " " << to_printable(rhs);
    fail_at(site, detail.str());
}

// `holds` is whether the asserted property is true (cond for ASSERT_TRUE,
// !cond for ASSERT_FALSE).
inline void assert_condition(bool holds, std::string_view macro_name,
                             std::string_view condition_text, const AssertionSite& site) {
    if (!holds) {
        fail_at(site, std::string{macro_name} + " failed: " + std::string{condition_text});
    }
}

template <typename Actual, typename Expected, typename Tolerance>
void assert_near(const Actual& actual, const Expected& expected, const Tolerance& tolerance,
                 const AssertionSite& site) {
    const auto difference = std::abs(static_cast<double>(actual) - static_cast<double>(expected));

    // Written as !(difference <= tolerance) so a NaN on either side fails.
    if (!(difference <= static_cast<double>(tolerance))) {
        std::ostringstream detail;
        detail << "ASSERT_NEAR failed: |" << to_printable(actual) << " - " << to_printable(expected)
               << "| > " << to_printable(tolerance);
        fail_at(site, detail.str());
    }
}

// Run `action`, which must throw a std::exception whose message contains
// `expected_substring`. A non-std exception is not caught: it propagates and
// run_test reports it as a failure.
template <typename Action>
void assert_throws_with_message(Action&& action, std::string_view expected_substring,
                                std::string_view expression_text, const AssertionSite& site) {
    if (expected_substring.empty()) {
        fail_at(site, "ASSERT_THROWS_WITH_MESSAGE needs a non-empty expected message");
    }

    try {
        std::forward<Action>(action)();
    } catch (const std::exception& error) {
        const std::string_view message{error.what()};

        if (message.find(expected_substring) == std::string_view::npos) {
            std::ostringstream detail;
            detail << "ASSERT_THROWS_WITH_MESSAGE failed: exception message '" << message
                   << "' does not contain '" << expected_substring << "'";
            fail_at(site, detail.str());
        }

        return;
    }

    fail_at(site, "ASSERT_THROWS_WITH_MESSAGE failed: no exception from: " +
                      std::string{expression_text});
}

// Run `action`, which must throw an exception of (or derived from) Expected.
// Unlike a message check, this pins the exception class — essential where
// distinct error types (e.g. ChannelClosedError vs ChannelEmptyError) must be
// distinguishable by callers.
template <typename Expected, typename Action>
void assert_throws_as(Action&& action, std::string_view type_name, std::string_view expression_text,
                      const AssertionSite& site) {
    static_assert(std::is_base_of_v<std::exception, Expected>,
                  "ASSERT_THROWS_AS expects a std::exception-derived type");

    std::ostringstream detail;
    detail << "ASSERT_THROWS_AS failed: expected " << type_name << " from: " << expression_text;

    try {
        std::forward<Action>(action)();
    } catch (const std::exception& error) {
        if (dynamic_cast<const Expected*>(&error) != nullptr) {
            return;
        }

        detail << " (got a different exception: " << error.what() << ")";
        fail_at(site, detail.str());
    }

    detail << " (no exception thrown)";
    fail_at(site, detail.str());
}

// Unwrap an optional in a test, throwing a clear assertion failure (rather than
// std::bad_optional_access) when it is empty.  The dereference is guarded by the
// preceding has_value() check on the same local reference, so static analysers
// recognise it as a checked access — unlike a bare optional::value() whose guard
// cannot be tracked when the optional is reached through a subscript or call.
template <typename T>
const T& require_value(const std::optional<T>& opt, const char* file, int line) {
    if (!opt.has_value()) {
        std::ostringstream oss;
        oss << file << ":" << line << ": REQUIRE_VALUE failed: optional has no value";
        throw_assertion_error(oss.str());
    }

    return *opt;
}

} // namespace luma::test

// ─── Assertion macros ───
//
// Macros only capture the call site and the source text; the checks themselves
// are the function templates above, so every argument is evaluated once.

#define LUMA_ASSERTION_SITE                                                                        \
    luma::test::AssertionSite {                                                                    \
        __FILE__, __LINE__, __func__                                                               \
    }

#define ASSERT_EQ(a, b)                                                                            \
    luma::test::assert_comparison<luma::test::Comparison::equal>((a), (b), "ASSERT_EQ",            \
                                                                 LUMA_ASSERTION_SITE)

#define ASSERT_NE(a, b)                                                                            \
    luma::test::assert_comparison<luma::test::Comparison::not_equal>((a), (b), "ASSERT_NE",        \
                                                                     LUMA_ASSERTION_SITE)

#define ASSERT_LT(a, b)                                                                            \
    luma::test::assert_comparison<luma::test::Comparison::less>((a), (b), "ASSERT_LT",             \
                                                                LUMA_ASSERTION_SITE)

#define ASSERT_LE(a, b)                                                                            \
    luma::test::assert_comparison<luma::test::Comparison::less_equal>((a), (b), "ASSERT_LE",       \
                                                                      LUMA_ASSERTION_SITE)

#define ASSERT_GT(a, b)                                                                            \
    luma::test::assert_comparison<luma::test::Comparison::greater>((a), (b), "ASSERT_GT",          \
                                                                   LUMA_ASSERTION_SITE)

#define ASSERT_GE(a, b)                                                                            \
    luma::test::assert_comparison<luma::test::Comparison::greater_equal>((a), (b), "ASSERT_GE",    \
                                                                         LUMA_ASSERTION_SITE)

#define ASSERT_TRUE(cond)                                                                          \
    luma::test::assert_condition(static_cast<bool>(cond), "ASSERT_TRUE", #cond, LUMA_ASSERTION_SITE)

#define ASSERT_FALSE(cond)                                                                         \
    luma::test::assert_condition(!static_cast<bool>(cond), "ASSERT_FALSE", #cond,                  \
                                 LUMA_ASSERTION_SITE)

#define ASSERT_NEAR(a, b, epsilon) luma::test::assert_near((a), (b), (epsilon), LUMA_ASSERTION_SITE)

// Assert that `expr` throws a std::exception whose message contains
// `expected_substr`. Pinning the message keeps a test from passing because of an
// unrelated failure (a typo in the Luma source raises "undefined variable", not
// the error the test is about).
#define ASSERT_THROWS_WITH_MESSAGE(expr, expected_substr)                                          \
    luma::test::assert_throws_with_message([&] { (void)(expr); }, (expected_substr), #expr,        \
                                           LUMA_ASSERTION_SITE)

// Assert that `expr` throws an exception that is (or derives from) ExceptionType.
#define ASSERT_THROWS_AS(expr, ExceptionType)                                                      \
    luma::test::assert_throws_as<ExceptionType>([&] { (void)(expr); }, #ExceptionType, #expr,      \
                                                LUMA_ASSERTION_SITE)

// Unwrap an optional, failing the test with a located message when it is empty:
//   const auto& v = REQUIRE_VALUE(maybe_value);
#define REQUIRE_VALUE(opt) luma::test::require_value((opt), __FILE__, __LINE__)

namespace luma::test {

// ─── Environment access ───

// Read an environment variable; std::nullopt when it is unset. Keyed on _WIN32
// (not _MSC_VER) so every Windows toolchain uses the secure _dupenv_s.
[[nodiscard]] inline std::optional<std::string> read_environment(const char* name) {
#ifdef _WIN32
    char* raw = nullptr;
    std::size_t length = 0;

    if (_dupenv_s(&raw, &length, name) != 0 || raw == nullptr) {
        return std::nullopt;
    }

    std::string value{raw};
    std::free(raw); // NOLINT — _dupenv_s allocates with malloc.
    return value;
#else
    const char* raw = std::getenv(name); // NOLINT — read-only access.

    if (raw == nullptr) {
        return std::nullopt;
    }

    return std::string{raw};
#endif
}

// ─── RAII helpers ───

// RAII guard that redirects std::cin to a custom buffer and restores the
// original on destruction — even if an exception is thrown.
class StdinRedirect {
public:
    explicit StdinRedirect(std::streambuf* buf) : orig_{std::cin.rdbuf(buf)} {}

    StdinRedirect(const StdinRedirect&) = delete;
    StdinRedirect& operator=(const StdinRedirect&) = delete;
    StdinRedirect(StdinRedirect&&) = delete;
    StdinRedirect& operator=(StdinRedirect&&) = delete;

    ~StdinRedirect() noexcept {
        std::cin.rdbuf(orig_);
    }

private:
    std::streambuf* orig_;
};

// RAII guard that redirects an output stream (e.g. std::cout or std::cerr)
// into an internal buffer so emitted text can be asserted on, and restores the
// original stream buffer on destruction — even if an exception is thrown.
// The captured text is available via str().
class CapturedStream {
public:
    explicit CapturedStream(std::ostream& stream)
        : stream_{stream}, orig_{stream.rdbuf(buffer_.rdbuf())} {}

    CapturedStream(const CapturedStream&) = delete;
    CapturedStream& operator=(const CapturedStream&) = delete;
    CapturedStream(CapturedStream&&) = delete;
    CapturedStream& operator=(CapturedStream&&) = delete;

    ~CapturedStream() noexcept {
        stream_.rdbuf(orig_);
    }

    [[nodiscard]] std::string str() const {
        return buffer_.str();
    }

private:
    std::ostream& stream_;
    std::ostringstream buffer_;
    std::streambuf* orig_;
};

// RAII guard that redirects two output streams (typically std::cout and
// std::cerr) into a single shared buffer, so their combined output can be
// asserted on in emission order, and restores both original stream buffers on
// destruction — even if an exception is thrown. The captured text is available
// via str().
class CapturedStreams {
public:
    CapturedStreams(std::ostream& first, std::ostream& second)
        : first_{first},
          second_{second},
          orig_first_{first.rdbuf(buffer_.rdbuf())},
          orig_second_{second.rdbuf(buffer_.rdbuf())} {}

    CapturedStreams(const CapturedStreams&) = delete;
    CapturedStreams& operator=(const CapturedStreams&) = delete;
    CapturedStreams(CapturedStreams&&) = delete;
    CapturedStreams& operator=(CapturedStreams&&) = delete;

    ~CapturedStreams() noexcept {
        first_.rdbuf(orig_first_);
        second_.rdbuf(orig_second_);
    }

    [[nodiscard]] std::string str() const {
        return buffer_.str();
    }

private:
    std::ostream& first_;
    std::ostream& second_;
    std::ostringstream buffer_;
    std::streambuf* orig_first_;
    std::streambuf* orig_second_;
};

// RAII guard that sets an environment variable for the duration of a scope and
// restores the previous value — or removes the variable if it was previously
// unset — on destruction, even if an exception is thrown. Test-only helper for
// exercising code that reads the environment (e.g. LUMA_PATH resolution).
//
// On Windows an empty value removes the variable (a _putenv_s limitation), so
// ScopedEnv{"NAME", ""} behaves like unsetting it there.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const std::string& value)
        : name_{name}, previous_{read_environment(name)} {
        if (!set(name_, value)) {
            throw std::runtime_error{"ScopedEnv: cannot set environment variable " + name_};
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
    ScopedEnv(ScopedEnv&&) = delete;
    ScopedEnv& operator=(ScopedEnv&&) = delete;

    // A destructor cannot report a failed restore; restoring is best effort.
    ~ScopedEnv() noexcept {
        if (previous_.has_value()) {
            (void)set(name_, *previous_);
        } else {
            (void)unset(name_);
        }
    }

private:
    [[nodiscard]] static bool set(const std::string& name, const std::string& value) noexcept {
#ifdef _WIN32
        return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
        return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
    }

    [[nodiscard]] static bool unset(const std::string& name) noexcept {
#ifdef _WIN32
        // Assigning the empty string removes the variable on Windows.
        return _putenv_s(name.c_str(), "") == 0;
#else
        return unsetenv(name.c_str()) == 0;
#endif
    }

    std::string name_;
    std::optional<std::string> previous_;
};

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
    LimitGuard(T& target, T temporary_value) : target_{target}, original_{target} {
        target_ = temporary_value;
    }

    LimitGuard(const LimitGuard&) = delete;
    LimitGuard& operator=(const LimitGuard&) = delete;
    LimitGuard(LimitGuard&&) = delete;
    LimitGuard& operator=(LimitGuard&&) = delete;

    ~LimitGuard() noexcept {
        target_ = original_;
    }

private:
    T& target_;
    T original_;
};

// ─── Files ───

// Read the entire contents of a file into a string.
// Returns an empty optional if the file does not exist.
[[nodiscard]] inline std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};

    if (!in.is_open()) {
        return std::nullopt;
    }

    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// Read an entire file into a string, throwing std::runtime_error if it cannot
// be opened. For optional presence (no throw when the file is absent) call
// read_file directly.
[[nodiscard]] inline std::string read_file_text(const std::filesystem::path& path) {
    auto contents = read_file(path);

    if (!contents.has_value()) {
        throw std::runtime_error{"read_file_text: cannot open " + path.string()};
    }

    return *contents;
}

// Write content to a file in binary mode (no CRLF translation), creating parent
// directories as needed; throws when the file cannot be written.
inline void write_file(const std::filesystem::path& path, std::string_view content) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out << content;
    out.close();

    if (!out) {
        throw std::runtime_error{"write_file: cannot write " + path.string()};
    }
}

// A token unique to this process, mixed into temporary paths. Combined with the
// per-process counter below, it prevents collisions when these helpers are used
// from multiple test binaries running concurrently (e.g. under `ctest -j`),
// which would otherwise race on identically named entries in the shared system
// temp directory.
[[nodiscard]] inline const std::string& process_token() {
    static const std::string token = std::to_string(std::random_device{}());

    return token;
}

// Generate a temporary path unique within this process (and, via process_token,
// across concurrent processes) by combining the given prefix/extension with a
// monotonically increasing counter, rooted at the system temp directory.
[[nodiscard]] inline std::filesystem::path unique_temp_path(std::string_view prefix,
                                                            std::string_view extension) {
    static std::atomic<int> counter{0};

    return std::filesystem::temp_directory_path() /
           (std::string{prefix} + process_token() + "_" + std::to_string(counter++) +
            std::string{extension});
}

// RAII helper that writes a file on construction and removes it on destruction
// (even if an exception is thrown), so tests can exercise real files without
// leaking them into the working tree. Two constructions are supported:
//   * TempFile{content}        — path auto-generated in the system temp
//     directory with a unique name and a ".luma" suffix.
//   * TempFile{path, content}  — caller supplies the full path; parent
//     directories are created as needed.
class TempFile {
public:
    explicit TempFile(const std::string& content)
        : TempFile{unique_temp_path("luma_test_file_", ".luma"), content} {}

    TempFile(const std::filesystem::path& path, const std::string& content) : path_{path} {
        write_file(path_, content);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;

    ~TempFile() noexcept {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

    [[nodiscard]] std::string path_string() const {
        return path_.string();
    }

private:
    std::filesystem::path path_;
};

// RAII helper that creates a fresh, unique temporary directory on construction
// and removes it recursively on destruction — even if an exception is thrown.
class TempDir {
public:
    TempDir() : path_{unique_temp_path("luma_test_dir_", "")} {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    ~TempDir() noexcept {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

    [[nodiscard]] std::string path_string() const {
        return path_.string();
    }

private:
    std::filesystem::path path_;
};

// RAII helper for a file or directory that a test creates under a fixed name
// (for example through a Luma program): removes any stale leftover on
// construction and removes the path recursively on destruction, so a failing
// assertion cannot leave it behind.
class ScopedPathCleanup {
public:
    explicit ScopedPathCleanup(std::filesystem::path path) : path_{std::move(path)} {
        remove();
    }

    ScopedPathCleanup(const ScopedPathCleanup&) = delete;
    ScopedPathCleanup& operator=(const ScopedPathCleanup&) = delete;
    ScopedPathCleanup(ScopedPathCleanup&&) = delete;
    ScopedPathCleanup& operator=(ScopedPathCleanup&&) = delete;

    ~ScopedPathCleanup() noexcept {
        remove();
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

private:
    void remove() const noexcept {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    std::filesystem::path path_;
};

// ─── Snapshot testing ───
//
// Compare a string against a stored .expected file.  When the environment
// variable UPDATE_SNAPSHOTS is set (to any value), the expected file is
// (re)written from the actual value, making the test pass and creating or
// updating the baseline.
//
// Usage:
//   ASSERT_SNAPSHOT("diagnostic_type_mismatch", actual_string, __FILE__);
//
// Snapshot files are stored in a "snapshots/" directory next to the test
// source file.

namespace snapshot {

[[nodiscard]] inline bool should_update() {
    static const bool update = read_environment("UPDATE_SNAPSHOTS").has_value();
    return update;
}

// test_file: the __FILE__ of the calling test.
// name:      snapshot name (used as the filename stem).
// actual:    the string to compare against the snapshot.
inline void assert_snapshot(const char* test_file, const char* name, const std::string& actual,
                            const char* file, int line) {
    namespace fs = std::filesystem;

    const auto test_dir = fs::path{test_file}.parent_path();
    const auto snapshot_path = test_dir / "snapshots" / (std::string{name} + ".expected");

    if (should_update()) {
        write_file(snapshot_path, actual);
        return;
    }

    const auto expected = read_file(snapshot_path);

    if (!expected.has_value()) {
        std::ostringstream oss;
        oss << file << ":" << line
            << ": ASSERT_SNAPSHOT failed: snapshot file not found: " << snapshot_path.string()
            << "\n  Run with UPDATE_SNAPSHOTS=1 to create it.";
        throw_assertion_error(oss.str());
    }

    if (*expected != actual) {
        std::ostringstream oss;
        oss << file << ":" << line << ": ASSERT_SNAPSHOT failed for '" << name << "'\n"
            << "--- expected (from " << snapshot_path.string() << "):\n"
            << *expected << "\n+++ actual:\n"
            << actual;
        throw_assertion_error(oss.str());
    }
}

} // namespace snapshot

// ─── Test fixture support ───
//
// Derive from TestFixture and override set_up() / tear_down() to share
// common setup across related tests.
//
// Usage:
//   class MyFixture : public luma::test::TestFixture {
//   public:
//       int value{};
//       void set_up() override { value = 42; }
//       void tear_down() override { /* cleanup */ }
//   };
//
//   TEST_F(MyFixture, test_uses_value) { ASSERT_EQ(fixture.value, 42); }
//
//   int main() { RUN(test_uses_value); return SUMMARY(); }

class TestFixture {
public:
    TestFixture() = default;
    TestFixture(const TestFixture&) = delete;
    TestFixture& operator=(const TestFixture&) = delete;
    TestFixture(TestFixture&&) = delete;
    TestFixture& operator=(TestFixture&&) = delete;
    virtual ~TestFixture() = default;

    virtual void set_up() {}

    virtual void tear_down() {}
};

// Run `body` against a freshly set-up fixture. tear_down() runs exactly once,
// after the body, whether or not it failed; the body's failure is then
// rethrown so the test is still reported as failed.
template <typename Fixture> void run_fixture_test(void (*body)(Fixture&)) {
    Fixture fixture;
    fixture.set_up();

    std::exception_ptr failure;

    try {
        body(fixture);
    } catch (...) {
        failure = std::current_exception();
    }

    fixture.tear_down();

    if (failure) {
        std::rethrow_exception(failure);
    }
}

// ─── Parameterized tests ───
//
// Run a test function once per parameter set, reporting each case individually.
//
// Usage:
//   void test_add(const std::string& expr, int expected) {
//       ASSERT_EQ(eval(expr).as_integer(), expected);
//   }
//
//   int main() {
//       RUN_PARAMETERIZED(test_add, std::vector<std::tuple<std::string, int>>{
//           {"1 + 2", 3},
//           {"4 + 5", 9},
//       });
//       return SUMMARY();
//   }

// Tuple-like types (std::tuple, std::pair, std::array, …).
template <typename T>
concept TupleLike = requires { std::tuple_size<T>::value; };

// Run `fn` for each element in `params`, reporting each as a separate test case.
// Tuple-like elements are unpacked into fn's arguments via std::apply; scalar
// elements are passed directly.
template <typename Container, typename Fn>
void run_parameterized(const char* name, const Container& params, Fn&& fn) noexcept {
    std::size_t index = 0;

    for (const auto& param : params) {
        const auto case_name = std::string{name} + "[" + std::to_string(index) + "]";

        run_test(case_name.c_str(), [&]() {
            if constexpr (TupleLike<std::decay_t<decltype(param)>>) {
                std::apply(fn, param);
            } else {
                fn(param);
            }
        });

        ++index;
    }
}

// ─── Auto-registration ───
//
// Opt-in alternative to manual RUN() calls.  Use LUMA_TEST(name) to define a
// test that is automatically registered in a global registry, then call
// LUMA_RUN_ALL() in main() to execute every registered test.
//
// Usage:
//   #include "test_framework.hpp"
//
//   LUMA_TEST(addition)       { ASSERT_EQ(1 + 2, 3); }
//   LUMA_TEST(subtraction)    { ASSERT_EQ(5 - 3, 2); }
//
//   int main() { LUMA_RUN_ALL(); }
//
// LUMA_TEST and manual RUN() can coexist in the same file — both share the
// same counters and SUMMARY() output.

// Collects the auto-registered tests (populated during static initialisation).
class TestRegistry {
public:
    using TestFunction = void (*)();

    [[nodiscard]] static TestRegistry& instance() {
        static TestRegistry registry;
        return registry;
    }

    void add(const char* name, TestFunction fn) {
        tests_.emplace_back(name, fn);
    }

    [[nodiscard]] const std::vector<std::pair<const char*, TestFunction>>& tests() const {
        return tests_;
    }

private:
    TestRegistry() = default;

    std::vector<std::pair<const char*, TestFunction>> tests_;
};

// Static-initialiser helper.  Constructing one of these at file scope adds
// the test to the registry before main() runs.
struct TestRegistrar {
    TestRegistrar(const char* name, TestRegistry::TestFunction fn) {
        TestRegistry::instance().add(name, fn);
    }
};

// Run every auto-registered test and return the summary exit code.
[[nodiscard]] inline int run_all_registered() {
    for (const auto& [name, fn] : TestRegistry::instance().tests()) {
        run_test(name, fn);
    }

    return summary();
}

} // namespace luma::test

// Assert that `actual` matches the contents of
// snapshots/<name>.expected next to `test_file`.
#define ASSERT_SNAPSHOT(name, actual, test_file)                                                   \
    luma::test::snapshot::assert_snapshot(test_file, name, actual, __FILE__, __LINE__)

// Define a fixture-based test. The test body receives `fixture` by reference.
#define TEST_F(FixtureClass, test_name)                                                            \
    static void test_name##_body(FixtureClass& fixture);                                           \
    static void test_name() {                                                                      \
        luma::test::run_fixture_test<FixtureClass>(&test_name##_body);                             \
    }                                                                                              \
    static void test_name##_body(FixtureClass& fixture)

// Register and run a test function: RUN(test_name);
#define RUN(fn) luma::test::run_test(#fn, fn)

// Run a parameterized test: RUN_PARAMETERIZED(fn, params_container);
// Use __VA_ARGS__ so template commas (e.g. std::pair<A, B>) are handled.
#define RUN_PARAMETERIZED(fn, ...) luma::test::run_parameterized(#fn, __VA_ARGS__, fn)

// Print summary and return exit code: return SUMMARY();
#define SUMMARY() luma::test::summary()

// Define and auto-register a test function.
#define LUMA_TEST(name)                                                                            \
    static void luma_test_##name();                                                                \
    static const luma::test::TestRegistrar luma_registrar_##name(#name, luma_test_##name);         \
    static void luma_test_##name()

// Run every auto-registered test, then return the summary exit code.
// Intended as the sole statement in main():  int main() { LUMA_RUN_ALL(); }
#define LUMA_RUN_ALL() return luma::test::run_all_registered()

#endif // LUMA_TEST_FRAMEWORK_HPP
