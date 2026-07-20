// Shared test infrastructure for LSP test suite.
// Provides: MockTransport, LspTestSession helper, and re-exports the shared test framework.

#ifndef LUMA_LSP_TEST_HELPERS_HPP
#define LUMA_LSP_TEST_HELPERS_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "json/json.hpp"
#include "lsp_server.hpp"
#include "lsp_transport.hpp"
#include "test_framework.hpp"

using namespace luma::json;
using namespace luma::lsp;

// ─── Mock transport ────────────────────────────────────────────────

class MockTransport : public Transport {
public:
    void enqueue(const std::string& json_str) {
        inbox_.push_back(JsonValue::parse(json_str));
    }

    [[nodiscard]] std::optional<JsonValue> read_message() override {
        if (read_idx_ >= inbox_.size()) {
            return std::nullopt;
        }
        return inbox_[read_idx_++];
    }

    void write_message(const JsonValue& message) override {
        outbox_.push_back(message);
    }

    [[nodiscard]] const std::vector<JsonValue>& outbox() const {
        return outbox_;
    }

    [[nodiscard]] const JsonValue* find_response(int64_t id) const {
        for (const auto& msg : outbox_) {
            if (msg.is_object() && msg.has("id") && msg["id"].as_integer() == id) {
                return &msg;
            }
        }
        return nullptr;
    }

protected:
    [[nodiscard]] std::optional<std::string> read_line() override {
        return std::nullopt;
    }

    [[nodiscard]] std::string read_exact(std::size_t /*count*/) override {
        return {};
    }

private:
    std::vector<JsonValue> inbox_;
    std::size_t read_idx_{0};
    std::vector<JsonValue> outbox_;
};

// ─── JSON helpers ──────────────────────────────────────────────────

inline std::string json_escape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

// ─── Protocol message constants ────────────────────────────────────

inline const std::string k_init = R"({
    "jsonrpc": "2.0", "id": 1, "method": "initialize",
    "params": { "capabilities": { "textDocument": {
        "completion": { "completionItem": { "snippetSupport": true } }
    } } }
})";

inline const std::string k_initialized = R"({"jsonrpc":"2.0","method":"initialized","params":{}})";

inline const std::string k_exit = R"({"jsonrpc":"2.0","method":"exit","params":{}})";

// ─── Protocol message builders ─────────────────────────────────────

inline std::string make_shutdown(int64_t id) {
    return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
           R"(,"method":"shutdown","params":{}})";
}

inline std::string make_request(int64_t id, const std::string& method,
                                const std::string& params_json) {
    return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + R"(,"method":")" + method +
           R"(","params":)" + params_json + "}";
}

inline std::string make_did_open(const std::string& uri, const std::string& text) {
    return R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":)"
           R"({"textDocument":{"uri":")" +
           uri + R"(","languageId":"luma","version":1,"text":")" + json_escape(text) + R"("}}})";
}

inline std::string make_td_params(const std::string& uri) {
    return R"({"textDocument":{"uri":")" + uri + R"("}})";
}

inline std::string make_td_position(const std::string& uri, int line, int character) {
    return R"({"textDocument":{"uri":")" + uri + R"("},"position":{"line":)" +
           std::to_string(line) + R"(,"character":)" + std::to_string(character) + "}}";
}

inline std::string make_range_params(const std::string& uri, int start_line, int start_char,
                                     int end_line, int end_char) {
    return R"({"textDocument":{"uri":")" + uri + R"("},"range":{"start":{"line":)" +
           std::to_string(start_line) + R"(,"character":)" + std::to_string(start_char) +
           R"(},"end":{"line":)" + std::to_string(end_line) + R"(,"character":)" +
           std::to_string(end_char) + "}}}";
}

// ─── Test session helper ───────────────────────────────────────────
//
// Encapsulates the LSP lifecycle boilerplate (initialize, initialized, shutdown, exit)
// and provides a fluent API for building test scenarios.

class LspTestSession {
public:
    LspTestSession() : mock_(std::make_unique<MockTransport>()), transport_(mock_.get()) {
        mock_->enqueue(k_init);
        mock_->enqueue(k_initialized);
    }

    void open_document(const std::string& uri, const std::string& text) {
        mock_->enqueue(make_did_open(uri, text));
    }

    [[nodiscard]] int64_t request(const std::string& method, const std::string& params) {
        const int64_t id = next_id_++;
        mock_->enqueue(make_request(id, method, params));
        return id;
    }

    void notify(const std::string& json) {
        mock_->enqueue(json);
    }

    int run() {
        mock_->enqueue(make_shutdown(next_id_++));
        mock_->enqueue(k_exit);
        // Store the server to keep the mock transport alive after run() returns.
        server_ = LspServer::create(LspServerConfig{std::move(mock_)});
        return server_->run();
    }

    [[nodiscard]] const JsonValue* find_response(int64_t id) const {
        return transport_->find_response(id);
    }

    [[nodiscard]] const std::vector<JsonValue>& outbox() const {
        return transport_->outbox();
    }

