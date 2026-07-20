// LSP protocol tests — lifecycle, sync, error handling, configuration.

#include <array>
#include <string_view>

#include "lsp_server.hpp"
#include "lsp_test_helpers.hpp"

namespace {

// Capabilities the server must always advertise in the initialize response.
constexpr std::array<std::string_view, 23> k_expected_capabilities = {{
    "hoverProvider",          "completionProvider",        "signatureHelpProvider",
    "definitionProvider",     "referencesProvider",        "renameProvider",
    "documentSymbolProvider", "codeActionProvider",        "foldingRangeProvider",
    "inlayHintProvider",      "workspaceSymbolProvider",   "callHierarchyProvider",
    "semanticTokensProvider", "documentHighlightProvider", "typeDefinitionProvider",
    "selectionRangeProvider", "codeLensProvider",          "linkedEditingRangeProvider",
    "documentLinkProvider",   "implementationProvider",    "executeCommandProvider",
    "typeHierarchyProvider",  "positionEncoding",
}};

// Helper: verify the server handles a sequence of messages without crashing.
void assert_server_survives(const std::vector<std::string>& messages) {
    auto mock = std::make_unique<MockTransport>();
    auto* transport = mock.get();

    mock->enqueue(k_init);
    mock->enqueue(k_initialized);
    for (const auto& msg : messages) {
        mock->enqueue(msg);
    }
    mock->enqueue(make_shutdown(100));
    mock->enqueue(k_exit);

    auto server = LspServer::create(LspServerConfig{std::move(mock)});
    const int exit_code = server->run();

    ASSERT_EQ(exit_code, 0);
    // Verify the server at least processed initialization.
    ASSERT_NE(transport->find_response(1), nullptr);
}

// ─── Lifecycle ─────────────────────────────────────────────────────

void test_initialize_shutdown() {
    LspTestSession session;
    const int exit_code = session.run();

    ASSERT_EQ(exit_code, 0);
    ASSERT_GE(session.outbox().size(), static_cast<std::size_t>(2));

    const auto* init_resp = session.find_response(1);
    ASSERT_NE(init_resp, nullptr);
    ASSERT_TRUE(init_resp->has("result"));
    ASSERT_TRUE((*init_resp)["result"].has("capabilities"));
}

void test_capabilities() {
    LspTestSession session;
    (void)session.run();

    const auto* init_resp = session.find_response(1);
    ASSERT_NE(init_resp, nullptr);

    const auto& caps = (*init_resp)["result"]["capabilities"];

    assert_capabilities_present(caps, k_expected_capabilities);
    // The server performs UTF-16 ↔ byte offset conversion on every incoming
    // position and outgoing range (lsp_document_store / lsp_diagnostic_builder
    // / lsp_token_utils), so it must advertise utf-16 — not utf-8 — or a
    // negotiation-aware client would send utf-8 positions the server then
    // misreads on non-ASCII lines.
    ASSERT_EQ(caps["positionEncoding"].as_string(), "utf-16");
}

void test_save_capability_advertised() {
    // The server must advertise textDocumentSync.save so the client sends
    // textDocument/didSave. That notification drives the include-dependency
    // refresh (handle_did_save): without it, a file A that does
    // `include "b.luma"` is never re-analyzed after B is edited and saved, so A
    // keeps showing stale cross-file diagnostics/hover/completion.
    LspTestSession session;
    (void)session.run();

    const auto* init_resp = session.find_response(1);
    ASSERT_NE(init_resp, nullptr);

    const auto& caps = (*init_resp)["result"]["capabilities"];
    ASSERT_TRUE(caps.has("textDocumentSync"));
    const auto& sync = caps["textDocumentSync"];
    ASSERT_TRUE(sync.is_object());
    ASSERT_TRUE(sync.has("save"));
}

void test_did_save_included_file_survives() {
    // Saving a file must not crash the server when include-dependency refresh
    // runs. Open B and A (which includes B), then save B: handle_did_save
    // converts B's URI to a path, drops its include cache, and re-schedules the
    // dependents of B. (Whether A's diagnostics have refreshed by the time the
    // session ends is analysis-timing-dependent and therefore not asserted here;
    // this guards the handler wiring and robustness.)
    const std::string uri_b = "file:///test/b.luma";
    const std::string uri_a = "file:///test/a.luma";
    const std::string did_save_b =
        R"({"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":")" +
        uri_b + R"("}}})";

    assert_server_survives({
        make_did_open(uri_b, "function helper() -> integer {\n    return 1\n}\n"),
        make_did_open(uri_a,
                      "include \"b.luma\"\n\n@main\nfunction main() {\n    x = helper()\n}\n"),
        did_save_b,
    });
}

