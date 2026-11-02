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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/source/source_location.hpp"
#include "common/string_hash.hpp"
#include "json/json.hpp"
#include "lsp_analysis_cache.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_analysis_service_impl.hpp"
#include "lsp_completion_strategy.hpp"
#include "lsp_config.hpp"
#include "lsp_constants.hpp"
#include "lsp_definition_resolver.hpp"
#include "lsp_diagnostic_builder.hpp"
#include "lsp_hover_handler.hpp"
#include "lsp_identifier_collector.hpp"
#include "lsp_optional_ref.hpp"
#include "lsp_path_utils.hpp"
#include "lsp_quickfix_handler.hpp"
#include "lsp_rename_handler.hpp"
#include "lsp_semantic_tokens_handler.hpp"
#include "lsp_stdlib_registry.hpp"
#include "lsp_symbol_handler.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"
#include "protocol/uri_utils.hpp"
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
                                    .notify = [](std::string_view, const JsonValue&) {}}) {}

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

// ─── Include: resolved under a non-ASCII directory ────────────────

// Creates a uniquely named temporary directory (UTF-8 `name` prefix) and
// removes it again on destruction.
struct TempDirectory {
    std::filesystem::path path;

    explicit TempDirectory(std::string_view name)
        : path(std::filesystem::temp_directory_path() /
               luma::lsp::utf8_path(std::format(
                   "{}_{}", name, std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(path);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    TempDirectory(TempDirectory&&) = delete;
    TempDirectory& operator=(TempDirectory&&) = delete;

    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    void write(std::string_view relative, std::string_view content) const {
        const auto file = path / luma::lsp::utf8_path(relative);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out << content;
    }
};

void test_include_resolves_under_non_ascii_directory() {
    ServiceFixture fx;

    // Regression: the document path decoded from its URI is UTF-8, but it was
    // turned into a filesystem path with the ANSI code page on Windows, so
    // every include under e.g. C:\Users\José\ was reported as not found.
    const TempDirectory dir{"luma_lsp_caf\xC3\xA9_\xE5\x90\x8D"};
    dir.write("lib.luma", "function integer helper() {\n    return 1\n}\n");
    const std::string main_path = luma::lsp::path_to_utf8(dir.path / "main.luma");

    const auto result = fx.analyze("include \"lib.luma\"\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    integer x = helper()\n"
                                   "}\n",
                                   luma::protocol::path_to_uri(main_path));

    ASSERT_FALSE(has_code(result, "E4005"));
    ASSERT_TRUE(result.semantic.symbols.user_functions.contains("helper"));
}

void test_include_completion_accepts_backslash_and_unicode() {
    const TempDirectory dir{"luma_lsp_compl\xC3\xA9"};
    dir.write("sub/caf\xC3\xA9.luma", "");
    dir.write("sub/other.txt", "");

    const auto labels = [&](const std::string& partial) {
        std::vector<std::string> out;
        for (const auto& item : luma::lsp::collect_include_completions(dir.path, partial)) {
            out.push_back(item["label"].as_string());
        }
        return out;
    };

    // Both separators descend into `sub`, and the UTF-8 file name survives.
    const auto forward = labels("sub/ca");
    ASSERT_EQ(forward.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(forward.front(), std::string("sub/caf\xC3\xA9.luma"));

    const auto backward = labels("sub\\ca");
    ASSERT_EQ(backward.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(backward.front(), std::string("sub\\caf\xC3\xA9.luma"));
}

// ─── B02/B05: include diagnostic range spans the quoted path in UTF-16 ──

void test_include_diagnostic_range_measures_utf16_width() {
    ServiceFixture fx;

    // The include path contains a two-byte UTF-8 character (é = U+00E9), so its
    // byte length (10) differs from its UTF-16 width (9). The include-diagnostic
    // range must span the path (including its surrounding quotes) in the
    // client's UTF-16 coordinate space — the same space every other diagnostic
    // uses — not by raw byte count, and not by the unquoted keyword-end offset.
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

    // The range must start at the opening quote: right after "include" (7
    // codepoints) plus the separating space (1) = column 8.
    ASSERT_EQ(range.start.character, 8);

    // "café.luma" is 9 codepoints / UTF-16 code units but 10 UTF-8 bytes, so the
    // range width must be 9 (UTF-16) plus 2 for the surrounding quotes = 11,
    // proving both byte length and the unquoted keyword-end anchor are no
    // longer used.
    const int width = range.end.character - range.start.character;
    ASSERT_EQ(width, 11);
    ASSERT_NE(width, 10);
    ASSERT_NE(width, 9);
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

void test_type_error_diagnostic_preserves_late_line_column() {
    ServiceFixture fx;
    const auto result = fx.analyze("function integer add(integer a, integer b) {\n"
                                   "    return a + b\n"
                                   "}\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    string note = \"café\"\n"
                                   "    integer sum = add(1, \"bad\")\n"
                                   "}\n");

    for (const auto& diagnostic : result.semantic.diagnostics) {
        if (diagnostic.severity == severity::error) {
            ASSERT_EQ(diagnostic.range.start.line, 6);
            ASSERT_GT(diagnostic.range.start.character, 0);
            return;
        }
    }
    ASSERT_TRUE(false);
}

void test_type_error_diagnostic_range_covers_token() {
    // Regression: the type checker anchors on AST locations, which point one
    // past their first token, so the squiggle started just after `x` and ran
    // over the following word.  It must cover exactly the token `x`.
    ServiceFixture fx;
    const auto result = fx.analyze("@main\n"
                                   "function void main() {\n"
                                   "    mutable integer x = 1\n"
                                   "    x += 0.5\n"
                                   "    print(x)\n"
                                   "}\n");

    for (const auto& diagnostic : result.semantic.diagnostics) {
        if (diagnostic.severity == severity::error) {
            ASSERT_EQ(diagnostic.range.start.line, 3);
            ASSERT_EQ(diagnostic.range.start.character, 4);
            ASSERT_EQ(diagnostic.range.end.line, 3);
            ASSERT_EQ(diagnostic.range.end.character, 5);
            return;
        }
    }
    ASSERT_TRUE(false);
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

// ─── Rename conflict: renaming to the current name is a no-op ─────

void test_rename_to_same_name_is_not_a_self_conflict() {
    // Renaming a top-level function to its OWN current name must never be
    // reported as a conflict. has_rename_conflict's global-scope check looks
    // up `new_name` in every cached document's `definitions`/`user_functions`
    // maps — but those maps necessarily already contain the name being
    // renamed (it names the very symbol under the cursor), so without an
    // explicit self-rename exemption every no-op rename would be misreported
    // as colliding with itself and rejected.
    ServiceFixture fx;
    const std::string uri = "file:///test/main.luma";
    auto result = fx.analyze("function string greet() {\n"
                             "    return \"hello\"\n"
                             "}\n",
                             uri);

    luma::lsp::LspAnalysisCache cache;
    cache.insert(uri, std::move(result));
    const auto& cached = *cache.find(uri);

    const std::optional<std::string> no_fn;
    ASSERT_FALSE(luma::lsp::has_rename_conflict(cached, /*new_name=*/"greet",
                                                /*target_name=*/"greet", /*is_local=*/false, no_fn,
                                                /*rename_ns=*/"", cache));
}

// ─── Rename conflict: renaming to a distinct existing name conflicts ──

void test_rename_to_distinct_existing_name_is_a_conflict() {
    // Renaming "greet" to "existing", where "existing" already names an
    // unrelated top-level function, must still be flagged as a genuine
    // conflict — the self-rename exemption above must not swallow real
    // collisions with other symbols.
    ServiceFixture fx;
    const std::string uri = "file:///test/main.luma";
    auto result = fx.analyze("function string greet() {\n"
                             "    return \"hello\"\n"
                             "}\n"
                             "\n"
                             "function integer existing() {\n"
                             "    return 0\n"
                             "}\n",
                             uri);

    luma::lsp::LspAnalysisCache cache;
    cache.insert(uri, std::move(result));
    const auto& cached = *cache.find(uri);

    const std::optional<std::string> no_fn;
    ASSERT_TRUE(luma::lsp::has_rename_conflict(cached, /*new_name=*/"existing",
                                               /*target_name=*/"greet", /*is_local=*/false, no_fn,
                                               /*rename_ns=*/"", cache));
    // A genuinely unused new name is still accepted.
    ASSERT_FALSE(luma::lsp::has_rename_conflict(cached, /*new_name=*/"say_hello",
                                                /*target_name=*/"greet", /*is_local=*/false, no_fn,
                                                /*rename_ns=*/"", cache));
}

// ─── Cancellation vs timeout: distinct handling ────────────────────

void test_cancellation_is_quiet_and_flagged() {
    // When a newer edit requests cancellation mid-analysis (routine at startup
    // as documents open in quick succession), the aborted result must be a
    // silent, stale result — flagged for the caller to discard — never a
    // user-facing "timed out" popup or warning diagnostic.
    LspConfig config;
    std::atomic<bool> cancel_flag{true}; // request cancellation up front
    bool notified = false;
    LspAnalysisService service(
        config, cancel_flag,
        AnalysisCallbacks{.log = [](const std::string&) {},
                          .notify =
                              [&](std::string_view method, const JsonValue&) {
                                  if (method == "window/showMessage") {
                                      notified = true;
                                  }
                              }});

    const auto result =
        service.analyze("file:///test/main.luma", "@main\nfunction void main() {\n}\n");

    ASSERT_TRUE(result.metadata.cancelled);
    ASSERT_FALSE(notified);
    ASSERT_FALSE(any_message_contains(result, "timed out"));
}

void test_timeout_warns_and_is_not_flagged_cancelled() {
    // A genuine deadline overrun is a real, user-relevant condition: it must
    // surface the warning popup and diagnostic, and must not be mistaken for a
    // routine cancellation.
    LspConfig config;
    std::atomic<bool> cancel_flag{false};
    bool notified = false;
    LspAnalysisService service(
        config, cancel_flag,
        AnalysisCallbacks{.log = [](const std::string&) {},
                          .notify =
                              [&](std::string_view method, const JsonValue&) {
                                  if (method == "window/showMessage") {
                                      notified = true;
                                  }
                              }});

    // A deadline already in the past trips the timeout branch at the first
    // phase check.
    const auto past = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    const auto result =
        service.analyze("file:///test/main.luma", "@main\nfunction void main() {\n}\n", past);

    ASSERT_FALSE(result.metadata.cancelled);
    ASSERT_TRUE(notified);
    ASSERT_TRUE(any_message_contains(result, "timed out"));
}

// ─── Helpers for the scope / namespace regression tests ───────────

// Index of the `nth` (0-based) Identifier token `name` on 1-based `line`.
[[nodiscard]] std::size_t token_index_of(const AnalysisResult& result, std::string_view name,
                                         int line, int nth = 0) {
    const auto& tokens = result.get_tokens();
    for (std::size_t i{0}; i < tokens.size(); ++i) {
        if (tokens[i].lexeme == name && tokens[i].location.line == line && nth-- == 0) {
            return i;
        }
    }
    luma::test::throw_assertion_error(std::format("no token '{}' on line {}", name, line));
    return 0;
}

// Sorted 1-based lines of the given occurrence token indices, comma-joined.
[[nodiscard]] std::string occurrence_lines(const AnalysisResult& result,
                                           const std::vector<std::size_t>& indices) {
    std::vector<int> lines;
    for (const std::size_t idx : indices) {
        lines.push_back(result.get_tokens()[idx].location.line);
    }
    std::ranges::sort(lines);
    std::string joined;
    for (const int line : lines) {
        joined += (joined.empty() ? "" : ",") + std::to_string(line);
    }
    return joined;
}

[[nodiscard]] std::vector<std::size_t> occurrences(const AnalysisResult& result,
                                                   const std::string& name, std::string_view ns,
                                                   bool is_local,
                                                   const std::optional<std::string>& enclosing,
                                                   bool include_declaration = true) {
    const std::string uri = "file:///test/main.luma";
    return collect_scoped_occurrences(
        result, name, uri,
        ScopedOccurrenceFilter{.namespace_prefix = ns,
                               .is_local = is_local,
                               .enclosing_function = enclosing,
                               .origin_uri = uri,
                               .include_declaration = include_declaration});
}

// ─── Function and block scopes end at their closing brace ─────────

void test_function_range_covers_trailing_multiline_block() {
    ServiceFixture fx;

    // Regression: the body range ended one line after the *start* of the last
    // statement, so a trailing multi-line `if` cut `main` off at line 5. A
    // local rename from line 3 then missed line 6, and one started on line 6
    // found no enclosing function and renamed `total` in `other` too.
    const auto result = fx.analyze("@main\n"
                                   "function void main() {\n"        // 2
                                   "    integer total = 1\n"         // 3
                                   "    if total > 0 {\n"            // 4
                                   "        integer inner = total\n" // 5
                                   "        integer again = total\n" // 6
                                   "    }\n"                         // 7
                                   "}\n"                             // 8
                                   "\n"
                                   "function void other() {\n"  // 10
                                   "    integer total = 2\n"    // 11
                                   "    integer copy = total\n" // 12
                                   "}\n");

    const auto& ranges = result.semantic.functions.function_body_ranges;
    ASSERT_TRUE(ranges.contains("main"));
    ASSERT_EQ(ranges.at("main").first, 2);
    ASSERT_EQ(ranges.at("main").second, 8);
    ASSERT_EQ(ranges.at("other").second, 13);
    ASSERT_EQ(luma::lsp::find_enclosing_function(result, 6).value_or(""), std::string("main"));

    // The block-local `inner` is scoped to the `if` block, which closes on line 7.
    const auto& inner = result.semantic.locals.scoped_locals.at("main").at("inner");
    ASSERT_EQ(inner.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(inner.front().scope_end_line, 7);

    const std::optional<std::string> main_fn{"main"};
    ASSERT_EQ(occurrence_lines(result, occurrences(result, "total", "", true, main_fn)),
              std::string("3,4,5,6"));
}

void test_unclosed_function_keeps_a_body_range() {
    ServiceFixture fx;

    // While the closing brace is still missing the range falls back to the
    // statements, and must not borrow the next function's block.
    const auto result = fx.analyze("function void first() {\n" // 1
                                   "    integer a = 1\n"       // 2
                                   "    integer b = a\n"       // 3
                                   "function void second() {\n"
                                   "}\n");

    const auto& ranges = result.semantic.functions.function_body_ranges;
    ASSERT_TRUE(ranges.contains("first"));
    ASSERT_GE(ranges.at("first").second, 3);
    ASSERT_LT(ranges.at("first").second, 5);
}

// ─── Namespaced rename / references ───────────────────────────────

void test_namespace_member_occurrences() {
    ServiceFixture fx;

    // Regression: occurrences of `Geo.area` were only those textually written
    // `Geo.area`, so renaming from a call skipped the declaration (line 2) and
    // renaming from the declaration skipped every qualified call. A bare
    // `area(r)` inside the namespace (line 6) is NOT the member: Luma requires
    // `Geo.area` or `use` there too, so it binds to the top-level `area`.
    const auto result = fx.analyze("namespace Geo {\n"
                                   "    function number area(number r) {\n" // 2
                                   "        return r * r\n"
                                   "    }\n"
                                   "    function number twice(number r) {\n"
                                   "        return area(r) * 2.0\n" // 6
                                   "    }\n"
                                   "}\n"
                                   "\n"
                                   "function number area(number r) {\n" // 10
                                   "    return r\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    number a = Geo.area(1.0)\n" // 16
                                   "    number b = area(2.0)\n"     // 17
                                   "}\n");

    using luma::lsp::target_namespace_prefix;
    ASSERT_EQ(target_namespace_prefix(result, token_index_of(result, "area", 2)),
              std::string("Geo"));
    ASSERT_EQ(target_namespace_prefix(result, token_index_of(result, "area", 6)), std::string());
    ASSERT_EQ(target_namespace_prefix(result, token_index_of(result, "area", 16)),
              std::string("Geo"));
    ASSERT_EQ(target_namespace_prefix(result, token_index_of(result, "area", 10)), std::string());

    const std::optional<std::string> no_fn;
    ASSERT_EQ(occurrence_lines(result, occurrences(result, "area", "Geo", false, no_fn)),
              std::string("2,16"));
    ASSERT_EQ(occurrence_lines(result, occurrences(result, "area", "", false, no_fn)),
              std::string("6,10,17"));
    ASSERT_EQ(occurrence_lines(result, occurrences(result, "area", "Geo", false, no_fn, false)),
              std::string("16"));
    ASSERT_EQ(occurrence_lines(result, occurrences(result, "area", "", false, no_fn, false)),
              std::string("6,17"));
}

void test_use_imported_names_bind_to_the_namespace() {
    ServiceFixture fx;

    // `use Geo` makes the bare `area` (line 11) refer to `Geo.area`.
    const auto result = fx.analyze("namespace Geo {\n"
                                   "    function number area(number r) {\n" // 2
                                   "        return r * r\n"
                                   "    }\n"
                                   "}\n"
                                   "\n"
                                   "use Geo\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    number a = area(1.0)\n"     // 11
                                   "    number b = Geo.area(2.0)\n" // 12
                                   "}\n");

    using luma::lsp::target_namespace_prefix;
    ASSERT_EQ(target_namespace_prefix(result, token_index_of(result, "area", 11)),
              std::string("Geo"));

    const std::optional<std::string> no_fn;
    ASSERT_EQ(occurrence_lines(result, occurrences(result, "area", "Geo", false, no_fn)),
              std::string("2,11,12"));
    ASSERT_EQ(occurrence_lines(result, occurrences(result, "area", "", false, no_fn)),
              std::string());
}

// ─── "Add 'mutable'" quick fix ────────────────────────────────────

void test_mutable_insertion_goes_before_the_type() {
    ServiceFixture fx;

    // Regression: the fix looked for `name :` (a named argument, not a Luma
    // declaration) and inserted `mutable` there. It must insert before the
    // type of the type-first declaration instead.
    const auto result = fx.analyze("function void report(integer count) {\n"
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    array<integer> items = [1]\n"                        // 6
                                   "    integer count = 0\n"                                 // 7
                                   "    function(integer) -> integer f = (integer x) -> x\n" // 8
                                   "    mutable integer done = 0\n"                          // 9
                                   "    report(count: count)\n"                              // 10
                                   "    count = count + 1\n"                                 // 11
                                   "    items = [2]\n"                                       // 12
                                   "    f = (integer y) -> y\n"                              // 13
                                   "    done = 1\n"                                          // 14
                                   "}\n");

    using luma::lsp::mutable_insertion_position;
    const auto count_pos = mutable_insertion_position(result, "count", 11);
    ASSERT_TRUE(count_pos.has_value());
    ASSERT_EQ(count_pos->line, 6);
    ASSERT_EQ(count_pos->character, 4);

    const auto items_pos = mutable_insertion_position(result, "items", 12);
    ASSERT_TRUE(items_pos.has_value());
    ASSERT_EQ(items_pos->line, 5);
    ASSERT_EQ(items_pos->character, 4);

    const auto f_pos = mutable_insertion_position(result, "f", 13);
    ASSERT_TRUE(f_pos.has_value());
    ASSERT_EQ(f_pos->line, 7);
    ASSERT_EQ(f_pos->character, 4);

    ASSERT_FALSE(mutable_insertion_position(result, "done", 14).has_value());
}

// ─── Semantic tokens ──────────────────────────────────────────────

struct DecodedToken {
    int line;
    int column;
    int length;
    int type;
};

// Decode LSP delta-encoded semantic token data into absolute positions,
// asserting the stream is well formed (sorted, non-overlapping, positive).
[[nodiscard]] std::vector<DecodedToken> decode_semantic_tokens(const std::vector<int64_t>& data) {
    ASSERT_EQ(data.size() % 5, static_cast<std::size_t>(0));
    std::vector<DecodedToken> out;
    int line = 0;
    int column = 0;
    int previous_end = 0;
    for (std::size_t i{0}; i < data.size(); i += 5) {
        ASSERT_GE(data[i], 0);
        if (data[i] > 0) {
            line += static_cast<int>(data[i]);
            column = static_cast<int>(data[i + 1]);
            previous_end = 0;
        } else {
            column += static_cast<int>(data[i + 1]);
        }
        ASSERT_GE(column, previous_end);
        ASSERT_GT(data[i + 2], 0);
        out.push_back({line, column, static_cast<int>(data[i + 2]), static_cast<int>(data[i + 3])});
        previous_end = column + static_cast<int>(data[i + 2]);
    }
    return out;
}

[[nodiscard]] std::optional<DecodedToken> semantic_token_at(const std::vector<DecodedToken>& tokens,
                                                            int line, int column) {
    for (const auto& tok : tokens) {
        if (tok.line == line && tok.column == column) {
            return tok;
        }
    }
    return std::nullopt;
}

void test_semantic_tokens_types_strings_and_multiline() {
    ServiceFixture fx;
    luma::lsp::StdlibRegistry registry;
    registry.init();

    const auto result = fx.analyze("@main\n"
                                   "function void main() {\n"          // 1
                                   "    string text = \"\"\"\n"        // 2
                                   "        one\n"                     // 3
                                   "        \"\"\"\n"                  // 4
                                   "    string name = \"Ada\"\n"       // 5
                                   "    string hi = \"Hi ${name}!\"\n" // 6
                                   "    integer set = 1\n"             // 7
                                   "    boolean ok = true\n"           // 8
                                   "}\n");
    const auto tokens =
        decode_semantic_tokens(luma::lsp::encode_document_semantic_tokens(result, registry));
    constexpr int k_type = static_cast<int>(luma::lsp::SemanticTokenType::Type);
    constexpr int k_string = static_cast<int>(luma::lsp::SemanticTokenType::String);
    constexpr int k_variable = static_cast<int>(luma::lsp::SemanticTokenType::Variable);

    // Regression: `void` lexes as an identifier and was coloured as a variable.
    const auto void_tok = semantic_token_at(tokens, 1, 9);
    ASSERT_TRUE(void_tok.has_value());
    ASSERT_EQ(void_tok->type, k_type);

    // Regression: a triple-quoted string was dropped; now one segment per line.
    const auto open = semantic_token_at(tokens, 2, 18);
    ASSERT_TRUE(open.has_value());
    ASSERT_EQ(open->type, k_string);
    ASSERT_EQ(open->length, 3);
    const auto body = semantic_token_at(tokens, 3, 0);
    ASSERT_TRUE(body.has_value());
    ASSERT_EQ(body->length, 11);
    const auto close = semantic_token_at(tokens, 4, 0);
    ASSERT_TRUE(close.has_value());
    ASSERT_EQ(close->length, 11);

    // Regression: the literal segments of an interpolated string were dropped.
    const auto start_segment = semantic_token_at(tokens, 6, 16);
    ASSERT_TRUE(start_segment.has_value());
    ASSERT_EQ(start_segment->type, k_string);
    ASSERT_EQ(start_segment->length, 4); // `"Hi `
    const auto end_segment = semantic_token_at(tokens, 6, 27);
    ASSERT_TRUE(end_segment.has_value());
    ASSERT_EQ(end_segment->type, k_string);

    // A demoted type name used as a variable stays a variable.
    const auto set_tok = semantic_token_at(tokens, 7, 12);
    ASSERT_TRUE(set_tok.has_value());
    ASSERT_EQ(set_tok->type, k_variable);

    // Word literals keep the grammar's constant.language colour.
    ASSERT_FALSE(semantic_token_at(tokens, 8, 17).has_value());
}

void test_semantic_tokens_type_names_only_in_type_positions() {
    ServiceFixture fx;
    luma::lsp::StdlibRegistry registry;
    registry.init();

    // Regression (review): `task`, `set`, … are also valid field names, so a
    // field `integer task` and the member in `job.task` must not be coloured
    // as types, while `set<integer>` and `-> void` are.
    const auto result = fx.analyze("record Job {\n"
                                   "    integer task\n" // 1
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"               // 5
                                   "    Job job = Job { task = 1 }\n"       // 6
                                   "    integer n = job.task\n"             // 7
                                   "    set<integer> seen = Set.new()\n"    // 8
                                   "    function() -> void f = () -> { }\n" // 9
                                   "}\n");
    const auto tokens =
        decode_semantic_tokens(luma::lsp::encode_document_semantic_tokens(result, registry));
    constexpr int k_type = static_cast<int>(luma::lsp::SemanticTokenType::Type);

    const auto is_type_at = [&](int line, int column) {
        const auto tok = semantic_token_at(tokens, line, column);
        return tok.has_value() && tok->type == k_type;
    };
    ASSERT_FALSE(is_type_at(1, 12)); // field declaration `integer task`
    ASSERT_FALSE(is_type_at(6, 20)); // record literal `{ task = 1 }`
    ASSERT_FALSE(is_type_at(7, 20)); // member access `job.task`
    ASSERT_TRUE(is_type_at(8, 4));   // `set<integer>`
    ASSERT_TRUE(is_type_at(9, 18));  // `-> void`
    ASSERT_TRUE(is_type_at(5, 9));   // `function void main`
}

// ─── Document symbols: member ranges ──────────────────────────────

void test_document_symbol_members_have_own_ranges() {
    ServiceFixture fx;

    // Regression: every record field and choice variant claimed its parent's
    // whole range, so outline "follow cursor" always picked the last member.
    const auto result = fx.analyze("record Point {\n"
                                   "    number x,\n" // 1
                                   "    number y\n"  // 2
                                   "}\n"
                                   "choice Color { Red, Green }\n" // 4
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "}\n");
    ASSERT_TRUE(result.metadata.cached_program.has_value());
    const auto symbols = luma::lsp::LspSymbolHandler::build_document_symbols(
        result.metadata.cached_program->declarations, result.get_tokens());

    const auto find = [&](std::string_view name) -> const luma::lsp::DocumentSymbol& {
        for (const auto& sym : symbols) {
            if (sym.name == name) {
                return sym;
            }
        }
        luma::test::throw_assertion_error(std::format("no symbol '{}'", name));
        return symbols.front();
    };

    const auto& point = find("Point");
    ASSERT_EQ(point.range.start.character, 0); // anchored at `record`, not after it
    ASSERT_EQ(point.children.size(), static_cast<std::size_t>(2));
    const auto& x = point.children[0];
    ASSERT_EQ(x.range.start.line, 1);
    ASSERT_EQ(x.range.start.character, 4); // `number x`
    ASSERT_EQ(x.range.end.character, 12);
    ASSERT_EQ(x.selection_range.start.character, 11);
    ASSERT_EQ(point.children[1].range.start.line, 2);

    // Members sharing a line each get just their name.
    const auto& color = find("Color");
    ASSERT_EQ(color.children.size(), static_cast<std::size_t>(2));
    for (const auto& variant : color.children) {
        ASSERT_EQ(variant.range.start.character, variant.selection_range.start.character);
        ASSERT_EQ(variant.range.end.character, variant.selection_range.end.character);
    }
}

// ─── Go-to-definition on a local that shadows a global ────────────

[[nodiscard]] std::optional<luma::lsp::Location> definition_at(const AnalysisResult& result,
                                                               std::size_t token_idx) {
    // DefinitionResolver keeps a reference to the URI, so it must outlive it.
    const std::string uri = "file:///test/main.luma";
    const luma::StringMap<AnalysisResult> no_other_results;
    const luma::lsp::DefinitionResolver resolver(
        uri, result, result.get_tokens(),
        [](const std::string&) { return luma::lsp::optional_ref<const AnalysisResult>{}; },
        [](const std::vector<luma::Token>&, const luma::SourceLocation&) { return Range{}; },
        no_other_results);
    const auto& tok = result.get_tokens()[token_idx];
    return resolver.resolve(tok.lexeme, tok, token_idx);
}

void test_definition_prefers_shadowing_local() {
    ServiceFixture fx;

    // Regression: locals and parameters were never consulted, so a local named
    // like a top-level function jumped to the function.
    const auto result = fx.analyze("function integer value() {\n"
                                   "    return 42\n"
                                   "}\n"
                                   "\n"
                                   "function integer twice(integer value) {\n" // 5
                                   "    return value * 2\n"                    // 6
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    integer value = 5\n"    // 11
                                   "    integer copy = value\n" // 12
                                   "    integer other = twice(copy)\n"
                                   "}\n");

    const auto local = definition_at(result, token_index_of(result, "value", 12));
    ASSERT_TRUE(local.has_value());
    ASSERT_EQ(local->range.start.line, 10);
    ASSERT_EQ(local->range.start.character, 12);

    const auto param = definition_at(result, token_index_of(result, "value", 6));
    ASSERT_TRUE(param.has_value());
    ASSERT_EQ(param->range.start.line, 4);
    ASSERT_EQ(param->range.start.character, 31);

    // A call to the top-level function still resolves to its declaration.
    const auto global = definition_at(result, token_index_of(result, "twice", 13));
    ASSERT_TRUE(global.has_value());
    ASSERT_EQ(global->range.start.line, 4);
}

void test_definition_picks_the_binding_before_the_cursor() {
    ServiceFixture fx;

    // Regression (review): the first occurrence in the function was taken as
    // the declaration, so a call to the top-level `value()` made before a
    // local `value` is declared resolved to the call itself, and later uses
    // resolved to that call. A binding in a block that already closed (line 6)
    // and a lambda parameter (line 8) are covered too.
    const auto result =
        fx.analyze("function integer value() {\n" // 1
                   "    return 42\n"
                   "}\n"
                   "\n"
                   "@main\n"
                   "function void main() {\n"
                   "    integer first = value()\n" // 7
                   "    integer value = 5\n"       // 8
                   "    if first > 0 {\n"
                   "        integer shadow = value\n" // 10
                   "    }\n"
                   "    integer copy = value\n"                                          // 12
                   "    function(integer) -> integer f = (integer value) -> value + 1\n" // 13
                   "}\n");

    const auto call = definition_at(result, token_index_of(result, "value", 7));
    ASSERT_TRUE(call.has_value());
    ASSERT_EQ(call->range.start.line, 0); // the top-level function

    const auto later = definition_at(result, token_index_of(result, "value", 12));
    ASSERT_TRUE(later.has_value());
    ASSERT_EQ(later->range.start.line, 7);
    ASSERT_EQ(later->range.start.character, 12);

    const auto nested = definition_at(result, token_index_of(result, "value", 10));
    ASSERT_TRUE(nested.has_value());
    ASSERT_EQ(nested->range.start.line, 7);

    // `(integer value) -> value + 1`: the body's `value` is the lambda parameter.
    const auto lambda_body = definition_at(result, token_index_of(result, "value", 13, 1));
    ASSERT_TRUE(lambda_body.has_value());
    ASSERT_EQ(lambda_body->range.start.line, 12);
    ASSERT_EQ(lambda_body->range.start.character, 46);
}

void test_binding_occurrence_forms() {
    ServiceFixture fx;
    const auto result = fx.analyze("@main\n"
                                   "function void main() {\n"
                                   "    array<integer> items = [1, 2]\n"  // 3
                                   "    for index, item in items {\n"     // 4
                                   "        print(\"${index}${item}\")\n" // 5
                                   "    }\n"
                                   "    result<integer> parsed = success(1)\n" // 7
                                   "    match parsed {\n"
                                   "        case success(value) { print(\"${value}\") }\n" // 9
                                   "        case failure(error) { print(error) }\n"        // 10
                                   "    }\n"
                                   "}\n");
    const auto& tokens = result.get_tokens();
    using luma::lsp::is_binding_occurrence;
    ASSERT_TRUE(is_binding_occurrence(tokens, token_index_of(result, "items", 3)));
    ASSERT_FALSE(is_binding_occurrence(tokens, token_index_of(result, "items", 4)));
    ASSERT_TRUE(is_binding_occurrence(tokens, token_index_of(result, "index", 4)));
    ASSERT_TRUE(is_binding_occurrence(tokens, token_index_of(result, "item", 4)));
    ASSERT_TRUE(is_binding_occurrence(tokens, token_index_of(result, "value", 9)));
    ASSERT_TRUE(is_binding_occurrence(tokens, token_index_of(result, "error", 10)));
    ASSERT_FALSE(is_binding_occurrence(tokens, token_index_of(result, "error", 10, 1)));
    ASSERT_FALSE(is_binding_occurrence(tokens, token_index_of(result, "parsed", 8)));
}

// ─── Hover ────────────────────────────────────────────────────────

[[nodiscard]] std::string hover_at(const AnalysisResult& result, std::string_view name, int line,
                                   int nth = 0) {
    luma::lsp::StdlibRegistry registry;
    registry.init();
    return luma::lsp::resolve_hover_text(result, token_index_of(result, name, line, nth), registry);
}

void test_hover_resolves_user_symbols() {
    ServiceFixture fx;

    // Regression: only the first hover resolver ever ran, so user symbols had
    // no hover; and (review) once they ran, the member in `p.x` showed an
    // unrelated local `x`, and a local shadowing a top-level function showed
    // the function.
    const auto result = fx.analyze("record Point {\n" // 1
                                   "    number x,\n"
                                   "    number y\n"
                                   "}\n"
                                   "\n"
                                   "function integer value(integer seed) {\n" // 6
                                   "    return seed\n"                        // 7
                                   "}\n"
                                   "\n"
                                   "@main\n"
                                   "function void main() {\n"
                                   "    integer first = value(1)\n"             // 12
                                   "    mutable integer value = 2\n"            // 13
                                   "    string x = \"local\"\n"                 // 14
                                   "    Point p = Point { x = 1.0, y = 2.0 }\n" // 15
                                   "    number px = p.x\n"                      // 16
                                   "    value = value + first\n"                // 17
                                   "}\n");

    const auto block = [](std::string_view code) { return std::format("```luma\n{}\n```", code); };
    ASSERT_EQ(hover_at(result, "Point", 1),
              block("record Point {\n    number x,\n    number y\n}"));
    ASSERT_EQ(hover_at(result, "value", 6), block("function integer value(integer seed)"));
    ASSERT_EQ(hover_at(result, "seed", 7), block("(parameter) integer seed"));
    ASSERT_EQ(hover_at(result, "value", 12), block("function integer value(integer seed)"));
    ASSERT_EQ(hover_at(result, "value", 17), block("mutable integer value"));
    ASSERT_EQ(hover_at(result, "x", 16), block("(field) number x"));
    ASSERT_EQ(hover_at(result, "x", 14), block("string x"));
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_include_absolute_path_rejected);
    RUN(test_include_directory_traversal_rejected);
    RUN(test_include_missing_file_reported);
    RUN(test_include_resolves_under_non_ascii_directory);
    RUN(test_include_completion_accepts_backslash_and_unicode);
    RUN(test_include_diagnostic_range_measures_utf16_width);
    RUN(test_include_not_validated_without_uri);
    RUN(test_user_functions_collected);
    RUN(test_record_and_choice_collected);
    RUN(test_find_definition_accessor);
    RUN(test_type_error_produces_error_diagnostic);
    RUN(test_type_error_diagnostic_preserves_late_line_column);
    RUN(test_type_error_diagnostic_range_covers_token);
    RUN(test_clean_program_has_no_error_diagnostics);
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
    RUN(test_rename_to_same_name_is_not_a_self_conflict);
    RUN(test_rename_to_distinct_existing_name_is_a_conflict);
    RUN(test_cancellation_is_quiet_and_flagged);
    RUN(test_timeout_warns_and_is_not_flagged_cancelled);
    RUN(test_function_range_covers_trailing_multiline_block);
    RUN(test_unclosed_function_keeps_a_body_range);
    RUN(test_namespace_member_occurrences);
    RUN(test_use_imported_names_bind_to_the_namespace);
    RUN(test_mutable_insertion_goes_before_the_type);
    RUN(test_definition_prefers_shadowing_local);
    RUN(test_definition_picks_the_binding_before_the_cursor);
    RUN(test_binding_occurrence_forms);
    RUN(test_hover_resolves_user_symbols);
    RUN(test_semantic_tokens_types_strings_and_multiline);
    RUN(test_semantic_tokens_type_names_only_in_type_positions);
    RUN(test_document_symbol_members_have_own_ranges);

    return SUMMARY();
}
