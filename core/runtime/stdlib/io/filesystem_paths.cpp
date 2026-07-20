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
                [](const std::filesystem::path& p) { return p.extension().string(); });
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
                [](const std::filesystem::path& p) { return p.filename().string(); });
        })
        .func("parent", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(
                args, "FileSystem.parent", loc,
                [](const std::filesystem::path& p) { return p.parent_path().string(); });
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

                    std::filesystem::path result{args[0].as_string()};

                    for (const auto& arg : std::span{args}.subspan(1)) {
                        result /= arg.as_string();
                    }

                    // Validate the joined path to prevent sandbox escape.
                    const auto safe_path = validate_path(result.string(), loc);

                    return Value{safe_path.string()};
                })
        .func("stem", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(
                args, "FileSystem.stem", loc,
                [](const std::filesystem::path& p) { return p.stem().string(); });
        })
        .func("normalize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return pure_path_query(
                args, "FileSystem.normalize", loc,
                [](const std::filesystem::path& p) { return p.lexically_normal().string(); });
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

            const auto path = std::filesystem::path{args[0].as_string()};
            const auto base = std::filesystem::path{args[1].as_string()};

            return Value{path.lexically_relative(base).string()};
        })
        .func("absolute_path", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.absolute_path", loc);
            // Pure path operation — no filesystem access, so no path
            // validation against the working directory.
            return wrap_result_operation("FileSystem", "absolute_path", [&]() -> Value {
                return make_success_value(Value{std::filesystem::absolute(path_str).string()});
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
