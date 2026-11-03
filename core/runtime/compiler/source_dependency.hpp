#ifndef LUMA_COMPILER_SOURCE_DEPENDENCY_HPP
#define LUMA_COMPILER_SOURCE_DEPENDENCY_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "analysis/source/source_location.hpp"

namespace luma {

class SourceManager;

// A source file, other than the entry point, that a compiled program was built
// from — in practice every file pulled in through `include`.  The compilation
// caches (in-memory CompilationCache and on-disk .lumc) key on the entry
// point's text alone, so they record these to detect an edited include.
struct SourceDependency {
    std::string path;              // Canonical absolute path, as loaded.
    std::uint64_t content_hash{0}; // fnv1a_hash of the text (UTF-8 BOM stripped).

    [[nodiscard]] bool operator==(const SourceDependency&) const = default;
};

// Collect every on-disk file in `source_manager` except `root` (the entry
// point, already covered by the caller's own source hash).  Virtual,
// compiler-injected sources have no file to re-check and are skipped.
[[nodiscard]] std::vector<SourceDependency>
collect_source_dependencies(const SourceManager& source_manager, FileId root);

// Return true when every dependency still exists and its text hashes to the
// recorded value.  A missing, unreadable, oversized, or edited file returns
// false so the caller recompiles — which also reports any real error.
//
// Limitation: this re-checks the files that were included, not include
// resolution itself.  A new file that would now shadow a resolved include
// (e.g. one added earlier on LUMA_PATH) is not noticed until a dependency or
// the entry point changes.
[[nodiscard]] bool are_source_dependencies_current(std::span<const SourceDependency> dependencies);

} // namespace luma

#endif // LUMA_COMPILER_SOURCE_DEPENDENCY_HPP
