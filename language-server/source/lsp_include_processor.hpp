#ifndef LUMA_LSP_INCLUDE_PROCESSOR_HPP
#define LUMA_LSP_INCLUDE_PROCESSOR_HPP

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

#include "analysis/ast/declaration.hpp"
#include "lsp_analysis_result.hpp"

namespace luma::lsp::include_processor {

// Merge declarations from included programs into the main program.
// Include files are read from disk at most once per analysis (via an internal
// pre-read map) and their token streams are reused from `cache_snapshot` when
// the content hash matches; freshly lexed files are staged into `cache_updates`
// for the caller to fold back into the shared cache.
void merge_include_declarations(Program& program, const std::filesystem::path& base_dir,
                                const IncludeCacheMap& cache_snapshot,
                                IncludeCacheMap& cache_updates, AnalysisResult& result,
                                const std::function<void(const std::string&)>& log);

} // namespace luma::lsp::include_processor

#endif // LUMA_LSP_INCLUDE_PROCESSOR_HPP
