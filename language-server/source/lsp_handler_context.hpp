#ifndef LUMA_LSP_HANDLER_CONTEXT_HPP
#define LUMA_LSP_HANDLER_CONTEXT_HPP

#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/lexer/token.hpp"
#include "json/json.hpp"
#include "lsp_analysis_cache.hpp"
#include "lsp_configuration_manager.hpp"
#include "lsp_constants.hpp"
#include "lsp_document_store.hpp"
#include "lsp_optional_ref.hpp"
#include "lsp_pending_uri_set.hpp"
#include "lsp_semantic_token_cache.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_stdlib_registry.hpp"
#include "lsp_transport_wrapper.hpp"
#include "lsp_types.hpp"
#include "lsp_workspace_manager.hpp"

// Forward declarations for types used only by specific handlers.
namespace luma::lsp {
class AnalysisPipeline;
class AnalysisService;
class DocumentSynchronizer;
struct AnalysisResult;

[[nodiscard]] std::optional<std::size_t> find_token_at(const AnalysisResult& result, int line,
                                                       int character);
} // namespace luma::lsp

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════════════════
// TokenContext — shared context for handlers that operate on a token
// at a cursor position.
//
// The analysis cache pointer is valid for the lifetime of the lock held
// by resolve_token_context — handlers must not store it beyond their return.
// ═══════════════════════════════════════════════════════════════════════

struct TokenContext {
    std::string uri;
    const AnalysisResult* result;
    std::size_t token_idx;
    const Token* token;
    const LspAnalysisCache* cache;
};

// ═══════════════════════════════════════════════════════════════════════
// DocumentAtPosition — context for handlers that need document content
// at a specific position.
// ═══════════════════════════════════════════════════════════════════════

struct DocumentAtPosition {
    std::string uri;
    int line;
    int character;
    const std::string* content;
};

// ═══════════════════════════════════════════════════════════════════════
// LspHandlerContext — shared context passed to all LSP handler classes.
//
// Provides references to the server's shared state and convenience
// methods (find_analysis, resolve_token_context, communication helpers)
// so that handler classes need not know about LspServer itself.
// ═══════════════════════════════════════════════════════════════════════

struct LspHandlerContext {
    // ═══ Threading & Shared State ═══
    std::shared_mutex& state_mutex;
    DocumentStore& doc_store;
    LspAnalysisCache& analysis_cache;
    PendingUriSet& pending_uris;

    // ═══ Feature-Specific State ═══
    StdlibRegistry& stdlib_registry;
    SemanticTokenCache& semantic_token_cache;
    ConfigurationManager& configuration;
    WorkspaceManager& workspace;

    // ═══ Communication ═══
    LspTransportWrapper& transport_wrapper;

    // ═══ State Lock Acquisition ═══

    [[nodiscard]] ReadStateLock acquire_read_lock() const {
        return ReadStateLock(state_mutex, doc_store, analysis_cache, pending_uris);
    }

    [[nodiscard]] WriteStateLock acquire_write_lock() const {
        return WriteStateLock(state_mutex, doc_store, analysis_cache, pending_uris);
    }

    // ═══ Cache Lookup ═══
    //
    // Two overloads exist for caller convenience: const handlers get a
    // const result, non-const handlers get a mutable result.
    //
    // Note on const-correctness: LspHandlerContext is a struct of
    // *references*, and in C++ const-qualifying a reference member does
    // not propagate to the referent.  The const overload therefore still
    // calls the non-const find() on analysis_cache at the language level.
    // This is safe because:
    //   (a) the returned optional_ref wraps the result as const, so the
    //       caller cannot mutate through it;
    //   (b) all cache access is already serialised by state_mutex.

    [[nodiscard]] optional_ref<const AnalysisResult> find_analysis(const std::string& uri) const {
        return analysis_cache.find(uri);
    }

    [[nodiscard]] optional_ref<AnalysisResult> find_analysis(const std::string& uri) {
        return analysis_cache.find(uri);
    }

    // ═══ Document Position Helpers ═══

    [[nodiscard]] std::optional<DocumentAtPosition>
    get_document_at_position(const JsonValue& params, const LockToken& lock_token) {
        const auto tdp = TextDocumentPosition::from_params(params);
        if (!tdp) {
            return std::nullopt;
        }
        const auto* content = doc_store.get_content(lock_token, tdp->uri);
        if (!content) {
            return std::nullopt;
        }
        return DocumentAtPosition{tdp->uri, tdp->line, tdp->character, content};
    }

    // ═══ Token Context Helper ═══
    //
    // Resolve the token context and invoke `handler` if successful.
    // Returns `fallback` when no cached analysis or matching token is
    // found (default: null JsonValue).  Throws InvalidParamsError if
    // params are structurally invalid.

    template <typename Handler>
        requires std::invocable<Handler, const TokenContext&>
    [[nodiscard]] JsonValue resolve_token_context(const JsonValue& params, Handler&& handler,
                                                  JsonValue fallback = {}) {
        const auto tdp = TextDocumentPosition::from_params(params);
        if (!tdp) {
            throw InvalidParamsError("Missing or malformed textDocument/position params");
        }
        auto state = acquire_read_lock();
        const auto result = find_analysis(tdp->uri);
        if (!result) {
            return fallback;
        }
        const auto idx_opt = find_token_at(*result, tdp->line, tdp->character);
        if (!idx_opt.has_value()) {
            return fallback;
        }
        const auto ctx = TokenContext{tdp->uri, &*result, *idx_opt,
                                      &result->semantic.tokens[*idx_opt], &analysis_cache};
        return std::forward<Handler>(handler)(ctx);
    }

    // ═══ Communication Helpers ═══
    //
    // These are intentionally thin wrappers over transport_wrapper. They form
    // a facade so handler classes depend only on LspHandlerContext and never
    // reach through to the transport layer directly. This keeps the transport
    // implementation swappable and provides a single seam for intercepting or
    // instrumenting outgoing messages, so they are kept rather than inlined.

    void publish_diagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics,
                             int version = 0) {
        transport_wrapper.publish_diagnostics(uri, diagnostics, version);
    }

    void send_response(const JsonValue& id, const JsonValue& result) {
        transport_wrapper.send_response(id, result);
    }

    void send_error(const JsonValue& id, int code, std::string_view message) {
        transport_wrapper.send_error(id, code, message);
    }

    void send_notification(std::string_view method, const JsonValue& params) {
        transport_wrapper.send_notification(method, params);
    }

    void log_message(const std::string& text, int type = constants::message_type::info) {
        transport_wrapper.log_message(text, type);
    }

    void send_progress_begin(const std::string& token, const std::string& title) {
        transport_wrapper.send_progress_begin(token, title);
    }

    void send_progress_report(const std::string& token, const std::string& message) {
        transport_wrapper.send_progress_report(token, message);
    }

    void send_progress_end(const std::string& token) {
        transport_wrapper.send_progress_end(token);
    }
};

} // namespace luma::lsp

#endif // LUMA_LSP_HANDLER_CONTEXT_HPP
