#include "lsp_workspace_indexer.hpp"

#include <algorithm>
#include <cctype>
#include <format>

#include "lsp_exception_utils.hpp"

namespace luma::lsp {

WorkspaceIndexer::WorkspaceIndexer(const std::atomic<bool>& running) : running_(running) {}

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

std::size_t WorkspaceIndexer::scan(const std::vector<std::string>& roots,
                                   WorkspaceScanObserver& observer) {
    namespace fs = std::filesystem;

    std::size_t count{0};

    try {
        for (const auto& root : roots) {
            if (!running_.load()) {
                break;
            }

            std::error_code ec;
            if (!fs::is_directory(root, ec)) {
                continue;
            }

            for (auto it = fs::recursive_directory_iterator(
                     root, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!running_.load()) {
                    break;
                }
                if (ec) {
                    continue;
                }
                if (count >= k_max_files) {
                    observer.on_log(std::format("Workspace scan capped at {} files", k_max_files));
                    break;
                }

                const auto& entry = *it;
                if (!entry.is_regular_file(ec)) {
                    continue;
                }
                if (entry.path().extension() != ".luma") {
                    continue;
                }

                try {
                    observer.on_file_found(entry.path().string());
                    ++count;

                    if (count % 10 == 0) {
                        observer.on_progress(count);
                    }
                } catch (const std::exception& e) {
                    observer.on_log(
                        std::format("Error loading {}: {}", entry.path().string(), e.what()));
                }
            }

            if (count >= k_max_files) {
                break;
            }
        }
    } catch (const std::exception& e) {
        observer.on_log(std::format("Workspace scan error: {}", e.what()));
    } catch (...) {
        observer.on_log(std::format("Workspace scan failed: {}", format_current_exception()));
    }

    return count;
}

bool WorkspaceIndexer::is_in_workspace(const std::string& file_path,
                                       const std::vector<std::string>& roots) {
    for (const auto& root : roots) {
        if (path_starts_with(file_path, root)) {
            return true;
        }
    }
    return false;
}

} // namespace luma::lsp
