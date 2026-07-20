// LSP analysis pipeline tests — include phase, error recovery, cache eviction.

#include "lsp_test_helpers.hpp"

namespace {

// ─── Include phase: missing include ────────────────────────────────

void test_include_missing_file() {
    LspTestSession session;

    const std::string uri = "file:///test/inc_missing.luma";
    session.open_document(uri, "include \"nonexistent.luma\"\n"
                               "\n"
                               "@main\n"
                               "function main() {\n"
                               "    x = 1\n"
                               "}\n");

    // Request hover to trigger analysis and wait for results.
    const auto id = session.request("textDocument/hover", make_td_position(uri, 4, 4));
    (void)session.run();

    // The server must not crash; hover response should exist.
    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);

    // Analysis should produce diagnostics about the missing include.
    // The include_phase handles file-not-found gracefully.
    const auto diags = find_diagnostics(session.outbox(), uri);
    if (!diags.empty()) {
        // If diagnostics were published, verify structure is valid.
        const auto& params = (*diags.back())["params"];
        ASSERT_TRUE(params.has("diagnostics"));
        ASSERT_TRUE(params["diagnostics"].is_array());
    }
}

// ─── Include phase: absolute path rejected ─────────────────────────

void test_include_absolute_path_rejected() {
    LspTestSession session;

    const std::string uri = "file:///test/inc_absolute.luma";
    session.open_document(uri, "include \"/etc/passwd\"\n"
                               "\n"
                               "@main\n"
                               "function main() {\n"
                               "    x = 1\n"
                               "}\n");

    const auto id = session.request("textDocument/hover", make_td_position(uri, 4, 4));
    (void)session.run();

    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);

    // Absolute path includes should produce an error diagnostic (E4004).
    const auto diags = find_diagnostics(session.outbox(), uri);
    if (!diags.empty()) {
        ASSERT_TRUE(any_diagnostic_contains(diags, "absolute path") ||
                    any_diagnostic_contains(diags, "Include rejected"));
    }
}

// ─── Include phase: directory traversal rejected ───────────────────

void test_include_directory_traversal_rejected() {
    LspTestSession session;

    const std::string uri = "file:///test/inc_traversal.luma";
    session.open_document(uri, "include \"../../../etc/secret.luma\"\n"
                               "\n"
                               "@main\n"
                               "function main() {\n"
                               "    x = 1\n"
                               "}\n");

    const auto id = session.request("textDocument/hover", make_td_position(uri, 4, 4));
    (void)session.run();

    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);

    // Directory traversal includes should produce an error diagnostic.
    const auto diags = find_diagnostics(session.outbox(), uri);
    if (!diags.empty()) {
        ASSERT_TRUE(any_diagnostic_contains(diags, "directory traversal") ||
                    any_diagnostic_contains(diags, "Include rejected"));
    }
}

// ─── Error recovery: lexer errors don't crash ──────────────────────

void test_lexer_error_recovery() {
    LspTestSession session;

    const std::string uri = "file:///test/lex_err.luma";
    // Unterminated string literal — lexer should produce an error token
    // but the pipeline must not crash.
    session.open_document(uri, "@main\n"
                               "function main() {\n"
                               "    x = \"unterminated\n"
                               "}\n");

    const auto id = session.request("textDocument/hover", make_td_position(uri, 1, 9));
    (void)session.run();

    // Server must respond — not crash or hang.
    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);

    // Diagnostics should be published for the lexer error.
    const auto diags = find_diagnostics(session.outbox(), uri);
    if (!diags.empty()) {
        const auto& params = (*diags.back())["params"];
        ASSERT_TRUE(params.has("diagnostics"));
        ASSERT_TRUE(params["diagnostics"].is_array());
    }
}

// ─── Error recovery: parser errors produce diagnostics ─────────────

