// LSP analysis-service tests — deterministic coverage of the synchronous
// analysis pipeline (lex → parse → include → symbol → type-check → lint).
//
// Why a dedicated suite: the pipeline tests in lsp_test_analysis_pipeline.cpp
// drive the *asynchronous* worker through the mock transport, where diagnostic
// publication races the transport drain and is therefore only checked
// conditionally (see the "timing-dependent" notes there). LspAnalysisService
// exposes analyze() as a pure, synchronous function of (uri, source), so the
// same behaviours — include-security rejection, missing-include reporting,
// symbol collection, type errors, and parse-error recovery — can be asserted
// unconditionally and deterministically here.

#include <atomic>
#include <string>
#include <string_view>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/source/source_location.hpp"
#include "json/json.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_analysis_service_impl.hpp"
#include "lsp_config.hpp"
#include "lsp_constants.hpp"
#include "lsp_diagnostic_builder.hpp"
#include "lsp_identifier_collector.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"
#include "test_framework.hpp"

using luma::json::JsonValue;
using luma::lsp::AnalysisCallbacks;
using luma::lsp::AnalysisResult;
using luma::lsp::collect_scoped_occurrences;
using luma::lsp::find_declaration_name_range;
using luma::lsp::LspAnalysisService;
using luma::lsp::LspConfig;
using luma::lsp::Range;
using luma::lsp::ScopedOccurrenceFilter;
using luma::lsp::token_range;
namespace severity = luma::lsp::constants::severity;

namespace {

// ─── Fixture ───────────────────────────────────────────────────────
//
// Owns the config and cancellation flag (both must outlive the service)
// and constructs a service wired to no-op callbacks. Non-copyable and
// non-movable because LspConfig holds a mutex and cancel_flag_ is atomic —
// construct one as a local in each test.
struct ServiceFixture {
    LspConfig config;
    std::atomic<bool> cancel_flag{false};
    LspAnalysisService service;

    ServiceFixture()
        : service(config, cancel_flag,
                  AnalysisCallbacks{.log = [](const std::string&) {},
                                    .notify =
                                        [](std::string_view, const JsonValue&) {
                                        }}) {}

