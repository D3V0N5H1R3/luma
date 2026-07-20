// Unit tests for core/common/path_utils.hpp.

#include <filesystem>
#include <string>

#include "common/path_utils.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "test_framework.hpp"

using namespace luma;
namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════
// has_directory_traversal
// ═══════════════════════════════════════════════════════════

static void test_traversal_clean_relative_path() {
    ASSERT_FALSE(has_directory_traversal(fs::path("src/main.cpp")));
}

static void test_traversal_clean_filename() {
    ASSERT_FALSE(has_directory_traversal(fs::path("file.txt")));
}

static void test_traversal_dotdot_at_start() {
    ASSERT_TRUE(has_directory_traversal(fs::path("../etc/passwd")));
}

static void test_traversal_dotdot_in_middle() {
    ASSERT_TRUE(has_directory_traversal(fs::path("src/../secrets/key")));
}

static void test_traversal_dotdot_at_end() {
    ASSERT_TRUE(has_directory_traversal(fs::path("src/..")));
}

static void test_traversal_single_dot() {
    ASSERT_TRUE(has_directory_traversal(fs::path("./file.txt")));
}

static void test_traversal_multiple_dotdot() {
    ASSERT_TRUE(has_directory_traversal(fs::path("../../etc/passwd")));
}

static void test_traversal_empty_path() {
    ASSERT_FALSE(has_directory_traversal(fs::path("")));
}

static void test_traversal_dotdot_only() {
    ASSERT_TRUE(has_directory_traversal(fs::path("..")));
}

static void test_traversal_dot_only() {
    ASSERT_TRUE(has_directory_traversal(fs::path(".")));
}

static void test_traversal_dots_in_filename() {
    // "file..txt" and "..." are filenames, not traversal.
    ASSERT_FALSE(has_directory_traversal(fs::path("file..txt")));
    ASSERT_FALSE(has_directory_traversal(fs::path("src/file...dat")));
}

// ═══════════════════════════════════════════════════════════
// escapes_root
// ═══════════════════════════════════════════════════════════

static void test_escapes_root_inside() {
    const auto root = fs::current_path();
    const auto path = root / "subdir" / "file.txt";
    ASSERT_FALSE(escapes_root(path, root));
}

static void test_escapes_root_exact_root() {
    const auto root = fs::current_path();
    ASSERT_FALSE(escapes_root(root, root));
}

static void test_escapes_root_outside() {
    const auto root = fs::current_path() / "sandbox";
    const auto path = fs::current_path() / "other" / "file.txt";
    ASSERT_TRUE(escapes_root(path, root));
}

static void test_escapes_root_parent_traversal() {
    const auto root = fs::current_path();
    const auto path = root / ".." / "outside";
    ASSERT_TRUE(escapes_root(path, root));
}

// ═══════════════════════════════════════════════════════════
// is_symlink_or_contains_symlinks (basic smoke tests)
// ═══════════════════════════════════════════════════════════

static void test_contains_symlinks_regular_path() {
    // A path that does not exist should not contain symlinks.
    ASSERT_FALSE(luma::is_symlink_or_contains_symlinks(
        fs::path("nonexistent_dir_xyz/nonexistent_file_xyz.tmp")));
}

static void test_contains_symlinks_existing_regular_file() {
    // The test executable itself is a regular file, not a symlink.
    auto exe_path = fs::current_path() / "path_security_test";
#ifdef _WIN32
    exe_path = fs::current_path() / "path_security_test.exe";
#endif
    if (fs::exists(exe_path)) {
        ASSERT_FALSE(luma::is_symlink_or_contains_symlinks(exe_path));
    }
}

static void test_contains_symlinks_empty_path() {
    ASSERT_FALSE(luma::is_symlink_or_contains_symlinks(fs::path("")));
}

// ═══════════════════════════════════════════════════════════
// is_symlink (basic smoke test)
// ═══════════════════════════════════════════════════════════

static void test_is_symlink_regular_path() {
    // A path that does not exist should not be a symlink.
    ASSERT_FALSE(luma::is_symlink(fs::path("nonexistent_file_xyz_12345.tmp")));
}

