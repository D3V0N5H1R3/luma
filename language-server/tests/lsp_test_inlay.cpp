// LSP inlay-hint handler tests — deterministic coverage of the request-time
// range windowing.
//
// Why a dedicated suite: the inlay-hint tests in lsp_test_features.cpp drive the
// *asynchronous* worker through the mock transport, where analysis races the
// transport drain, so those tests only check hint output conditionally (see the
// "timing-dependent" guards there). LspAnalysisService::analyze() is a pure,
// synchronous function of (uri, source), so here we populate the analysis cache
// directly and run LspInlayHintHandler against it — letting us assert
// unconditionally that the handler restricts hints to the client's requested
// range and still returns whole-document hints when no range is supplied.

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "json/json.hpp"
#include "lsp_analysis_cache.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_analysis_service_impl.hpp"
#include "lsp_config.hpp"
#include "lsp_configuration_manager.hpp"
#include "lsp_document_store.hpp"
#include "lsp_handler_context.hpp"
#include "lsp_inlay_hint_handler.hpp"
#include "lsp_pending_uri_set.hpp"
#include "lsp_semantic_token_cache.hpp"
#include "lsp_stdlib_registry.hpp"
#include "lsp_transport.hpp"
#include "lsp_transport_wrapper.hpp"
#include "lsp_workspace_manager.hpp"
#include "test_framework.hpp"

using namespace luma::json;
using namespace luma::lsp;

namespace {

// ─── Minimal transport ─────────────────────────────────────────────
//
// The inlay-hint handler never touches the transport, but LspHandlerContext
// holds an LspTransportWrapper reference that must be constructed from some
// Transport. This no-op implementation satisfies that dependency.
class NullTransport : public Transport {
public:
    void write_message(const JsonValue& /*message*/) override {}

protected:
    [[nodiscard]] std::optional<std::string> read_line() override {
        return std::nullopt;
    }

    [[nodiscard]] std::string read_exact(std::size_t /*count*/) override {
        return {};
    }
};

// ─── Fixture ───────────────────────────────────────────────────────
//
// Owns every collaborator an LspHandlerContext references, plus the analysis
// service used to populate the cache. Non-copyable and non-movable (mutexes,
// atomics, and the context of references), so construct one as a local in each
// test. Field order matters: each member must be declared before anything that
// binds a reference to it, and `ctx` is declared last so all its referents
// already exist.
struct InlayFixture {
    LspConfig config;
    std::atomic<bool> cancel_flag{false};
    LspAnalysisService service;

    std::shared_mutex state_mutex;
    DocumentStore doc_store;
    LspAnalysisCache analysis_cache;
    PendingUriSet pending_uris;
    StdlibRegistry stdlib_registry;
    SemanticTokenCache semantic_token_cache;
    ConfigurationManager configuration;
    WorkspaceManager workspace;
    std::atomic<bool> initialized{true};
    LspTransportWrapper transport_wrapper;

    LspHandlerContext ctx;

    InlayFixture()
        : service(config, cancel_flag,
                  AnalysisCallbacks{.log = [](const std::string&) {},
                                    .notify =
                                        [](std::string_view, const JsonValue&) {
                                        }}),
          transport_wrapper(std::make_unique<NullTransport>(), initialized),
          ctx{state_mutex,   doc_store,       analysis_cache,
              pending_uris,  stdlib_registry, semantic_token_cache,
              configuration, workspace,       transport_wrapper} {
        // Inlay hints are off by default; enable them so these tests exercise
        // the hint-generation logic rather than the disabled short-circuit.
        configuration.config().apply_lsp_settings(
            JsonValue::parse(R"({"luma":{"inlayHints":{"enabled":true}}})"));
    }

    void analyze_and_cache(const std::string& uri, const std::string& source) {
        analysis_cache.insert(uri, service.analyze(uri, source));
    }

