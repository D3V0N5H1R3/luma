#ifndef LUMA_COMMON_FILE_TIME_HPP
#define LUMA_COMMON_FILE_TIME_HPP

// Convert a std::filesystem::file_time_type to a std::chrono::system_clock
// time point.
//
// std::chrono::clock_cast is the standard tool for this, but Apple libc++
// (and some other standard libraries) do not yet provide it.  This helper
// feature-tests for clock_cast and falls back to rebasing the file-clock time
// point onto the system clock, keeping the conversion in one place for every
// caller (stdlib FileSystem module, LSP persisted index).

#include <chrono>
#include <filesystem>

namespace luma {

// Millisecond precision is sufficient for the modified-time queries that use
// this helper, so the fallback's small rebasing error is acceptable.
[[nodiscard]] inline std::chrono::system_clock::time_point
file_time_to_system_clock(std::filesystem::file_time_type ftime) {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    return std::chrono::clock_cast<std::chrono::system_clock>(ftime);
#else
    return std::chrono::system_clock::now() +
           std::chrono::duration_cast<std::chrono::system_clock::duration>(
               ftime - std::filesystem::file_time_type::clock::now());
#endif
}

} // namespace luma

#endif // LUMA_COMMON_FILE_TIME_HPP
