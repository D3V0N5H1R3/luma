#ifndef LUMA_LSP_DIAGNOSTIC_CODES_HPP
#define LUMA_LSP_DIAGNOSTIC_CODES_HPP

#include <string_view>

namespace luma::lsp {

// Diagnostic code strings shared between the diagnostic builder and
// code-action provider.  These must match the DiagnosticCode enum values
// defined in analysis/diagnostics/diagnostic.hpp.
namespace diagnostic_code {

inline constexpr std::string_view unused_variable = "W0001";
inline constexpr std::string_view unused_function = "W0002";
inline constexpr std::string_view unused_parameter = "W0003";
inline constexpr std::string_view mutable_never_mutated = "W0004";
inline constexpr std::string_view self_assignment = "W0005";
inline constexpr std::string_view unreachable_code = "W0006";
inline constexpr std::string_view shadowed_variable = "W0012";

} // namespace diagnostic_code
} // namespace luma::lsp

#endif // LUMA_LSP_DIAGNOSTIC_CODES_HPP