    [[nodiscard]] JsonValue request_hints(const JsonValue& params) {
        LspInlayHintHandler handler(ctx);
        return handler.handle_inlay_hint(params);
    }
};

// ─── JSON param builders ───────────────────────────────────────────

[[nodiscard]] JsonValue make_position(int line, int character) {
    return JsonValue(JsonValue::ObjectType{
        {"line", JsonValue(line)},
        {"character", JsonValue(character)},
    });
}

[[nodiscard]] JsonValue make_range_params(const std::string& uri, int start_line, int start_char,
                                          int end_line, int end_char) {
    return JsonValue(JsonValue::ObjectType{
        {"textDocument", JsonValue(JsonValue::ObjectType{{"uri", JsonValue(uri)}})},
        {"range", JsonValue(JsonValue::ObjectType{
                      {"start", make_position(start_line, start_char)},
                      {"end", make_position(end_line, end_char)},
                  })},
    });
}

[[nodiscard]] JsonValue make_no_range_params(const std::string& uri) {
    return JsonValue(JsonValue::ObjectType{
        {"textDocument", JsonValue(JsonValue::ObjectType{{"uri", JsonValue(uri)}})},
    });
}

// A document with four typed local declarations on distinct lines; each yields
// exactly one type inlay hint (0-based lines 2, 3, 4, and 5), giving hints
// spread across multiple lines for the range-windowing assertions below.
constexpr std::string_view k_four_decls = "@main\n"
                                          "function void main() {\n"
                                          "    integer a = 1\n"
                                          "    integer b = 2\n"
                                          "    integer c = 3\n"
                                          "    integer d = 4\n"
                                          "}\n";

// ─── Tests ─────────────────────────────────────────────────────────

// A whole-document range returns hints spread over several distinct lines,
// establishing that the narrow-range assertions below are meaningful.
void test_inlay_hint_full_range_returns_multiple_lines() {
    InlayFixture fx;
    const std::string uri = "file:///test/inlay_full.luma";
    fx.analyze_and_cache(uri, std::string(k_four_decls));

    const auto hints = fx.request_hints(make_range_params(uri, 0, 0, 100, 0));
    ASSERT_TRUE(hints.is_array());
    // One type hint per declaration (a, b, c, d).
    ASSERT_GT(hints.as_array().size(), 1U);
}

// The requested range must window the returned hints: a range covering only one
// declaration line yields strictly fewer hints, all on that line. This is the
// regression guard — before the fix the handler ignored `range` and returned
// every hint regardless of the requested window.
void test_inlay_hint_respects_requested_range() {
    InlayFixture fx;
    const std::string uri = "file:///test/inlay_window.luma";
    fx.analyze_and_cache(uri, std::string(k_four_decls));

    const auto all = fx.request_hints(make_range_params(uri, 0, 0, 100, 0));
    ASSERT_TRUE(all.is_array());

    // Narrow the window to 0-based line 3 (the `integer b = 2` declaration).
    const auto windowed = fx.request_hints(make_range_params(uri, 3, 0, 3, 100));
    ASSERT_TRUE(windowed.is_array());

    // Deterministic: line 3 carries a hint, so the window is non-empty …
    ASSERT_FALSE(windowed.as_array().empty());
    // … strictly smaller than the whole-document result …
    ASSERT_LT(windowed.as_array().size(), all.as_array().size());
    // … and every returned hint lies on the requested line.
    for (const auto& hint : windowed.as_array()) {
        const int line = static_cast<int>(hint["position"]["line"].as_integer());
        ASSERT_EQ(line, 3);
    }
}

// Omitting `range` preserves the previous whole-document behaviour so clients
// that never send a range are unaffected.
void test_inlay_hint_absent_range_returns_all_hints() {
    InlayFixture fx;
    const std::string uri = "file:///test/inlay_norange.luma";
    fx.analyze_and_cache(uri, std::string(k_four_decls));

    const auto with_range = fx.request_hints(make_range_params(uri, 0, 0, 100, 0));
    const auto without_range = fx.request_hints(make_no_range_params(uri));
    ASSERT_TRUE(without_range.is_array());
    ASSERT_EQ(without_range.as_array().size(), with_range.as_array().size());
}

// Function declarations must not receive parameter-name inlay hints — those are
// reserved for call sites. Before the fix, `Identifier LeftParen` in a function
// declaration was mistaken for a function call.
void test_inlay_hint_no_param_hints_on_function_declaration() {
    InlayFixture fx;
    const std::string uri = "file:///test/inlay_decl.luma";
    const std::string source = "@main\n"
                               "function string format_money(number amount) {\n"
                               "    return \"hello\"\n"
                               "}\n";
    fx.analyze_and_cache(uri, source);

    const auto hints = fx.request_hints(make_range_params(uri, 0, 0, 100, 0));
    ASSERT_TRUE(hints.is_array());

    // No parameter-name hints (kind 2) should appear on the declaration line.
    for (const auto& hint : hints.as_array()) {
        if (!hint.is_object() || !hint.has("kind")) {
            continue;
        }
        const int kind = static_cast<int>(hint["kind"].as_integer());
        // InlayHintKind::Parameter == 2
        if (kind == 2) {
            const int line = static_cast<int>(hint["position"]["line"].as_integer());
            // Line 1 is the function declaration (0-based).
            ASSERT_NE(line, 1);
        }
    }
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_inlay_hint_full_range_returns_multiple_lines);
    RUN(test_inlay_hint_respects_requested_range);
    RUN(test_inlay_hint_absent_range_returns_all_hints);
    RUN(test_inlay_hint_no_param_hints_on_function_declaration);

    return SUMMARY();
}
