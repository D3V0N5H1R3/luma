#ifndef LUMA_STDLIB_PATH_VALIDATOR_HPP
#define LUMA_STDLIB_PATH_VALIDATOR_HPP

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/path_utils.hpp"

namespace luma {

namespace detail {

// Wrap a std::filesystem failure as a RuntimeError.  Kept out-of-line and
// [[noreturn]] so the construct-and-throw of this non-trivial exception runs in
// a normal stack frame rather than inside validate_path()'s catch funclet: a
// non-trivial temporary built and thrown from a catch funclet triggers a
// clang-cl MSVC-EH codegen bug that traps (int3).  See the matching note in
// core/runtime/vm/vm.hpp.
[[noreturn]] inline void throw_invalid_path(std::string_view user_path, const char* reason,
                                            const SourceLocation& loc) {
    throw RuntimeError{std::format("invalid path '{}': {}", user_path, reason), loc,
                       "check that the path is valid and accessible"};
}

} // namespace detail

// Convert a Luma string (always UTF-8) into a filesystem path.  Never build a
// std::filesystem::path straight from a std::string in the stdlib: on MSVC that
// decodes the bytes in the ANSI code page, so a name such as "测试.txt" turns
// into mojibake (or fails).  Throws a RuntimeError when the text is not
// well-formed UTF-8 (only detectable on Windows; a POSIX path is any byte
// string).  Pair with path_to_utf8() when handing a path back to Luma or
// embedding it in a message.
[[nodiscard]] inline std::filesystem::path path_from_user_utf8(std::string_view user_path,
                                                               const SourceLocation& loc) {
    auto path = try_path_from_utf8(user_path);

    if (!path) {
        detail::throw_invalid_path(user_path, "path is not valid UTF-8", loc);
    }

    return std::move(*path);
}

// Validate that a user-supplied path does not escape the current working
// directory.  Returns the resolved canonical path on success; throws
// RuntimeError if the path traverses outside the working directory.
[[nodiscard]] inline std::filesystem::path validate_path(std::string_view user_path,
                                                         const SourceLocation& loc) {
    namespace fs = std::filesystem;

    // Only the std::filesystem resolution below can throw, so keep it alone in
    // the try.  The access-denied RuntimeError is thrown *after* the try (it is
    // itself a std::exception); catching it here would re-wrap and double its
    // message.
    const fs::path relative = path_from_user_utf8(user_path, loc);

    fs::path cwd;
    fs::path resolved;
    try {
        cwd = fs::weakly_canonical(fs::current_path());
        resolved = fs::weakly_canonical(cwd / relative);
    } catch (const std::exception& e) {
        detail::throw_invalid_path(user_path, e.what(), loc);
    }

    const auto rel = resolved.lexically_relative(cwd);
    const auto first = rel.begin();
    if (first == rel.end() || *first == "..") {
        throw RuntimeError{
            std::format("path access denied: '{}' resolves outside the working directory",
                        user_path),
            loc, "file operations are restricted to the current working directory"};
    }

    return resolved;
}

} // namespace luma

#endif // LUMA_STDLIB_PATH_VALIDATOR_HPP
