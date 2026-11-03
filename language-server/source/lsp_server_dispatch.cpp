#include <format>
#include <utility>

#include "lsp_analysis_pipeline.hpp"
#include "lsp_code_action_handler.hpp"
#include "lsp_completion_handler.hpp"
#include "lsp_constants.hpp"
#include "lsp_folding_handler.hpp"
#include "lsp_formatting_handler.hpp"
#include "lsp_hover_handler.hpp"
#include "lsp_navigation_handler.hpp"
#include "lsp_rename_handler.hpp"
#include "lsp_response_helpers.hpp"
#include "lsp_semantic_tokens_handler.hpp"
#include "lsp_server.hpp"
#include "lsp_symbol_handler.hpp"
#include "lsp_sync_handler.hpp"
#include "lsp_workspace_handler.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Dispatch table registration
// ═══════════════════════════════════════════════════════════

void LspServer::register_handlers() {
    // ─── Request handlers — routed to handler instances ───

    // Hover
    handlers_.register_request("textDocument/hover", [this](const JsonValue& p) {
        return hover_handler_->handle_hover(p);
    });

    // Completion
    handlers_.register_request("textDocument/completion", [this](const JsonValue& p) {
        return completion_handler_->handle_completion(p);
    });
    handlers_.register_request("completionItem/resolve", [this](const JsonValue& p) {
        return completion_handler_->handle_completion_resolve(p);
    });
    handlers_.register_request("textDocument/signatureHelp", [this](const JsonValue& p) {
        return completion_handler_->handle_signature_help(p);
    });

    // Symbols
    handlers_.register_request("textDocument/documentSymbol", [this](const JsonValue& p) {
        return symbol_handler_->handle_document_symbol(p);
    });

    // Navigation
    handlers_.register_request("textDocument/definition", [this](const JsonValue& p) {
        return navigation_handler_->handle_definition(p);
    });
    handlers_.register_request("textDocument/references", [this](const JsonValue& p) {
        return navigation_handler_->handle_references(p);
    });
    handlers_.register_request("textDocument/typeDefinition", [this](const JsonValue& p) {
        return navigation_handler_->handle_type_definition(p);
    });
    handlers_.register_request("textDocument/implementation", [this](const JsonValue& p) {
        return navigation_handler_->handle_implementation(p);
    });
    handlers_.register_request("textDocument/documentLink", [this](const JsonValue& p) {
        return navigation_handler_->handle_document_link(p);
    });

    // Rename
    handlers_.register_request("textDocument/rename", [this](const JsonValue& p) {
        return rename_handler_->handle_rename(p);
    });
    handlers_.register_request("textDocument/prepareRename", [this](const JsonValue& p) {
        return rename_handler_->handle_prepare_rename(p);
    });

    // Code actions
    handlers_.register_request("textDocument/codeAction", [this](const JsonValue& p) {
        return code_action_handler_->handle_code_action(p);
    });
    handlers_.register_request("workspace/executeCommand", [this](const JsonValue& p) {
        return code_action_handler_->handle_execute_command(p);
    });

    // Semantic tokens
    handlers_.register_request("textDocument/semanticTokens/full", [this](const JsonValue& p) {
        return semantic_tokens_handler_->handle_semantic_tokens_full(p);
    });

    // Folding
    handlers_.register_request("textDocument/foldingRange", [this](const JsonValue& p) {
        return folding_handler_->handle_folding_range(p);
    });

    // Formatting
    handlers_.register_request("textDocument/formatting", [this](const JsonValue& p) {
        return formatting_handler_->handle_formatting(p);
    });

    // ─── Notification handlers ───

    handlers_.register_notification(
        "textDocument/didOpen", [this](const JsonValue& p) { sync_handler_->handle_did_open(p); });
    handlers_.register_notification("textDocument/didChange", [this](const JsonValue& p) {
        sync_handler_->handle_did_change(p);
    });
    handlers_.register_notification("textDocument/didClose", [this](const JsonValue& p) {
        sync_handler_->handle_did_close(p);
    });
    handlers_.register_notification("textDocument/didSave", [this](const JsonValue& p) {
        workspace_handler_->handle_did_save(p);
    });
    handlers_.register_notification("workspace/didChangeWatchedFiles", [this](const JsonValue& p) {
        workspace_handler_->handle_did_change_watched_files(p);
    });
    handlers_.register_notification("workspace/didChangeConfiguration", [this](const JsonValue& p) {
        workspace_handler_->handle_did_change_configuration(p);
    });
}

// ═══════════════════════════════════════════════════════════
// Request and notification dispatch
// ═══════════════════════════════════════════════════════════
//
// Error handling contract
// ───────────────────────
// Handlers participate in a two-tier error propagation policy:
//
//   1. Throw InvalidParamsError when the request is structurally invalid
//      (missing required fields, wrong JSON types, malformed URIs).
//      The dispatch layer catches this and returns a JSON-RPC error
//      response with code -32602 (InvalidParams).
//
//   2. Return a null JsonValue or an empty array when the request is
//      well-formed but the feature is not applicable (no symbol at
//      cursor, no cached analysis, file not open).  This is NOT an
//      error — the LSP client interprets null/empty as "nothing to
//      show" and does not display an error to the user.
//
// Any other std::exception is caught by dispatch and surfaced as an
// internal error (-32603).  Handlers must never swallow exceptions
// that indicate a programming error; they should propagate so the
// dispatch layer can log them.
//
// See also: lsp_param_extraction.hpp (response convention comment),
//           lsp_handler_context.hpp  (resolve_token_context throw/return
//                                     semantics).

