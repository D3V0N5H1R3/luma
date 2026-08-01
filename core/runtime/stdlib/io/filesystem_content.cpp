// FileSystem module — file content I/O (read/write/append, line-based I/O,
// symlink and modified-time queries, recursive listing).  Split from
// filesystem_module.cpp for readability.  Registered by
// register_filesystem_ns() via register_filesystem_content().

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
#include "common/file_time.hpp"
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

void register_filesystem_content(const EnvPtr& env) {
    ModuleBuilder{"FileSystem", env} // ─── File Content I/O ───
        .func("read_file", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.read_file", loc);

            return fs_safe_execute(path_str, "FileSystem.read_file", loc, false,
                                   [](const std::filesystem::path& safe_path) -> Value {
                                       return read_file_within_limit(
                                           safe_path, "read_file", ResourceLimits::max_string_size,
                                           "file exceeds maximum readable size");
                                   });
        })
        .func("read_file_typed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.read_file_typed", loc);

            // Opt-in typed-error variant of read_file: failures are surfaced as a
            // FileSystem.IoError choice (result<string, FileSystem.IoError>)
            // instead of a string message.  Path validation is done here rather
            // than via fs_safe_execute so a rejected path maps to a typed
            // InvalidInput failure rather than the shared string-failure wrapper.
            try {
                const auto safe_path = validate_path(path_str, loc);

                return read_file_typed_impl(safe_path);
            } catch (const RuntimeError&) {
                return make_io_error_failure(std::make_error_code(std::errc::invalid_argument));
            }
        })
        .func("read_file_limited", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str =
                expect_string(args[0], "FileSystem.read_file_limited", loc);

            const auto max_bytes = expect_integer(args[1], "FileSystem.read_file_limited", loc);

            if (max_bytes < 0) {
                return make_failure_value(
                    error_msg("FileSystem", "read_file_limited", "max_bytes must be non-negative"));
            }

            const auto limit = static_cast<std::size_t>(max_bytes);

            if (limit > ResourceLimits::max_string_size) {
                return make_failure_value(error_msg("FileSystem", "read_file_limited",
                                                    "max_bytes exceeds interpreter limit"));
            }

            return fs_safe_execute(path_str, "FileSystem.read_file_limited", loc, false,
                                   [limit](const std::filesystem::path& safe_path) -> Value {
                                       return read_file_within_limit(
                                           safe_path, "read_file_limited", limit,
                                           "file exceeds max_bytes limit");
                                   });
        })
        .func("write_file", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.write_file", loc);
            const std::string& content = expect_string(args[1], "FileSystem.write_file", loc);

            return fs_safe_execute(path_str, "FileSystem.write_file", loc, false,
                                   [&content](const std::filesystem::path& safe_path) -> Value {
                                       return write_to_file(
                                           safe_path, "write_file", std::ios::out,
                                           "cannot write file", "write failed",
                                           [&content](std::ofstream& ofs) { ofs << content; });
                                   });
        })
        .func("append_file", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.append_file", loc);
            const std::string& content = expect_string(args[1], "FileSystem.append_file", loc);

            return fs_safe_execute(path_str, "FileSystem.append_file", loc, false,
                                   [&content](const std::filesystem::path& safe_path) -> Value {
                                       return write_to_file(
                                           safe_path, "append_file", std::ios::app,
                                           "cannot append to file", "append failed",
                                           [&content](std::ofstream& ofs) { ofs << content; });
                                   });
        })
        .func("read_bytes", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.read_bytes", loc);

            return fs_safe_execute(path_str, "FileSystem.read_bytes", loc, false,
                                   [](const std::filesystem::path& safe_path) -> Value {
                                       return read_bytes_impl(safe_path, "read_bytes");
                                   });
        })
        .func("write_bytes", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.write_bytes", loc);
            const auto& bytes = expect_array(args[1], "FileSystem.write_bytes", loc);

            // Validate and pack every byte before touching the disk so an
            // out-of-range or non-integer element fails without creating a
            // partial file.  Each element must be an integer in 0–255 (the same
            // byte convention as String.to_bytes/from_bytes and Encoder).
            std::string buffer{};
            buffer.reserve(bytes->elements->size());

            for (const auto& elem : *bytes->elements) {
                if (!elem.is_integer()) {
                    return make_failure_value(
                        error_msg("FileSystem", "write_bytes",
                                  "every element must be an integer byte (0-255)"));
                }

                const auto byte = elem.as_integer();

                if (byte < 0 || byte > 255) {
                    return make_failure_value(
                        error_msg("FileSystem", "write_bytes",
                                  std::format("byte value out of range (0-255): {}", byte)));
                }

                buffer += static_cast<char>(static_cast<std::uint8_t>(byte));
            }

            return fs_safe_execute(
                path_str, "FileSystem.write_bytes", loc, false,
                [&buffer](const std::filesystem::path& safe_path) -> Value {
                    return write_to_file(
                        safe_path, "write_bytes", std::ios::out | std::ios::binary,
                        "cannot write file", "write failed", [&buffer](std::ofstream& ofs) {
                            ofs.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                        });
                });
        })
        .func("read_lines", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.read_lines", loc);

            return fs_safe_execute(
                path_str, "FileSystem.read_lines", loc, false,
                [](const std::filesystem::path& safe_path) -> Value {
                    // The per-line loop below caps the number of lines, but a
                    // single enormous newline-free line would let std::getline
                    // allocate the whole file before that check ever runs.  Bound
                    // total bytes up front — the same guard FileSystem.read_file
                    // and Hash.*_file use — before opening the stream, failing
                    // closed when the size cannot be determined (e.g. a FIFO or
                    // device node).
                    std::error_code size_ec;
                    const auto file_bytes = std::filesystem::file_size(safe_path, size_ec);
                    if (size_ec) {
                        return make_failure_value(
                            error_msg("FileSystem", "read_lines",
                                      std::format("cannot determine the size of '{}': {}",
                                                  safe_path.string(), size_ec.message())));
                    }
                    if (file_bytes > ResourceLimits::max_string_size) {
                        return make_failure_value(error_msg(
                            "FileSystem", "read_lines",
                            std::format("file '{}' exceeds the maximum size of {} bytes",
                                        safe_path.string(), ResourceLimits::max_string_size)));
                    }

                    std::ifstream ifs{safe_path};

                    if (!ifs) {
                        return make_failure_value(
                            error_msg("FileSystem", "read_lines",
                                      std::format("cannot read file '{}'", safe_path.string())));
                    }

                    auto arr = std::make_shared<ArrayValue>();

                    std::string line{};

                    while (std::getline(ifs, line)) {
                        if (arr->elements->size() >= ResourceLimits::max_array_size) {
                            return make_failure_value(
                                "FileSystem.read_lines: file has too many lines");
                        }

                        arr->elements->emplace_back(line);
                    }

                    if (ifs.fail() && !ifs.eof()) {
                        return make_failure_value(
                            error_msg("FileSystem", "read_lines",
                                      std::format("error reading file '{}'", safe_path.string())));
                    }

                    return make_success_value(Value{std::move(arr)});
                });
        })
        .func("write_lines", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& path_str = expect_string(args[0], "FileSystem.write_lines", loc);

            const auto& lines = expect_array(args[1], "FileSystem.write_lines", loc);

            return fs_safe_execute(path_str, "FileSystem.write_lines", loc, false,
                                   [&lines](const std::filesystem::path& safe_path) -> Value {
                                       return write_to_file(safe_path, "write_lines", std::ios::out,
                                                            "cannot write file", "write failed",
                                                            [&lines](std::ofstream& ofs) {
                                                                for (const auto& elem :
                                                                     *lines->elements) {
                                                                    ofs << elem.to_string() << "\n";
                                                                }
                                                            });
                                   });
        })
        .func("is_symbolic_link", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op("FileSystem.is_symbolic_link", args[0], loc, false,
                                          [](const std::filesystem::path& safe_path) -> Value {
                                              return make_success_value(
                                                  Value{std::filesystem::is_symlink(safe_path)});
                                          });
        })
        .func("get_modified_time", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op(
                "FileSystem.get_modified_time", args[0], loc, false,
                [](const std::filesystem::path& safe_path) -> Value {
                    const auto ftime = std::filesystem::last_write_time(safe_path);
                    const auto sctp = file_time_to_system_clock(ftime);
                    const auto epoch = sctp.time_since_epoch();
                    const auto secs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();

                    return make_success_value(Value{static_cast<double>(secs) / 1000.0});
                });
        })
        .func("metadata", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op(
                "FileSystem.metadata", args[0], loc, false,
                [](const std::filesystem::path& safe_path) -> Value {
                    std::error_code ec;

                    // Query the link status first (does not follow symlinks) so a
                    // dangling or symlinked entry is still reported rather than
                    // treated as absent.
                    const auto sym_status = std::filesystem::symlink_status(safe_path, ec);
                    if (ec || !std::filesystem::exists(sym_status)) {
                        return make_failure_value(
                            error_msg("FileSystem", "metadata",
                                      std::format("path does not exist '{}'", safe_path.string())));
                    }

                    const bool symlink = std::filesystem::is_symlink(sym_status);

                    // Follow symlinks for the type/size/time queries so the fields
                    // agree with size / is_directory / is_file / get_modified_time.
                    std::error_code status_ec;
                    const auto status = std::filesystem::status(safe_path, status_ec);
                    const bool directory = std::filesystem::is_directory(status);
                    const bool regular_file = std::filesystem::is_regular_file(status);

                    // file_size is only meaningful for regular files; report 0 for
                    // directories and other kinds rather than failing the call.
                    std::int64_t byte_count = 0;
                    if (regular_file) {
                        std::error_code size_ec;
                        const auto sz = std::filesystem::file_size(safe_path, size_ec);
                        if (!size_ec) {
                            byte_count = static_cast<std::int64_t>(sz);
                        }
                    }

                    // Modified time as fractional seconds since the Unix epoch,
                    // matching FileSystem.get_modified_time.
                    double modified_seconds = 0.0;
                    std::error_code time_ec;
                    const auto ftime = std::filesystem::last_write_time(safe_path, time_ec);
                    if (!time_ec) {
                        const auto sctp = file_time_to_system_clock(ftime);
                        const auto epoch = sctp.time_since_epoch();
                        const auto ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
                        modified_seconds = static_cast<double>(ms) / 1000.0;
                    }

                    auto rec = std::make_shared<RecordValue>();
                    rec->type_name = "FileInfo";
                    rec->fields.emplace_back("size", Value{byte_count});
                    rec->fields.emplace_back("modified_time", Value{modified_seconds});
                    rec->fields.emplace_back("is_directory", Value{directory});
                    rec->fields.emplace_back("is_file", Value{regular_file});
                    rec->fields.emplace_back("is_symbolic_link", Value{symlink});
                    const auto kind = file_kind_variant(symlink, directory, regular_file);
                    rec->fields.emplace_back("kind", make_file_kind_choice(kind));

                    return make_success_value(Value{std::move(rec)});
                });
        })
        .func("permissions", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op(
                "FileSystem.permissions", args[0], loc, false,
                [](const std::filesystem::path& safe_path) -> Value {
                    std::error_code ec;

                    // Query the link status first (does not follow symlinks) so a
                    // dangling or symlinked entry is still reported rather than
                    // treated as absent — mirroring FileSystem.metadata.
                    const auto sym_status = std::filesystem::symlink_status(safe_path, ec);
                    if (ec || !std::filesystem::exists(sym_status)) {
                        return make_failure_value(
                            error_msg("FileSystem", "permissions",
                                      std::format("path does not exist '{}'", safe_path.string())));
                    }

                    // Follow symlinks for the permission bits so they describe the
                    // target, agreeing with the size / type queries in metadata.
                    std::error_code status_ec;
                    const auto status = std::filesystem::status(safe_path, status_ec);

                    return make_success_value(make_permissions_record(status.permissions()));
                });
        })
        .func("kind", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op(
                "FileSystem.kind", args[0], loc, false,
                [](const std::filesystem::path& safe_path) -> Value {
                    const auto variant = classify_file_kind(safe_path);
                    if (!variant) {
                        return make_failure_value(
                            error_msg("FileSystem", "kind",
                                      std::format("path does not exist '{}'", safe_path.string())));
                    }
                    return make_success_value(make_file_kind_choice(*variant));
                });
        })
        .func("list_recursively", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return fs_validated_string_op(
                "FileSystem.list_recursively", args[0], loc, true,
                [](const std::filesystem::path& safe_path) -> Value {
                    auto arr = std::make_shared<ArrayValue>();

                    for (const auto& entry : std::filesystem::recursive_directory_iterator(
                             safe_path,
                             std::filesystem::directory_options::skip_permission_denied)) {
                        if (entry.is_symlink()) {
                            continue;
                        }

                        if (arr->elements->size() >= ResourceLimits::max_array_size) {
                            return make_failure_value(ErrorMessages::result_exceeds_maximum_size(
                                "FileSystem", "list_recursively"));
                        }

                        const auto rel = std::filesystem::relative(entry.path(), safe_path);
                        arr->elements->emplace_back(rel.string());
                    }

                    return make_success_value(Value{std::move(arr)});
                });
        });
}

} // namespace luma
