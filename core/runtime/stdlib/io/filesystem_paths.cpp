// FileSystem module — path queries and metadata (extension, name, parent,
// size, normalize, absolute/relative, home directory, and pure path
// functions).  Split from filesystem_module.cpp for readability.  Registered
// by register_filesystem_ns() via register_filesystem_paths().

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "analysis/source/source_location.hpp"
#include "common/path_utils.hpp"
#include "common/platform_utils.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "runtime/stdlib/common/stdlib_error_helpers.hpp"
#include "runtime/stdlib/io/filesystem_internal.hpp"
#include "runtime/stdlib/io/filesystem_module.hpp"

namespace luma {

using namespace filesystem_detail;

void register_filesystem_paths(const EnvPtr& env) {
    ModuleBuilder{"FileSystem", env}
        .func("extension", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(
                args, "FileSystem.extension", loc,
                [](const std::filesystem::path& p) { return path_to_utf8(p.extension()); });
        })
        .func("is_directory", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op("FileSystem.is_directory", args[0], loc, false,
                                          [](const std::filesystem::path& path) -> Value {
                                              return make_success_value(
                                                  Value{std::filesystem::is_directory(path)});
                                          });
        })
        .func("is_file", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op("FileSystem.is_file", args[0], loc, false,
                                          [](const std::filesystem::path& path) -> Value {
                                              return make_success_value(
                                                  Value{std::filesystem::is_regular_file(path)});
                                          });
        })
        .func("list_files", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op("FileSystem.list_files", args[0], loc, true,
                                          [](const std::filesystem::path& safe_path) -> Value {
                                              return collect_directory_entries(
                                                  safe_path, "list_files",
                                                  [](const std::filesystem::directory_entry& e) {
                                                      return e.is_regular_file();
                                                  });
                                          });
        })
        .func("name", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(
                args, "FileSystem.name", loc,
                [](const std::filesystem::path& p) { return path_to_utf8(p.filename()); });
        })
        .func("parent", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(
                args, "FileSystem.parent", loc,
                [](const std::filesystem::path& p) { return path_to_utf8(p.parent_path()); });
        })
        .func("size", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op(
                "FileSystem.size", args[0], loc, false,
                [](const std::filesystem::path& safe_path) -> Value {
                    const auto byte_count = std::filesystem::file_size(safe_path);
                    return make_success_value(Value{static_cast<std::int64_t>(byte_count)});
                });
        })
        // ─── Path Functions ───
        // FileSystem.join uses expect_min_args (variadic: 2+ string args).
        .native("join",
                [](std::span<const Value> args, SourceLocation loc) -> Value {
                    expect_min_args("FileSystem.join", args, 2, loc);
                    expect_all_strings(args, "FileSystem.join", loc, path_args_hint);

                    // Luma strings are UTF-8; build the path without the
                    // MSVC ANSI code-page decoding of std::string.
                    std::filesystem::path result = path_from_user_utf8(args[0].as_string(), loc);

                    for (const auto& arg : std::span{args}.subspan(1)) {
                        result /= path_from_user_utf8(arg.as_string(), loc);
                    }

                    // Validate the joined path to prevent sandbox escape.
                    const auto safe_path = validate_path(path_to_utf8(result), loc);

                    return Value{path_to_utf8(safe_path)};
                })
        .func("stem", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(
                args, "FileSystem.stem", loc,
                [](const std::filesystem::path& p) { return path_to_utf8(p.stem()); });
        })
        // FileSystem.split_path decomposes a path into its parts in one call.
        // Pure string manipulation (no I/O), so it needs no sandbox guard and
        // never fails — it mirrors FileSystem.parent/name/stem/extension bundled
        // into a single FileSystem.PathParts record.
        .func("split_path", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_all_strings(args, "FileSystem.split_path", loc, path_args_hint);

            const std::filesystem::path p = path_from_user_utf8(args[0].as_string(), loc);

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "PathParts";
            rec->fields.emplace_back("parent", Value{path_to_utf8(p.parent_path())});
            rec->fields.emplace_back("name", Value{path_to_utf8(p.filename())});
            rec->fields.emplace_back("stem", Value{path_to_utf8(p.stem())});
            rec->fields.emplace_back("extension", Value{path_to_utf8(p.extension())});

            return Value{std::move(rec)};
        })
        .func("normalize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(
                args, "FileSystem.normalize", loc,
                [](const std::filesystem::path& p) { return path_to_utf8(p.lexically_normal()); });
        })
        .func("is_absolute", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(args, "FileSystem.is_absolute", loc,
                                   [](const std::filesystem::path& p) { return p.is_absolute(); });
        })
        .func("is_relative", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(args, "FileSystem.is_relative", loc,
                                   [](const std::filesystem::path& p) { return p.is_relative(); });
        })
        .func("relative", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_all_strings(args, "FileSystem.relative", loc, path_args_hint);

            const auto path = path_from_user_utf8(args[0].as_string(), loc);
            const auto base = path_from_user_utf8(args[1].as_string(), loc);

            // Pure lexical operation — no filesystem access. lexically_relative
            // legitimately yields ".."-prefixed results (e.g. relative("a/b",
            // "a/c") == "../b"), and the result is relative to `base`, not the
            // working directory, so it must NOT be run through validate_path:
            // the sandbox is already enforced at every real I/O boundary
            // (read_file, write_file, ...).
            return Value{path.lexically_relative(base).string()};
        })
        .func("absolute_path", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.absolute_path", loc);
            return wrap_result_operation("FileSystem", "absolute_path", [&]() -> Value {
                const auto safe_path = validate_path(path_str, loc);
                return make_success_value(Value{path_to_utf8(safe_path)});
            });
        })
        .func("home_directory", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return wrap_result_operation("FileSystem", "home_directory", []() -> Value {
#ifdef _WIN32
                const auto home = safe_getenv("USERPROFILE");
#else
                    const auto home = safe_getenv("HOME");
#endif

                if (home && !home->empty()) {
                    return make_success_value(Value{*home});
                }

                return make_failure_value(
                    error_msg("FileSystem", "home_directory", "home directory not found"));
            });
        });
}

} // namespace luma