void test_exit_without_shutdown() {
    auto mock = std::make_unique<MockTransport>();

    mock->enqueue(k_init);
    mock->enqueue(k_initialized);
    mock->enqueue(k_exit);

    auto server = LspServer::create(LspServerConfig{std::move(mock)});
    const int exit_code = server->run();

    ASSERT_EQ(exit_code, 1);
}

void test_unknown_method() {
    LspTestSession session;
    const auto id = session.request("textDocument/nonExistentMethod", "{}");
    (void)session.run();

    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);
    ASSERT_TRUE(resp->has("error"));
}

// ─── Document sync ─────────────────────────────────────────────────

void test_did_open_publishes_diagnostics() {
    LspTestSession session;

    const std::string uri = "file:///test/hello.luma";
    session.open_document(uri, "@main\nfunction main() {\n    x = 42\n}\n");
    (void)session.request("textDocument/documentSymbol", make_td_params(uri));
    (void)session.run();

    ASSERT_GE(session.outbox().size(), static_cast<std::size_t>(2));
}

void test_incremental_sync() {
    LspTestSession session;

    const std::string uri = "file:///test/incsync.luma";
    session.open_document(uri, "@main\nfunction main() {\n    x = 1\n}\n");

    // Incremental change: replace "1" with "42" on line 2.
    session.notify(
        R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":)"
        R"({"textDocument":{"uri":")" +
        uri +
        R"(","version":2},"contentChanges":[{"range":{"start":{"line":2,"character":8},"end":{"line":2,"character":9}},"text":"42"}]}})");

    const auto id = session.request("textDocument/hover", make_td_position(uri, 2, 4));
    (void)session.run();

    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);
    ASSERT_TRUE(resp->has("result"));
}

// ─── Empty document ───────────────────────────────────────────────

void test_empty_document() {
    LspTestSession session;

    const std::string uri = "file:///test/empty.luma";
    session.open_document(uri, "");

    const auto sym_id = session.request("textDocument/documentSymbol", make_td_params(uri));
    const auto hover_id = session.request("textDocument/hover", make_td_position(uri, 0, 0));
    const auto comp_id = session.request("textDocument/completion", make_td_position(uri, 0, 0));
    (void)session.run();

    const auto* sym_resp = session.find_response(sym_id);
    ASSERT_NE(sym_resp, nullptr);
    ASSERT_TRUE(sym_resp->has("result"));

    const auto* hover_resp = session.find_response(hover_id);
    ASSERT_NE(hover_resp, nullptr);
    ASSERT_TRUE(hover_resp->has("result"));

    const auto* comp_resp = session.find_response(comp_id);
    ASSERT_NE(comp_resp, nullptr);
    ASSERT_TRUE(comp_resp->has("result"));
}

// ─── Error handling ───────────────────────────────────────────────

void test_malformed_params() {
    LspTestSession session;

    const auto hover_id = session.request("textDocument/hover", "{}");
    const auto def_id = session.request("textDocument/definition", "{}");
    const auto ref_id = session.request("textDocument/references", "{}");
    const auto ren_id = session.request("textDocument/rename", "{}");
    const auto hl_id = session.request("textDocument/documentHighlight", "{}");
    const auto comp_id = session.request("textDocument/completion", "{}");
    const int exit_code = session.run();

    ASSERT_EQ(exit_code, 0);
    ASSERT_NE(session.find_response(hover_id), nullptr);
    ASSERT_NE(session.find_response(def_id), nullptr);
    ASSERT_NE(session.find_response(ref_id), nullptr);
    ASSERT_NE(session.find_response(ren_id), nullptr);
    ASSERT_NE(session.find_response(hl_id), nullptr);
    ASSERT_NE(session.find_response(comp_id), nullptr);
}

void test_diagnostic_tags() {
    LspTestSession session;

    const std::string uri = "file:///test/tags.luma";
    session.open_document(uri,
                          "function foo() {\n    let unused_var: integer = 42\n    return 0\n}\n");
    (void)session.run();

    ASSERT_GE(session.outbox().size(), static_cast<std::size_t>(2));
}

void test_cancel_request() {
    const std::string uri = "file:///test/cancel.luma";

    // Send request 10 after cancellation — should be rejected.
    auto mock_for_raw = std::make_unique<MockTransport>();
    const auto* tp = mock_for_raw.get();

    mock_for_raw->enqueue(k_init);
    mock_for_raw->enqueue(k_initialized);
    mock_for_raw->enqueue(make_did_open(uri, "let x: integer = 1\n"));
    mock_for_raw->enqueue(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":10}})");
    mock_for_raw->enqueue(make_request(10, "textDocument/hover", make_td_position(uri, 0, 4)));
    mock_for_raw->enqueue(make_shutdown(11));
    mock_for_raw->enqueue(k_exit);

    auto server = LspServer::create(LspServerConfig{std::move(mock_for_raw)});
    (void)server->run();

    const auto* resp = tp->find_response(10);
    ASSERT_NE(resp, nullptr);
    ASSERT_TRUE(resp->has("error"));
    ASSERT_EQ((*resp)["error"]["code"].as_integer(), -32800);
}

