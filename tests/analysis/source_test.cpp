// Unit tests for SourceLocation and SourceManager.

#include <string>

#include "analysis/source/source_location.hpp"
#include "analysis/source/source_manager.hpp"
#include "common/string_utils.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── SourceLocation ───

static void test_source_location_defaults() {
    const SourceLocation loc{};
    ASSERT_EQ(loc.file_id, 0);
    ASSERT_EQ(loc.line, 1);
    ASSERT_EQ(loc.column, 1);
}

static void test_source_location_values() {
    const SourceLocation loc{3, 10, 25};
    ASSERT_EQ(loc.file_id, 3);
    ASSERT_EQ(loc.line, 10);
    ASSERT_EQ(loc.column, 25);
}

static void test_source_location_equality() {
    const SourceLocation a{1, 5, 10};
    const SourceLocation b{1, 5, 10};
    const SourceLocation c{2, 5, 10};
    ASSERT_TRUE(a == b);
    ASSERT_FALSE(a == c);
    ASSERT_TRUE(a != c);
}

static void test_source_location_ordering() {
    const SourceLocation a{1, 1, 1};
    const SourceLocation b{1, 2, 1};
    const SourceLocation c{2, 1, 1};
    ASSERT_TRUE(a < b);
    ASSERT_TRUE(b < c);
    ASSERT_FALSE(c < a);
}

static void test_source_location_bool_conversion() {
    ASSERT_TRUE(static_cast<bool>(SourceLocation{1, 1, 1}));
    ASSERT_TRUE(static_cast<bool>(SourceLocation{0, 1, 1}));
    ASSERT_FALSE(static_cast<bool>(SourceLocation{0, 0, 0}));
}

// ─── SourceManager ───

static void test_source_manager_not_loaded() {
    const SourceManager sm;
    ASSERT_FALSE(sm.is_loaded("nonexistent.luma"));
    ASSERT_FALSE(sm.find_file_id("nonexistent.luma").has_value());
}

static void test_source_manager_get_file_null() {
    const SourceManager sm;
    ASSERT_TRUE(sm.get_file(999) == nullptr);
}

static void test_source_manager_get_line_empty() {
    const SourceManager sm;
    // Out-of-range file should return empty.
    const auto line = sm.get_line(999, 1);
    ASSERT_TRUE(line.empty());
}

static void test_source_manager_load_file() {
    SourceManager sm;

    // Load an existing file from the project.
    const auto& file = sm.load("examples/language-features/hello.luma");
    ASSERT_TRUE(file.file_id > 0);
    ASSERT_FALSE(file.text.empty());
    ASSERT_FALSE(file.line_offsets.empty());
    ASSERT_TRUE(sm.is_loaded("examples/language-features/hello.luma"));
}

static void test_source_manager_deduplication() {
    SourceManager sm;

    const auto& first = sm.load("examples/language-features/hello.luma");
    const auto& second = sm.load("examples/language-features/hello.luma");

    // Same file_id — not loaded twice.
    ASSERT_EQ(first.file_id, second.file_id);
}

static void test_source_manager_get_file() {
    SourceManager sm;
    const auto& file = sm.load("examples/language-features/hello.luma");

    const auto* retrieved = sm.get_file(file.file_id);
    ASSERT_TRUE(retrieved != nullptr);
    ASSERT_EQ(retrieved->file_id, file.file_id);
}

static void test_source_manager_get_line() {
    SourceManager sm;
    const auto& file = sm.load("examples/language-features/hello.luma");

    const auto line1 = sm.get_line(file.file_id, 1);
    ASSERT_FALSE(line1.empty());

    // Out of range line returns empty.
    const auto bad_line = sm.get_line(file.file_id, 99999);
    ASSERT_TRUE(bad_line.empty());
}

// get_line slices the stored text via the line-offset index. These cases lock
// in the offset arithmetic against the behaviour of the previous split_lines()
// implementation: exact per-line content, the final line without a trailing
// newline, an empty trailing line, CRLF stripping, and out-of-range access.
static void test_source_manager_get_line_multiline() {
    SourceManager sm;
    const TempFile tmp{"first\nsecond\nthird"};
    const auto& file = sm.load(tmp.path_string());

    ASSERT_EQ(sm.get_line(file.file_id, 1), std::string_view{"first"});
    ASSERT_EQ(sm.get_line(file.file_id, 2), std::string_view{"second"});
    // Final line has no trailing newline — runs to end-of-text.
    ASSERT_EQ(sm.get_line(file.file_id, 3), std::string_view{"third"});
    // One past the last line is out of range.
    ASSERT_TRUE(sm.get_line(file.file_id, 4).empty());
}

static void test_source_manager_get_line_trailing_newline() {
    SourceManager sm;
    const TempFile tmp{"alpha\nbeta\n"};
    const auto& file = sm.load(tmp.path_string());

    ASSERT_EQ(sm.get_line(file.file_id, 1), std::string_view{"alpha"});
    ASSERT_EQ(sm.get_line(file.file_id, 2), std::string_view{"beta"});
    // A trailing newline yields a valid (empty) final line, matching
    // split_lines(): "alpha\nbeta\n" is three lines, the last empty.
    ASSERT_EQ(file.line_offsets.size(), static_cast<std::size_t>(3));
    ASSERT_TRUE(sm.get_line(file.file_id, 3).empty());
    ASSERT_TRUE(sm.get_line(file.file_id, 4).empty());
}

