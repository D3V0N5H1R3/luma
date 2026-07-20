// LSP document store tests — position/offset conversion, content deduplication,
// dirty tracking, line-start caching, and background-file bookkeeping.
//
// DocumentStore is not internally synchronised (the caller holds the server's
// state mutex and passes a LockToken to prove it), so its pure bookkeeping logic
// is tested here directly rather than only through the didOpen/didChange path.

#include <cstddef>
#include <string>
#include <vector>

#include "lsp_document_store.hpp"
#include "test_framework.hpp"

using luma::lsp::DocumentStore;
using luma::lsp::LockToken;

namespace {

// A LockToken is a documentation-only marker; constructing one directly is fine
// in a single-threaded test where no real locking is required.
const LockToken tok{};

// ─── Content storage ───────────────────────────────────────────────

void test_set_and_get_content() {
    DocumentStore store;
    store.set_content(tok, "uri://a", "hello");

    const auto* content = store.get_content(tok, "uri://a");
    ASSERT_NE(content, nullptr);
    ASSERT_EQ(*content, std::string("hello"));
    ASSERT_TRUE(store.contains(tok, "uri://a"));
}

void test_unknown_document_defaults() {
    const DocumentStore store;
    ASSERT_EQ(store.get_content(tok, "uri://missing"), nullptr);
    ASSERT_FALSE(store.contains(tok, "uri://missing"));
    ASSERT_EQ(store.get_version(tok, "uri://missing"), -1);
    ASSERT_EQ(store.get_content_hash(tok, "uri://missing"), static_cast<std::size_t>(0));
    ASSERT_EQ(store.get_line_starts(tok, "uri://missing"), nullptr);
    ASSERT_TRUE(store.is_dirty(tok, "uri://missing")); // absent → treated as dirty
}

void test_version_and_content_hash() {
    DocumentStore store;
    store.set_version(tok, "uri://a", 5);
    ASSERT_EQ(store.get_version(tok, "uri://a"), 5);

    store.set_content_hash(tok, "uri://a", 999);
    ASSERT_EQ(store.get_content_hash(tok, "uri://a"), static_cast<std::size_t>(999));

    store.erase_content_hash(tok, "uri://a");
    ASSERT_EQ(store.get_content_hash(tok, "uri://a"), static_cast<std::size_t>(0));
}

// ─── Line-start caching ────────────────────────────────────────────

void test_line_starts_cached() {
    DocumentStore store;
    store.set_content(tok, "uri://a", "line0\nline1\nline2");

    const auto* starts = store.get_line_starts(tok, "uri://a");
    ASSERT_NE(starts, nullptr);
    ASSERT_TRUE(*starts == std::vector<std::size_t>({0, 6, 12}));
}

void test_line_starts_trailing_newline() {
    DocumentStore store;
    store.set_content(tok, "uri://a", "a\nb\n");

    const auto* starts = store.get_line_starts(tok, "uri://a");
    ASSERT_NE(starts, nullptr);
    ASSERT_TRUE(*starts == std::vector<std::size_t>({0, 2, 4}));
}

// ─── Position → offset conversion ──────────────────────────────────

void test_position_to_offset_ascii() {
    DocumentStore store;
    const std::string text = "line0\nline1\nline2";
    store.set_content(tok, "uri://a", text);

    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 0, 0), static_cast<std::size_t>(0));
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 0, 3), static_cast<std::size_t>(3));
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 1, 0), static_cast<std::size_t>(6));
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 2, 2), static_cast<std::size_t>(14));
}

void test_position_to_offset_clamps() {
    DocumentStore store;
    const std::string text = "line0\nline1\nline2";
    store.set_content(tok, "uri://a", text);

    // Character past the end of a line clamps to the line's end.
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 0, 99), static_cast<std::size_t>(5));
    // Line past the end of the document clamps to the document size.
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 99, 0), text.size());
}

void test_position_to_offset_crlf() {
    DocumentStore store;
    const std::string text = "a\r\nbb\r\n";
    store.set_content(tok, "uri://a", text);

    // The trailing CR/LF are excluded from the line before UTF-16 conversion.
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 0, 1), static_cast<std::size_t>(1));
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 1, 2), static_cast<std::size_t>(5));
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 1, 99), static_cast<std::size_t>(5));
}

void test_position_to_offset_without_cached_document() {
    // When the document was never stored, the fallback path recomputes line
    // starts from the supplied text on the fly.
    const DocumentStore store;
    const std::string text = "ab\ncd";
    ASSERT_EQ(store.position_to_offset(tok, "uri://never", text, 1, 1),
              static_cast<std::size_t>(4));
}