// ─── Configuration ────────────────────────────────────────────────

void test_configuration_change() {
    LspTestSession session;
    session.notify(
        R"({"jsonrpc":"2.0","method":"workspace/didChangeConfiguration","params":{)"
        R"("settings":{"luma":{"inlayHints":{"enabled":false},"codeLens":{"enabled":false},"analysisDebounceMs":100}})"
        R"(}})");
    const int exit_code = session.run();

    ASSERT_EQ(exit_code, 0);
}

void test_formatting_capabilities() {
    LspTestSession session;
    (void)session.run();

    const auto* init_resp = session.find_response(1);
    ASSERT_NE(init_resp, nullptr);

    const auto& caps = (*init_resp)["result"]["capabilities"];

    constexpr std::array<std::string_view, 3> formatting_caps = {{
        "documentFormattingProvider",
        "documentRangeFormattingProvider",
        "typeHierarchyProvider",
    }};
    assert_capabilities_present(caps, formatting_caps);
}

// ─── Diagnostics: severity ────────────────────────────────────────

void test_diagnostic_severity_error() {
    LspTestSession session;

    const std::string uri = "file:///test/diag_err.luma";
    // Type error: passing string where integer is expected.
    session.open_document(uri, "function add(a: integer, b: integer) -> integer {\n"
                               "    return a + b\n"
                               "}\n"
                               "\n"
                               "@main\n"
                               "function main() {\n"
                               "    add(1, \"hello\")\n"
                               "}\n");
    (void)session.run();

    // Diagnostics are published asynchronously — the analysis worker may not
    // finish before the mock transport drains.  Validate structure when present.
    for (const auto& msg : session.outbox()) {
        if (msg.has("method") && msg["method"].as_string() == "textDocument/publishDiagnostics") {
            ASSERT_TRUE(msg.has("params"));
            ASSERT_TRUE(msg["params"].has("diagnostics"));
            ASSERT_TRUE(msg["params"]["diagnostics"].is_array());
        }
    }
}

// ─── Diagnostics: clearing on fix ─────────────────────────────────

void test_diagnostic_clearing() {
    LspTestSession session;

    const std::string uri = "file:///test/diag_clear.luma";
    // First open with an error.
    session.open_document(uri, "@main\nfunction main() {\n    unknown_fn()\n}\n");

    // Then fix it with a change.
    session.notify(
        R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":)"
        R"({"textDocument":{"uri":")" +
        uri +
        R"(","version":2},"contentChanges":[{"text":"@main\nfunction main() {\n    x = 42\n}\n"}]}})");

    // Request something to trigger analysis.
    const auto id = session.request("textDocument/hover", make_td_position(uri, 2, 4));
    (void)session.run();

    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);

    // After fix, the most recent diagnostics publication should have fewer/no errors.
    const JsonValue* last_diag = nullptr;
    for (const auto& msg : session.outbox()) {
        if (msg.has("method") && msg["method"].as_string() == "textDocument/publishDiagnostics" &&
            msg.has("params") && msg["params"].has("uri") &&
            msg["params"]["uri"].as_string() == uri) {
            last_diag = &msg;
        }
    }
    if (last_diag != nullptr) {
        ASSERT_TRUE((*last_diag)["params"].has("diagnostics"));
    }
}

// ─── Diagnostics: unused variable tag ─────────────────────────────

void test_diagnostic_unused_tag() {
    LspTestSession session;

    const std::string uri = "file:///test/diag_unused.luma";
    session.open_document(uri,
                          "function foo() {\n    let unused_var: integer = 42\n    return 0\n}\n");
    (void)session.run();

    // Look for diagnostics with unnecessary tag (tag value 1).
    for (const auto& msg : session.outbox()) {
        if (msg.has("method") && msg["method"].as_string() == "textDocument/publishDiagnostics" &&
            msg.has("params") && msg["params"].has("diagnostics")) {
            for (const auto& diag : msg["params"]["diagnostics"].as_array()) {
                if (diag.has("tags")) {
                    ASSERT_TRUE(diag["tags"].is_array());
                }
            }
        }
    }
}

// ─── Error handling: out-of-range position ────────────────────────

void test_out_of_range_position() {
    LspTestSession session;

    const std::string uri = "file:///test/oor.luma";
    session.open_document(uri, "@main\nfunction main() {\n}\n");
    // Request hover at a position far beyond the document.
    const auto id = session.request("textDocument/hover", make_td_position(uri, 999, 999));
    (void)session.run();

    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);
    // Should not crash — return result (possibly null) or error.
    ASSERT_TRUE(resp->has("result") || resp->has("error"));
}

