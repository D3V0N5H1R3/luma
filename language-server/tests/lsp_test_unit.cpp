// LSP unit tests — config, JSON, workspace indexer, symbol resolver, path safety.

#include <filesystem>
#include <system_error>

#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_keyword_catalog.hpp"
#include "lsp_param_utils.hpp"
#include "lsp_path_utils.hpp"
#include "lsp_server.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_test_helpers.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_workspace_indexer.hpp"

using luma::SourceLocation;
using luma::Token;
using luma::TokenType;

namespace {

// ─── JSON parsing edge cases ───────────────────────────────────────

void test_json_rejects_trailing_decimal() {
    ASSERT_THROWS(JsonValue::parse("1."));
}

void test_json_rejects_trailing_exponent() {
    ASSERT_THROWS(JsonValue::parse("1e"));
}

void test_json_duplicate_keys_throws() {
    ASSERT_THROWS(JsonValue::parse(R"({"a": 1, "a": 2})"));
}

void test_json_rejects_leading_zeros() {
    ASSERT_THROWS(JsonValue::parse("007"));
    ASSERT_THROWS(JsonValue::parse("00"));

    // "0" alone is valid.
    const auto zero = JsonValue::parse("0");
    ASSERT_EQ(zero.as_integer(), 0);

    // "0.5" is valid (leading zero only before decimal).
    const auto half = JsonValue::parse("0.5");
    ASSERT_TRUE(half.is_number());
}

// ─── LspConfig tests ──────────────────────────────────────────────

void test_config_defaults() {
    const LspConfig config;
    const auto snap = config.get();
    ASSERT_TRUE(snap->inlay_hints_enabled);
    ASSERT_TRUE(snap->code_lens_enabled);
    ASSERT_EQ(snap->analysis_debounce_ms, 50);
    ASSERT_EQ(snap->analysis_timeout_ms, 10000);
}

void test_config_apply_lsp_settings() {
    LspConfig config;
    const auto settings = JsonValue::parse(R"({
        "luma": {
            "inlayHints": { "enabled": false },
            "codeLens": { "enabled": false },
            "analysisDebounceMs": 200
        }
    })");
    config.apply_lsp_settings(settings);
    const auto snap = config.get();
    ASSERT_FALSE(snap->inlay_hints_enabled);
    ASSERT_FALSE(snap->code_lens_enabled);
    ASSERT_EQ(snap->analysis_debounce_ms, 200);
}

void test_config_apply_project_config() {
    LspConfig config;
    const auto project = JsonValue::parse(R"({
        "inlayHints": { "enabled": false },
        "analysisTimeoutMs": 5000
    })");
    config.apply_project_config(project);
    const auto snap = config.get();
    ASSERT_FALSE(snap->inlay_hints_enabled);
    ASSERT_EQ(snap->analysis_timeout_ms, 5000);
}

void test_config_rejects_out_of_range() {
    LspConfig config;
    const auto settings = JsonValue::parse(R"({ "luma": { "analysisDebounceMs": 9999 } })");
    config.apply_lsp_settings(settings);
    ASSERT_EQ(config.get()->analysis_debounce_ms, 50); // unchanged
}

// ─── WorkspaceIndexer tests ────────────────────────────────────────

void test_workspace_indexer_is_in_workspace() {
    const std::vector<std::string> roots = {"C:\\projects\\myapp"};
    ASSERT_TRUE(WorkspaceIndexer::is_in_workspace("C:\\projects\\myapp\\src\\main.luma", roots));
    ASSERT_FALSE(WorkspaceIndexer::is_in_workspace("C:\\projects\\myapp_backup\\x.luma", roots));
    ASSERT_FALSE(WorkspaceIndexer::is_in_workspace("C:\\other\\file.luma", roots));
}

// ─── SymbolResolver tests ──────────────────────────────────────────

void test_find_token_at_basic() {
    AnalysisResult result;

    result.semantic.tokens.push_back(
        Token{.type = TokenType::Identifier,
              .lexeme = "foo",
              .location = SourceLocation{.file_id = 0, .line = 1, .column = 4}});
    result.metadata.token_index.push_back(TokenIndexEntry{1, 1, 4, 0});
    result.metadata.line_index.build(result.semantic.tokens);

    // LSP position (0-based): line=0, character=1 → Luma line=1, col=2 → inside [1, 4).
    const auto idx = find_token_at(result, 0, 1);
    ASSERT_TRUE(idx.has_value());
    ASSERT_EQ(*idx, static_cast<std::size_t>(0));

    // Outside range.
    const auto none = find_token_at(result, 0, 5);
    ASSERT_FALSE(none.has_value());
}

