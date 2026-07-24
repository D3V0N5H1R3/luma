// Standard library tests: FileSystem (and Sandbox gating).

#include <string>
#include <string_view>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "stdlib_test_helpers.hpp"

static void test_filesystem_absolute_path() {
    const auto ok = eval("FileSystem.absolute_path(\"some_file.txt\")");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->is_string());
    ASSERT_TRUE(ok.as_result()->owned_inner->as_string().size() >
                std::string_view{"some_file.txt"}.size());
}

static void test_filesystem_delete_directory_not_a_dir() {
    // Trying to delete a non-directory should fail.
    ASSERT_EVAL_FAILURE("FileSystem.delete_directory(\"nonexistent_dir_xyz\")");
}

static void test_filesystem_exists_returns_result() {
    // FileSystem.exists wraps its boolean in result<boolean>.
    ASSERT_EVAL_BOOL("FileSystem.exists(\".\")", true);
}

static void test_filesystem_is_absolute() {
    // Relative path should not be absolute.
    ASSERT_FALSE(eval("FileSystem.is_absolute(\"hello.txt\")").as_bool());
}

static void test_filesystem_rename_directory_not_a_dir() {
    // Trying to rename a non-directory should fail.
    ASSERT_EVAL_FAILURE("FileSystem.rename_directory(\"nonexistent_dir_xyz\", \"other\")");
}

static void test_filesystem_stem() {
    ASSERT_EQ(eval("FileSystem.stem(\"dir/hello.txt\")").as_string(), "hello");
}

static void test_filesystem_split_path() {
    // split_path returns a bare PathParts record (no result — it never fails).
    const auto v = eval("FileSystem.split_path(\"dir/hello.txt\")");

    ASSERT_TRUE(v.is_record());

    const auto& rec = *v.as_record();

    ASSERT_EQ(rec.type_name, "PathParts");
    ASSERT_EQ(rec.find_field("parent")->as_string(), "dir");
    ASSERT_EQ(rec.find_field("name")->as_string(), "hello.txt");
    ASSERT_EQ(rec.find_field("stem")->as_string(), "hello");
    ASSERT_EQ(rec.find_field("extension")->as_string(), ".txt");
}

static void test_filesystem_split_path_no_extension() {
    // A bare name has an empty parent and extension, and a stem equal to the name.
    const auto v = eval("FileSystem.split_path(\"README\")");

    ASSERT_TRUE(v.is_record());

    const auto& rec = *v.as_record();

    ASSERT_EQ(rec.find_field("parent")->as_string(), "");
    ASSERT_EQ(rec.find_field("name")->as_string(), "README");
    ASSERT_EQ(rec.find_field("stem")->as_string(), "README");
    ASSERT_EQ(rec.find_field("extension")->as_string(), "");
}

static void test_filesystem_append_file() {
    const LumaTempFile file{"_test_io_app.txt", "a"};
    eval(R"(FileSystem.append_file("_test_io_app.txt", "b"))");

    ASSERT_EVAL_STR("FileSystem.read_file(\"_test_io_app.txt\")", "ab");
}

static void test_filesystem_read_file_missing() {
    ASSERT_EVAL_FAILURE("FileSystem.read_file(\"_nonexistent_io_file.txt\")");
}

static void test_filesystem_write_read_file() {
    const LumaTempFile file{"_test_io_rw.txt", "hello"};

    ASSERT_EVAL_STR("FileSystem.read_file(\"_test_io_rw.txt\")", "hello");
}

static void test_filesystem_write_read_lines() {
    const LumaTempFile file{"_test_io_lines.txt"};
    eval(R"(FileSystem.write_lines("_test_io_lines.txt", ["one", "two"]))");

    const auto v = eval("FileSystem.read_lines(\"_test_io_lines.txt\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_array());
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 2U);
}