    const JsonValue* request_and_run(const std::string& method, const std::string& uri,
                                     const std::string& code, const std::string& params) {
        open_document(uri, code);
        const auto id = request(method, params);
        (void)run();
        return find_response(id);
    }

private:
    std::unique_ptr<MockTransport> mock_;
    MockTransport* transport_;
    std::unique_ptr<LspServer> server_;
    int64_t next_id_{3}; // IDs 1 and 2 reserved for initialize and shutdown
};

// ─── Response assertion helpers ────────────────────────────────────

inline void assert_has_result(const JsonValue* resp) {
    ASSERT_NE(resp, nullptr);
    ASSERT_TRUE(resp->has("result"));
}

inline void assert_result_is_array(const JsonValue* resp) {
    assert_has_result(resp);
    ASSERT_TRUE((*resp)["result"].is_array());
}

inline void assert_result_is_object(const JsonValue* resp) {
    assert_has_result(resp);
    ASSERT_TRUE((*resp)["result"].is_object());
}

// ─── Capability assertion helper ──────────────────────────────────
//
// Assert that every named capability is advertised in the given
// capabilities object. Reports the specific missing capability on failure.

inline void assert_capabilities_present(const JsonValue& capabilities,
                                        std::span<const std::string_view> names) {
    for (const auto name : names) {
        const std::string key{name};
        if (!capabilities.has(key)) {
            luma::test::throw_assertion_error("missing capability: " + key);
        }
    }
}

// ─── Request + assert helper ──────────────────────────────────────
//
// Send a request, run the session, and return the response.
// Asserts that a response with a "result" field was received.

[[nodiscard]] inline const JsonValue*
request_and_assert(LspTestSession& session, const std::string& method, const std::string& params) {
    const auto id = session.request(method, params);
    (void)session.run();
    const auto* resp = session.find_response(id);
    assert_has_result(resp);
    return resp;
}

// ─── Open + request + assert helper ───────────────────────────────
//
// Open a document, send a request, run the session, and return the
// response. Combines the common open→request→run→find→assert boilerplate
// for the many single-document request tests. Asserts a "result" was received.

[[nodiscard]] inline const JsonValue*
open_request_and_assert(LspTestSession& session, const std::string& method, const std::string& uri,
                        const std::string& code, const std::string& params) {
    session.open_document(uri, code);
    return request_and_assert(session, method, params);
}

// ─── Completion search helpers ─────────────────────────────────────

// Check if completion result contains an item with the given label
[[nodiscard]] inline bool has_completion_label(const JsonValue& result, const std::string& label) {
    if (!result.is_array()) {
        return false;
    }
    for (const auto& item : result.as_array()) {
        if (item.has("label") && item["label"].as_string() == label) {
            return true;
        }
    }
    return false;
}

// Extract all completion labels from a result
[[nodiscard]] inline std::vector<std::string> get_completion_labels(const JsonValue& result) {
    std::vector<std::string> labels;
    if (!result.is_array()) {
        return labels;
    }
    for (const auto& item : result.as_array()) {
        if (item.has("label")) {
            labels.push_back(item["label"].as_string());
        }
    }
    return labels;
}

// ─── Hover text helper ────────────────────────────────────────────

// Extract hover markdown text from a response
[[nodiscard]] inline std::string get_hover_text(const JsonValue* resp) {
    if (!resp || !resp->has("result")) {
        return "";
    }
    const auto& result = (*resp)["result"];
    if (result.has("contents")) {
        const auto& contents = result["contents"];
        if (contents.has("value")) {
            return contents["value"].as_string();
        }
        if (contents.is_string()) {
            return contents.as_string();
        }
    }
    return "";
}

// ─── Symbol-finding helpers ───────────────────────────────────────

[[nodiscard]] inline bool has_symbol(const JsonValue& result, const std::string& name) {
    if (!result.is_array()) {
        return false;
    }
    for (const auto& sym : result.as_array()) {
        if (sym.has("name") && sym["name"].as_string() == name) {
            return true;
        }
        // Check children for document symbols
        if (sym.has("children")) {
            for (const auto& child : sym["children"].as_array()) {
                if (child.has("name") && child["name"].as_string() == name) {
                    return true;
                }
            }
        }
    }
    return false;
}

// ─── Diagnostic helpers ───────────────────────────────────────────

[[nodiscard]] inline std::vector<const JsonValue*>
find_diagnostics(const std::vector<JsonValue>& outbox, const std::string& uri) {
    std::vector<const JsonValue*> results;
    for (const auto& msg : outbox) {
        if (msg.has("method") && msg["method"].as_string() == "textDocument/publishDiagnostics" &&
            msg.has("params") && msg["params"].has("uri") &&
            msg["params"]["uri"].as_string() == uri) {
            results.push_back(&msg);
        }
    }
    return results;
}

[[nodiscard]] inline bool any_diagnostic_contains(const std::vector<const JsonValue*>& publications,
                                                  const std::string& substring) {
    for (const auto* pub : publications) {
        if (!pub->has("params") || !(*pub)["params"].has("diagnostics")) {
            continue;
        }
        for (const auto& diag : (*pub)["params"]["diagnostics"].as_array()) {
            if (diag.has("message") &&
                diag["message"].as_string().find(substring) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

#endif // LUMA_LSP_TEST_HELPERS_HPP