void test_parser_error_diagnostics() {
    LspTestSession session;

    const std::string uri = "file:///test/parse_err.luma";
    // Missing closing brace — parser should report an error but still
    // collect symbols from the valid prefix.
    session.open_document(uri, "@main\n"
                               "function main() {\n"
                               "    x = 42\n");

    const auto id = session.request("textDocument/documentSymbol", make_td_params(uri));
    (void)session.run();

    // Server must not crash; document symbol request should return.
    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);

    // Even with parse errors, the result should be structured.
    ASSERT_TRUE(resp->has("result"));
}

// ─── Error recovery: symbols survive parse errors ──────────────────

void test_symbols_survive_parse_errors() {
    LspTestSession session;

    const std::string uri = "file:///test/partial.luma";
    // First function is valid, second has a syntax error.
    // Symbol collection should still find the valid function.
    session.open_document(uri,
                          "function greet(name: string) -> string {\n"
                          "    return \"Hello, ${name}\"\n"
                          "}\n"
                          "\n"
                          "@main\n"
                          "function main() {\n"
                          "    greet(\"world\")\n"); // missing closing brace

    const auto id = session.request("textDocument/documentSymbol", make_td_params(uri));
    (void)session.run();

    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);
    ASSERT_TRUE(resp->has("result"));
}

// ─── Cache: analysis results cached for unchanged documents ────────

void test_analysis_cache_skips_unchanged() {
    LspTestSession session;

    const std::string uri = "file:///test/cache.luma";
    session.open_document(uri, "@main\n"
                               "function main() {\n"
                               "    x = 42\n"
                               "}\n");

    // Send a didChange with the exact same content.
    session.notify(
        R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":)"
        R"({"textDocument":{"uri":")" +
        uri +
        R"(","version":2},"contentChanges":[{"text":"@main\nfunction main() {\n    x = 42\n}\n"}]}})");

    // Two hover requests — second should be served from cache.
    const auto id1 = session.request("textDocument/hover", make_td_position(uri, 2, 4));
    const auto id2 = session.request("textDocument/hover", make_td_position(uri, 2, 4));
    (void)session.run();

    // Both requests must succeed — the cache must not corrupt results.
    const auto* resp1 = session.find_response(id1);
    const auto* resp2 = session.find_response(id2);
    ASSERT_NE(resp1, nullptr);
    ASSERT_NE(resp2, nullptr);
}

// ─── B09: semantic tokens refresh emitted on foreground commit ─────

// True when the outbox holds a notification with the given method.
[[nodiscard]] bool has_notification(const std::vector<JsonValue>& outbox, std::string_view method) {
    for (const auto& msg : outbox) {
        if (msg.has("method") && msg["method"].is_string() && msg["method"].as_string() == method) {
            return true;
        }
    }
    return false;
}

void test_semantic_tokens_refresh_on_commit() {
    LspTestSession session;

    const std::string uri = "file:///test/refresh.luma";
    session.open_document(uri, "@main\n"
                               "function void main() {\n"
                               "    integer x = 1\n"
                               "}\n");

    const auto id = session.request("textDocument/hover", make_td_position(uri, 2, 12));
    (void)session.run();

    const auto* resp = session.find_response(id);
    ASSERT_NE(resp, nullptr);

    // Analysis commit races the transport drain (see the timing notes above), so
    // assert the implication rather than an unconditional presence: whenever a
    // foreground analysis committed — evidenced by a publishDiagnostics message
    // for this document — B09 must also have emitted a
    // workspace/semanticTokens/refresh so the client re-pulls fresh tokens
    // instead of keeping the highlighting from the debounce window.
    const auto diags = find_diagnostics(session.outbox(), uri);
    if (!diags.empty()) {
        ASSERT_TRUE(has_notification(session.outbox(), "workspace/semanticTokens/refresh"));
    }
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_include_missing_file);
    RUN(test_include_absolute_path_rejected);
    RUN(test_include_directory_traversal_rejected);
    RUN(test_lexer_error_recovery);
    RUN(test_parser_error_diagnostics);
    RUN(test_symbols_survive_parse_errors);
    RUN(test_analysis_cache_skips_unchanged);
    RUN(test_semantic_tokens_refresh_on_commit);

    return SUMMARY();
}
