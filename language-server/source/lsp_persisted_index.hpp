#ifndef LUMA_LSP_PERSISTED_INDEX_HPP
#define LUMA_LSP_PERSISTED_INDEX_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Persisted Workspace Index
// ─────────────────────────────────────────────────────────────────────────────
// Stores workspace symbol information to disk so that subsequent LSP startups
// don't need to re-scan and re-analyse all files.
//
// Format: binary file with:
//   - Header (magic, version, file count)
//   - Per-file entries: (path hash, content hash, serialized symbols)
//
// On startup: load the index, check each file's content hash. If unchanged,
// use the cached symbols. If changed (or new), re-analyse only that file.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lsp_optional_ref.hpp"

namespace luma::lsp {

// Per-file cached symbol data.
struct IndexedFileEntry {
    std::string path;
    std::size_t content_hash{0};
    std::uint64_t last_modified{0};

    // Cached analysis results (serialized).
    std::vector<std::string> function_names;
    std::vector<std::string> record_names;
    std::vector<std::string> choice_names;
    std::vector<std::string> exported_symbols;
    bool has_main{false};
    bool has_tests{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Thread safety
// ─────────────────────────────────────────────────────────────────────────────
// PersistedIndex is NOT internally synchronised.  It is owned by
// WorkspaceManager and shared between the main (dispatch) thread and the
// background analysis worker.  Callers must serialise access through the
// server's state_mutex_:
//   - Mutating access (upsert/remove/validate/load/clear) happens either at
//     startup/shutdown (single-threaded lifecycle) or while holding the write
//     (exclusive) state lock — e.g. the analysis worker's commit phase.
//   - Read access (find/is_valid/all_paths/size) happens while holding at
//     least the shared state lock.
// ─────────────────────────────────────────────────────────────────────────────
class PersistedIndex {
public:
    static constexpr char k_magic[4] = {'L', 'I', 'D', 'X'};
    static constexpr std::uint32_t k_version = 1;

    // Load the index from disk. Returns false if the file doesn't exist
    // or is corrupted/outdated.
    [[nodiscard]] bool load(const std::filesystem::path& index_path);

    // Save the current index to disk.
    [[nodiscard]] bool save(const std::filesystem::path& index_path) const;

    // Look up a file's cached entry.
    [[nodiscard]] optional_ref<const IndexedFileEntry> find(const std::string& path) const;

    // Check if a file's cached entry is still valid (content hash matches).
    [[nodiscard]] bool is_valid(const std::string& path, std::size_t current_hash) const;

    // Update or insert a file entry.
    void upsert(IndexedFileEntry entry);

    // Remove a file entry.
    void remove(const std::string& path);

    // Get all indexed file paths.
    [[nodiscard]] std::vector<std::string> all_paths() const;

    // Clear the entire index.
    void clear();

    // Validate loaded entries against the filesystem.
    // Removes entries whose files no longer exist or whose last-modified
    // timestamp has changed. Returns the number of entries removed.
    std::size_t validate();

    // Number of indexed files.
    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

    // Get the default index file path for a workspace root.
    [[nodiscard]] static std::filesystem::path
    default_path(const std::filesystem::path& workspace_root);

private:
    std::unordered_map<std::string, IndexedFileEntry> entries_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_PERSISTED_INDEX_HPP
