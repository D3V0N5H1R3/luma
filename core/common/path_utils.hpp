#ifndef LUMA_COMMON_PATH_UTILS_HPP
#define LUMA_COMMON_PATH_UTILS_HPP

// Unified path utilities — security checks, extension validation, and
// combined path validation in a single header.

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace luma {

// ─── Extension validation ───────────────────────────────────────────────────

// The canonical file extension for Luma source files.
inline constexpr std::string_view luma_extension{".luma"};

// Check whether a file path ends with the Luma source extension.
[[nodiscard]] inline constexpr bool has_luma_extension(std::string_view path) {
    return path.ends_with(luma_extension);
}

// ─── UTF-8 path conversion ──────────────────────────────────────────────────
//
// Luma source text — and so every path written in it, such as an include
// path — is UTF-8.  On MSVC, std::filesystem::path{std::string} decodes the
// bytes in the active ANSI code page instead, so a non-ASCII include such as
// "café.luma" turns into mojibake.  Build paths from UTF-8 text with these
// helpers; on POSIX they are equivalent to the plain std::string conversions.

// Construct a filesystem path from UTF-8 encoded text.  On MSVC this throws
// std::system_error when `utf8` is not well-formed UTF-8 (see
// try_path_from_utf8); on POSIX the bytes are used as-is.
[[nodiscard]] inline std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path{std::u8string{utf8.begin(), utf8.end()}};
}

// Non-throwing path_from_utf8: std::nullopt when the text cannot be decoded as
// UTF-8 (MSVC only — a POSIX path is any byte string).  Use it at boundaries
// where the text comes from users (REPL input, program strings) rather than
// from the filesystem.
[[nodiscard]] inline std::optional<std::filesystem::path>
try_path_from_utf8(std::string_view utf8) noexcept {
    try {
        return path_from_utf8(utf8);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Render a filesystem path as UTF-8 text.  Unlike path::string() on MSVC this
// never throws for characters the ANSI code page cannot represent.
[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string{utf8.begin(), utf8.end()};
}

// ─── Security checks ────────────────────────────────────────────────────────

// Returns true if any component of the given path is ".." or ".",
// indicating directory traversal.
[[nodiscard]] inline bool has_directory_traversal(const std::filesystem::path& path) noexcept {
    for (const auto& component : path) {
        if (component == ".." || component == ".") {
            return true;
        }
    }
    return false;
}

// Returns true if the resolved path is a symbolic link.
[[nodiscard]] inline bool is_symlink(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_symlink(path, ec);
}

// Returns true if any component in the path chain (from the path itself
// up to but not including the filesystem root) is a symbolic link.
// On Windows, is_symlink may behave differently — errors are handled
// gracefully via error codes and a catch-all guard.
[[nodiscard]] inline bool
is_symlink_or_contains_symlinks(const std::filesystem::path& path) noexcept {
    try {
        for (auto current = path; current.has_relative_path(); current = current.parent_path()) {
            std::error_code ec;
            if (std::filesystem::is_symlink(current, ec) && !ec) {
                return true;
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        return true; // Assume unsafe if any filesystem check fails unexpectedly.
    }
    return false;
}

// Returns true if the already-canonical path escapes the already-canonical root.
// Both inputs MUST be canonical (e.g., produced by weakly_canonical).
// Avoids redundant filesystem calls compared to escapes_root().
[[nodiscard]] inline bool
canonical_escapes_root(const std::filesystem::path& canonical_path,
                       const std::filesystem::path& canonical_root) noexcept {
    // The path stays within the root only if the root is a prefix of it.
    auto [root_end, _] = std::mismatch(canonical_root.begin(), canonical_root.end(),
                                       canonical_path.begin(), canonical_path.end());
    return root_end != canonical_root.end();
}

// Returns true if the resolved path escapes the given root directory.
// Both arguments are canonicalised internally via weakly_canonical;
// use canonical_escapes_root() instead when the paths are already canonical.
[[nodiscard]] inline bool escapes_root(const std::filesystem::path& path,
                                       const std::filesystem::path& root) noexcept {
    std::error_code ec;
    const auto canonical_path = std::filesystem::weakly_canonical(path, ec);

    if (ec) {
        return true; // Treat unresolvable paths as escaping.
    }

    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);

    if (ec) {
        return true;
    }

    return canonical_escapes_root(canonical_path, canonical_root);
}

// ─── Combined path validation ───────────────────────────────────────────────

struct PathValidationResult {
    bool is_valid = false;
    bool is_secure = false;
    std::string error_message;
};

// Check whether `path` ends with the given extension.  An empty
// `expected_extension` matches any path.
[[nodiscard]] inline bool check_extension(std::string_view path,
                                          std::string_view expected_extension) {
    return expected_extension.empty() || path.ends_with(expected_extension);
}

// Check that the path does not contain traversal components, symlinks, or
// escape the given root directory.  Populates `result.error_message` and
// returns false on the first violation.
[[nodiscard]] inline bool check_traversal(const std::filesystem::path& fs_path,
                                          const std::filesystem::path& root,
                                          PathValidationResult& result) {
    if (has_directory_traversal(fs_path)) {
        result.error_message = "path contains directory traversal";
        return false;
    }

    if (luma::is_symlink_or_contains_symlinks(fs_path)) {
        result.error_message = "path contains a symbolic link";
        return false;
    }

    if (!root.empty() && escapes_root(fs_path, root)) {
        result.error_message = "path escapes the allowed root directory";
        return false;
    }

    return true;
}

/// Validate a file path for both correctness and security.
///
/// Checks performed:
///   1. Extension matches `expected_extension` (when non-empty).
///   2. No directory-traversal components (. or ..).
///   3. Path is not a symbolic link.
///
/// An optional `root` directory restricts the path to that subtree.
[[nodiscard]] inline PathValidationResult validate_path(std::string_view path,
                                                        std::string_view expected_extension = "",
                                                        const std::filesystem::path& root = {}) {
    PathValidationResult result;

    if (!check_extension(path, expected_extension)) {
        result.error_message = "path does not have the expected extension";
        return result;
    }

    result.is_valid = true;

    // `path` is UTF-8 (REPL input / program text), not the ANSI code page.
    const auto fs_path = try_path_from_utf8(path);

    if (!fs_path) {
        result.error_message = "path is not valid UTF-8";
        return result;
    }

    if (!check_traversal(*fs_path, root, result)) {
        return result;
    }

    result.is_secure = true;
    return result;
}

} // namespace luma

#endif // LUMA_COMMON_PATH_UTILS_HPP