static void test_filesystem_metadata_file() {
    const LumaTempFile file{"_test_io_meta.txt", "hello"};

    const auto v = eval("FileSystem.metadata(\"_test_io_meta.txt\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = *v.as_result()->owned_inner->as_record();

    ASSERT_EQ(rec.find_field("size")->as_integer(), 5);
    ASSERT_TRUE(rec.find_field("is_file")->as_bool());
    ASSERT_FALSE(rec.find_field("is_directory")->as_bool());
    ASSERT_FALSE(rec.find_field("is_symlink")->as_bool());
    // The kind field mirrors the booleans as a single, match-able choice.
    ASSERT_TRUE(rec.find_field("kind")->is_choice());
    ASSERT_EQ(rec.find_field("kind")->as_choice()->type_name, "FileKind");
    ASSERT_EQ(rec.find_field("kind")->as_choice()->variant, "File");
    // Modified time is a Unix timestamp — after 2020-01-01.
    ASSERT_TRUE(rec.find_field("modified_time")->is_number());
    ASSERT_TRUE(rec.find_field("modified_time")->as_number() > 1577836800.0);
}

static void test_filesystem_metadata_directory() {
    const auto v = eval("FileSystem.metadata(\".\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = *v.as_result()->owned_inner->as_record();

    ASSERT_TRUE(rec.find_field("is_directory")->as_bool());
    ASSERT_FALSE(rec.find_field("is_file")->as_bool());
    // A directory reports size 0 rather than failing.
    ASSERT_EQ(rec.find_field("size")->as_integer(), 0);
    ASSERT_TRUE(rec.find_field("kind")->is_choice());
    ASSERT_EQ(rec.find_field("kind")->as_choice()->variant, "Directory");
}

static void test_filesystem_kind_file() {
    const LumaTempFile file{"_test_kind_file.txt", "hello"};

    const auto v = eval("FileSystem.kind(\"_test_kind_file.txt\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "FileKind");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "File");
}

static void test_filesystem_kind_directory() {
    const auto v = eval("FileSystem.kind(\".\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "FileKind");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "Directory");
}

static void test_filesystem_kind_nonexistent() {
    // A path that does not exist fails, mirroring FileSystem.metadata.
    ASSERT_EVAL_FAILURE("FileSystem.kind(\"_nonexistent_kind_file.txt\")");
}

static void test_filesystem_kind_rejects_traversal() {
    // Path traversal is a security violation, so kind throws (like every other
    // FileSystem function) rather than returning a failure result.
    ASSERT_THROWS(eval("FileSystem.kind(\"../escape.txt\")"));
}

// ─── FileSystem.read_file_typed — result<string, FileSystem.IoError> (N08) ──
// The opt-in typed-error read: failures surface a FileSystem.IoError choice as
// the result's error value rather than the default string message.  read_file
// itself is unchanged (still string-error).

static void test_filesystem_read_file_typed_success() {
    const LumaTempFile file{"_test_typed_read.txt", "typed hello"};

    const auto v = eval("FileSystem.read_file_typed(\"_test_typed_read.txt\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_string());
    ASSERT_EQ(v.as_result()->owned_inner->as_string(), "typed hello");
}

static void test_filesystem_read_file_typed_not_found() {
    const auto v = eval("FileSystem.read_file_typed(\"_nonexistent_typed_read.txt\")");

    ASSERT_RESULT_FAILURE(v);
    // The error value is the typed IoError choice, not a string message.
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "IoError");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "NotFound");
}

static void test_filesystem_read_file_typed_directory_is_invalid_input() {
    // Reading a directory as a file is a misuse of the API; the errno (EISDIR)
    // maps to the InvalidInput variant.
    const auto v = eval("FileSystem.read_file_typed(\".\")");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "IoError");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "InvalidInput");
}

static void test_filesystem_read_file_typed_traversal_is_invalid_input() {
    // Unlike the throwing string-error functions, read_file_typed maps a rejected
    // (traversing) path to a typed InvalidInput failure rather than throwing.
    const auto v = eval("FileSystem.read_file_typed(\"../escape.txt\")");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "IoError");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "InvalidInput");
}

static void test_filesystem_metadata_nonexistent() {
    ASSERT_EVAL_FAILURE("FileSystem.metadata(\"_nonexistent_meta_file.txt\")");
}

static void test_filesystem_metadata_rejects_traversal() {
    // Path traversal is a security violation, so metadata throws (like every
    // other FileSystem function) rather than returning a failure result.
    ASSERT_THROWS(eval("FileSystem.metadata(\"../escape.txt\")"));
}

static void test_filesystem_read_lines_rejects_oversized_file() {
    // read_lines caps the number of lines, but a single enormous newline-free
    // line would let std::getline allocate the whole file before that check
    // runs.  A file larger than the maximum string size must be rejected up
    // front, yielding a failure result rather than an unbounded allocation. The
    // file is created under the default cap, then the cap is lowered so the test
    // need not materialise a 256 MB file.
    const LumaTempFile file{"_test_read_lines_oversize.txt", std::string(64, 'x')};
    const LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(16)};

    const auto v = eval(R"(FileSystem.read_lines("_test_read_lines_oversize.txt"))");
    ASSERT_RESULT_FAILURE(v);
}

