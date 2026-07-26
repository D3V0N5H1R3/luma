#ifndef LUMA_STDLIB_FILESYSTEM_INTERNAL_HPP
#define LUMA_STDLIB_FILESYSTEM_INTERNAL_HPP

// Internal helper primitives shared by the filesystem_*.cpp registration
// units.  Not part of the public stdlib surface — include only from
// filesystem_*.cpp translation units.

#include <cerrno>
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

/// Wraps a FileSystem.FileKind variant name in a ChoiceValue.  The runtime short
/// name "FileKind" matches how DateTime.Weekday is built (make_weekday_choice);
/// the type checker resolves the qualified "FileSystem.FileKind" separately.  The
/// four variant names must match the ChoiceDeclaration in
/// core/analysis/types/stdlib_type_arities.cpp exactly.
[[nodiscard]] inline Value make_file_kind_choice(std::string_view variant) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "FileKind";
    cv->variant = std::string{variant};
    return Value{std::move(cv)};
}

/// Maps the three status booleans FileSystem.metadata already computes to a
/// single, mutually-exclusive FileKind variant, classified symlink-first (like
/// lstat): a symbolic link is reported as "Symlink" even when its target is a
/// directory or regular file.  Anything that is none of these (device, fifo,
/// socket, unknown) is "Other".  Shared by FileSystem.metadata and
/// FileSystem.kind so the two always agree.
[[nodiscard]] inline std::string_view file_kind_variant(bool is_symlink, bool is_directory,
                                                        bool is_regular_file) {
    if (is_symlink) {
        return "Symlink";
    }
    if (is_directory) {
        return "Directory";
    }
    if (is_regular_file) {
        return "File";
    }
    return "Other";
}

/// Classifies the file kind of `safe_path`, following the same symlink-first
/// precedence as FileSystem.metadata.  Returns std::nullopt when the path does
/// not exist (not even as a dangling symlink), which FileSystem.kind maps to a
/// result failure.
[[nodiscard]] inline std::optional<std::string_view>
classify_file_kind(const std::filesystem::path& safe_path) {
    std::error_code ec;

    // Query the link status first (does not follow symlinks) so a dangling or
    // symlinked entry is reported as Symlink rather than treated as absent.
    const auto sym_status = std::filesystem::symlink_status(safe_path, ec);
    if (ec || !std::filesystem::exists(sym_status)) {
        return std::nullopt;
    }

    const bool symlink = std::filesystem::is_symlink(sym_status);

    // Follow symlinks for the directory / regular-file classification so the
    // non-symlink kinds agree with is_directory / is_file.
    std::error_code status_ec;
    const auto status = std::filesystem::status(safe_path, status_ec);
    const bool directory = std::filesystem::is_directory(status);
    const bool regular_file = std::filesystem::is_regular_file(status);

    return file_kind_variant(symlink, directory, regular_file);
}

/// Builds a FileSystem.Permissions record from a std::filesystem::perms value.
/// The runtime short name "Permissions" matches how FileSystem.FileInfo is built
/// (the type checker resolves the qualified "FileSystem.Permissions" separately),
/// and the four field names must match the RecordDeclaration in
/// core/analysis/types/stdlib_type_arities.cpp exactly.  The three booleans read
/// the owner permission bits — the beginner-facing "what may I do with this
/// file?" answer — and mode carries the standard 12 POSIX mode bits (owner /
/// group / others plus set-uid / set-gid / sticky) as an integer.  On Windows,
/// std::filesystem synthesises perms from the read-only attribute, so the record
/// is cross-platform and never null.
[[nodiscard]] inline Value make_permissions_record(std::filesystem::perms perms) {
    namespace fs = std::filesystem;

    const bool readable = (perms & fs::perms::owner_read) != fs::perms::none;
    const bool writable = (perms & fs::perms::owner_write) != fs::perms::none;
    const bool executable = (perms & fs::perms::owner_exec) != fs::perms::none;
    const auto mode = static_cast<std::int64_t>(perms & fs::perms::mask);

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Permissions";
    rec->fields.emplace_back("readable", Value{readable});
    rec->fields.emplace_back("writable", Value{writable});
    rec->fields.emplace_back("executable", Value{executable});
    rec->fields.emplace_back("mode", Value{mode});
    return Value{std::move(rec)};
}

/// Wraps a FileSystem.IoError variant name in a ChoiceValue.  Runtime short name
/// "IoError" mirrors make_file_kind_choice (the type checker resolves the
/// qualified "FileSystem.IoError" separately).  The five variant names must match
/// the ChoiceDeclaration in core/analysis/types/stdlib_type_arities.cpp exactly.
[[nodiscard]] inline Value make_io_error_choice(std::string_view variant) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "IoError";
    cv->variant = std::string{variant};
    return Value{std::move(cv)};
}

/// Maps a std::error_code to the closest FileSystem.IoError variant.  Uses
/// error_condition equivalence (ec == std::errc::…) so a platform-specific
/// system error still matches its portable category.  Anything without a named
/// variant collapses to "Other".
[[nodiscard]] inline std::string_view io_error_variant(const std::error_code& ec) {
    if (ec == std::errc::no_such_file_or_directory) {
        return "NotFound";
    }
    if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted) {
        return "PermissionDenied";
    }
    if (ec == std::errc::file_exists) {
        return "AlreadyExists";
    }
    if (ec == std::errc::invalid_argument || ec == std::errc::is_a_directory) {
        return "InvalidInput";
    }
    return "Other";
}

/// Builds a result<T, FileSystem.IoError> failure carrying the classified choice
/// as its typed error value (rather than the default string message).
[[nodiscard]] inline Value make_io_error_failure(const std::error_code& ec) {
    return Value{ResultValue::failure(make_io_error_choice(io_error_variant(ec)))};
}

/// Reads a whole file into a result<string, FileSystem.IoError>, classifying any
/// failure into a typed IoError variant instead of a string message.  This is the
/// opt-in typed-error counterpart of read_file_within_limit; the path has already
/// been validated by the caller.  Existence and directory checks run first (a
/// reliable std::error_code source across platforms), then the open itself falls
/// back to errno for permission-style failures.
[[nodiscard]] inline Value read_file_typed_impl(const std::filesystem::path& safe_path) {
    std::error_code ec;
    const auto status = std::filesystem::status(safe_path, ec);

    if (ec) {
        return make_io_error_failure(ec);
    }

    if (!std::filesystem::exists(status)) {
        return make_io_error_failure(std::make_error_code(std::errc::no_such_file_or_directory));
    }

    if (std::filesystem::is_directory(status)) {
        return make_io_error_failure(std::make_error_code(std::errc::is_a_directory));
    }

    errno = 0;
    std::ifstream ifs{safe_path, std::ios::binary};

    if (!ifs) {
        const int captured = errno;
        const std::error_code open_ec = (captured != 0)
                                            ? std::error_code{captured, std::generic_category()}
                                            : std::make_error_code(std::errc::permission_denied);

        return make_io_error_failure(open_ec);
    }

    std::error_code size_ec;
    const auto file_bytes = std::filesystem::file_size(safe_path, size_ec);

    if (size_ec) {
        return make_io_error_failure(size_ec);
    }

    if (file_bytes > ResourceLimits::max_string_size) {
        return make_io_error_failure(std::make_error_code(std::errc::file_too_large));
    }

    std::string file_content{std::istreambuf_iterator<char>{ifs}, std::istreambuf_iterator<char>{}};

    return make_success_value(Value{std::move(file_content)});
}

} // namespace luma::filesystem_detail

#endif // LUMA_STDLIB_FILESYSTEM_INTERNAL_HPP
