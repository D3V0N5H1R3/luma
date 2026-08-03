#include "lsp_include_processor.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_constants.hpp"
#include "lsp_exception_utils.hpp"
#include "lsp_path_utils.hpp"
#include "lsp_position_utils.hpp"
#include "lsp_types.hpp"

namespace luma::lsp::include_processor {

// Pre-read file content used to batch disk I/O across all includes.
// Kept as a named struct (rather than std::pair) for readability at call sites.
struct PreReadContent {
    std::string content;
    std::size_t content_hash{0};
};

// Map from resolved file path to pre-read content.
using PreReadMap = std::unordered_map<std::string, PreReadContent>;

// Bundles the mutable state threaded through recursive include processing.
// Avoids passing multiple maps/sets as separate parameters.
struct IncludeProcessingContext {
    const IncludeCacheMap& cache_snapshot;
    IncludeCacheMap& cache_updates;
    PreReadMap& pre_read;
    std::unordered_set<std::string>& included_set;
    AnalysisResult& result;
};

namespace {

// ─── Include-phase helpers ────────────────────────────────

// Copy cached include tokens and stamp them with the file id reserved for this
// include during the current analysis.
//
// Cached tokens carry whatever file id the include held when it was first
// lexed. Because the include cache is shared across every document's analysis
// (and file ids are assigned per-analysis in include-processing order starting
// at 1), a cache hit can reuse tokens whose baked-in file id no longer matches
// the id reserved for this file this time round. build_symbol_origins maps each
// symbol's location.file_id through file_id_to_path, so reusing a stale id would
// attribute the include's symbols to the wrong file (or none). Re-tagging keeps
// the reused tokens consistent with the freshly reserved id. The Parser owns its
// tokens by value, so this single copy replaces the copy it would make anyway.
[[nodiscard]] std::vector<Token> retag_tokens(const std::vector<Token>& cached_tokens,
                                              FileId file_id) {
    std::vector<Token> tokens = cached_tokens;
    for (auto& token : tokens) {
        token.location.file_id = file_id;
    }
    return tokens;
}

// Construct an include diagnostic at the location of the include declaration.
[[nodiscard]] Diagnostic make_include_diagnostic(const IncludeDeclaration& inc, int severity,
                                                 const std::string& message,
                                                 const std::string& code) {
    const int line = inc.location.line - 1;
    // inc.location is the `include` keyword token, and Token.location records
    // the 1-based column *past* the lexeme's last character (see
    // Lexer::add_token, which stamps the location after scanning). So
    // `keyword_end` is the 0-based column immediately following "include" —
    // i.e. the single ASCII space that separates the keyword from the path
    // string literal on a top-level include declaration.
    const int keyword_end = inc.location.column - 1;
    // The squiggle width must be measured in the client's UTF-16 code units, the
    // same coordinate space every other diagnostic uses (see
    // lsp_diagnostic_builder). inc.path is UTF-8, so path.size() is a *byte*
    // count that over-measures the width for any non-ASCII path; convert it to
    // UTF-16 code units instead. The start column is the include keyword, which
    // on a top-level declaration is preceded on its line only by ASCII
    // whitespace, so its codepoint column already equals its UTF-16 column and
    // needs no conversion.
    const int path_width = byte_offset_to_utf16_column(inc.path, inc.path.size());
    // The path string literal starts one column past the keyword-end (the
    // separating space), at its opening quote, and spans the path plus both
    // surrounding quote characters.
    const int path_start = keyword_end + 1;
    const int path_end = path_start + path_width + 2;
    return Diagnostic{
        .range = Range{.start = Position{.line = line, .character = path_start},
                       .end = Position{.line = line, .character = path_end}},
        .severity = severity,
        .source = std::string(constants::diagnostic::source),
        .message = message,
        .code = code,
        .code_description = {},
    };
}

// Validate an include path for security: reject absolute paths, directory
// traversal, and symbolic links.  Returns true if the path is valid.
[[nodiscard]] bool validate_include_path(const IncludeDeclaration& inc,
                                         const std::filesystem::path& resolved_path,
                                         AnalysisResult& result,
                                         const std::function<void(const std::string&)>& log) {
    if (!is_safe_include_path(inc.path)) {
        const auto inc_fs_path = std::filesystem::path(inc.path);
        const auto msg =
            inc_fs_path.is_absolute()
                ? std::format("Include rejected: '{}' is an absolute path", inc.path)
                : std::format("Include rejected: '{}' contains directory traversal", inc.path);
        log(msg);
        result.semantic.diagnostics.push_back(
            make_include_diagnostic(inc, constants::severity::error, msg, "E4004"));
        return false;
    }

    if (!is_safe_resolved_path(resolved_path)) {
        const auto msg = std::format("Include rejected: '{}' is a symbolic link", inc.path);
        log(msg);
        result.semantic.diagnostics.push_back(
            make_include_diagnostic(inc, constants::severity::error, msg, "E4004"));
        return false;
    }

    return true;
}

// Load an include file from disk and lex it, using the token cache when
// the content hash matches.
struct LoadedInclude {
    Program program;
    std::vector<Token> tokens;
    std::size_t content_hash{0};
    bool cache_hit{false};
};

constexpr std::uintmax_t k_max_include_bytes = luma::lsp::constants::limits::max_file_bytes;

// Read a single include file, using the pre-read batch map when the file
// was already loaded.
[[nodiscard]] std::optional<std::pair<std::string, std::size_t>>
read_include_content(const IncludeDeclaration& inc, const std::filesystem::path& inc_path,
                     IncludeProcessingContext& ctx) {
    const auto inc_key = inc_path.string();

    auto pre_it = ctx.pre_read.find(inc_key);
    if (pre_it != ctx.pre_read.end()) {
        return std::pair{pre_it->second.content, pre_it->second.content_hash};
    }

    // File not yet cached — read from disk and cache for reuse by later includes.
    auto file_data = read_file_with_hash(inc_path, k_max_include_bytes);
    if (!file_data) {
        // Distinguish between size-exceeded and not-found.
        std::error_code ec;
        if (std::filesystem::exists(inc_path, ec) && !ec) {
            ctx.result.semantic.diagnostics.push_back(make_include_diagnostic(
                inc, constants::severity::error,
                std::format("Include file '{}' exceeds size limit", inc.path), "E4006"));
        } else {
            ctx.result.semantic.diagnostics.push_back(make_include_diagnostic(
                inc, constants::severity::error,
                std::format("Include file not found: '{}'", inc.path), "E4005"));
        }
        return std::nullopt;
    }

    // Cache for potential reuse by later includes or nested includes.
    ctx.pre_read[inc_key] =
        PreReadContent{.content = file_data->first, .content_hash = file_data->second};

    return file_data;
}

[[nodiscard]] std::optional<LoadedInclude>
load_and_cache_include_file(const IncludeDeclaration& inc, const std::filesystem::path& inc_path,
                            IncludeProcessingContext& ctx) {
    const auto inc_key = inc_path.string();

    auto content_pair = read_include_content(inc, inc_path, ctx);
    if (!content_pair) {
        return std::nullopt;
    }

    const auto& [inc_src, inc_hash] = *content_pair;

    auto inc_cache_it = ctx.cache_snapshot.find(inc_key);
    const bool cache_hit = inc_cache_it != ctx.cache_snapshot.end() && inc_cache_it->second &&
                           inc_cache_it->second->content_hash == inc_hash &&
                           !inc_cache_it->second->cached_tokens.empty();

    // Reserve this include's file id before parsing so the mapping is recorded
    // even when parsing fails (matching the previous behaviour).
    const auto file_id = ctx.result.semantic.includes.next_file_id;
    ctx.result.semantic.includes.file_id_to_path[file_id] = inc_key;
    ++ctx.result.semantic.includes.next_file_id;

    LoadedInclude loaded;
    loaded.content_hash = inc_hash;
    loaded.cache_hit = cache_hit;

    if (cache_hit) {
        // Reuse the cached tokens, re-tagged with this analysis's file id so the
        // include's symbols resolve to the right origin (see retag_tokens). The
        // Parser owns its tokens by value, so this copies once; no cache
        // write-back is needed on a hit.
        Parser inc_parser(retag_tokens(inc_cache_it->second->cached_tokens, file_id));
        auto inc_program = inc_parser.parse();
        if (!inc_parser.get_errors().empty()) {
            return std::nullopt;
        }
        loaded.program = std::move(inc_program);
        return loaded;
    }

    // Cache miss: lex the file and retain the tokens for write-back.
    DiagnosticCollector inc_collector;
    Lexer inc_lexer(inc_src, inc_collector, file_id);
    auto inc_tokens = inc_lexer.tokenize();

    Parser inc_parser(inc_tokens);
    auto inc_program = inc_parser.parse();

    if (!inc_parser.get_errors().empty()) {
        // NOTE: Skipped includes (circular, too large, unresolvable) are silently ignored.
        // The analysis still produces diagnostics for unresolved symbols, which indirectly
        // surfaces the problem to the user. Explicit include-skip notifications were
        // considered but deemed too noisy for the typical editing workflow.
        return std::nullopt;
    }

    loaded.program = std::move(inc_program);
    loaded.tokens = std::move(inc_tokens);
    return loaded;
}

// Check whether a nested include should be processed.
[[nodiscard]] bool
should_process_nested_include(const std::string& include_path,
                              const std::filesystem::path& resolved_path,
                              const std::unordered_set<std::string>& already_included) {
    if (!is_safe_include_path(include_path)) {
        return false;
    }
    if (!std::filesystem::exists(resolved_path)) {
        return false;
    }
    if (!is_safe_resolved_path(resolved_path)) {
        return false;
    }
    return !already_included.contains(resolved_path.string());
}

// Process a single nested include: read content, lex, parse, and collect
// its declarations.
[[nodiscard]] std::optional<std::vector<std::unique_ptr<Declaration>>>
process_single_nested_include(const IncludeDeclaration& parent_inc,
                              const IncludeDeclaration& nested_inc,
                              const std::filesystem::path& nested_path,
                              IncludeProcessingContext& ctx) {
    const auto nk = nested_path.string();
    std::string nsrc;
    std::size_t nhash{0};

    // Use pre-read content when available to avoid redundant disk I/O.
    auto pre_it = ctx.pre_read.find(nk);
    if (pre_it != ctx.pre_read.end()) {
        nsrc = pre_it->second.content;
        nhash = pre_it->second.content_hash;
    } else {
        auto file_data = read_file_with_hash(nested_path);
        if (!file_data) {
            return std::nullopt;
        }
        nsrc = std::move(file_data->first);
        nhash = file_data->second;
        // Cache for potential reuse by later nested includes.
        ctx.pre_read[nk] = PreReadContent{.content = nsrc, .content_hash = nhash};
    }

    // Reserve this include's file id before parsing so the mapping is recorded
    // even when parsing fails (matching the previous behaviour).
    const auto file_id = ctx.result.semantic.includes.next_file_id;
    ctx.result.semantic.includes.file_id_to_path[file_id] = nk;
    ++ctx.result.semantic.includes.next_file_id;

    // Consult the token cache by content hash — mirrors the top-level path so
    // unchanged nested files are not re-lexed on every analysis.
    auto n_cache_it = ctx.cache_snapshot.find(nk);
    const bool cache_hit = n_cache_it != ctx.cache_snapshot.end() && n_cache_it->second &&
                           n_cache_it->second->content_hash == nhash &&
                           !n_cache_it->second->cached_tokens.empty();

    Program nprog;
    if (cache_hit) {
        // Re-tag the reused tokens with this analysis's file id (see
        // retag_tokens) so nested-include symbols resolve to the right origin.
        Parser nparser(retag_tokens(n_cache_it->second->cached_tokens, file_id));
        nprog = nparser.parse();
        if (!nparser.get_errors().empty()) {
            ctx.result.semantic.diagnostics.push_back(make_include_diagnostic(
                parent_inc, constants::severity::warning,
                std::format("Parse errors in nested include '{}'", nested_inc.path), "W0010"));
            return std::nullopt;
        }
    } else {
        DiagnosticCollector n_collector;
        Lexer nlexer(nsrc, n_collector, file_id);
        auto ntokens = nlexer.tokenize();
        Parser nparser(ntokens);
        nprog = nparser.parse();
        if (!nparser.get_errors().empty()) {
            ctx.result.semantic.diagnostics.push_back(make_include_diagnostic(
                parent_inc, constants::severity::warning,
                std::format("Parse errors in nested include '{}'", nested_inc.path), "W0010"));
            return std::nullopt;
        }
        // Stage the freshly lexed tokens so unchanged nested files hit the
        // cache on subsequent analyses.
        ctx.cache_updates[nk] = std::make_shared<const IncludeCache>(
            IncludeCache{.content_hash = nhash, .cached_tokens = std::move(ntokens)});
    }

    ctx.result.semantic.includes.included_paths.push_back(nk);
    ctx.included_set.insert(nk);
    return std::move(nprog.declarations);
}

// Recursively resolve nested include declarations inside an already-parsed
// included program.
void resolve_nested_includes(const IncludeDeclaration& parent_inc, Program& inc_program,
                             const std::filesystem::path& inc_path, IncludeProcessingContext& ctx) {
    const auto inc_dir = inc_path.parent_path();
    std::vector<std::unique_ptr<Declaration>> nested_decls;

    for (auto& d : inc_program.declarations) {
        if (d->kind != DeclarationKind::Include) {
            nested_decls.push_back(std::move(d));
            continue;
        }

        const auto& nested_inc = static_cast<const IncludeDeclaration&>(*d);
        const auto nested_path = inc_dir / std::filesystem::path(nested_inc.path);

        if (!should_process_nested_include(nested_inc.path, nested_path, ctx.included_set)) {
            continue;
        }

        try {
            auto decls = process_single_nested_include(parent_inc, nested_inc, nested_path, ctx);
            if (decls) {
                for (auto& nd : *decls) {
                    nested_decls.push_back(std::move(nd));
                }
            }
        } catch (const std::exception& ex) {
            ctx.result.semantic.diagnostics.push_back(make_include_diagnostic(
                parent_inc, constants::severity::warning,
                std::format("Error processing nested include '{}': {}", nested_inc.path, ex.what()),
                "W0010"));
        } catch (...) {
            ctx.result.semantic.diagnostics.push_back(
                make_include_diagnostic(parent_inc, constants::severity::warning,
                                        std::format("Error processing nested include '{}': {}",
                                                    nested_inc.path, format_current_exception()),
                                        "W0010"));
        }
    }

    inc_program.declarations = std::move(nested_decls);
}

} // anonymous namespace

// Processes a single top-level include: validates, loads, and merges its declarations.
static void process_single_include(const IncludeDeclaration& inc,
                                   const std::filesystem::path& base_dir,
                                   IncludeProcessingContext& ctx,
                                   std::vector<std::unique_ptr<Declaration>>& merged_decls,
                                   const std::function<void(const std::string&)>& log) {
    const auto inc_path = base_dir / std::filesystem::path(inc.path);

    if (!validate_include_path(inc, inc_path, ctx.result, log)) {
        return;
    }

    const auto inc_key = inc_path.string();

    try {
        auto loaded = load_and_cache_include_file(inc, inc_path, ctx);
        if (!loaded) {
            return;
        }

        if (!loaded->cache_hit) {
            ctx.cache_updates[inc_key] = std::make_shared<const IncludeCache>(IncludeCache{
                .content_hash = loaded->content_hash, .cached_tokens = std::move(loaded->tokens)});
        }

        const bool already_included = ctx.included_set.contains(inc_key);
        ctx.result.semantic.includes.included_paths.push_back(inc_key);
        ctx.included_set.insert(inc_key);

        if (!already_included) {
            resolve_nested_includes(inc, loaded->program, inc_path, ctx);
        }

        for (auto& d : loaded->program.declarations) {
            merged_decls.push_back(std::move(d));
        }
    } catch (const std::exception& e) {
        log(std::format("Include resolution failed for '{}': {}", inc_key, e.what()));
        ctx.result.semantic.diagnostics.push_back(make_include_diagnostic(
            inc, constants::severity::warning,
            std::format("Failed to resolve include: {}", e.what()), "W0010"));
    }
}

void merge_include_declarations(Program& program, const std::filesystem::path& base_dir,
                                const IncludeCacheMap& cache_snapshot,
                                IncludeCacheMap& cache_updates, AnalysisResult& result,
                                const std::function<void(const std::string&)>& log) {
    // Single O(N) pass over declarations.  Include files are read lazily via
    // read_include_content() which caches each file in pre_read on first access,
    // so any file referenced more than once (e.g. as both a top-level and a
    // nested include) is only read from disk once.  The previous two-pass design
    // (batch_preread_include_files + processing loop) has been collapsed here to
    // eliminate the redundant first scan of the declarations list.
    PreReadMap pre_read;

    // ── Process each top-level declaration ──
    std::vector<std::unique_ptr<Declaration>> merged_decls;
    std::unordered_set<std::string> included_set;

    IncludeProcessingContext ctx{.cache_snapshot = cache_snapshot,
                                 .cache_updates = cache_updates,
                                 .pre_read = pre_read,
                                 .included_set = included_set,
                                 .result = result};

    for (auto& decl : program.declarations) {
        if (decl->kind != DeclarationKind::Include) {
            merged_decls.push_back(std::move(decl));
            continue;
        }

        const auto& inc = static_cast<const IncludeDeclaration&>(*decl);
        process_single_include(inc, base_dir, ctx, merged_decls, log);
    }

    program.declarations = std::move(merged_decls);
}

} // namespace luma::lsp::include_processor
