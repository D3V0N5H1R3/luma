#ifndef LUMA_LSP_PATH_UTILS_HPP
#define LUMA_LSP_PATH_UTILS_HPP

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════════════════════
// Path safety checks
// ═══════════════════════════════════════════════════════════════════════════

// Path validation is split between this file and lsp_include_processor.cpp.
// Consider consolidating into a single IncludePathValidator class.

// Returns true if the include path is safe to use (no traversal, no
// absolute paths).  Used by analysis and completion.
[[nodiscard]] inline bool is_safe_include_path(std::string_view path) {
    const auto fs_path = std::filesystem::path(path);

    if (fs_path.is_absolute()) {
        return false;
    }

    for (const auto& component : fs_path) {
        if (component == "..") {
            return false;
        }
    }

    return true;
}

// Returns true if a resolved filesystem path is safe (not a symlink).
[[nodiscard]] inline bool is_safe_resolved_path(const std::filesystem::path& resolved) {
    return !std::filesystem::is_symlink(resolved);
}

// ═══════════════════════════════════════════════════════════════════════════
// File reading helpers — merged from the former lsp_file_utils.hpp.
// ═══════════════════════════════════════════════════════════════════════════

// Reads a file and returns its content paired with a content hash.
// Returns std::nullopt if the file cannot be read or exceeds max_size.
// Pass max_size == 0 to skip the size check.
[[nodiscard]] inline std::optional<std::pair<std::string, std::size_t>>
read_file_with_hash(const std::filesystem::path& path, std::size_t max_size = 0) {
    if (max_size > 0) {
        std::error_code ec;
        const auto fsize = std::filesystem::file_size(path, ec);
        if (ec || fsize > max_size) {
            return std::nullopt;
        }
    }

    std::ifstream ifs(path);
    if (!ifs) {
        return std::nullopt;
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string content = oss.str();
    const auto hash = std::hash<std::string>{}(content);

    return std::pair{std::move(content), hash};
}

} // namespace luma::lsp

#endif // LUMA_LSP_PATH_UTILS_HPP