static void test_sandbox_disables_dangerous_modules() {
    const auto env = luma::test::make_std_env(true);

    // Modules disabled in sandbox mode.
    ASSERT_FALSE(env->has("FileSystem.read_file"));
    ASSERT_FALSE(env->has("FileSystem.write_file"));
    ASSERT_FALSE(env->has("FileSystem.exists"));
    ASSERT_FALSE(env->has("FileSystem.list_files"));
    ASSERT_FALSE(env->has("Process.run"));
    ASSERT_FALSE(env->has("Process.exit"));
    ASSERT_FALSE(env->has("Socket.connect"));
    ASSERT_FALSE(env->has("Socket.listen"));
    ASSERT_FALSE(env->has("Http.get"));
    ASSERT_FALSE(env->has("Http.post"));
    ASSERT_FALSE(env->has("Csv.read_file"));
    ASSERT_FALSE(env->has("Xml.deserialize_file"));
    ASSERT_FALSE(env->has("KeyValueStore.open"));
}

static void test_sandbox_gates_file_io_in_safe_modules() {
    const auto env = luma::test::make_std_env(true);

    // File-I/O functions inside otherwise-safe modules must be disabled.
    ASSERT_FALSE(env->has("Log.set_output"));
    ASSERT_FALSE(env->has("Compression.gzip_file"));
    ASSERT_FALSE(env->has("Compression.gunzip_file"));
    ASSERT_FALSE(env->has("Hash.sha256_file"));
    ASSERT_FALSE(env->has("Hash.sha512_file"));

    // Non-file functions in those modules remain available.
    ASSERT_TRUE(env->has("Log.info"));
    ASSERT_TRUE(env->has("Log.set_level"));
    ASSERT_TRUE(env->has("Compression.deflate"));
    ASSERT_TRUE(env->has("Compression.inflate"));
    ASSERT_TRUE(env->has("Hash.sha256"));
    ASSERT_TRUE(env->has("Hash.sha512"));
}

static void test_sandbox_keeps_safe_modules() {
    const auto env = luma::test::make_std_env(true);

    // Core builtins remain available.
    ASSERT_TRUE(env->has("print"));
    ASSERT_TRUE(env->has("assert"));
    ASSERT_TRUE(env->has("type_of"));

    // Safe modules remain available.
    ASSERT_TRUE(env->has("String.length"));
    ASSERT_TRUE(env->has("Array.map"));
    ASSERT_TRUE(env->has("Dictionary.get"));
    ASSERT_TRUE(env->has("Math.absolute"));
    ASSERT_TRUE(env->has("Result.unwrap"));
    ASSERT_TRUE(env->has("Converter.to_string"));
    ASSERT_TRUE(env->has("Json.serialize"));
    ASSERT_TRUE(env->has("Hash.sha256"));
    ASSERT_TRUE(env->has("Random.generate_integer"));
    ASSERT_TRUE(env->has("Channel.new"));
    ASSERT_TRUE(env->has("Terminal.clear_screen"));
}

static void test_sandbox_non_sandbox_has_file_io() {
    const auto env = luma::test::make_std_env(false);

    // Outside sandbox, file-I/O functions must be available.
    ASSERT_TRUE(env->has("Log.set_output"));
    ASSERT_TRUE(env->has("Compression.gzip_file"));
    ASSERT_TRUE(env->has("Compression.gunzip_file"));
    ASSERT_TRUE(env->has("Hash.sha256_file"));
    ASSERT_TRUE(env->has("Hash.sha512_file"));
}

static void test_sandbox_violation_all_blocked_modules() {
    const auto env = luma::test::make_std_env(true);

    const SourceLocation loc{.file_id = 0, .line = 1, .column = 1};

    // Every blocked module prefix should produce a sandbox-specific error.
    const std::vector<std::string> blocked_names{
        "Csv.read_file",      "FileSystem.exists", "Http.get",       "FileSystem.read_file",
        "KeyValueStore.open", "Process.run",       "Socket.connect", "Xml.deserialize_file"};

    for (const auto& name : blocked_names) {
        bool caught = false;

        try {
            (void)env->get(name, loc);
        } catch (const RuntimeError& e) {
            caught = true;

            const std::string msg = e.what();

            ASSERT_TRUE(msg.find("sandbox") != std::string::npos);
        }

        ASSERT_TRUE(caught);
    }
}