void test_is_local_variable_check() {
    AnalysisResult result;
    result.semantic.locals.local_variable_types["x"] = "integer";
    result.semantic.symbols.definitions["y"] = SymbolDefinition{{}, "integer", false};

    ASSERT_TRUE(is_local_variable(result, "x"));
    ASSERT_FALSE(is_local_variable(result, "y"));
    ASSERT_FALSE(is_local_variable(result, "z"));
}

void test_find_enclosing_function_basic() {
    AnalysisResult result;
    result.semantic.functions.sorted_function_ranges.push_back({2, 10, "foo"});
    result.semantic.functions.sorted_function_ranges.push_back({15, 20, "bar"});

    const auto fn = find_enclosing_function(result, 5);
    ASSERT_TRUE(fn.has_value());
    ASSERT_EQ(*fn, std::string("foo"));

    const auto fn2 = find_enclosing_function(result, 17);
    ASSERT_TRUE(fn2.has_value());
    ASSERT_EQ(*fn2, std::string("bar"));

    const auto none = find_enclosing_function(result, 12);
    ASSERT_FALSE(none.has_value());
}

// ─── Keyword catalog tests ─────────────────────────────────────────

void test_keyword_catalog_matches_lexer() {
    const auto names = reserved_keyword_names();
    ASSERT_FALSE(names.empty());

    for (const auto name : names) {
        // The LSP must agree that the name is reserved.
        ASSERT_TRUE(is_reserved_keyword_name(name));

        // The lexer must tokenize the bare name as a keyword token (and not
        // as an identifier).  This catches drift where the catalog lists a
        // name the lexer no longer treats as a keyword, or vice versa.
        luma::DiagnosticCollector diagnostics;
        luma::Lexer lexer{std::string(name), diagnostics};
        const auto tokens = lexer.tokenize();
        ASSERT_FALSE(tokens.empty());
        ASSERT_TRUE(luma::is_keyword_token_type(tokens.front().type));
        ASSERT_NE(tokens.front().type, luma::TokenType::Identifier);
    }
}

// ─── Include path safety tests ─────────────────────────────────────

void test_is_safe_include_path_accepts_relative() {
    ASSERT_TRUE(is_safe_include_path("file.luma"));
    ASSERT_TRUE(is_safe_include_path("sub/file.luma"));
    ASSERT_TRUE(is_safe_include_path("a/b/c.luma"));
}

void test_is_safe_include_path_rejects_traversal() {
    // Directory traversal must be rejected regardless of platform separators.
    ASSERT_FALSE(is_safe_include_path("../secret.luma"));
    ASSERT_FALSE(is_safe_include_path("a/../b.luma"));
    ASSERT_FALSE(is_safe_include_path("../../etc/passwd.luma"));
}

void test_is_safe_include_path_rejects_absolute() {
#ifdef _WIN32
    ASSERT_FALSE(is_safe_include_path("C:\\Windows\\system.luma"));
#else
    ASSERT_FALSE(is_safe_include_path("/etc/passwd"));
#endif
}

void test_is_safe_resolved_path_accepts_regular_file() {
    const TempFile file{"@main\nfunction main() {}\n"};
    ASSERT_TRUE(is_safe_resolved_path(file.path()));
}

void test_is_safe_resolved_path_rejects_symlink() {
    const TempDir dir;
    const auto target = dir.path() / "target.luma";
    const auto link = dir.path() / "link.luma";
    {
        const TempFile keep{target, "@main\nfunction main() {}\n"};

        // Symlink creation needs elevated privileges / developer mode on some
        // platforms; skip the assertion when it is unavailable rather than fail.
        std::error_code ec;
        std::filesystem::create_symlink(target, link, ec);
        if (!ec) {
            ASSERT_FALSE(is_safe_resolved_path(link));
            std::error_code remove_ec;
            std::filesystem::remove(link, remove_ec);
        }
    }
}

// ─── B08: rename identifier validation accepts Unicode ─────────────

