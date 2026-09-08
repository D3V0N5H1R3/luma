#include "lsp_workspace_manager.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>

#include "json/json.hpp"
#include "lsp_config.hpp"

namespace luma::lsp {

using luma::json::JsonValue;

namespace {

// Case-insensitive path prefix check on Windows where drive letters and
// directory names may differ in casing.
[[nodiscard]] bool path_starts_with(const std::string& path, const std::string& prefix) {
    // Strip trailing separators from prefix.
    std::size_t prefix_len = prefix.size();
    while (prefix_len > 0 && (prefix[prefix_len - 1] == '/' || prefix[prefix_len - 1] == '\\')) {
        --prefix_len;
    }
    if (path.size() <= prefix_len) {
        return false;
    }
    const auto chars_equal = [](char lhs, char rhs) {
        const auto a = static_cast<unsigned char>(lhs);
        const auto b = static_cast<unsigned char>(rhs);
#ifdef _WIN32
        return std::tolower(a) == std::tolower(b);
#else
        return a == b;
#endif
    };
    if (!std::equal(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(prefix_len),
                    prefix.begin(), prefix.begin() + static_cast<std::ptrdiff_t>(prefix_len),
                    chars_equal)) {
        return false;
    }
    // Ensure the prefix ends at a directory boundary.
    const auto next = path[prefix_len];
    return next == '/' || next == '\\';
}

} // namespace

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
    return std::ranges::any_of(workspace_roots_, [&path](const std::string& root) {
        return path_starts_with(path, root);
    });
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
