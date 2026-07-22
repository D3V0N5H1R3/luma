#ifndef LUMA_LSP_CONSTANTS_HPP
#define LUMA_LSP_CONSTANTS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace luma::lsp::constants {

// ═══════════════════════════════════════════════════════════
// LSP method names
// ═══════════════════════════════════════════════════════════

namespace method {

inline constexpr std::string_view log_message = "window/logMessage";
inline constexpr std::string_view show_message = "window/showMessage";
inline constexpr std::string_view publish_diagnostics = "textDocument/publishDiagnostics";
inline constexpr std::string_view progress = "$/progress";
inline constexpr std::string_view configuration = "workspace/configuration";
inline constexpr std::string_view register_capability = "client/registerCapability";
inline constexpr std::string_view refresh_semantic_tokens = "workspace/semanticTokens/refresh";
inline constexpr std::string_view refresh_inlay_hints = "workspace/inlayHint/refresh";

} // namespace method

// ═══════════════════════════════════════════════════════════
// Completion sort priorities (string keys for LSP sort_text)
// ═══════════════════════════════════════════════════════════

namespace sort_priority {

inline constexpr std::string_view highest = "0";
inline constexpr std::string_view high = "1";
inline constexpr std::string_view normal = "2";
inline constexpr std::string_view low = "3";

} // namespace sort_priority

// ═══════════════════════════════════════════════════════════
// LSP MessageType enum values (window/showMessage, window/logMessage)
// ═══════════════════════════════════════════════════════════

namespace message_type {

inline constexpr int error = 1;
inline constexpr int warning = 2;
inline constexpr int info = 3;
inline constexpr int log = 4;

} // namespace message_type

// ═══════════════════════════════════════════════════════════
// LSP DiagnosticSeverity enum values (textDocument/publishDiagnostics)
// ═══════════════════════════════════════════════════════════

namespace severity {

inline constexpr int error = 1;
inline constexpr int warning = 2;
inline constexpr int information = 3;
inline constexpr int hint = 4;

} // namespace severity

// ═══════════════════════════════════════════════════════════
// LSP DiagnosticTag enum values (textDocument/publishDiagnostics)
// ═══════════════════════════════════════════════════════════

namespace diagnostic_tag {

inline constexpr int unnecessary = 1;
inline constexpr int deprecated = 2;

} // namespace diagnostic_tag

// ═══════════════════════════════════════════════════════════
// LSP CompletionItemKind enum values (textDocument/completion) — subset
// ═══════════════════════════════════════════════════════════

namespace completion_kind {

inline constexpr int function = 3;
inline constexpr int field = 5;
inline constexpr int variable = 6;
inline constexpr int class_ = 7;
inline constexpr int module_ = 9;
inline constexpr int enum_ = 13;
inline constexpr int keyword = 14;
inline constexpr int constant = 21;
inline constexpr int struct_ = 22;

} // namespace completion_kind

// ═══════════════════════════════════════════════════════════
// LSP InsertTextFormat enum values (completion items)
// ═══════════════════════════════════════════════════════════

namespace insert_text_format {

inline constexpr int plaintext = 1;
inline constexpr int snippet = 2;

} // namespace insert_text_format

// ═══════════════════════════════════════════════════════════
// Diagnostic source label (reported to the client for every Luma diagnostic)
// ═══════════════════════════════════════════════════════════

namespace diagnostic {

inline constexpr std::string_view source = "luma";

} // namespace diagnostic

// ═══════════════════════════════════════════════════════════
// LSP log message default type
// ═══════════════════════════════════════════════════════════

namespace log_level {

inline constexpr int default_type = 3; // Info

} // namespace log_level

// ═══════════════════════════════════════════════════════════
// LSP InlayHintKind enum values (textDocument/inlayHint)
// ═══════════════════════════════════════════════════════════

namespace inlay_hint_kind {

inline constexpr int type = 1;
inline constexpr int parameter = 2;

} // namespace inlay_hint_kind

// ═══════════════════════════════════════════════════════════
// Type definition kind strings
// ═══════════════════════════════════════════════════════════

namespace type_kind {

inline constexpr std::string_view record = "record";
inline constexpr std::string_view choice = "choice";
inline constexpr std::string_view interface_ = "interface";
inline constexpr std::string_view type_alias = "type_alias";

// Check whether a type_string represents a type definition kind.
[[nodiscard]] inline constexpr bool is_type_definition(std::string_view ts) {
    return ts == record || ts == choice || ts == interface_ || ts == type_alias;
}

} // namespace type_kind

// ═══════════════════════════════════════════════════════════
// File change types (LSP FileChangeType enum values)
// ═══════════════════════════════════════════════════════════

namespace file_change {

inline constexpr int created = 1;
inline constexpr int changed = 2;
inline constexpr int deleted = 3;

} // namespace file_change

// ═══════════════════════════════════════════════════════════
// Resource limits
// ═══════════════════════════════════════════════════════════

namespace limits {

inline constexpr std::size_t max_background_files = 10'000;
inline constexpr std::uintmax_t max_file_bytes = std::uintmax_t{10} * 1024 * 1024;
inline constexpr std::size_t max_cache_size = 600;
inline constexpr std::size_t max_workspace_symbols = 1000;

// Maximum number of characters to scan backwards when searching for an
// enclosing call context (signature help, parameter hints).
inline constexpr std::size_t max_scan_chars = 2000;

} // namespace limits

} // namespace luma::lsp::constants

#endif // LUMA_LSP_CONSTANTS_HPP
