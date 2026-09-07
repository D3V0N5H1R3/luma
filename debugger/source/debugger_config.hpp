#ifndef LUMA_DAP_DEBUGGER_CONFIG_HPP
#define LUMA_DAP_DEBUGGER_CONFIG_HPP

#include <chrono>
#include <cstddef>

namespace luma::dap::config {

// Expression evaluation limits.
namespace expression {
// Maximum time to evaluate a single watch expression.
inline constexpr std::chrono::milliseconds k_default_evaluation_timeout{5000};

// Maximum number of cached compiled expressions.
inline constexpr std::size_t k_max_cache_entries = 256;

// Maximum length of a single expression string (64 KiB).
inline constexpr std::size_t k_max_code_size = std::size_t{64} * 1024;
} // namespace expression

// Variable inspection limits.
namespace variable {
// Maximum nesting depth for structured variable expansion.
inline constexpr int k_max_expansion_depth = 32;

// Maximum number of variable references before a full reset.
inline constexpr int k_max_variable_references = 1'000'000;

// Maximum number of entries across both registries before a purge is triggered.
inline constexpr int k_default_purge_entry_threshold = 10'000;

// Number of generation advances between automatic purges.
inline constexpr int k_default_purge_generation_interval = 10;
} // namespace variable

// Breakpoint management.
namespace breakpoint {
// Session-assigned breakpoint IDs start at this value.
inline constexpr int k_initial_breakpoint_id = 1;
} // namespace breakpoint

} // namespace luma::dap::config

#endif // LUMA_DAP_DEBUGGER_CONFIG_HPP
