// FileSystem module — existence checks and entry mutation (create, rename,
// delete, copy for files and directories).  Split from filesystem_module.cpp
// for readability.  Registered by register_filesystem_ns() via
// register_filesystem_operations().

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

void register_filesystem_operations(const EnvPtr& env) {
    ModuleBuilder{"FileSystem", env}
        .func("exists", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op("FileSystem.exists", args[0], loc, false,
                                          [](const std::filesystem::path& path) -> Value {
                                              return make_success_value(
                                                  Value{std::filesystem::exists(path)});
                                          });
        })
        .func("list_directories", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op(
                "FileSystem.list_directories", args[0], loc, true,
                [](const std::filesystem::path& safe_path) -> Value {
                    return collect_directory_entries(
                        safe_path, "list_directories",
                        [](const std::filesystem::directory_entry& e) { return e.is_directory(); });
                });
        })
        .func("create_directory", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op("FileSystem.create_directory", args[0], loc, false,
                                          [](const std::filesystem::path& safe_path) -> Value {
                                              std::filesystem::create_directories(safe_path);
                                              return make_success_value(Value{true});
                                          });
        })
        .func("rename", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_all_strings(args, "FileSystem.rename", loc, path_args_hint);

            const auto dst = validate_path(args[1].as_string(), loc);

            return fs_safe_execute(args[0].as_string(), "FileSystem.rename", loc, false,
                                   [&dst](const std::filesystem::path& src) -> Value {
                                       std::filesystem::rename(src, dst);
                                       return make_success_value(Value{true});
                                   });
        })
        .func("delete", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op("FileSystem.delete", args[0], loc, true,
                                          [](const std::filesystem::path& path) -> Value {
                                              std::filesystem::remove(path);
                                              return make_success_value(Value{true});
                                          });
        })
        .func("delete_directory", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op(
                "FileSystem.delete_directory", args[0], loc, true,
                [](const std::filesystem::path& path) -> Value {
                    if (!std::filesystem::is_directory(path)) {
                        return make_failure_value(
                            error_msg("FileSystem", "delete_directory",
                                      std::format("path is not a directory '{}'", path.string())));
                    }

                    std::filesystem::remove_all(path);
                    return make_success_value(Value{true});
                });
        })
        .func("rename_directory", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_all_strings(args, "FileSystem.rename_directory", loc, path_args_hint);

            const auto dst = validate_path(args[1].as_string(), loc);

            return fs_safe_execute(
                args[0].as_string(), "FileSystem.rename_directory", loc, false,
                [&dst](const std::filesystem::path& src) -> Value {
                    if (!std::filesystem::is_directory(src)) {
                        return make_failure_value(
                            error_msg("FileSystem", "rename_directory",
                                      std::format("path is not a directory '{}'", src.string())));
                    }

                    std::filesystem::rename(src, dst);
                    return make_success_value(Value{true});
                });
        })
        .func("copy", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_all_strings(args, "FileSystem.copy", loc, path_args_hint);

            const auto dst = validate_path(args[1].as_string(), loc);

            return fs_safe_execute(args[0].as_string(), "FileSystem.copy", loc, true,
                                   [&dst](const std::filesystem::path& src) -> Value {
                                       std::filesystem::copy(
                                           src, dst,
                                           std::filesystem::copy_options::overwrite_existing);
                                       return make_success_value(Value{true});
                                   });
        })
        .func("copy_directory", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_all_strings(args, "FileSystem.copy_directory", loc, path_args_hint);

            const auto dst = validate_path(args[1].as_string(), loc);

            return fs_safe_execute(
                args[0].as_string(), "FileSystem.copy_directory", loc, true,
                [&dst](const std::filesystem::path& src) -> Value {
                    if (!std::filesystem::is_directory(src)) {
                        return make_failure_value(
                            error_msg("FileSystem", "copy_directory",
                                      std::format("path is not a directory '{}'", src.string())));
                    }

                    // Documented contract: fail rather than merge/overwrite when
                    // the destination already exists.
                    if (std::filesystem::exists(dst)) {
                        return make_failure_value(error_msg(
                            "FileSystem", "copy_directory",
                            std::format("destination already exists '{}'", dst.string())));
                    }

                    // Recurse into subdirectories but skip symbolic links (a
                    // symlink could point outside the sandbox), mirroring the
                    // symlink rejection of FileSystem.copy.
                    std::filesystem::copy(src, dst,
                                          std::filesystem::copy_options::recursive |
                                              std::filesystem::copy_options::skip_symlinks);
                    return make_success_value(Value{true});
                });
        })
        .func("create_directories", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            // mkdir -p: create the full path including any missing parents; a
            // path that already exists is a no-op success (create_directories
            // returns false without setting an error in that case).
            return fs_validated_string_op(
                "FileSystem.create_directories", args[0], loc, false,
                [](const std::filesystem::path& safe_path) -> Value {
                    std::error_code ec;
                    std::filesystem::create_directories(safe_path, ec);

                    if (ec && !std::filesystem::is_directory(safe_path)) {
                        return make_failure_value(
                            error_msg("FileSystem", "create_directories",
                                      std::format("cannot create directory '{}': {}",
                                                  safe_path.string(), ec.message())));
                    }

                    return make_success_value(Value{true});
                });
        });
}

} // namespace luma