void test_is_valid_identifier_accepts_ascii_and_unicode() {
    using luma::lsp::util::is_valid_identifier;

    // ASCII identifiers, including a leading underscore.
    ASSERT_TRUE(is_valid_identifier("foo"));
    ASSERT_TRUE(is_valid_identifier("_x"));
    ASSERT_TRUE(is_valid_identifier("value2"));

    // Unicode identifiers — the lexer admits UTF-8 lead/continuation bytes in
    // identifiers, so rename validation must too (B08). café / π / 名前.
    ASSERT_TRUE(is_valid_identifier("caf\xC3\xA9"));
    ASSERT_TRUE(is_valid_identifier("\xCF\x80"));
    ASSERT_TRUE(is_valid_identifier("\xE5\x90\x8D\xE5\x89\x8D"));
}

void test_is_valid_identifier_rejects_malformed() {
    using luma::lsp::util::is_valid_identifier;

    ASSERT_FALSE(is_valid_identifier(""));     // empty
    ASSERT_FALSE(is_valid_identifier("1abc")); // leading digit
    ASSERT_FALSE(is_valid_identifier("a-b"));  // hyphen
    ASSERT_FALSE(is_valid_identifier("a b"));  // space
    ASSERT_FALSE(is_valid_identifier("a.b"));  // dot
}

// ─── Parameter-name extraction (inlay hints) ───────────────────────

void test_extract_param_name_strips_type() {
    const auto name = luma::lsp::util::extract_param_name("count: integer");

    ASSERT_TRUE(name.has_value());
    ASSERT_EQ(*name, "count");
}

void test_extract_param_name_trims_space_before_colon() {
    const auto name = luma::lsp::util::extract_param_name("value : number");

    ASSERT_TRUE(name.has_value());
    ASSERT_EQ(*name, "value");
}

void test_extract_param_name_without_colon_is_nullopt() {
    ASSERT_FALSE(luma::lsp::util::extract_param_name("bareword").has_value());
}

void test_extract_param_name_from_split_signature() {
    const auto parts = luma::lsp::util::split_param_list("(text: string, items: array<T>)");

    ASSERT_EQ(parts.size(), 2U);
    ASSERT_EQ(*luma::lsp::util::extract_param_name(parts[0]), "text");
    ASSERT_EQ(*luma::lsp::util::extract_param_name(parts[1]), "items");
}

// ─── Position encoding: codepoint ↔ UTF-16 (LS-15) ─────────────────
//
// The Luma lexer records token columns as codepoint indices, but the LSP wire
// protocol (utf-16 position encoding) expects UTF-16 code-unit indices. The two
// agree for every BMP character and diverge on supplementary-plane characters
// (4-byte UTF-8 = a UTF-16 surrogate pair = 2 code units), e.g. the emoji
// U+1F600 (😀, bytes F0 9F 98 80). These tests pin the encoder and the two wire
// boundary call sites: find_token_at (incoming) and to_wire (outgoing).

void test_position_encoder_bmp_and_supplementary() {
    // Line "é😀x": é (U+00E9, 2 bytes, 1 UTF-16 unit), 😀 (U+1F600, 4 bytes,
    // 2 UTF-16 units), x (1 byte, 1 unit). Built by concatenation so the \x80
    // escape cannot greedily consume the following ASCII byte.
    const std::string source = std::string("\xC3\xA9") + "\xF0\x9F\x98\x80" + "x";
    const std::vector<std::size_t> line_starts = {0};
    const PositionEncoder enc{&source, &line_starts};
    ASSERT_TRUE(enc.valid());

    // Codepoint → UTF-16. col 0 (é) stays 0; col 1 (😀) stays 1 because é is a
    // single UTF-16 unit; col 2 (x) becomes 3 because the emoji is two units.
    ASSERT_EQ(enc.to_utf16(0, 0), 0);
    ASSERT_EQ(enc.to_utf16(0, 1), 1);
    ASSERT_EQ(enc.to_utf16(0, 2), 3);

    // UTF-16 → codepoint (the inverse direction).
    ASSERT_EQ(enc.to_codepoint(0, 0), 0);
    ASSERT_EQ(enc.to_codepoint(0, 1), 1);
    ASSERT_EQ(enc.to_codepoint(0, 3), 2);

    // Round-trip every codepoint column.
    for (const int cp : {0, 1, 2}) {
        ASSERT_EQ(enc.to_codepoint(0, enc.to_utf16(0, cp)), cp);
    }

    // The Range overload converts only the character, never the line number.
    const Range r = enc.to_utf16(Range{Position{0, 2}, Position{0, 2}});
    ASSERT_EQ(r.start.line, 0);
    ASSERT_EQ(r.start.character, 3);
    ASSERT_EQ(r.end.character, 3);
}