template <typename Produce>
void LspServer::respond(const JsonValue& id, const std::string& method, Produce&& produce) {
    try {
        // Each handler acquires ReadStateLock (or uses resolve_token_context
        // which does so) when it needs access to shared state.  A blanket
        // lock here would cause deadlock via recursive shared locking when
        // a writer (analysis worker) is waiting.
        transport_wrapper_.send_response(id, std::forward<Produce>(produce)());
    } catch (const InvalidParamsError& e) {
        transport_wrapper_.send_error(id, k_json_rpc_invalid_params, e.what());
    } catch (const std::exception& e) {
        // A well-formed request that fails here indicates a server-side
        // programming error.  Log the detail (the client only receives a
        // generic message) so the failure is diagnosable.
        transport_wrapper_.log_message(std::format("Error handling '{}': {}", method, e.what()),
                                       constants::message_type::error);
        transport_wrapper_.send_error(id, k_json_rpc_internal_error,
                                      "Internal error processing request");
    }
}

void LspServer::dispatch_request(const JsonValue& id, const std::string& method,
                                 const JsonValue& params) {
    // Early cancellation check — catches requests cancelled before dispatch begins.
    if (cancellation_manager_.check_and_clear(id)) {
        transport_wrapper_.send_error(id, k_json_rpc_request_cancelled, "Request cancelled");
        return;
    }

    // Lifecycle (LSP 3.17 §lifecycle): `initialize` is the first request and
    // may be sent only once; every other request — `shutdown` included — is
    // rejected until it has succeeded, and every request after `shutdown`.
    if (method == "initialize") {
        if (initialize_received_) {
            transport_wrapper_.send_error(id, k_json_rpc_invalid_request,
                                          "The server is already initialized");
            return;
        }
        respond(id, method, [&] {
            auto capabilities = handle_initialize(params);
            initialize_received_ = true;
            return capabilities;
        });
        return;
    }

    if (!initialize_received_) {
        transport_wrapper_.send_error(id, k_json_rpc_server_not_initialized,
                                      "Server not initialized");
        return;
    }

    if (shutdown_requested_) {
        transport_wrapper_.send_error(id, k_json_rpc_invalid_request, "Server is shutting down");
        return;
    }

    if (method == "shutdown") {
        respond(id, method, [&] { return handle_shutdown(); });
        return;
    }

    // Feature handlers via dispatch table.
    const auto* handler = handlers_.find_request(method);
    if (handler == nullptr) {
        transport_wrapper_.send_error(id, k_json_rpc_method_not_found,
                                      std::format("Method not found: {}", method));
        return;
    }

    // Late cancellation check — catches cancellations that arrived during
    // lifecycle guards and handler lookup, giving one final opportunity to
    // avoid executing a potentially expensive handler.
    if (cancellation_manager_.check_and_clear(id)) {
        transport_wrapper_.send_error(id, k_json_rpc_request_cancelled, "Request cancelled");
        return;
    }

    respond(id, method, [&] { return (*handler)(params); });
}

bool LspServer::dispatch_message(const JsonValue& message) {
    // A message carrying `result` or `error` but no `method` is a response to
    // a request the server sent (e.g. workspace/configuration); it needs no
    // reply.
    if (message.is_object() && !message.has("method") &&
        (message.has("result") || message.has("error"))) {
        return false;
    }

    // Anything else without a string `method` is not a valid request or
    // notification (JSON-RPC 2.0 §5): answer with InvalidRequest, echoing
    // the id when it is usable and null otherwise.
    if (!message.is_object() || !message.has("method") || !message["method"].is_string()) {
        JsonValue id;
        if (message.is_object() && message.has("id") &&
            (message["id"].is_string() || message["id"].is_integer())) {
            id = message["id"];
        }
        transport_wrapper_.send_error(id, k_json_rpc_invalid_request, "Invalid Request");
        return false;
    }

    const auto& method = message["method"].as_string();
    const auto params = message.has("params") ? message["params"] : JsonValue();

    if (message.has("id")) {
        dispatch_request(message["id"], method, params);
        return false;
    }
    return dispatch_notification(method, params);
}

bool LspServer::dispatch_notification(const std::string& method, const JsonValue& params) {
    if (method == "initialized") {
        handle_initialized();
    } else if (method == "exit") {
        exit_code_ = shutdown_requested_ ? 0 : 1;
        transport_wrapper_.log_message(std::format("Exit with code {}", exit_code_));
        return true;
    } else if (method == "$/cancelRequest") {
        if (params.is_object() && params.has("id")) {
            cancellation_manager_.cancel(params["id"]);
            // Signal the analysis worker to abort current work.
            analysis_pipeline_->request_cancellation();
        }
    } else {
        // Document sync and other notifications via dispatch table.
        const auto* handler = handlers_.find_notification(method);
        if (handler != nullptr) {
            (*handler)(params);
        }
        // Unknown notifications silently ignored.
    }

    return false;
}

} // namespace luma::lsp