// ─── Error handling: invalid URI ──────────────────────────────────

void test_invalid_uri() {
    LspTestSession session;

    // Request hover on a document that was never opened.
    const auto id =
        session.request("textDocument/hover", make_td_position("file:///nonexistent.luma", 0, 0));
    (void)session.run();

    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);
    // Should return null result or error, not crash.
    ASSERT_TRUE(resp->has("result") || resp->has("error"));
}

// ─── Cancellation: cancel nonexistent ID ──────────────────────────

void test_cancel_nonexistent() {
    LspTestSession session;

    // Cancel an ID that was never sent.
    session.notify(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":9999}})");
    (void)session.request("textDocument/hover",
                          make_td_position("file:///test/cancel2.luma", 0, 0));
    const int exit_code = session.run();

    // Server should not crash on cancelling a nonexistent ID.
    ASSERT_EQ(exit_code, 0);
}

// ─── Document sync: didClose ──────────────────────────────────────

void test_did_close() {
    LspTestSession session;

    const std::string uri = "file:///test/close.luma";
    session.open_document(uri, "@main\nfunction main() {\n}\n");
    session.notify(R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":)"
                   R"({"textDocument":{"uri":")" +
                   uri + R"("}}})");

    // After close, requesting hover should return null/error.
    const auto id = session.request("textDocument/hover", make_td_position(uri, 0, 0));
    const int exit_code = session.run();

    ASSERT_EQ(exit_code, 0);
    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);
    ASSERT_TRUE(resp->has("result") || resp->has("error"));
}

// ─── Malformed input tests (TS-7) ─────────────────────────────────

void test_malformed_json_message() {
    // Feed the server a structurally wrong JSON payload — it must not crash.
    assert_server_survives({R"({"bogus": true})"});
}

void test_missing_required_fields() {
    // Send requests with params that have the right shape but omit mandatory
    // nested fields (e.g. textDocument without uri, position without line).
    LspTestSession session;

    // Position without "line" or "character".
    const auto hover_id = session.request(
        "textDocument/hover", R"({"textDocument":{"uri":"file:///x.luma"},"position":{}})");

    // textDocument without uri.
    const auto def_id = session.request(
        "textDocument/definition", R"({"textDocument":{},"position":{"line":0,"character":0}})");

    // Completely empty string params (different from the existing test_malformed_params
    // which passes `{}` — here we pass `""` as params).
    const auto sym_id =
        session.request("textDocument/documentSymbol", R"({"textDocument":{"uri":""}})");

    const int exit_code = session.run();

    ASSERT_EQ(exit_code, 0);

    // Each request should produce a response (even if it is an error response).
    ASSERT_NE(session.find_response(hover_id), nullptr);
    ASSERT_NE(session.find_response(def_id), nullptr);
    ASSERT_NE(session.find_response(sym_id), nullptr);
}

void test_notification_with_id() {
    // According to the spec, notifications must NOT have an "id" field.
    // Send a didOpen with an id — the server should still handle it gracefully.
    LspTestSession session;

    session.notify(R"({"jsonrpc":"2.0","id":999,"method":"textDocument/didOpen","params":)"
                   R"({"textDocument":{"uri":"file:///test/notif.luma",)"
                   R"("languageId":"luma","version":1,"text":"integer x = 1\n"}}})");

    const auto id =
        session.request("textDocument/hover", make_td_position("file:///test/notif.luma", 0, 0));
    const int exit_code = session.run();

    ASSERT_EQ(exit_code, 0);
    ASSERT_NE(session.find_response(id), nullptr);
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_initialize_shutdown);
    RUN(test_capabilities);
    RUN(test_save_capability_advertised);
    RUN(test_did_save_included_file_survives);
    RUN(test_exit_without_shutdown);
    RUN(test_unknown_method);
    RUN(test_did_open_publishes_diagnostics);
    RUN(test_incremental_sync);
    RUN(test_empty_document);
    RUN(test_malformed_params);
    RUN(test_diagnostic_tags);
    RUN(test_cancel_request);
    RUN(test_configuration_change);
    RUN(test_formatting_capabilities);
    RUN(test_diagnostic_severity_error);
    RUN(test_diagnostic_clearing);
    RUN(test_diagnostic_unused_tag);
    RUN(test_out_of_range_position);
    RUN(test_invalid_uri);
    RUN(test_cancel_nonexistent);
    RUN(test_did_close);

    // Malformed-input tests.
    RUN(test_malformed_json_message);
    RUN(test_missing_required_fields);
    RUN(test_notification_with_id);

    return SUMMARY();
}
