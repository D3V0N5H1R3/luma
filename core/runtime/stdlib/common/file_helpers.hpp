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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "common/resource_limits.hpp"

namespace luma::file_helpers {

// Outcome of read_file_bounded.
enum class BoundedReadStatus {
    Ok,
    OpenFailed,
    TooLarge,
    ReadError,
};

struct BoundedRead {
    BoundedReadStatus status{BoundedReadStatus::OpenFailed};
    std::string data{}; // The file contents when status == Ok, else empty.
};

// Read a whole file while enforcing `max_bytes` AT READ TIME.  Checking
// std::filesystem::file_size first and then slurping the stream is not a bound:
// the file can grow between the check and the read, and special files (e.g.
// /proc entries) report a size of 0.  This reads in fixed-size chunks and gives
// up with TooLarge as soon as more than `max_bytes` bytes arrive, so memory use
// never exceeds the limit (plus one chunk of scratch space) whatever the file
// does.  Callers may still do an up-front size check for a fast, descriptive
// failure; this is the guarantee.  Pass std::ios::binary for byte-exact reads.
[[nodiscard]] inline BoundedRead
read_file_bounded(const std::filesystem::path& path, std::uintmax_t max_bytes,
                  std::ios_base::openmode mode = std::ios_base::in) {
    BoundedRead result{};

    std::ifstream file{path, mode | std::ios_base::in};

    if (!file.is_open()) {
        return result;
    }

    // The on-disk size is only an allocation hint here, never trusted as a bound.
    std::error_code size_ec;
    const auto size_hint = std::filesystem::file_size(path, size_ec);

    if (!size_ec && size_hint <= max_bytes) {
        result.data.reserve(static_cast<std::size_t>(size_hint));
    }

    constexpr std::size_t k_chunk_size{64 * 1024};
    std::string chunk(k_chunk_size, '\0');

    while (true) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));

        const auto got = static_cast<std::size_t>(file.gcount());

        if (got > 0) {
            if (result.data.size() + got > max_bytes) {
                result.status = BoundedReadStatus::TooLarge;
                result.data = std::string{};

                return result;
            }

            result.data.append(chunk.data(), got);
        }

        if (!file) {
            break;
        }
    }

    if (file.bad()) {
        result.status = BoundedReadStatus::ReadError;
        result.data = std::string{};

        return result;
    }

    result.status = BoundedReadStatus::Ok;

    return result;
}

// Read entire file contents as a string.
// Returns std::nullopt if the file cannot be opened or a read error occurs.
// Pass std::ios::binary for binary-mode reads.
[[nodiscard]] inline std::optional<std::string>
read_file_contents(const std::filesystem::path& path,
                   std::ios_base::openmode mode = std::ios_base::in) {
    // Bound the on-disk size before reading a byte, mirroring the guard
    // FileSystem.read_file / read_lines, Hash.*_file, and KeyValueStore use, so
    // an obviously oversized file fails without being opened.  Fail closed when
    // the size cannot be determined (e.g. a FIFO or device node), treating such
    // a file as unreadable — exactly like one that cannot be opened.
    std::error_code size_ec;
    const auto file_bytes = std::filesystem::file_size(path, size_ec);
    if (size_ec || file_bytes > ResourceLimits::max_string_size) {
        return std::nullopt;
    }

    // The size check above gives a fast failure; the bounded read enforces the
    // limit even if the file grows in between (or misreports its size).
    auto read = read_file_bounded(path, ResourceLimits::max_string_size, mode);

    if (read.status != BoundedReadStatus::Ok) {
        return std::nullopt;
    }

    return std::move(read.data);
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
