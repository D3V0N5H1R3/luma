#ifndef LUMA_LSP_WORKSPACE_INDEXER_HPP
#define LUMA_LSP_WORKSPACE_INDEXER_HPP

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// WorkspaceScanObserver — observer interface for workspace
// file discovery events.
// ═══════════════════════════════════════════════════════════

class WorkspaceScanObserver {
public:
    virtual ~WorkspaceScanObserver() = default;

    virtual void on_file_found(const std::string& /*path*/) {}

    virtual void on_progress(std::size_t /*file_count*/) {}

    virtual void on_log(const std::string& /*message*/) {}
};

// ═══════════════════════════════════════════════════════════
// WorkspaceIndexer — separated workspace file discovery.
//
// Responsible for recursively scanning workspace roots for
// .luma files and notifying an observer for each discovered
// file.  Does not manage document state or analysis — that
// stays in LspServer.
// ═══════════════════════════════════════════════════════════

class WorkspaceIndexer {
public:
    // Maximum number of files to index before stopping.
    static constexpr std::size_t k_max_files = 500;

    // Construct with the running flag (checked to allow early termination).
    explicit WorkspaceIndexer(const std::atomic<bool>& running);

    // Scan all workspace roots for .luma files.
    // Returns the number of files discovered.
    [[nodiscard]] std::size_t scan(const std::vector<std::string>& roots,
                                   WorkspaceScanObserver& observer);

    // Check if a path is within one of the workspace roots.
    // Case-insensitive on Windows.
    [[nodiscard]] static bool is_in_workspace(const std::string& file_path,
                                              const std::vector<std::string>& roots);

private:
    const std::atomic<bool>& running_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_WORKSPACE_INDEXER_HPP
