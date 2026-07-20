// LSP persistence tests — PersistedIndex disk format and binary_format helpers.
//
// The persisted index stores workspace symbol information to disk so subsequent
// LSP startups can skip re-analysing unchanged files. A silent break here would
// degrade every restart without any integration test failing, so these tests
// exercise the save/load round-trip, the corruption guards (magic, version,
// count, checksum, truncation), and the low-level big-endian serialisers with
// their out-of-range safety limits.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "lsp_binary_format.hpp"
#include "lsp_persisted_index.hpp"
#include "test_framework.hpp"

namespace binary_format = luma::lsp::binary_format;
using luma::lsp::fnv1a_hash;
using luma::lsp::IndexedFileEntry;
using luma::lsp::PersistedIndex;

namespace {

// ─── Helpers ───────────────────────────────────────────────────────

// Big-endian 4-byte encoding, matching binary_format::read_u32.
[[nodiscard]] std::string be32(std::uint32_t v) {
    std::string s(4, '\0');
    s[0] = static_cast<char>((v >> 24) & 0xFF);
    s[1] = static_cast<char>((v >> 16) & 0xFF);
    s[2] = static_cast<char>((v >> 8) & 0xFF);
    s[3] = static_cast<char>(v & 0xFF);
    return s;
}

[[nodiscard]] IndexedFileEntry make_entry(const std::string& path) {
    IndexedFileEntry entry;
    entry.path = path;
    entry.content_hash = 0xDEADBEEFu;
    entry.last_modified = 0; // 0 disables timestamp validation
    entry.function_names = {"main", "helper"};
    entry.record_names = {"Point"};
    entry.choice_names = {"Color"};
    entry.exported_symbols = {"main", "Point", "Color"};
    entry.has_main = true;
    entry.has_tests = false;
    return entry;
}

[[nodiscard]] std::string read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void write_whole_file(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// ─── PersistedIndex: round-trip ────────────────────────────────────

void test_save_load_round_trip() {
    const TempDir dir;
    const auto path = dir.path() / "index.lidx";

    PersistedIndex saved;
    saved.upsert(make_entry("src/a.luma"));

    IndexedFileEntry second = make_entry("src/b.luma");
    second.content_hash = 42;
    second.has_main = false;
    second.has_tests = true;
    second.function_names = {}; // exercise empty collections too
    saved.upsert(std::move(second));

    ASSERT_TRUE(saved.save(path));
    ASSERT_TRUE(std::filesystem::exists(path));

    PersistedIndex loaded;
    ASSERT_TRUE(loaded.load(path));
    ASSERT_EQ(loaded.size(), static_cast<std::size_t>(2));

    const auto a = loaded.find("src/a.luma");
    ASSERT_TRUE(a.has_value());
    ASSERT_EQ(a->content_hash, static_cast<std::size_t>(0xDEADBEEFu));
    ASSERT_TRUE(a->function_names == std::vector<std::string>({"main", "helper"}));
    ASSERT_TRUE(a->record_names == std::vector<std::string>({"Point"}));
    ASSERT_TRUE(a->choice_names == std::vector<std::string>({"Color"}));
    ASSERT_TRUE(a->exported_symbols == std::vector<std::string>({"main", "Point", "Color"}));
    ASSERT_TRUE(a->has_main);
    ASSERT_FALSE(a->has_tests);

    const auto b = loaded.find("src/b.luma");
    ASSERT_TRUE(b.has_value());
    ASSERT_EQ(b->content_hash, static_cast<std::size_t>(42));
    ASSERT_TRUE(b->function_names.empty());
    ASSERT_FALSE(b->has_main);
    ASSERT_TRUE(b->has_tests);
}

void test_empty_index_round_trip() {
    const TempDir dir;
    const auto path = dir.path() / "empty.lidx";

    PersistedIndex saved;
    ASSERT_TRUE(saved.save(path));

    PersistedIndex loaded;
    ASSERT_TRUE(loaded.load(path));
    ASSERT_EQ(loaded.size(), static_cast<std::size_t>(0));
}

void test_save_creates_parent_directories() {
    const TempDir dir;
    // Nested path whose parent directories do not yet exist.
    const auto path = dir.path() / "nested" / "deeper" / "index.lidx";

    PersistedIndex saved;
    saved.upsert(make_entry("x.luma"));
    ASSERT_TRUE(saved.save(path));
    ASSERT_TRUE(std::filesystem::exists(path));
}

// ─── PersistedIndex: corruption guards ─────────────────────────────

void test_load_missing_file() {
    const TempDir dir;
    PersistedIndex idx;
    ASSERT_FALSE(idx.load(dir.path() / "does_not_exist.lidx"));
}

void test_load_rejects_bad_magic() {
    const TempFile file{std::string("XXXX") + be32(PersistedIndex::k_version)};
    PersistedIndex idx;
    ASSERT_FALSE(idx.load(file.path()));
}

void test_load_rejects_version_mismatch() {
    const std::string bytes = std::string("LIDX") + be32(PersistedIndex::k_version + 1);
    const TempFile file{bytes};
    PersistedIndex idx;
    ASSERT_FALSE(idx.load(file.path()));
}

void test_load_rejects_excessive_count() {
    // file_count beyond k_max_collection_count must be rejected before any
    // per-entry allocation happens.
    const std::string bytes =
        std::string("LIDX") + be32(PersistedIndex::k_version) +
        be32(static_cast<std::uint32_t>(binary_format::k_max_collection_count + 1));
    const TempFile file{bytes};
    PersistedIndex idx;
    ASSERT_FALSE(idx.load(file.path()));
}

void test_load_rejects_truncated_entry() {
    // Header claims one entry, but the file ends immediately — the entry read
    // hits EOF and load must fail cleanly (and leave no partial entries).
    const std::string bytes = std::string("LIDX") + be32(PersistedIndex::k_version) + be32(1);
    const TempFile file{bytes};
    PersistedIndex idx;
    ASSERT_FALSE(idx.load(file.path()));
    ASSERT_EQ(idx.size(), static_cast<std::size_t>(0));
}

void test_load_rejects_missing_checksum() {
    // Zero entries but no trailing checksum word — the integrity check reads
    // past EOF and must reject the file.
    const std::string bytes = std::string("LIDX") + be32(PersistedIndex::k_version) + be32(0);
    const TempFile file{bytes};
    PersistedIndex idx;
    ASSERT_FALSE(idx.load(file.path()));
}

void test_load_rejects_corrupted_checksum() {
    const TempDir dir;
    const auto path = dir.path() / "index.lidx";

    PersistedIndex saved;
    saved.upsert(make_entry("a.luma"));
    ASSERT_TRUE(saved.save(path));

    // Flip the final checksum byte and confirm the tampered file is rejected.
    std::string bytes = read_whole_file(path);
    ASSERT_FALSE(bytes.empty());
    bytes.back() = static_cast<char>(bytes.back() ^ 0xFF);
    write_whole_file(path, bytes);

    PersistedIndex loaded;
    ASSERT_FALSE(loaded.load(path));
}

// ─── PersistedIndex: query and mutation ────────────────────────────

void test_is_valid_hash_match() {
    PersistedIndex idx;
    IndexedFileEntry entry = make_entry("a.luma");
    entry.content_hash = 12345;
    idx.upsert(std::move(entry));

    ASSERT_TRUE(idx.is_valid("a.luma", 12345));
    ASSERT_FALSE(idx.is_valid("a.luma", 99999));       // hash mismatch
    ASSERT_FALSE(idx.is_valid("unknown.luma", 12345)); // not indexed
}

void test_upsert_replaces_existing() {
    PersistedIndex idx;
    idx.upsert(make_entry("a.luma"));

    IndexedFileEntry updated = make_entry("a.luma");
    updated.content_hash = 777;
    idx.upsert(std::move(updated));

    ASSERT_EQ(idx.size(), static_cast<std::size_t>(1)); // replaced, not duplicated
    const auto found = idx.find("a.luma");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->content_hash, static_cast<std::size_t>(777));
}

void test_remove_and_clear() {
    PersistedIndex idx;
    idx.upsert(make_entry("a.luma"));
    idx.upsert(make_entry("b.luma"));
    ASSERT_EQ(idx.size(), static_cast<std::size_t>(2));

    idx.remove("a.luma");
    ASSERT_EQ(idx.size(), static_cast<std::size_t>(1));
    ASSERT_FALSE(idx.find("a.luma").has_value());
    ASSERT_TRUE(idx.find("b.luma").has_value());

    idx.remove("nonexistent.luma"); // no-op, must not throw
    ASSERT_EQ(idx.size(), static_cast<std::size_t>(1));

    idx.clear();
    ASSERT_EQ(idx.size(), static_cast<std::size_t>(0));
}

void test_all_paths() {
    PersistedIndex idx;
    idx.upsert(make_entry("a.luma"));
    idx.upsert(make_entry("b.luma"));

    auto paths = idx.all_paths();
    std::sort(paths.begin(), paths.end()); // order is unspecified
    ASSERT_TRUE(paths == std::vector<std::string>({"a.luma", "b.luma"}));
}

void test_validate_removes_missing_files() {
    const TempFile present{"@main\nfunction main() {}\n"};

    PersistedIndex idx;
    // One entry points at a real file, one at a path that does not exist.
    idx.upsert(make_entry(present.path_string()));
    idx.upsert(make_entry("this/file/does/not/exist.luma"));
    ASSERT_EQ(idx.size(), static_cast<std::size_t>(2));

    const auto removed = idx.validate();
    ASSERT_EQ(removed, static_cast<std::size_t>(1));
    ASSERT_EQ(idx.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(idx.find(present.path_string()).has_value());
    ASSERT_FALSE(idx.find("this/file/does/not/exist.luma").has_value());
}

void test_default_path() {
    const std::filesystem::path root = std::filesystem::path("some") / "workspace";
    const auto expected = root / ".luma" / "index.lidx";
    ASSERT_TRUE(PersistedIndex::default_path(root) == expected);
}

// ─── binary_format: round-trips ────────────────────────────────────

void test_binary_u32_u64_round_trip() {
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    binary_format::write_u32(ss, 0u);
    binary_format::write_u32(ss, 0xABCDEF01u);
    binary_format::write_u64(ss, 0ull);
    binary_format::write_u64(ss, 0x0123456789ABCDEFull);

    ASSERT_EQ(binary_format::read_u32(ss), 0u);
    ASSERT_EQ(binary_format::read_u32(ss), 0xABCDEF01u);
    ASSERT_EQ(binary_format::read_u64(ss), 0ull);
    ASSERT_EQ(binary_format::read_u64(ss), 0x0123456789ABCDEFull);
}

void test_binary_big_endian_byte_order() {
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    binary_format::write_u32(ss, 0x01020304u);
    const std::string bytes = ss.str();
    ASSERT_EQ(bytes.size(), static_cast<std::size_t>(4));
    // Most-significant byte first.
    ASSERT_EQ(static_cast<std::uint8_t>(bytes[0]), static_cast<std::uint8_t>(0x01));
    ASSERT_EQ(static_cast<std::uint8_t>(bytes[1]), static_cast<std::uint8_t>(0x02));
    ASSERT_EQ(static_cast<std::uint8_t>(bytes[2]), static_cast<std::uint8_t>(0x03));
    ASSERT_EQ(static_cast<std::uint8_t>(bytes[3]), static_cast<std::uint8_t>(0x04));
}

void test_binary_string_round_trip() {
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    binary_format::write_string(ss, "");
    binary_format::write_string(ss, "hello");
    // Embedded NUL and high bytes must survive (length-prefixed, not NUL-terminated).
    binary_format::write_string(ss, std::string("a\0b\xC3\xA9", 5));

    ASSERT_EQ(binary_format::read_string(ss), std::string(""));
    ASSERT_EQ(binary_format::read_string(ss), std::string("hello"));
    ASSERT_EQ(binary_format::read_string(ss), std::string("a\0b\xC3\xA9", 5));
}

void test_binary_string_vec_round_trip() {
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    const std::vector<std::string> empty;
    const std::vector<std::string> values = {"one", "", "three"};
    binary_format::write_string_vec(ss, empty);
    binary_format::write_string_vec(ss, values);

    ASSERT_TRUE(binary_format::read_string_vec(ss).empty());
    ASSERT_TRUE(binary_format::read_string_vec(ss) == values);
}

// ─── binary_format: safety limits ──────────────────────────────────

void test_read_string_rejects_oversized() {
    // A length prefix beyond k_max_string_size must fail the stream rather than
    // attempt a multi-gigabyte allocation.
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    binary_format::write_u32(ss, static_cast<std::uint32_t>(binary_format::k_max_string_size + 1));

    const auto result = binary_format::read_string(ss);
    ASSERT_TRUE(result.empty());
    ASSERT_TRUE(ss.fail());
}

void test_read_string_vec_rejects_oversized() {
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    binary_format::write_u32(ss,
                             static_cast<std::uint32_t>(binary_format::k_max_collection_count + 1));

    const auto result = binary_format::read_string_vec(ss);
    ASSERT_TRUE(result.empty());
    ASSERT_TRUE(ss.fail());
}

// ─── FNV-1a hash ───────────────────────────────────────────────────

void test_fnv1a_hash() {
    // Empty input yields the 32-bit FNV offset basis (no bytes mixed in).
    ASSERT_EQ(fnv1a_hash("", 0), 0x811c9dc5u);

    // Deterministic: same bytes → same hash.
    const std::string a = "workspace/main.luma";
    ASSERT_EQ(fnv1a_hash(a.data(), a.size()), fnv1a_hash(a.data(), a.size()));

    // Distinct inputs → distinct hashes (avalanche on a single-character change).
    const std::string b = "workspace/main.lumb";
    ASSERT_NE(fnv1a_hash(a.data(), a.size()), fnv1a_hash(b.data(), b.size()));

    // constexpr-evaluable.
    static_assert(fnv1a_hash("", 0) == 0x811c9dc5u);
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_save_load_round_trip);
    RUN(test_empty_index_round_trip);
    RUN(test_save_creates_parent_directories);
    RUN(test_load_missing_file);
    RUN(test_load_rejects_bad_magic);
    RUN(test_load_rejects_version_mismatch);
    RUN(test_load_rejects_excessive_count);
    RUN(test_load_rejects_truncated_entry);
    RUN(test_load_rejects_missing_checksum);
    RUN(test_load_rejects_corrupted_checksum);
    RUN(test_is_valid_hash_match);
    RUN(test_upsert_replaces_existing);
    RUN(test_remove_and_clear);
    RUN(test_all_paths);
    RUN(test_validate_removes_missing_files);
    RUN(test_default_path);
    RUN(test_binary_u32_u64_round_trip);
    RUN(test_binary_big_endian_byte_order);
    RUN(test_binary_string_round_trip);
    RUN(test_binary_string_vec_round_trip);
    RUN(test_read_string_rejects_oversized);
    RUN(test_read_string_vec_rejects_oversized);
    RUN(test_fnv1a_hash);

    return SUMMARY();
}
