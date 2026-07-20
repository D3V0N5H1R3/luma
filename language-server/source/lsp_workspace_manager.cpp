#include "lsp_workspace_manager.hpp"

#include <filesystem>
#include <format>
#include <fstream>

#include "json/json.hpp"
#include "lsp_config.hpp"

namespace luma::lsp {

using luma::json::JsonValue;

// ─── Workspace roots ───

void WorkspaceManager::add_root(const std::string& path) {
    workspace_roots_.push_back(path);
}

const std::vector<std::string>& WorkspaceManager::roots() const noexcept {
    return workspace_roots_;
}

bool WorkspaceManager::has_roots() const noexcept {
    return !workspace_roots_.empty();
}

bool WorkspaceManager::is_in_workspace(const std::string& path) const {
    return WorkspaceIndexer::is_in_workspace(path, workspace_roots_);
}

// ─── Indexing state ───

bool WorkspaceManager::is_indexing() const noexcept {
    return indexing_in_progress_.load(std::memory_order_acquire);
}

void WorkspaceManager::set_indexing(bool value) noexcept {
    indexing_in_progress_.store(value, std::memory_order_release);
}

// ─── Persisted index ───

PersistedIndex& WorkspaceManager::persisted_index() noexcept {
    return persisted_index_;
}

const PersistedIndex& WorkspaceManager::persisted_index() const noexcept {
    return persisted_index_;
}

bool WorkspaceManager::load_persisted_index(const LogCallback& log) {
    if (workspace_roots_.empty()) {
        return false;
    }

    const auto index_path = PersistedIndex::default_path(workspace_roots_[0]);

    if (!persisted_index_.load(index_path)) {
        return false;
    }

    if (log) {
        log(std::format("Loaded persisted index ({} files)", persisted_index_.size()));
    }

    return true;
}

std::size_t WorkspaceManager::validate_persisted_index() {
    return persisted_index_.validate();
}

bool WorkspaceManager::save_persisted_index(const LogCallback& log) {
    if (workspace_roots_.empty()) {
        return false;
    }

    const auto index_path = PersistedIndex::default_path(workspace_roots_[0]);

    if (!persisted_index_.save(index_path)) {
        return false;
    }

    if (log) {
        log(std::format("Saved persisted index ({} files)", persisted_index_.size()));
    }

    return true;
}

// ─── Project configuration ───

void WorkspaceManager::load_project_config(const std::string& path, LspConfig& config,
                                           const LogCallback& log) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        if (log) {
            log("luma.json not found or inaccessible: " + path);
        }
        return;
    }

    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            return;
        }

        const std::string content((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());

        auto parsed = JsonValue::parse(content);
        if (!parsed.is_object()) {
            if (log) {
                log("luma.json: expected root object");
            }
            return;
        }

        config.apply_project_config(parsed);

        if (log) {
            log(std::format("Loaded luma.json from {}", path));
        }
    } catch (const std::exception& e) {
        if (log) {
            log(std::format("Error parsing luma.json: {}", e.what()));
        }
    }
}

void WorkspaceManager::discover_project_config(LspConfig& config, const LogCallback& log) {
    namespace fs = std::filesystem;

    for (const auto& root : workspace_roots_) {
        const auto config_path = (fs::path(root) / "luma.json").string();
        std::error_code ec;
        if (fs::exists(config_path, ec) && !ec) {
            load_project_config(config_path, config, log);
            break;
        }
    }
}

} // namespace luma::lsp