    [[nodiscard]] AnalysisResult analyze(const std::string& source,
                                         const std::string& uri = "file:///test/main.luma") {
        return service.analyze(uri, source);
    }
};

// ─── Diagnostic predicates ─────────────────────────────────────────

[[nodiscard]] bool has_code(const AnalysisResult& result, std::string_view code) {
    for (const auto& diag : result.semantic.diagnostics) {
        if (diag.code == code) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool any_message_contains(const AnalysisResult& result, std::string_view substring) {
    for (const auto& diag : result.semantic.diagnostics) {
        if (diag.message.find(substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_error_diagnostic(const AnalysisResult& result) {
    for (const auto& diag : result.semantic.diagnostics) {
        if (diag.severity == severity::error) {
            return true;
        }
    }
    return false;
}

// ─── Include security: absolute path rejected ──────────────────────

void test_include_absolute_path_rejected() {
    ServiceFixture fx;

    // An absolute include path must be rejected with the E4004 security
    // diagnostic. The absolute form is platform-specific: on Windows a bare
    // POSIX-style "/etc/..." path has no root name and is *not* absolute, so
    // use a drive-qualified path there.
#ifdef _WIN32
    const std::string abs_path = "C:/Windows/System32/secret.luma";
#else
    const std::string abs_path = "/etc/secret.luma";
#endif

    const auto result = fx.analyze("include \"" + abs_path +
                                   "\"\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n");

    ASSERT_TRUE(has_code(result, "E4004"));
    ASSERT_TRUE(any_message_contains(result, "absolute path"));
    ASSERT_TRUE(has_error_diagnostic(result));
}

// ─── Include security: directory traversal rejected ────────────────

void test_include_directory_traversal_rejected() {
    ServiceFixture fx;

    const auto result = fx.analyze("include \"../secret.luma\"\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n");

    ASSERT_TRUE(has_code(result, "E4004"));
    ASSERT_TRUE(any_message_contains(result, "directory traversal"));
    ASSERT_TRUE(has_error_diagnostic(result));
}

// ─── Include: missing file reported ────────────────────────────────

void test_include_missing_file_reported() {
    ServiceFixture fx;

    // A safe relative path that does not exist on disk must surface as the
    // E4005 "not found" diagnostic rather than being silently ignored.
    const auto result = fx.analyze("include \"definitely_missing_3f9a7c.luma\"\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n");

    ASSERT_TRUE(has_code(result, "E4005"));
    ASSERT_TRUE(any_message_contains(result, "not found"));
}

// ─── B05: include diagnostic range spans the path in UTF-16 units ──

void test_include_diagnostic_range_measures_utf16_width() {
    ServiceFixture fx;

    // The include path contains a two-byte UTF-8 character (é = U+00E9), so its
    // byte length (10) differs from its UTF-16 width (9). The include-diagnostic
    // range must span the path in the client's UTF-16 coordinate space — the
    // same space every other diagnostic uses — not by raw byte count.
    const auto result = fx.analyze("include \"caf\xC3\xA9.luma\"\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n");

    ASSERT_TRUE(has_code(result, "E4005"));

    bool found = false;
    Range range{};
    for (const auto& diag : result.semantic.diagnostics) {
        if (diag.code == "E4005") {
            range = diag.range;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    // The include declaration is anchored on a single line, preceded only by the
    // ASCII `include` keyword, so the start column needs no UTF-16 conversion.
    ASSERT_EQ(range.start.line, 0);
    ASSERT_EQ(range.end.line, 0);

    // "café.luma" is 9 codepoints / UTF-16 code units but 10 UTF-8 bytes, so the
    // range width must be 9 (UTF-16), proving byte length is no longer used.
    const int width = range.end.character - range.start.character;
    ASSERT_EQ(width, 9);
    ASSERT_NE(width, 10);
}

// ─── Include phase skipped without a file path ─────────────────────

void test_include_not_validated_without_uri() {
    ServiceFixture fx;

    // With an empty URI the include phase cannot resolve a base directory,
    // so it returns early and no include diagnostic is produced. The rest of
    // the pipeline must still run and collect the function symbol.
    const auto result = fx.analyze("include \"../secret.luma\"\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n",
                                   /*uri=*/"");

    ASSERT_FALSE(has_code(result, "E4004"));
    ASSERT_TRUE(result.find_function("main").has_value());
}

// ─── Symbol collection: user functions ─────────────────────────────

void test_user_functions_collected() {
    ServiceFixture fx;

    const auto result = fx.analyze("function string greet(string name) {\n"
                                   "    return \"Hi, ${name}\"\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    string greeting = greet(\"world\")\n"
                                   "    print(greeting)\n"
                                   "}\n");

    const auto greet = result.find_function("greet");
    ASSERT_TRUE(greet.has_value());
    ASSERT_TRUE(result.find_function("main").has_value());
    ASSERT_TRUE(result.find_definition("greet").has_value());

    // The return type is captured from the signature.
    ASSERT_EQ(greet->return_type, std::string("string"));
}

// ─── Symbol collection: records and choices ────────────────────────

void test_record_and_choice_collected() {
    ServiceFixture fx;

    const auto result = fx.analyze("record Point {\n"
                                   "    integer x,\n"
                                   "    integer y\n"
                                   "}\n"
                                   "\n"
                                   "choice Color {\n"
                                   "    Red\n"
                                   "    Green\n"
                                   "    Blue\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n");

    const auto point = result.find_record("Point");
    ASSERT_TRUE(point.has_value());
    ASSERT_EQ(point->fields.size(), static_cast<std::size_t>(2));

    ASSERT_TRUE(result.semantic.symbols.choice_variants.contains("Color"));
    ASSERT_EQ(result.semantic.symbols.choice_variants.at("Color").size(),
              static_cast<std::size_t>(3));
}

// ─── find_definition accessor contract ────────────────────────────

void test_find_definition_accessor() {
    ServiceFixture fx;

    const auto result = fx.analyze("record Point {\n"
                                   "    integer x,\n"
                                   "    integer y\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n");

    // A present top-level definition is returned and exposes its stored fields.
    const auto point = result.find_definition("Point");
    ASSERT_TRUE(point.has_value());
    ASSERT_EQ(point->type_string, std::string("record"));
    ASSERT_FALSE(point->is_mutable);

    // An unknown name yields an empty accessor result.
    ASSERT_FALSE(result.find_definition("Nonexistent").has_value());
}

// ─── Type checking: argument mismatch is an error ──────────────────

void test_type_error_produces_error_diagnostic() {
    ServiceFixture fx;

    // add() expects two integers; passing a string is a type error that the
    // synchronous type-check phase must report.
    const auto result = fx.analyze("function integer add(integer a, integer b) {\n"
                                   "    return a + b\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    integer sum = add(1, \"hello\")\n"
                                   "    print(\"${sum}\")\n"
                                   "}\n");

    ASSERT_TRUE(has_error_diagnostic(result));
}

// ─── Clean program: no error diagnostics ───────────────────────────

void test_clean_program_has_no_error_diagnostics() {
    ServiceFixture fx;

    const auto result = fx.analyze("function integer add(integer a, integer b) {\n"
                                   "    return a + b\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    integer sum = add(1, 2)\n"
                                   "    print(\"${sum}\")\n"
                                   "}\n");

    ASSERT_FALSE(has_error_diagnostic(result));
    ASSERT_TRUE(result.find_function("add").has_value());
    ASSERT_TRUE(result.find_function("main").has_value());
}

// ─── Solaris prelude: programs using the surface analyse cleanly ───
//
// A program that mentions `Solaris` gets the built-in GUI prelude injected
// before type-checking. The prelude is tagged with its own file id and lives
// outside the user's document, so this exercises two guarantees at once:
// (a) its element constructors must resolve — otherwise every `Solaris.*` call
// would be an unknown-symbol error — and (b) no diagnostic originating in the
// prelude may be attributed to the user's document, where the service maps
// diagnostics by line and a prelude location would otherwise paint onto the
// wrong line. Both hold when the analysis is free of error diagnostics while
// the user's own functions are still collected.
void test_solaris_program_analyses_without_errors() {
    ServiceFixture fx;

    const auto result =
        fx.analyze("choice Msg { Inc }\n"
                   "\n"
                   "record Model {\n"
                   "    integer count = 0\n"
                   "}\n"
                   "\n"
                   "function Model update(Model model, Msg msg) {\n"
                   "    return match msg {\n"
                   "        case Msg.Inc { model with { count = model.count + 1 } }\n"
                   "    }\n"
                   "}\n"
                   "\n"
                   "function View view(Model model) {\n"
                   "    return Solaris.column([\n"
                   "        Solaris.heading(\"Count: ${model.count}\"),\n"
                   "        Solaris.button(\"+\") |> Solaris.on_click(Msg.Inc)\n"
                   "    ])\n"
                   "}\n"
                   "\n"
                   "@main\n"
                   "function void main() {\n"
                   "    Solaris.run(Solaris.app(\"Demo\", Model { count = 0 }, update, view))\n"
                   "}\n");

    ASSERT_FALSE(has_error_diagnostic(result));
    ASSERT_TRUE(result.find_function("update").has_value());
    ASSERT_TRUE(result.find_function("view").has_value());
    ASSERT_TRUE(result.find_function("main").has_value());
}

// ─── Parse-error recovery: symbols survive ─────────────────────────

void test_symbols_survive_parse_errors() {
    ServiceFixture fx;

    // The first function is well-formed; the second omits its closing brace.
    // The parser reports an error, but symbol collection must still recover
    // the valid prefix so navigation and hover keep working.
    const auto result = fx.analyze("function string greet(string name) {\n"
                                   "    return \"Hi\"\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    print(greet(\"world\"))\n"); // missing closing brace

    ASSERT_TRUE(has_error_diagnostic(result));
    ASSERT_TRUE(result.find_function("greet").has_value());
}

// ─── Lexer-error recovery: no crash, diagnostics produced ──────────

void test_lexer_error_recovery() {
    ServiceFixture fx;

    // Unterminated string literal — the lexer must surface a diagnostic and
    // the pipeline must not throw.
    const auto result = fx.analyze("@main\n"
                                   "function void main() {\n"
                                   "    string x = \"unterminated\n"
                                   "}\n");

    ASSERT_TRUE(has_error_diagnostic(result));
}

// ─── Empty source: no crash ────────────────────────────────────────

void test_empty_source_is_safe() {
    ServiceFixture fx;

    const auto result = fx.analyze("");

    // An empty document has no symbols and no error diagnostics.
    ASSERT_FALSE(has_error_diagnostic(result));
    ASSERT_TRUE(result.semantic.symbols.user_functions.empty());
}

// ─── B01: global rename skips shadowing locals ────────────────────

void test_global_occurrences_skip_shadowing_local() {
    ServiceFixture fx;

    // A top-level function `value`, plus a `main` whose body declares a local
    // also named `value`. Collecting occurrences for a GLOBAL rename target
    // must touch only the global declaration (line 1) and never the unrelated
    // shadowing local on lines 7-8 (B01: matching by lexeme alone clobbered
    // unrelated locals/params/fields).
    const std::string uri = "file:///test/main.luma";
    const auto result = fx.analyze("function integer value() {\n" // line 1: global decl
                                   "    return 42\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"  // line 6
                                   "    integer value = 5\n"   // line 7: shadowing local
                                   "    print(\"${value}\")\n" // line 8: local use
                                   "}\n",
                                   uri);

    const std::optional<std::string> no_fn;
    const auto indices =
        collect_scoped_occurrences(result, "value", uri,
                                   ScopedOccurrenceFilter{.namespace_prefix = "",
                                                          .is_local = false,
                                                          .enclosing_function = no_fn,
                                                          .origin_uri = uri,
                                                          .include_declaration = true});

    // The global declaration itself is retained ...
    ASSERT_FALSE(indices.empty());
    // ... but every retained occurrence must be the global on line 1; the
    // shadowing local occurrences on lines 7-8 must be filtered out.
    for (const std::size_t idx : indices) {
        ASSERT_EQ(result.get_tokens()[idx].location.line, 1);
    }
}

// ─── B07: includeDeclaration=false excludes the declaration ────────

void test_include_declaration_toggle_excludes_decl() {
    ServiceFixture fx;

    // `compute` is declared once (line 1) and called twice (lines 7-8).
    const std::string uri = "file:///test/main.luma";
    const auto result = fx.analyze("function integer compute() {\n" // line 1: decl
                                   "    return 0\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    integer a = compute()\n" // line 7: call
                                   "    integer b = compute()\n" // line 8: call
                                   "    print(\"${a}${b}\")\n"
                                   "}\n",
                                   uri);

    const std::optional<std::string> no_fn;
    const auto make_filter = [&](bool include_decl) {
        return ScopedOccurrenceFilter{.namespace_prefix = "",
                                      .is_local = false,
                                      .enclosing_function = no_fn,
                                      .origin_uri = uri,
                                      .include_declaration = include_decl};
    };

    const auto with_decl = collect_scoped_occurrences(result, "compute", uri, make_filter(true));
    const auto without_decl =
        collect_scoped_occurrences(result, "compute", uri, make_filter(false));

    // Excluding the declaration must drop exactly one occurrence — the decl on
    // line 1 — while the two call sites remain (B07: the flag was ignored
    // because it compared the name token against the keyword location).
    ASSERT_EQ(with_decl.size(), without_decl.size() + 1);
    ASSERT_FALSE(without_decl.empty());

    const auto has_line = [&](const std::vector<std::size_t>& occ, int line) {
        for (const std::size_t idx : occ) {
            if (result.get_tokens()[idx].location.line == line) {
                return true;
            }
        }
        return false;
    };

    ASSERT_TRUE(has_line(with_decl, 1));     // declaration present when included
    ASSERT_FALSE(has_line(without_decl, 1)); // and absent when excluded
}

// ─── B02: declaration ranges anchor on the name, not the keyword ───

void test_declaration_name_range_anchors_on_name() {
    ServiceFixture fx;

    const auto result = fx.analyze("function integer compute() {\n"
                                   "    return 0\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n");

    const auto fn = result.find_function("compute");
    ASSERT_TRUE(fn.has_value());

    // The declaration stores the `function` keyword location (character 0). The
    // name range must instead span `compute`, which begins at 0-based character
    // 17 ("function integer ") and is 7 columns wide (B02).
    const Range r = find_declaration_name_range(result.get_tokens(), fn->location, "compute");
    ASSERT_EQ(r.start.line, 0);
    ASSERT_EQ(r.start.character, 17);
    ASSERT_EQ(r.end.character, 24);
}

void test_declaration_name_range_measures_codepoints() {
    ServiceFixture fx;

    // `café` is 4 codepoints but 5 UTF-8 bytes. The name range width must be
    // measured in codepoints, so the name spans characters [17, 21) — using
    // the byte length (5) would over-run the name by one column (B02).
    const auto result = fx.analyze("function integer caf\xC3\xA9() {\n"
                                   "    return 0\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n");

    const auto fn = result.find_function("caf\xC3\xA9");
    ASSERT_TRUE(fn.has_value());
    const Range r = find_declaration_name_range(result.get_tokens(), fn->location, "caf\xC3\xA9");
    ASSERT_EQ(r.start.character, 17);
    ASSERT_EQ(r.end.character, 21);
}

// ─── B03: diagnostic columns map codepoints → UTF-16 ───────────────

void test_diagnostic_column_maps_codepoint_to_utf16() {
    // Source line: `aé bc` — é is 2 UTF-8 bytes but 1 codepoint / 1 UTF-16 unit.
    //   codepoint cols (1-based): a=1 é=2 space=3 b=4 c=5
    // A diagnostic whose primary span starts at the word `bc` (codepoint col 4)
    // must map to UTF-16 column 3; the old builder mis-read the codepoint column
    // as a byte offset and shifted the range left (B03).
    const std::string source = "a\xC3\xA9 bc";
    const std::vector<std::size_t> line_starts{0};

    luma::Diagnostic diag;
    diag.severity = luma::Severity::Error;
    diag.message = "test";
    diag.spans.push_back(luma::DiagnosticSpan{.start = luma::SourceLocation{.line = 1, .column = 4},
                                              .end = luma::SourceLocation{.line = 1, .column = 4},
                                              .label = "",
                                              .is_primary = true});

    const auto lsp_diag = luma::lsp::diagnostic_builder::make_diagnostic(
        diag, source, "file:///test/main.luma", line_starts);

    ASSERT_EQ(lsp_diag.range.start.line, 0);
    ASSERT_EQ(lsp_diag.range.start.character, 3);
    ASSERT_EQ(lsp_diag.range.end.character, 5);
}

// ─── B01: string-literal token ranges cover the source span ─────────

void test_string_literal_range_covers_quotes() {
    // A string literal's lexeme is PROCESSED (quotes stripped, escapes resolved),
    // so its range must derive from the recorded source start, not the lexeme
    // width. `x = "héllo"` places the literal at 0-based columns [4, 11): opening
    // quote at col 4, seven codepoints total including both quotes. Deriving the
    // start from the 5-codepoint lexeme would wrongly begin the range at col 6.
    ServiceFixture fx;
    const auto result = fx.analyze("x = \"h\xC3\xA9llo\"\n");

    const luma::Token* str = nullptr;
    for (const auto& tok : result.semantic.tokens) {
        if (tok.type == luma::TokenType::StringLiteral) {
            str = &tok;
            break;
        }
    }
    ASSERT_NE(str, nullptr);

    const Range r = token_range(*str);
    ASSERT_EQ(r.start.line, 0);
    ASSERT_EQ(r.start.character, 4);
    ASSERT_EQ(r.end.line, 0);
    ASSERT_EQ(r.end.character, 11);
}

void test_find_token_at_resolves_inside_string() {
    // A click inside a string literal must resolve to that string token. Before
    // the source-span fix the reconstructed start landed inside the literal (at
    // the 'l'), so a click on the opening quote or first character missed it.
    ServiceFixture fx;
    auto result = fx.analyze("x = \"h\xC3\xA9llo\"\n");

    // 0-based char 5 is the 'h' immediately after the opening quote — inside the
    // true span but outside the old (too-narrow) reconstructed span.
    const auto idx = luma::lsp::find_token_at(result, 0, 5);
    ASSERT_TRUE(idx.has_value());
    ASSERT_EQ(result.semantic.tokens[*idx].type, luma::TokenType::StringLiteral);
}

void test_multiline_string_token_extents_span_lines() {
    // A triple-quoted string spans several source lines: its recorded start (the
    // opening """) and end (the closing """) sit on DIFFERENT lines. The semantic
    // token encoder relies on this to SKIP the token — LSP semantic tokens cannot
    // cross lines, and encoding one would emit a negative deltaStart / oversized
    // length that desynchronises the entire delta-encoded stream. Before the
    // source-span fix, token_extents collapsed the token onto its END line with a
    // start column reconstructed from the dedented, newline-containing lexeme,
    // producing a large negative start column on a single line.
    ServiceFixture fx;
    const auto result = fx.analyze("text = \"\"\"\nhello\nworld\n\"\"\"\n");

    const luma::Token* str = nullptr;
    for (const auto& tok : result.semantic.tokens) {
        if (tok.type == luma::TokenType::StringLiteral) {
            str = &tok;
            break;
        }
    }
    ASSERT_NE(str, nullptr);

    const auto ext = luma::lsp::token_extents(*str);
    // Opening """ on line 0, closing """ on line 3 (0-based).
    ASSERT_EQ(ext.start_line_0based, 0);
    ASSERT_EQ(ext.end_line_0based, 3);
    ASSERT_NE(ext.start_line_0based, ext.end_line_0based);
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_include_absolute_path_rejected);
    RUN(test_include_directory_traversal_rejected);
    RUN(test_include_missing_file_reported);
    RUN(test_include_diagnostic_range_measures_utf16_width);
    RUN(test_include_not_validated_without_uri);
    RUN(test_user_functions_collected);
    RUN(test_record_and_choice_collected);
    RUN(test_find_definition_accessor);
    RUN(test_type_error_produces_error_diagnostic);
    RUN(test_clean_program_has_no_error_diagnostics);
    RUN(test_solaris_program_analyses_without_errors);
    RUN(test_symbols_survive_parse_errors);
    RUN(test_lexer_error_recovery);
    RUN(test_empty_source_is_safe);
    RUN(test_global_occurrences_skip_shadowing_local);
    RUN(test_include_declaration_toggle_excludes_decl);
    RUN(test_declaration_name_range_anchors_on_name);
    RUN(test_declaration_name_range_measures_codepoints);
    RUN(test_diagnostic_column_maps_codepoint_to_utf16);
    RUN(test_string_literal_range_covers_quotes);
    RUN(test_find_token_at_resolves_inside_string);
    RUN(test_multiline_string_token_extents_span_lines);

    return SUMMARY();
}
