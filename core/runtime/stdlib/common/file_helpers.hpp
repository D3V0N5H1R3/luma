#ifndef LUMA_STDLIB_FILE_HELPERS_HPP
#define LUMA_STDLIB_FILE_HELPERS_HPP

// ═══════════════════════════════════════════════════════════
// File I/O helper functions for stdlib modules
// ═══════════════════════════════════════════════════════════
//
// Provides safe file read/write wrappers used by Csv,
// Compression, and other stdlib modules that perform file I/O.
//
// These helpers assume the path has already been validated via
// validate_path() — they do not perform path validation.

#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "common/resource_limits.hpp"

namespace luma::file_helpers {

// Read entire file contents as a string.
// Returns std::nullopt if the file cannot be opened or a read error occurs.
// Pass std::ios::binary for binary-mode reads.
[[nodiscard]] inline std::optional<std::string>
read_file_contents(const std::filesystem::path& path,
                   std::ios_base::openmode mode = std::ios_base::in) {
    // Bound the on-disk size before reading a byte, mirroring the guard
    // FileSystem.read_file / read_lines, Hash.*_file, and KeyValueStore use:
    // the `ss << rdbuf()` below would otherwise slurp an arbitrarily large file
    // wholesale, bypassing the ResourceLimits::max_string_size rail that every
    // in-memory string is meant to respect.  Fail closed when the size cannot
    // be determined (e.g. a FIFO or device node), treating such a file as
    // unreadable — exactly like one that cannot be opened.
    std::error_code size_ec;
    const auto file_bytes = std::filesystem::file_size(path, size_ec);
    if (size_ec || file_bytes > ResourceLimits::max_string_size) {
        return std::nullopt;
    }

    std::ifstream file{path, mode};
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << file.rdbuf();

    if (file.bad()) {
        return std::nullopt;
    }

    return ss.str();
}

// Write string content to a file.
// Returns true on success, false if the file cannot be opened or a write
// error occurs.  Pass std::ios::binary for binary-mode writes.
[[nodiscard]] inline bool write_file_contents(const std::filesystem::path& path,
                                              std::string_view content,
                                              std::ios_base::openmode mode = std::ios_base::out) {
    std::ofstream file{path, mode};
    if (!file.is_open()) {
        return false;
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));

    return !file.bad() && !file.fail();
}

} // namespace luma::file_helpers

#endif // LUMA_STDLIB_FILE_HELPERS_HPP
