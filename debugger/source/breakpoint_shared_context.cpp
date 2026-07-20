#include "breakpoint_shared_context.hpp"

#include <filesystem>

#include "analysis/source/source_manager.hpp"
#include "dap_breakpoint_validator.hpp"
#include "dap_types.hpp"
#include "runtime/compiler/chunk.hpp"

namespace luma::dap {

FileId BreakpointSharedContext::find_file_id(std::string_view abs_path) const {
    if (source_locator == nullptr) {
        return kFileIdNotFound;
    }

    // Tier 1: O(1) direct string match.
    const auto id = source_locator->find_file_id(abs_path);

    if (id.has_value()) {
        return *id;
    }

    // Tier 2: canonical path cache (handles drive-letter / symlink differences).
    std::error_code ec;
    const auto canonical_target = std::filesystem::weakly_canonical(abs_path, ec);
    const auto canonical_str = canonical_target.string();

    if (const auto it = canonical_path_cache.find(canonical_str);
        it != canonical_path_cache.end()) {
        return it->second;
    }

    // Tier 3: full scan — populate the cache for future lookups.
    scan_for_canonical_match(canonical_target, canonical_str);

    if (const auto it = canonical_path_cache.find(canonical_str);
        it != canonical_path_cache.end()) {
        return it->second;
    }

    return kFileIdNotFound;
}

void BreakpointSharedContext::scan_for_canonical_match(
    const std::filesystem::path& canonical_target, const std::string& canonical_str) const {
    source_locator->for_each_file([&](FileId fid, const SourceFile* file) {
        std::error_code ec;
        const auto canonical_file = std::filesystem::weakly_canonical(file->path, ec);

        if (canonical_file == canonical_target) {
            canonical_path_cache[canonical_str] = fid;
        }
    });
}

const std::set<int>& BreakpointSharedContext::collect_executable_lines(FileId file_id) const {
    auto [it, inserted] = source_map_cache.try_emplace(file_id);

    if (!inserted) {
        return it->second;
    }

    auto& lines = it->second;

    if (compiled_top_level) {
        for (const auto& [offset, loc] : compiled_top_level->chunk().source_map) {
            if (loc.file_id == file_id) {
                lines.insert(loc.line);
            }
        }
    }

    if (compiled_functions) {
        for (const auto& compiled_function : *compiled_functions) {
            for (const auto& [offset, loc] : compiled_function.chunk().source_map) {
                if (loc.file_id == file_id) {
                    lines.insert(loc.line);
                }
            }
        }
    }

    return lines;
}

Breakpoint build_base_breakpoint_response(int bp_id, bool verified, int line,
                                          const std::string& source_path,
                                          const std::string& hit_condition) {
    Breakpoint breakpoint;
    breakpoint.id = bp_id;
    breakpoint.verified = verified;
    breakpoint.line = line;

    if (!source_path.empty()) {
        breakpoint.source.path = source_path;
        breakpoint.source.name = std::filesystem::path(source_path).filename().string();
    }

    append_hit_condition_warning(breakpoint, hit_condition);

    return breakpoint;
}

} // namespace luma::dap