void test_position_encoder_null_is_identity() {
    // A default-constructed (null) encoder performs identity conversions so an
    // AnalysisResult produced without source degrades safely instead of crashing.
    const PositionEncoder enc;
    ASSERT_FALSE(enc.valid());
    ASSERT_EQ(enc.to_utf16(3, 7), 7);
    ASSERT_EQ(enc.to_codepoint(3, 7), 7);
    ASSERT_EQ(enc.to_utf16(0, 0), 0);
}

void test_find_token_at_supplementary_plane() {
    // Source line 0 is "😀 foo": the emoji is codepoint col 0 but UTF-16 cols
    // 0..1, shifting `foo` right by one UTF-16 unit. `foo` spans codepoint cols
    // [2, 5) (1-based [3, 6)) and UTF-16 cols [3, 6).
    AnalysisResult result;
    const std::string emoji = "\xF0\x9F\x98\x80";
    result.metadata.source_text = emoji + " foo";
    result.metadata.line_starts = {0};

    result.semantic.tokens.push_back(
        Token{.type = TokenType::Identifier,
              .lexeme = "foo",
              .location = SourceLocation{.file_id = 0, .line = 1, .column = 6}});
    result.metadata.line_index.build(result.semantic.tokens);

    // Incoming UTF-16 col 5 is the last position inside `foo`. It converts to
    // codepoint 4 (inside [3, 6)) and resolves to the token. Without conversion,
    // col 5 would map to Luma col 6 and miss the token entirely — the bug.
    const auto hit = find_token_at(result, 0, 5);
    ASSERT_TRUE(hit.has_value());
    ASSERT_EQ(*hit, static_cast<std::size_t>(0));

    // UTF-16 col 3 is the start of `foo`.
    ASSERT_TRUE(find_token_at(result, 0, 3).has_value());

    // UTF-16 col 6 is one past the token end — no token there.
    ASSERT_FALSE(find_token_at(result, 0, 6).has_value());
}

void test_to_wire_supplementary_plane() {
    // Same "😀 foo" layout: token_range yields the codepoint range [2, 5); the
    // wire range sent to the client must be the UTF-16 range [3, 6).
    AnalysisResult result;
    const std::string emoji = "\xF0\x9F\x98\x80";
    result.metadata.source_text = emoji + " foo";
    result.metadata.line_starts = {0};

    const Token tok{.type = TokenType::Identifier,
                    .lexeme = "foo",
                    .location = SourceLocation{.file_id = 0, .line = 1, .column = 6}};

    const Range wire = result.to_wire(token_range(tok));
    ASSERT_EQ(wire.start.line, 0);
    ASSERT_EQ(wire.start.character, 3);
    ASSERT_EQ(wire.end.line, 0);
    ASSERT_EQ(wire.end.character, 6);
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_json_rejects_trailing_decimal);
    RUN(test_json_rejects_trailing_exponent);
    RUN(test_json_duplicate_keys_throws);
    RUN(test_json_rejects_leading_zeros);
    RUN(test_config_defaults);
    RUN(test_config_apply_lsp_settings);
    RUN(test_config_apply_project_config);
    RUN(test_config_rejects_out_of_range);
    RUN(test_workspace_indexer_is_in_workspace);
    RUN(test_find_token_at_basic);
    RUN(test_is_local_variable_check);
    RUN(test_find_enclosing_function_basic);
    RUN(test_keyword_catalog_matches_lexer);
    RUN(test_is_safe_include_path_accepts_relative);
    RUN(test_is_safe_include_path_rejects_traversal);
    RUN(test_is_safe_include_path_rejects_absolute);
    RUN(test_is_safe_resolved_path_accepts_regular_file);
    RUN(test_is_safe_resolved_path_rejects_symlink);
    RUN(test_is_valid_identifier_accepts_ascii_and_unicode);
    RUN(test_is_valid_identifier_rejects_malformed);
    RUN(test_extract_param_name_strips_type);
    RUN(test_extract_param_name_trims_space_before_colon);
    RUN(test_extract_param_name_without_colon_is_nullopt);
    RUN(test_extract_param_name_from_split_signature);
    RUN(test_position_encoder_bmp_and_supplementary);
    RUN(test_position_encoder_null_is_identity);
    RUN(test_find_token_at_supplementary_plane);
    RUN(test_to_wire_supplementary_plane);

    return SUMMARY();
}