// ═══════════════════════════════════════════════════════════
// validate_path — the FileSystem sandbox boundary
// (core/runtime/stdlib/common/path_validator.hpp)
//
// This is the hand-written security check that every FileSystem.* function
// runs against untrusted path strings.  It resolves the path against the
// current working directory and rejects anything that escapes it.  These
// tests pin down its accept/reject contract directly (the FileSystem feature
// tests exercise it end-to-end through the VM).
// ═══════════════════════════════════════════════════════════

static void test_validate_path_accepts_clean_relative() {
    const SourceLocation loc{};
    const auto resolved = validate_path("subdir/file.txt", loc);

    // The resolved path stays within the working directory.
    const auto cwd = fs::weakly_canonical(fs::current_path());
    ASSERT_FALSE(canonical_escapes_root(resolved, cwd));
}

static void test_validate_path_accepts_plain_filename() {
    const SourceLocation loc{};
    const auto resolved = validate_path("file.txt", loc);

    const auto cwd = fs::weakly_canonical(fs::current_path());
    ASSERT_EQ(resolved, cwd / "file.txt");
}

static void test_validate_path_accepts_internal_dotdot_that_stays_inside() {
    // "a/../b" normalises to "b", which is still inside the working directory,
    // so it must be accepted even though it contains a "..".
    const SourceLocation loc{};
    const auto resolved = validate_path("a/../b", loc);

    const auto cwd = fs::weakly_canonical(fs::current_path());
    ASSERT_FALSE(canonical_escapes_root(resolved, cwd));
    ASSERT_EQ(resolved, cwd / "b");
}

static void test_validate_path_rejects_parent_traversal() {
    const SourceLocation loc{};
    ASSERT_THROWS_AS(validate_path("../outside_xyz", loc), RuntimeError);
}

static void test_validate_path_rejects_deep_traversal() {
    const SourceLocation loc{};
    ASSERT_THROWS_AS(validate_path("../../etc/passwd", loc), RuntimeError);
}

static void test_validate_path_rejects_escaping_dotdot() {
    // "a/../../b" normalises to "../b", which escapes the working directory.
    const SourceLocation loc{};
    ASSERT_THROWS_AS(validate_path("a/../../b", loc), RuntimeError);
}

static void test_validate_path_rejects_absolute_outside() {
    const SourceLocation loc{};
    const auto outside = fs::current_path().parent_path() / "definitely_outside_xyz_123";
    ASSERT_THROWS_AS(validate_path(outside.string(), loc), RuntimeError);
}

static void test_validate_path_error_message_mentions_working_directory() {
    const SourceLocation loc{};
    ASSERT_THROWS_WITH_MESSAGE(validate_path("../escape", loc), "working directory");
}

// ─── main ───

int main() {
    // has_directory_traversal.
    RUN(test_traversal_clean_relative_path);
    RUN(test_traversal_clean_filename);
    RUN(test_traversal_dotdot_at_start);
    RUN(test_traversal_dotdot_in_middle);
    RUN(test_traversal_dotdot_at_end);
    RUN(test_traversal_single_dot);
    RUN(test_traversal_multiple_dotdot);
    RUN(test_traversal_empty_path);
    RUN(test_traversal_dotdot_only);
    RUN(test_traversal_dot_only);
    RUN(test_traversal_dots_in_filename);

    // escapes_root.
    RUN(test_escapes_root_inside);
    RUN(test_escapes_root_exact_root);
    RUN(test_escapes_root_outside);
    RUN(test_escapes_root_parent_traversal);

    // is_symlink_or_contains_symlinks.
    RUN(test_contains_symlinks_regular_path);
    RUN(test_contains_symlinks_existing_regular_file);
    RUN(test_contains_symlinks_empty_path);

    // is_symlink.
    RUN(test_is_symlink_regular_path);

    // validate_path — FileSystem sandbox boundary.
    RUN(test_validate_path_accepts_clean_relative);
    RUN(test_validate_path_accepts_plain_filename);
    RUN(test_validate_path_accepts_internal_dotdot_that_stays_inside);
    RUN(test_validate_path_rejects_parent_traversal);
    RUN(test_validate_path_rejects_deep_traversal);
    RUN(test_validate_path_rejects_escaping_dotdot);
    RUN(test_validate_path_rejects_absolute_outside);
    RUN(test_validate_path_error_message_mentions_working_directory);

    return SUMMARY();
}
