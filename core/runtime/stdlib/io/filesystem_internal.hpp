#ifndef LUMA_STDLIB_FILESYSTEM_INTERNAL_HPP
#define LUMA_STDLIB_FILESYSTEM_INTERNAL_HPP

// Internal helper primitives shared by the filesystem_*.cpp registration
// units.  Not part of the public stdlib surface — include only from
// filesystem_*.cpp translation units.

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
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "runtime/stdlib/common/stdlib_error_helpers.hpp"

namespace luma::filesystem_detail {

// Remediation hint attached to the type error every FileSystem function raises
// when a path argument is not a string.  Centralised so the wording stays
// consistent across the module.
inline constexpr std::string_view path_args_hint = "pass file paths as strings";

/// Checks if a path is a symbolic link and returns a failure value if so.
/// Returns std::nullopt if the path is not a symlink (safe to proceed).
[[nodiscard]] inline std::optional<Value> reject_if_symlink(const std::filesystem::path& path,
                                                            std::string_view function_name) {
    std::error_code ec;
    if (std::filesystem::is_symlink(path, ec)) {
        return make_failure_value(
            std::format("{}: cannot operate on symbolic link '{}'", function_name, path.string()));
    }
    return std::nullopt;
}

/// Executes a filesystem operation with standard error handling.
/// The operation lambda receives the validated path and returns a Value.
/// Filesystem exceptions are caught and wrapped in failure results.
template <typename Operation>
[[nodiscard]] Value fs_safe_execute(const std::string& path_str, std::string_view function_name,
                                    const SourceLocation& loc, bool reject_symlinks,
                                    Operation&& operation) {
    const auto safe_path = validate_path(path_str, loc);
    if (reject_symlinks) {
        // validate_path() returns a weakly_canonical path with symlinks already
        // resolved, so checking *it* for symlink-ness would always pass.  Check
        // the user-supplied path before canonicalisation so a symlinked final
        // component is actually detected (defense-in-depth).
        if (auto err = reject_if_symlink(std::filesystem::path{path_str}, function_name)) {
            return *err;
        }
    }
    try {
        return std::forward<Operation>(operation)(safe_path);
    } catch (const RuntimeError& e) {
        return make_failure_value(std::format("{}: {}", function_name, e.what()));
    } catch (const std::exception& e) {
        return make_failure_value(std::format("{}: {}", function_name, e.what()));
    }
}

/// Convenience wrapper for the common single-string-arg pattern:
///   expect_string_args → fs_safe_execute(args[0], ...).
/// Reduces three-line boilerplate to a single call for functions that
/// take one path argument, validate it, and run an operation on it.
template <typename Operation>
[[nodiscard]] Value fs_validated_string_op(std::string_view func_name, const Value& path_arg,
                                           const SourceLocation& loc, bool reject_symlinks,
                                           Operation&& op) {
    expect_all_strings(std::span{&path_arg, 1}, func_name, loc, path_args_hint);
    return fs_safe_execute(path_arg.as_string(), func_name, loc, reject_symlinks,
                           std::forward<Operation>(op));
}

/// Collects the names of directory entries matching `keep` into a
/// result<array<string>>.  Symlinked entries are always skipped (a symlink can
/// point outside the sandbox), and the interpreter's max array size is enforced.
/// Shared by FileSystem.list_directories and FileSystem.list_files, which differ
/// only in the entry predicate and the qualifying function name.
template <typename EntryPredicate>
[[nodiscard]] Value collect_directory_entries(const std::filesystem::path& safe_path,
                                              std::string_view function_name, EntryPredicate keep) {
    auto arr = std::make_shared<ArrayValue>();

    for (const auto& entry : std::filesystem::directory_iterator(safe_path)) {
        if (!keep(entry) || entry.is_symlink()) {
            continue;
        }

        if (arr->elements->size() >= ResourceLimits::max_array_size) {
            return make_failure_value(
                ErrorMessages::result_exceeds_maximum_size("FileSystem", function_name));
        }

        arr->elements->emplace_back(entry.path().filename().string());
    }

    return make_success_value(Value{std::move(arr)});
}

/// Reads a whole file into a result<string>, failing if it cannot be opened or
/// exceeds `max_bytes`.  Shared by FileSystem.read_file (bounded by the
/// interpreter's max string size) and FileSystem.read_file_limited (bounded by
/// the caller's limit); `oversize_detail` names the exceeded bound in the
/// failure message.
[[nodiscard]] inline Value read_file_within_limit(const std::filesystem::path& safe_path,
                                                  std::string_view function_name,
                                                  std::uintmax_t max_bytes,
                                                  std::string_view oversize_detail) {
    std::ifstream ifs{safe_path};

    if (!ifs) {
        return make_failure_value(error_msg(
            "FileSystem", function_name, std::format("cannot read file '{}'", safe_path.string())));
    }

    if (std::filesystem::file_size(safe_path) > max_bytes) {
        return make_failure_value(
            error_msg("FileSystem", function_name,
                      std::format("{} '{}'", oversize_detail, safe_path.string())));
    }

    std::string file_content{std::istreambuf_iterator<char>{ifs}, std::istreambuf_iterator<char>{}};

    return make_success_value(Value{std::move(file_content)});
}

/// Opens `safe_path` with `mode`, streams content through `write_body`, and
/// reports the outcome as a result<boolean>.  `open_error_detail` and
/// `write_error_detail` name the failing step in the message.  Shared by
/// FileSystem.write_file, append_file, and write_lines, which differ only in the
/// open mode, the message wording, and how they stream their content.
template <typename WriteBody>
[[nodiscard]] Value write_to_file(const std::filesystem::path& safe_path,
                                  std::string_view function_name, std::ios_base::openmode mode,
                                  std::string_view open_error_detail,
                                  std::string_view write_error_detail, WriteBody write_body) {
    std::ofstream ofs{safe_path, mode};

    if (!ofs) {
        return make_failure_value(
            error_msg("FileSystem", function_name,
                      std::format("{} '{}'", open_error_detail, safe_path.string())));
    }

    write_body(ofs);

    if (!ofs) {
        return make_failure_value(
            error_msg("FileSystem", function_name,
                      std::format("{} '{}'", write_error_detail, safe_path.string())));
    }

    return make_success_value(Value{true});
}

/// Applies a pure (filesystem-free) path transform to a single string argument
/// and returns the projected value directly — these operations cannot fail, so
/// they are not wrapped in result<T>.  `project` receives the parsed path and
/// returns any type constructible into a Value (e.g. a std::string component or
/// a bool predicate).
template <typename Project>
[[nodiscard]] Value pure_path_query(std::span<const Value> args, std::string_view function_name,
                                    const SourceLocation& loc, Project project) {
    expect_all_strings(args, function_name, loc, path_args_hint);
    return Value{project(std::filesystem::path{args[0].as_string()})};
}

} // namespace luma::filesystem_detail

#endif // LUMA_STDLIB_FILESYSTEM_INTERNAL_HPP
