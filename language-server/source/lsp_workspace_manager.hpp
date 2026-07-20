#ifndef LUMA_LSP_WORKSPACE_MANAGER_HPP
#define LUMA_LSP_WORKSPACE_MANAGER_HPP

// ═══════════════════════════════════════════════════════════
// WorkspaceManager — workspace roots, persisted index, and
// project configuration.
//
// Extracted from LspServer to give workspace-related state a
// single owner.  LspServer delegates workspace operations to
// this helper via composition.
// ═══════════════════════════════════════════════════════════

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "lsp_persisted_index.hpp"
#include "lsp_workspace_indexer.hpp"

namespace luma::lsp {

struct LspConfig;

class WorkspaceManager {
public:
    using LogCallback = std::function<void(const std::string&)>;

    // ─── Workspace roots ───

    void add_root(const std::string& path);

    [[nodiscard]] const std::vector<std::string>& roots() const noexcept;

    [[nodiscard]] bool has_roots() const noexcept;

    // Check if a path is within any workspace root.
    [[nodiscard]] bool is_in_workspace(const std::string& path) const;

    // ─── Indexing state ───

    [[nodiscard]] bool is_indexing() const noexcept;

    void set_indexing(bool value) noexcept;

    // ─── Persisted index ───

    [[nodiscard]] PersistedIndex& persisted_index() noexcept;

    [[nodiscard]] const PersistedIndex& persisted_index() const noexcept;

    // Load persisted index from disk (first workspace root).
    // Returns true on success. Does not validate entries against the
    // filesystem — call validate_persisted_index() off the init path for that.
    [[nodiscard]] bool load_persisted_index(const LogCallback& log = {});

    // Prune persisted index entries whose backing files are missing or have
    // changed on disk, returning the number of entries removed. Performs
    // O(entries) filesystem stats, so it should be run off the main
    // message-loop thread (e.g. on the workspace scan thread).
    //
    // PersistedIndex is not internally synchronized, and this call mutates it,
    // so the caller MUST hold the exclusive write state lock while invoking it
    // (the analysis worker upserts into the same index under that lock). Log
    // the returned count outside the lock to keep transport I/O off it.
    [[nodiscard]] std::size_t validate_persisted_index();

    // Save persisted index to disk (first workspace root).
    // Returns true on success.
    [[nodiscard]] bool save_persisted_index(const LogCallback& log = {});

    // ─── Project configuration ───

    // Load and apply a luma.json file to the given config.
    void load_project_config(const std::string& path, LspConfig& config,
                             const LogCallback& log = {});

    // Scan workspace roots for the first luma.json and apply it.
    void discover_project_config(LspConfig& config, const LogCallback& log = {});

private:
    std::vector<std::string> workspace_roots_;
    PersistedIndex persisted_index_;
    std::atomic<bool> indexing_in_progress_{false};
};

} // namespace luma::lsp

#endif // LUMA_LSP_WORKSPACE_MANAGER_HPP