static void test_sandbox_violation_clear_message() {
    const auto env = luma::test::make_std_env(true);

    const SourceLocation loc{.file_id = 0, .line = 1, .column = 1};

    // Accessing a blocked module should produce a sandbox-specific error.
    bool caught = false;

    try {
        (void)env->get("FileSystem.exists", loc);
    } catch (const RuntimeError& e) {
        caught = true;

        const std::string msg = e.what();

        ASSERT_TRUE(msg.find("sandbox") != std::string::npos);
        ASSERT_TRUE(msg.find("--box") != std::string::npos);
    }

    ASSERT_TRUE(caught);

    // Accessing a truly undefined variable should still give generic error.
    bool caught_generic = false;

    try {
        (void)env->get("NoSuchModule.foo", loc);
    } catch (const RuntimeError& e) {
        caught_generic = true;

        const std::string msg = e.what();

        ASSERT_TRUE(msg.find("undefined variable") != std::string::npos);
    }

    ASSERT_TRUE(caught_generic);
}

static void test_filesystem_new_functions_registered() {
    const auto env = luma::test::make_std_env();
    ASSERT_TRUE(env->has("FileSystem.is_symlink"));
    ASSERT_TRUE(env->has("FileSystem.get_modified_time"));
    ASSERT_TRUE(env->has("FileSystem.list_recursively"));
}

static void test_filesystem_is_symlink_regular_file() {
    const LumaTempFile file{"_test_symlink_check.txt", "x"};

    ASSERT_EVAL_BOOL("FileSystem.is_symlink(\"_test_symlink_check.txt\")", false);
}

static void test_filesystem_get_modified_time() {
    const LumaTempFile file{"_test_mtime.txt", "hello"};

    const auto v = eval("FileSystem.get_modified_time(\"_test_mtime.txt\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_number());
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() > 0.0);
}

static void test_filesystem_get_modified_time_missing() {
    ASSERT_EVAL_FAILURE("FileSystem.get_modified_time(\"_nonexistent_mtime.txt\")");
}

static void test_filesystem_list_recursively() {
    eval(R"(FileSystem.create_directory("_test_recurse/sub"))");
    eval(R"(FileSystem.write_file("_test_recurse/a.txt", "a"))");
    eval(R"(FileSystem.write_file("_test_recurse/sub/b.txt", "b"))");

    const auto v = eval("FileSystem.list_recursively(\"_test_recurse\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_array());
    ASSERT_TRUE(v.as_result()->owned_inner->as_array()->elements->size() >= 2U);

    eval("FileSystem.delete(\"_test_recurse/a.txt\")");
    eval("FileSystem.delete(\"_test_recurse/sub/b.txt\")");
    eval("FileSystem.delete_directory(\"_test_recurse/sub\")");
    eval("FileSystem.delete_directory(\"_test_recurse\")");
}

static void test_filesystem_read_file_nonexistent() {
    ASSERT_EVAL_FAILURE("FileSystem.read_file(\"nonexistent_file_xyz_123.txt\")");
}

static void test_filesystem_write_file_invalid_path() {
    // Writing to a path inside a nonexistent directory should fail.
    ASSERT_EVAL_FAILURE(R"(FileSystem.write_file("nonexistent_dir_xyz/test.txt", "data"))");
}

static void test_filesystem_size_nonexistent() {
    ASSERT_EVAL_FAILURE("FileSystem.size(\"nonexistent_file_xyz_123.txt\")");
}

static void test_filesystem_list_files_nonexistent() {
    ASSERT_EVAL_FAILURE("FileSystem.list_files(\"nonexistent_dir_xyz\")");
}

static void test_filesystem_copy_nonexistent() {
    ASSERT_EVAL_FAILURE(R"(FileSystem.copy("nonexistent_file_xyz.txt", "dest.txt"))");
}

// ═══════════════════════════════════════════════════════════
// FileSystem — additional positive coverage (pure path ops,
// round-trips) and negative coverage (sandbox-escape rejection,
// argument validation).
// ═══════════════════════════════════════════════════════════

// ─── Pure path operations (no filesystem access) ───

static void test_filesystem_normalize_collapses_dotdot() {
    const auto v = eval(R"(FileSystem.normalize("a/b/../c"))");

    ASSERT_TRUE(v.is_string());

    const auto s = v.as_string();
    ASSERT_TRUE(s.find("..") == std::string::npos);
    ASSERT_TRUE(s.find('a') != std::string::npos);
    ASSERT_TRUE(s.find('c') != std::string::npos);
}

static void test_filesystem_join_combines_segments() {
    const auto v = eval(R"(FileSystem.join("src", "stdlib", "x.txt"))");

    ASSERT_TRUE(v.is_string());

    const auto s = v.as_string();
    ASSERT_TRUE(s.find("src") != std::string::npos);
    ASSERT_TRUE(s.find("stdlib") != std::string::npos);
    ASSERT_TRUE(s.find("x.txt") != std::string::npos);
}

static void test_filesystem_relative_strips_base() {
    ASSERT_EQ(eval(R"(FileSystem.relative("a/b/c", "a/b"))").as_string(), "c");
}

static void test_filesystem_name_returns_filename() {
    ASSERT_EQ(eval(R"(FileSystem.name("dir/sub/file.txt"))").as_string(), "file.txt");
}

static void test_filesystem_extension_multi_dot() {
    ASSERT_EQ(eval(R"(FileSystem.extension("archive.tar.gz"))").as_string(), ".gz");
}

static void test_filesystem_is_relative_true() {
    ASSERT_TRUE(eval(R"(FileSystem.is_relative("a/b/c"))").as_bool());
}

static void test_filesystem_home_directory_returns_path() {
    const auto v = eval("FileSystem.home_directory()");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_string());
    ASSERT_TRUE(!v.as_result()->owned_inner->as_string().empty());
}

