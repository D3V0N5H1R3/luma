#ifndef LUMA_DAP_BREAKPOINT_SHARED_CONTEXT_HPP
#define LUMA_DAP_BREAKPOINT_SHARED_CONTEXT_HPP

#include <atomic>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/string_hash.hpp"
#include "dap_types.hpp"
#include "debugger_config.hpp"
#include "i_source_locator.hpp"

namespace luma {
struct CompiledFunction;
} // namespace luma

namespace luma::dap {

// ─── Breakpoint hit snapshot ───
//
// A lightweight, copyable snapshot of a matched breakpoint's identity and
// hit-evaluation fields, returned by the per-type managers'
// find_matching_breakpoint methods so the runtime check can evaluate
// conditions without holding a reference into manager-owned state.
struct BreakpointSnapshot {
    int id{0};
    int times_hit{0};
    std::string condition;
    std::string hit_condition;
    std::string log_message;
};

// Build a snapshot of a matched breakpoint.  When `record_hit` is true the
// snapshot also records the hit by incrementing the breakpoint's saturating hit
// counter; when false it reports the current count without advancing it (used
// to read a conditional breakpoint's identity before its condition is known to
// hold).  Shared by the line and function breakpoint managers, whose info
// structs expose the same fields (and a mutable times_hit accessed under
// ctx_->mutex).
template <typename BreakpointInfo>
[[nodiscard]] BreakpointSnapshot make_breakpoint_snapshot(const BreakpointInfo& info,
                                                          bool record_hit) {
    if (record_hit && info.times_hit < std::numeric_limits<int>::max()) {
        info.times_hit++;
    }

    return BreakpointSnapshot{.id = info.id,
                              .times_hit = info.times_hit,
                              .condition = info.condition,
                              .hit_condition = info.hit_condition,
                              .log_message = info.log_message};
}

// ═══════════════════════════════════════════════════════════
// BreakpointSharedContext — shared infrastructure for all
// breakpoint sub-managers.
//
// Owns the single mutex, caches, compiled program references,
// source locator, and the breakpoint ID allocator.  Passed by
// non-owning pointer to each sub-manager so they share a
// single lock and consistent view of compiled state.
//
// ─── Synchronisation model ───
// The mutex is a leaf-level lock — it is never held while
// acquiring any session-level lock (ThreadStates → PerThread →
// Config), and no session lock is ever acquired while this
// mutex is held.  This prevents deadlock.
// ═══════════════════════════════════════════════════════════

struct BreakpointSharedContext {
    // Single mutex protects all breakpoint collections across all
    // sub-managers.  Leaf-level lock — independent of the
    // OrderedLockGuard hierarchy.
    mutable std::mutex mutex;

    // Source map cache: file_id → set of executable lines.
    // Mutable because collect_executable_lines() is a const method
    // that lazily populates this cache.
    mutable std::unordered_map<FileId, std::set<int>> source_map_cache; // GUARDED_BY(mutex)

    // Canonical path cache: canonical path → file_id.
    // Mutable because find_file_id() is a const method that caches
    // the result of expensive canonical path comparisons.
    mutable StringMap<FileId> canonical_path_cache; // GUARDED_BY(mutex)

    // Non-owning reference set via configuration.
    ISourceLocator* source_locator{nullptr};

    // Compiled program references.
    std::shared_ptr<std::vector<CompiledFunction>> compiled_functions;
    std::shared_ptr<CompiledFunction> compiled_top_level;

    // ─── Breakpoint ID allocation ───
    //
    // Session-assigned breakpoint IDs start at k_initial_breakpoint_id (1)
    // and increment atomically via next_breakpoint_id.  These positive IDs
    // uniquely identify verified breakpoints within a debug session.
    //
    // Pre-launch (unverified) breakpoints use negative IDs (starting at -1,
    // decrementing) so they never collide with session-assigned positive IDs.
    // Once the program launches and breakpoints are resolved against compiled
    // source maps, they receive positive IDs from this allocator.
    //
    // ID 0 is reserved as a sentinel meaning "no breakpoint".
    static constexpr int k_initial_breakpoint_id = config::breakpoint::k_initial_breakpoint_id;
    std::atomic<int> next_breakpoint_id{k_initial_breakpoint_id};

    // ─── Shared helpers ───

    // Resolve an absolute path to a file_id via the source locator.
    // Must be called under mutex.
    // Uses a 3-tier lookup: direct match → canonical path cache → full scan.
    [[nodiscard]] FileId find_file_id(std::string_view abs_path) const;

    // Collect executable lines for a file_id from the compiled program.
    // Must be called under mutex.
    //
    // Returns a reference into the internal source_map_cache (lazily populated
    // on first request per file_id).  Callers must consume the result while
    // holding `mutex`; the reference stays valid until set_compiled_program()
    // clears the cache.  Returning by reference avoids copying the whole set on
    // every breakpoint set/resolve/locations query.
    [[nodiscard]] const std::set<int>& collect_executable_lines(FileId file_id) const;

private:
    // Scan all known source files for one whose canonical path matches
    // canonical_target. Populates canonical_path_cache on a hit.
    // Must be called under mutex.
    void scan_for_canonical_match(const std::filesystem::path& canonical_target,
                                  const std::string& canonical_str) const;
};

// ─── Shared breakpoint response builder ───

// Build the common fields of a DAP Breakpoint response (id, verified,
// line, source path/name, hit-condition warning).  Per-type managers
// call this and then add type-specific fields.
[[nodiscard]] Breakpoint build_base_breakpoint_response(int bp_id, bool verified, int line,
                                                        const std::string& source_path,
                                                        const std::string& hit_condition);

} // namespace luma::dap

#endif // LUMA_DAP_BREAKPOINT_SHARED_CONTEXT_HPP
