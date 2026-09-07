// test_framework.hpp — Shared micro-test framework for Luma C++ unit tests.
//
// Usage:
//   #include "test_framework.hpp"
//   void test_something() { ASSERT_EQ(1, 1); }
//   int main() { RUN(test_something); return SUMMARY(); }

#ifndef LUMA_TEST_FRAMEWORK_HPP
#define LUMA_TEST_FRAMEWORK_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace luma::test {

inline std::atomic<int> tests_run{0};
inline std::atomic<int> tests_passed{0};

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

// Helper to format values for assertion messages.
// Enums are cast to their underlying integer type; narrow character types are
// cast to int so byte/char assertions print readable numeric values instead of
// emitting raw (possibly control) characters; all other types use operator<<.
template <typename T> auto to_printable(const T& value) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<std::underlying_type_t<T>>(value);
    } else if constexpr (std::is_same_v<T, char> || std::is_same_v<T, signed char> ||
                         std::is_same_v<T, unsigned char>) {
        return static_cast<int>(value);
    } else {
        return value;
    }
}

} // namespace luma::test

// ─── Assertion macros ───
//
// Each macro follows the same pattern: check a condition, then throw with a
// formatted location + message.  The throw_assertion_error helper centralises
// the throw so the macros only build the message string.

namespace luma::test {

[[noreturn]] inline void throw_assertion_error(const std::string& msg) {
    throw std::runtime_error{msg};
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

// Unwrap an optional, failing the test with a located message when it is empty:
//   const auto& v = REQUIRE_VALUE(maybe_value);
#define REQUIRE_VALUE(opt) luma::test::require_value((opt), __FILE__, __LINE__)

// MSVC emits C4127 ("conditional expression is constant") for the
// `while (false)` in the do-while-false idiom used by every assertion macro.
// __pragma(warning(...)) is the MSVC inline form usable inside macro bodies.
#ifdef _MSC_VER
#define LUMA_MSVC_SUPPRESS_C4127_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4127))
#define LUMA_MSVC_SUPPRESS_C4127_END __pragma(warning(pop))
#else
#define LUMA_MSVC_SUPPRESS_C4127_BEGIN
#define LUMA_MSVC_SUPPRESS_C4127_END
#endif

#define ASSERT_EQ(a, b)                                                                            \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        if ((a) != (b)) {                                                                          \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_EQ failed: " << luma::test::to_printable(a)                          \
                 << " != " << luma::test::to_printable(b);                                         \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_NE(a, b)                                                                            \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        if ((a) == (b)) {                                                                          \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_NE failed: " << luma::test::to_printable(a)                          \
                 << " == " << luma::test::to_printable(b);                                         \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_TRUE(cond)                                                                          \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_TRUE failed: " << #cond;                                             \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_FALSE(cond)                                                                         \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        if ((cond)) {                                                                              \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_FALSE failed: " << #cond;                                            \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_THROWS(expr)                                                                        \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        bool caught = false;                                                                       \
        try {                                                                                      \
            (void)(expr);                                                                          \
        } catch (...) {                                                                            \
            caught = true;                                                                         \
        }                                                                                          \
        if (!caught) {                                                                             \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_THROWS failed: no exception from: " << #expr;                        \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_THROWS_WITH_MESSAGE(expr, expected_substr)                                          \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        bool caught_ = false;                                                                      \
        std::string msg_;                                                                          \
        try {                                                                                      \
            (void)(expr);                                                                          \
        } catch (const std::exception& e_) {                                                       \
            caught_ = true;                                                                        \
            msg_ = e_.what();                                                                      \
        } catch (...) {                                                                            \
            caught_ = true;                                                                        \
        }                                                                                          \
        if (!caught_) {                                                                            \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_THROWS_WITH_MESSAGE failed: no exception from: " << #expr;           \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
        if (!msg_.empty() && std::string_view(expected_substr).size() > 0 &&                       \
            msg_.find(expected_substr) == std::string::npos) {                                     \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_THROWS_WITH_MESSAGE failed: exception message '" << msg_             \
                 << "' does not contain '" << (expected_substr) << "'";                            \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

// Assert that `expr` throws an exception that is (or derives from)
// `ExceptionType`.  Unlike ASSERT_THROWS, which accepts any exception, this
// verifies the precise exception class — essential where distinct error types
// (e.g. ChannelClosedError vs ChannelEmptyError vs ChannelFullError) must be
// distinguishable by callers.  The ExceptionType handler is listed first so a
// derived type is matched before the std::exception fallback.
#define ASSERT_THROWS_AS(expr, ExceptionType)                                                      \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        bool caught_expected_ = false;                                                             \
        bool caught_other_ = false;                                                                \
        std::string other_msg_;                                                                    \
        try {                                                                                      \
            (void)(expr);                                                                          \
        } catch (const ExceptionType&) {                                                           \
            caught_expected_ = true;                                                               \
        } catch (const std::exception& e_) {                                                       \
            caught_other_ = true;                                                                  \
            other_msg_ = e_.what();                                                                \
        } catch (...) {                                                                            \
            caught_other_ = true;                                                                  \
        }                                                                                          \
        if (!caught_expected_) {                                                                   \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_THROWS_AS failed: expected " << #ExceptionType                       \
                 << " from: " << #expr;                                                            \
            if (caught_other_) {                                                                   \
                oss_ << " (got a different exception";                                             \
                if (!other_msg_.empty()) {                                                         \
                    oss_ << ": " << other_msg_;                                                    \
                }                                                                                  \
                oss_ << ")";                                                                       \
            } else {                                                                               \
                oss_ << " (no exception thrown)";                                                  \
            }                                                                                      \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_LT(a, b)                                                                            \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        if (!((a) < (b))) {                                                                        \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_LT failed: " << luma::test::to_printable(a)                          \
                 << " >= " << luma::test::to_printable(b);                                         \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_LE(a, b)                                                                            \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        if (!((a) <= (b))) {                                                                       \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_LE failed: " << luma::test::to_printable(a) << " > "                 \
                 << luma::test::to_printable(b);                                                   \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_GT(a, b)                                                                            \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        if (!((a) > (b))) {                                                                        \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_GT failed: " << luma::test::to_printable(a)                          \
                 << " <= " << luma::test::to_printable(b);                                         \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_GE(a, b)                                                                            \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        if (!((a) >= (b))) {                                                                       \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__                              \
                 << ": ASSERT_GE failed: " << luma::test::to_printable(a) << " < "                 \
                 << luma::test::to_printable(b);                                                   \
            luma::test::throw_assertion_error(oss_.str());                                         \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

#define ASSERT_NEAR(a, b, epsilon)                                                                 \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        auto diff_ = (a) - (b);                                                                    \
        if (diff_ < 0)                                                                             \
            diff_ = -diff_;                                                                        \
        if (diff_ > (epsilon)) {                                                                   \
            std::ostringstream oss_;                                                               \
            oss_ << __FILE__ << ":" << __LINE__ << " in " << __func__ << ": ASSERT_NEAR failed: |" \
                 << luma::test::to_printable(a) << " - " << luma::test::to_printable(b) << "| > "  \
                 << luma::test::to_printable(epsilon);                                             \
            throw std::runtime_error{oss_.str()};                                                  \
        }                                                                                          \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

// ─── RAII helpers ───

// RAII guard that redirects std::cin to a custom buffer and restores the
// original on destruction — even if an exception is thrown.
class StdinRedirect {
public:
    explicit StdinRedirect(std::streambuf* buf) : orig_{std::cin.rdbuf(buf)} {}

    ~StdinRedirect() noexcept {
        std::cin.rdbuf(orig_);
    }

    StdinRedirect(const StdinRedirect&) = delete;
    StdinRedirect& operator=(const StdinRedirect&) = delete;
    StdinRedirect(StdinRedirect&&) = delete;
    StdinRedirect& operator=(StdinRedirect&&) = delete;

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

    ~CapturedStream() noexcept {
        stream_.rdbuf(orig_);
    }

    CapturedStream(const CapturedStream&) = delete;
    CapturedStream& operator=(const CapturedStream&) = delete;
    CapturedStream(CapturedStream&&) = delete;
    CapturedStream& operator=(CapturedStream&&) = delete;

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

    ~CapturedStreams() noexcept {
        first_.rdbuf(orig_first_);
        second_.rdbuf(orig_second_);
    }

    CapturedStreams(const CapturedStreams&) = delete;
    CapturedStreams& operator=(const CapturedStreams&) = delete;
    CapturedStreams(CapturedStreams&&) = delete;
    CapturedStreams& operator=(CapturedStreams&&) = delete;

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
// Keyed on _WIN32 (not _MSC_VER) so it uses the portable _putenv_s on every
// Windows toolchain — including MinGW/clang, where the POSIX setenv/unsetenv
// are not available.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const std::string& value) : name_{name} {
#ifdef _WIN32
        char* prev = nullptr;
        std::size_t len = 0;

        if (_dupenv_s(&prev, &len, name) == 0 && prev != nullptr) {
            had_prev_ = true;
            prev_ = prev;
            free(prev); // NOLINT — _dupenv_s allocates with malloc.
        }

        _putenv_s(name, value.c_str());
#else
        if (const char* prev = std::getenv(name)) { // NOLINT — read-only access.
            had_prev_ = true;
            prev_ = prev;
        }

        setenv(name, value.c_str(), 1);
#endif
    }

    ~ScopedEnv() noexcept {
#ifdef _WIN32
        // _putenv_s(name, "") removes the variable, which is the correct
        // restoration when it was previously unset.
        _putenv_s(name_.c_str(), had_prev_ ? prev_.c_str() : "");
#else
        if (had_prev_) {
            setenv(name_.c_str(), prev_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
#endif
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
    ScopedEnv(ScopedEnv&&) = delete;
    ScopedEnv& operator=(ScopedEnv&&) = delete;

private:
    std::string name_;
    std::string prev_;
    bool had_prev_{false};
};

// ─── Snapshot testing ───
//
// Compare a string against a stored .expected file.  When the
// environment variable UPDATE_SNAPSHOTS is set, the expected file is
// (re)written from the actual value, making the test pass and
// creating or updating the baseline.
//
// Usage:
//   ASSERT_SNAPSHOT("diagnostic_type_mismatch", actual_string, __FILE__);
//
// Snapshot files are stored in a "snapshots/" directory next to the
// test source file.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>

namespace snapshot {

// Read the entire contents of a file into a string.
// Returns an empty optional if the file does not exist.
[[nodiscard]] inline std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};

    if (!in.is_open()) {
        return std::nullopt;
    }

    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// Write content to a file, creating parent directories as needed.
inline void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());

    std::ofstream out{path, std::ios::binary | std::ios::trunc};

    if (!out.is_open()) {
        throw std::runtime_error{"snapshot: cannot open " + path.string() + " for writing"};
    }

    out << content;
}

// Returns true when the UPDATE_SNAPSHOTS environment variable is set.
[[nodiscard]] inline bool should_update() {
#ifdef _MSC_VER
    static const bool update = []() {
        char* val = nullptr;
        size_t len = 0;
        const bool set = (_dupenv_s(&val, &len, "UPDATE_SNAPSHOTS") == 0 && val != nullptr);
        free(val);
        return set;
    }();
    return update;
#else
    static const bool update = std::getenv("UPDATE_SNAPSHOTS") != nullptr;
    return update;
#endif
}

// Core snapshot assertion logic.
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
        throw std::runtime_error{oss.str()};
    }

    if (*expected != actual) {
        // Build a clear diff-style message showing first divergence.
        std::ostringstream oss;
        oss << file << ":" << line << ": ASSERT_SNAPSHOT failed for '" << name << "'\n"
            << "--- expected (from " << snapshot_path.string() << "):\n"
            << *expected << "\n+++ actual:\n"
            << actual;
        throw std::runtime_error{oss.str()};
    }
}

} // namespace snapshot

// Read an entire file into a string, throwing std::runtime_error if it cannot
// be opened. Reuses snapshot::read_file (the shared whole-file reader) so tests
// need not hand-roll an ifstream + rdbuf slurp. For optional presence (no throw
// when the file is absent) call snapshot::read_file directly.
[[nodiscard]] inline std::string read_file_text(const std::filesystem::path& path) {
    auto contents = snapshot::read_file(path);

    if (!contents.has_value()) {
        throw std::runtime_error{"read_file_text: cannot open " + path.string()};
    }

    return *contents;
}

// ─── Temporary filesystem RAII helpers ───

namespace detail {

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
[[nodiscard]] inline std::filesystem::path unique_temp_path(const std::string& prefix,
                                                            const std::string& extension) {
    static std::atomic<int> counter{0};

    return std::filesystem::temp_directory_path() /
           (prefix + process_token() + "_" + std::to_string(counter++) + extension);
}

} // namespace detail

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
        : TempFile{detail::unique_temp_path("luma_test_file_", ".luma"), content} {}

    TempFile(const std::filesystem::path& path, const std::string& content) : path_{path} {
        if (path_.has_parent_path()) {
            std::filesystem::create_directories(path_.parent_path());
        }

        // Binary mode so the file holds exactly the given bytes (no CRLF
        // translation), matching the binary snapshot::read_file / read_file_text
        // readers on every platform.
        std::ofstream out{path_, std::ios::binary};
        out << content;
    }

    ~TempFile() noexcept {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;

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
    TempDir() : path_{detail::unique_temp_path("luma_test_dir_", "")} {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() noexcept {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

    [[nodiscard]] std::string path_string() const {
        return path_.string();
    }

private:
    std::filesystem::path path_;
};

// Assert that `actual` matches the contents of
// snapshots/<name>.expected next to `test_file`.
#define ASSERT_SNAPSHOT(name, actual, test_file)                                                   \
    snapshot::assert_snapshot(test_file, name, actual, __FILE__, __LINE__)

// ─── Test fixture support ───
//
// Derive from TestFixture and override set_up() / tear_down() to share
// common setup across related tests.
//
// Usage:
//   class MyFixture : public TestFixture {
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
    virtual ~TestFixture() = default;

    virtual void set_up() {}

    virtual void tear_down() {}
};

// Define a fixture-based test. The test body receives `fixture` by reference.
#define TEST_F(FixtureClass, test_name)                                                            \
    static void test_name##_body(FixtureClass& fixture);                                           \
    static void test_name() {                                                                      \
        FixtureClass fixture;                                                                      \
        fixture.set_up();                                                                          \
        try {                                                                                      \
            test_name##_body(fixture);                                                             \
        } catch (...) {                                                                            \
            fixture.tear_down();                                                                   \
            throw;                                                                                 \
        }                                                                                          \
        fixture.tear_down();                                                                       \
    }                                                                                              \
    static void test_name##_body(FixtureClass& fixture)

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

namespace luma::test {

// Trait to detect tuple-like types (std::tuple, std::pair, etc.)
template <typename T, typename = void> struct is_tuple_like : std::false_type {};

template <typename T>
struct is_tuple_like<T, std::void_t<decltype(std::tuple_size<T>::value)>> : std::true_type {};

// Run `fn` for each element in `params`, reporting each as a separate test case.
// Elements that are tuple-like (pair, tuple) are unpacked into fn's arguments
// via std::apply; scalar elements are passed directly.
template <typename Container, typename Fn>
void run_parameterized(const char* name, const Container& params, Fn&& fn) noexcept {
    size_t index = 0;

    for (const auto& param : params) {
        const auto case_name = std::string{name} + "[" + std::to_string(index) + "]";

        run_test(case_name.c_str(), [&]() {
            if constexpr (is_tuple_like<std::decay_t<decltype(param)>>::value) {
                std::apply(fn, param);
            } else {
                fn(param);
            }
        });

        ++index;
    }
}

} // namespace luma::test

// ─── Convenience macros ───

// Register and run a test function: RUN(test_name);
#define RUN(fn) luma::test::run_test(#fn, fn)

// Run a parameterized test: RUN_PARAMETERIZED(fn, params_container);
// Use __VA_ARGS__ so template commas (e.g. std::pair<A, B>) are handled.
#define RUN_PARAMETERIZED(fn, ...) luma::test::run_parameterized(#fn, __VA_ARGS__, fn)

// Print summary and return exit code: return SUMMARY();
#define SUMMARY() luma::test::summary()

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
// same assertion counters and SUMMARY() output.

#include <utility>
#include <vector>

namespace luma::test {

// Thread-safe singleton that collects auto-registered tests.
struct TestRegistry {
    static TestRegistry& instance() {
        static TestRegistry registry;
        return registry;
    }

    std::vector<std::pair<const char*, void (*)()>> tests;

private:
    TestRegistry() = default;
};

// Static-initialiser helper.  Constructing one of these at file scope adds
// the test to the registry before main() runs.
struct TestRegistrar {
    TestRegistrar(const char* name, void (*fn)()) {
        TestRegistry::instance().tests.push_back({name, fn});
    }
};

} // namespace luma::test

// Define and auto-register a test function.
#define LUMA_TEST(name)                                                                            \
    static void luma_test_##name();                                                                \
    static luma::test::TestRegistrar luma_registrar_##name(#name, luma_test_##name);               \
    static void luma_test_##name()

// Run every auto-registered test, then print the summary and return the exit
// code.  Intended as the sole statement in main():  int main() { LUMA_RUN_ALL(); }
#define LUMA_RUN_ALL()                                                                             \
    for (const auto& [name_, fn_] : luma::test::TestRegistry::instance().tests) {                  \
        luma::test::run_test(name_, fn_);                                                          \
    }                                                                                              \
    return SUMMARY()

#endif // LUMA_TEST_FRAMEWORK_HPP