// ─── Filesystem round-trip (create / copy / list / rename / size) ───

static void test_filesystem_create_copy_list_rename_roundtrip() {
    eval(R"(FileSystem.create_directory("_test_fs_round"))");
    eval(R"(FileSystem.write_file("_test_fs_round/a.txt", "hello"))");

    const auto copied = eval(R"(FileSystem.copy("_test_fs_round/a.txt", "_test_fs_round/b.txt"))");
    ASSERT_RESULT_SUCCESS(copied);

    ASSERT_TRUE(
        eval(R"(FileSystem.is_file("_test_fs_round/b.txt"))").as_result()->owned_inner->as_bool());
    ASSERT_TRUE(
        eval(R"(FileSystem.is_directory("_test_fs_round"))").as_result()->owned_inner->as_bool());

    const auto files = eval(R"(FileSystem.list_files("_test_fs_round"))");
    ASSERT_RESULT_SUCCESS(files);
    ASSERT_EQ(files.as_result()->owned_inner->as_array()->elements->size(), 2U);

    const auto renamed =
        eval(R"(FileSystem.rename("_test_fs_round/a.txt", "_test_fs_round/c.txt"))");
    ASSERT_RESULT_SUCCESS(renamed);
    ASSERT_FALSE(
        eval(R"(FileSystem.exists("_test_fs_round/a.txt"))").as_result()->owned_inner->as_bool());

    ASSERT_EVAL_INT(R"(FileSystem.size("_test_fs_round/c.txt"))", 5);

    eval(R"(FileSystem.delete("_test_fs_round/b.txt"))");
    eval(R"(FileSystem.delete("_test_fs_round/c.txt"))");
    eval(R"(FileSystem.delete_directory("_test_fs_round"))");
}

static void test_filesystem_read_file_limited_success() {
    const LumaTempFile file{"_test_rl_ok.txt", "hello"};

    ASSERT_EVAL_STR(R"(FileSystem.read_file_limited("_test_rl_ok.txt", 1048576))", "hello");
}

// ─── Sandbox-escape rejection (validate_path throws) ───

static void test_filesystem_read_file_rejects_traversal() {
    ASSERT_THROWS(eval(R"(FileSystem.read_file("../_outside_read.txt"))"));
}

static void test_filesystem_write_file_rejects_traversal() {
    ASSERT_THROWS(eval(R"(FileSystem.write_file("../_outside_write.txt", "x"))"));
}

static void test_filesystem_delete_rejects_traversal() {
    ASSERT_THROWS(eval(R"(FileSystem.delete("../_outside_delete.txt"))"));
}

static void test_filesystem_copy_rejects_traversal_destination() {
    const LumaTempFile file{"_test_copy_trav.txt", "x"};
    ASSERT_THROWS(eval(R"(FileSystem.copy("_test_copy_trav.txt", "../_outside_copy.txt"))"));
}

static void test_filesystem_list_files_rejects_traversal() {
    ASSERT_THROWS(eval(R"(FileSystem.list_files("../"))"));
}

static void test_filesystem_exists_rejects_absolute_outside() {
    ASSERT_THROWS(eval(R"(FileSystem.exists("/etc/passwd"))"));
}