static void test_source_manager_get_line_crlf() {
    SourceManager sm;
    const TempFile tmp{"one\r\ntwo\r\nthree"};
    const auto& file = sm.load(tmp.path_string());

    // The trailing '\r' of each CRLF-terminated line is stripped.
    ASSERT_EQ(sm.get_line(file.file_id, 1), std::string_view{"one"});
    ASSERT_EQ(sm.get_line(file.file_id, 2), std::string_view{"two"});
    ASSERT_EQ(sm.get_line(file.file_id, 3), std::string_view{"three"});
}

static void test_source_manager_get_line_empty_file() {
    SourceManager sm;
    const TempFile tmp{""};
    const auto& file = sm.load(tmp.path_string());

    // An empty file is a single empty line, like split_lines("").
    ASSERT_EQ(file.line_offsets.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(sm.get_line(file.file_id, 1).empty());
    ASSERT_TRUE(sm.get_line(file.file_id, 2).empty());
}

static void test_source_manager_find_file_id() {
    SourceManager sm;
    const auto& file = sm.load("examples/language-features/hello.luma");

    const auto id = sm.find_file_id("examples/language-features/hello.luma");
    ASSERT_TRUE(id.has_value());
    ASSERT_EQ(*id, file.file_id);
}

static void test_source_manager_load_nonexistent() {
    SourceManager sm;
    ASSERT_THROWS(sm.load("this_file_does_not_exist.luma"));
}

static void test_source_manager_get_file_negative_id() {
    const SourceManager sm;
    ASSERT_TRUE(sm.get_file(-1) == nullptr);
    ASSERT_TRUE(sm.get_file(0) == nullptr);
}

// ─── Identifier-token matching (string_utils) ───

static void test_contains_identifier_token() {
    // Standalone occurrences match, including at the very start and end.
    ASSERT_TRUE(contains_identifier_token("Marker.text()", "Marker"));
    ASSERT_TRUE(contains_identifier_token("use Marker", "Marker"));
    ASSERT_TRUE(contains_identifier_token("a Marker b", "Marker"));

    // Embedded in a longer identifier (letter, digit, or underscore adjacent)
    // must not match.
    ASSERT_FALSE(contains_identifier_token("myMarkerHelper", "Marker"));
    ASSERT_FALSE(contains_identifier_token("Marker2", "Marker"));
    ASSERT_FALSE(contains_identifier_token("Marker_x", "Marker"));
    ASSERT_FALSE(contains_identifier_token("_Marker", "Marker"));

    // A later standalone occurrence still matches past an embedded one.
    ASSERT_TRUE(contains_identifier_token("myMarkerHelper; Marker.run()", "Marker"));

    // Degenerate inputs.
    ASSERT_FALSE(contains_identifier_token("anything", ""));
    ASSERT_FALSE(contains_identifier_token("", "Marker"));
}

// ─── SourceManager::any_source_contains{,_word} ───

static void test_source_manager_any_source_contains() {
    SourceManager sm;
    sm.load_virtual("<a>", "value |> Marker.text(\"hi\")");

    // Plain substring gate matches, even a prefix of a longer identifier.
    ASSERT_TRUE(sm.any_source_contains("Marker"));
    ASSERT_TRUE(sm.any_source_contains("Mark"));
    ASSERT_FALSE(sm.any_source_contains("Missing"));
}

static void test_source_manager_any_source_contains_word() {
    SourceManager sm;
    sm.load_virtual("<a>", "value |> Marker.text(\"hi\")");

    // Whole-word gate matches a standalone identifier but not a substring of
    // a longer one.
    ASSERT_TRUE(sm.any_source_contains_word("Marker"));
    ASSERT_FALSE(sm.any_source_contains_word("Mark"));
}

static void test_source_manager_any_source_contains_word_rejects_embedded() {
    SourceManager sm;
    sm.load_virtual("<a>", "function myMarkerHelper() {}");

    // The trigger name is embedded in a longer identifier: the whole-word gate
    // must not fire, while the plain substring gate still does.
    ASSERT_FALSE(sm.any_source_contains_word("Marker"));
    ASSERT_TRUE(sm.any_source_contains("Marker"));
}

int main() {
    RUN(test_source_location_defaults);
    RUN(test_source_location_values);
    RUN(test_source_location_equality);
    RUN(test_source_location_ordering);
    RUN(test_source_location_bool_conversion);
    RUN(test_source_manager_not_loaded);
    RUN(test_source_manager_get_file_null);
    RUN(test_source_manager_get_line_empty);
    RUN(test_source_manager_load_file);
    RUN(test_source_manager_deduplication);
    RUN(test_source_manager_get_file);
    RUN(test_source_manager_get_line);
    RUN(test_source_manager_get_line_multiline);
    RUN(test_source_manager_get_line_trailing_newline);
    RUN(test_source_manager_get_line_crlf);
    RUN(test_source_manager_get_line_empty_file);
    RUN(test_source_manager_find_file_id);
    RUN(test_source_manager_load_nonexistent);
    RUN(test_source_manager_get_file_negative_id);
    RUN(test_contains_identifier_token);
    RUN(test_source_manager_any_source_contains);
    RUN(test_source_manager_any_source_contains_word);
    RUN(test_source_manager_any_source_contains_word_rejects_embedded);

    return SUMMARY();
}
