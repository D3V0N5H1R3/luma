#ifndef LUMA_PROTOCOL_CONSTANTS_HPP
#define LUMA_PROTOCOL_CONSTANTS_HPP

#include <cstddef>

namespace luma::protocol {

// ─── Protocol-layer constants ───
// These use inline constexpr because they are fixed at compile time and are
// specific to the LSP/DAP transport layer — not user-facing language limits.
// Language-level limits that can be overridden at runtime via LUMA_LIMIT_*
// environment variables live in ResourceLimits (common/resource_limits.hpp).

inline constexpr std::size_t k_default_max_message_bytes = std::size_t{50} * 1024 * 1024;
inline constexpr std::size_t k_default_max_header_length = 8192;
inline constexpr std::size_t k_default_max_resync_iterations = 1000;
inline constexpr std::size_t k_read_buffer_size = 8192;

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_CONSTANTS_HPP