static void test_filesystem_join_rejects_escape() {
    ASSERT_THROWS(eval(R"(FileSystem.join("a", "..", "..", "b"))"));
}

// ─── Argument validation ───

static void test_filesystem_join_requires_two_args() {
    ASSERT_THROWS(eval(R"(FileSystem.join("only_one"))"));
}

static void test_filesystem_exists_rejects_non_string_arg() {
    ASSERT_THROWS(eval("FileSystem.exists(123)"));
}

static void test_filesystem_write_lines_rejects_non_array() {
    ASSERT_THROWS(eval(R"(FileSystem.write_lines("_wl.txt", "notarray"))"));
}

static void test_filesystem_read_file_limited_rejects_negative_max_bytes() {
    const LumaTempFile file{"_test_rl_neg.txt", "data"};
    ASSERT_EVAL_FAILURE(R"(FileSystem.read_file_limited("_test_rl_neg.txt", -1))");
}

int main() {
    RUN(test_filesystem_absolute_path);
    RUN(test_filesystem_delete_directory_not_a_dir);
    RUN(test_filesystem_exists_returns_result);
    RUN(test_filesystem_is_absolute);
    RUN(test_filesystem_rename_directory_not_a_dir);
    RUN(test_filesystem_stem);
    RUN(test_filesystem_split_path);
    RUN(test_filesystem_split_path_no_extension);
    RUN(test_filesystem_new_functions_registered);
    RUN(test_filesystem_is_symlink_regular_file);
    RUN(test_filesystem_get_modified_time);
    RUN(test_filesystem_get_modified_time_missing);
    RUN(test_filesystem_list_recursively);
    RUN(test_filesystem_append_file);
    RUN(test_filesystem_read_file_missing);
    RUN(test_filesystem_write_read_file);
    RUN(test_filesystem_write_read_lines);
    RUN(test_filesystem_metadata_file);
    RUN(test_filesystem_metadata_directory);
    RUN(test_filesystem_metadata_nonexistent);
    RUN(test_filesystem_metadata_rejects_traversal);
    RUN(test_filesystem_kind_file);
    RUN(test_filesystem_kind_directory);
    RUN(test_filesystem_kind_nonexistent);
    RUN(test_filesystem_kind_rejects_traversal);
    RUN(test_filesystem_read_file_typed_success);
    RUN(test_filesystem_read_file_typed_not_found);
    RUN(test_filesystem_read_file_typed_directory_is_invalid_input);
    RUN(test_filesystem_read_file_typed_traversal_is_invalid_input);
    RUN(test_filesystem_read_lines_rejects_oversized_file);
    RUN(test_sandbox_disables_dangerous_modules);
    RUN(test_sandbox_gates_file_io_in_safe_modules);
    RUN(test_sandbox_keeps_safe_modules);
    RUN(test_sandbox_non_sandbox_has_file_io);
    RUN(test_sandbox_violation_all_blocked_modules);
    RUN(test_sandbox_violation_clear_message);
    RUN(test_filesystem_read_file_nonexistent);
    RUN(test_filesystem_write_file_invalid_path);
    RUN(test_filesystem_size_nonexistent);
    RUN(test_filesystem_list_files_nonexistent);
    RUN(test_filesystem_copy_nonexistent);
    RUN(test_filesystem_normalize_collapses_dotdot);
    RUN(test_filesystem_join_combines_segments);
    RUN(test_filesystem_relative_strips_base);
    RUN(test_filesystem_name_returns_filename);
    RUN(test_filesystem_extension_multi_dot);
    RUN(test_filesystem_is_relative_true);
    RUN(test_filesystem_home_directory_returns_path);
    RUN(test_filesystem_create_copy_list_rename_roundtrip);
    RUN(test_filesystem_read_file_limited_success);
    RUN(test_filesystem_read_file_rejects_traversal);
    RUN(test_filesystem_write_file_rejects_traversal);
    RUN(test_filesystem_delete_rejects_traversal);
    RUN(test_filesystem_copy_rejects_traversal_destination);
    RUN(test_filesystem_list_files_rejects_traversal);
    RUN(test_filesystem_exists_rejects_absolute_outside);
    RUN(test_filesystem_join_rejects_escape);
    RUN(test_filesystem_join_requires_two_args);
    RUN(test_filesystem_exists_rejects_non_string_arg);
    RUN(test_filesystem_write_lines_rejects_non_array);
    RUN(test_filesystem_read_file_limited_rejects_negative_max_bytes);

    return SUMMARY();
}