void test_position_to_offset_multibyte_utf8() {
    // `café` — é is 1 UTF-16 code unit but 2 UTF-8 bytes. A client column past
    // the é must resolve to a byte offset that accounts for the extra byte.
    // Selection-driven refactorings (ExtractVariable) resolve their UTF-16
    // selection columns through this conversion (B04); adding the column
    // straight onto the byte line-start would slice the source mid-codepoint
    // and corrupt the extracted/replaced text.
    DocumentStore store;
    const std::string text = "caf\xC3\xA9 x"; // "café x"
    store.set_content(tok, "uri://a", text);

    // UTF-16 cols:   c=0 a=1 f=2 é=3 space=4 x=5
    // byte offsets:  c=0 a=1 f=2 é=3(2 bytes) space=5 x=6
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 0, 3), static_cast<std::size_t>(3));
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 0, 4), static_cast<std::size_t>(5));
    ASSERT_EQ(store.position_to_offset(tok, "uri://a", text, 0, 5), static_cast<std::size_t>(6));
}

// ─── Dirty tracking and deduplication ──────────────────────────────

void test_set_content_dedup_marks_clean() {
    DocumentStore store;
    store.set_content(tok, "uri://a", "x");
    ASSERT_TRUE(store.is_dirty(tok, "uri://a"));

    // Re-storing identical content is deduplicated and marks the doc clean.
    store.set_content(tok, "uri://a", "x");
    ASSERT_FALSE(store.is_dirty(tok, "uri://a"));

    // Different content marks it dirty again.
    store.set_content(tok, "uri://a", "y");
    ASSERT_TRUE(store.is_dirty(tok, "uri://a"));
    ASSERT_EQ(*store.get_content(tok, "uri://a"), std::string("y"));
}

void test_mark_clean() {
    DocumentStore store;
    store.set_content(tok, "uri://a", "x");
    ASSERT_TRUE(store.is_dirty(tok, "uri://a"));

    store.mark_clean(tok, "uri://a");
    ASSERT_FALSE(store.is_dirty(tok, "uri://a"));
}

void test_refresh_stored_hash_enables_dedup_after_inplace_edit() {
    DocumentStore store;
    store.set_content(tok, "uri://a", "A");

    // Simulate the didChange path mutating content in place, bypassing
    // set_content and leaving stored_hash pointing at the pre-edit content.
    auto* content = store.get_content(tok, "uri://a");
    ASSERT_NE(content, nullptr);
    *content = "B";

    // Without refreshing, a re-open with the new content would be misclassified.
    store.refresh_stored_hash(tok, "uri://a");

    // Now storing the current content is correctly recognised as unchanged.
    store.set_content(tok, "uri://a", "B");
    ASSERT_FALSE(store.is_dirty(tok, "uri://a"));
    ASSERT_EQ(*store.get_content(tok, "uri://a"), std::string("B"));
}

// ─── Background-file bookkeeping ────────────────────────────────────

void test_background_tracking() {
    DocumentStore store;
    store.set_content(tok, "uri://a", "x");
    store.set_content(tok, "uri://b", "y");
    store.mark_background(tok, "uri://a");

    ASSERT_TRUE(store.is_background(tok, "uri://a"));
    ASSERT_FALSE(store.is_background(tok, "uri://b"));
    ASSERT_EQ(store.background_count(tok), static_cast<std::size_t>(1));

    store.unmark_background(tok, "uri://a");
    ASSERT_FALSE(store.is_background(tok, "uri://a"));
    ASSERT_EQ(store.background_count(tok), static_cast<std::size_t>(0));
}

// ─── Removal ───────────────────────────────────────────────────────

void test_remove() {
    DocumentStore store;
    store.set_content(tok, "uri://a", "x");
    store.set_content(tok, "uri://b", "y");
    ASSERT_EQ(store.all(tok).size(), static_cast<std::size_t>(2));

    store.remove(tok, "uri://a");
    ASSERT_FALSE(store.contains(tok, "uri://a"));
    ASSERT_EQ(store.get_content(tok, "uri://a"), nullptr);
    ASSERT_EQ(store.get_version(tok, "uri://a"), -1);
    ASSERT_TRUE(store.contains(tok, "uri://b"));
    ASSERT_EQ(store.all(tok).size(), static_cast<std::size_t>(1));
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_set_and_get_content);
    RUN(test_unknown_document_defaults);
    RUN(test_version_and_content_hash);
    RUN(test_line_starts_cached);
    RUN(test_line_starts_trailing_newline);
    RUN(test_position_to_offset_ascii);
    RUN(test_position_to_offset_clamps);
    RUN(test_position_to_offset_crlf);
    RUN(test_position_to_offset_without_cached_document);
    RUN(test_position_to_offset_multibyte_utf8);
    RUN(test_set_content_dedup_marks_clean);
    RUN(test_mark_clean);
    RUN(test_refresh_stored_hash_enables_dedup_after_inplace_edit);
    RUN(test_background_tracking);
    RUN(test_remove);

    return SUMMARY();
}
