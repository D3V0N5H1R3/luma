#ifndef LUMA_LSP_WORKSPACE_MANAGER_HPP
#define LUMA_LSP_WORKSPACE_MANAGER_HPP

// ═══════════════════════════════════════════════════════════
// WorkspaceManager — workspace roots and project configuration.
//
// Extracted from LspServer to give workspace-related state a
// single owner.  LspServer delegates workspace operations to
// this helper via composition.
// ═══════════════════════════════════════════════════════════

#include <functional>
#include <string>
#include <vector>

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

    // ─── Project configuration ───

    // Load and apply a luma.json file to the given config.
    void load_project_config(const std::string& path, LspConfig& config,
                             const LogCallback& log = {});

    // Scan workspace roots for the first luma.json and apply it.
    void discover_project_config(LspConfig& config, const LogCallback& log = {});

private:
    std::vector<std::string> workspace_roots_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_WORKSPACE_MANAGER_HPP
