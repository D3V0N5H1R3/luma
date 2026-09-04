#include "lsp_analysis_service_impl.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include "analysis/ast/ast_dispatch.hpp"
#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/errors/error.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/linter/linter.hpp"
#include "analysis/parser/parser.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/type_checker.hpp"
#include "lsp_constants.hpp"
#include "lsp_diagnostic_builder.hpp"
#include "lsp_exception_utils.hpp"
#include "lsp_include_processor.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_types.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp {

using diagnostic_builder::make_diagnostic;
using diagnostic_builder::make_whole_file_diagnostic;
using luma::protocol::uri_to_path;

namespace {

// Runs a single pipeline phase. Calls fn() and catches any exception,
// returning the exception message or empty string on success.
template <typename Fn> [[nodiscard]] std::string run_phase(Fn&& fn) {
    try {
        std::forward<Fn>(fn)();
        return {};
    } catch (...) {
        return format_current_exception();
    }
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════

LspAnalysisService::LspAnalysisService(const LspConfig& config,
                                       const std::atomic<bool>& cancel_flag,
                                       AnalysisCallbacks callbacks)
    : config_{config}, cancel_flag_{cancel_flag}, callbacks_{std::move(callbacks)} {}

// ═══════════════════════════════════════════════════════════
// Include cache management
// ═══════════════════════════════════════════════════════════

void LspAnalysisService::erase_cached_file(const std::string& path) {
    const std::unique_lock lock(cache_mutex_);
    include_cache_.erase(path);
}

// ═══════════════════════════════════════════════════════════
// Cancellation / deadline
// ═══════════════════════════════════════════════════════════

void LspAnalysisService::check_cancellation_and_deadline(
    const std::chrono::steady_clock::time_point& deadline, std::string_view phase_name) const {
    if (cancel_flag_.load(std::memory_order_acquire)) {
        throw std::runtime_error("Analysis cancelled");
    }
    if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error(std::format("Analysis timeout in {} phase", phase_name));
    }
}

// ═══════════════════════════════════════════════════════════
// Pipeline phases
// ═══════════════════════════════════════════════════════════

LspAnalysisService::LexResult LspAnalysisService::lex_phase(const std::string& source) {
    DiagnosticCollector collector;
    Lexer lexer(source, collector);
    auto tokens = lexer.tokenize();
    return LexResult{.tokens = std::move(tokens), .warnings = collector.take_diagnostics()};
}

void LspAnalysisService::include_phase(Program& program, const std::string& uri,
                                       AnalysisResult& result) {
    if (uri.empty()) {
        return;
    }

    // Snapshot the include cache under shared lock so we don't race with
    // erase_cached_file (called from the main thread for watched file changes).
    // The cache stores shared_ptr<const IncludeCache>, so this copies pointers
    // rather than each cached file's whole token vector.
    IncludeCacheMap cache_snapshot;
    {
        const std::shared_lock lock(cache_mutex_);
        include_cache_.for_each(
            [&cache_snapshot](const std::string& key, const IncludeCachePtr& value) {
                cache_snapshot[key] = value;
            });
    }

    IncludeCacheMap cache_updates;

    const auto error = run_phase([&] {
        const auto file_path_opt = uri_to_path(uri);
        const std::string file_path = file_path_opt.value_or("");
        if (file_path.empty()) {
            // Cannot resolve includes without a valid file path.
            return;
        }
        const auto base_dir = std::filesystem::path(file_path).parent_path();
        const auto log = [this](const std::string& msg) {
            callbacks_.log(msg);
        };

        include_processor::merge_include_declarations(program, base_dir, cache_snapshot,
                                                      cache_updates, result, log);

        // Apply cache updates under exclusive lock.
        if (!cache_updates.empty()) {
            const std::unique_lock lock(cache_mutex_);
            for (auto& [key, entry] : cache_updates) {
                (void)include_cache_.put(key, std::move(entry));
            }
        }
    });
    if (!error.empty()) {
        callbacks_.log(std::format("Include phase error for {}: {}", uri, error));
        result.semantic.diagnostics.push_back(make_whole_file_diagnostic(
            std::format("Include resolution failed: {}", error), constants::severity::warning));
    }
}

void LspAnalysisService::type_check_phase(Program& program, AnalysisResult& result,
                                          const std::string& source, const std::string& uri,
                                          FileId prelude_file_id) {
    const auto error = run_phase([&] {
        TypeChecker checker;
        auto type_errors = checker.check(program, false);

        const auto from_prelude = [&](const luma::Diagnostic& diag) {
            return prelude_file_id != 0 && diag.primary_location().file_id == prelude_file_id;
        };

        for (const auto& err : type_errors) {
            if (from_prelude(err)) {
                continue;
            }
            result.semantic.diagnostics.push_back(make_diagnostic(err, source, uri));
        }

        for (const auto& warning : checker.get_warnings()) {
            if (from_prelude(warning)) {
                continue;
            }
            result.semantic.diagnostics.push_back(make_diagnostic(warning, source, uri));
        }
    });
    if (!error.empty()) {
        callbacks_.log(std::format("Type-check phase error for {}: {}", uri, error));
        result.semantic.diagnostics.push_back(make_whole_file_diagnostic(
            std::format("Type checking failed: {}", error), constants::severity::warning));
    }
}

void LspAnalysisService::doc_comment_phase(AnalysisResult& result, const std::string& source) {
    std::unordered_map<int, std::string> decl_lines;
    for (const auto& [name, def] : result.semantic.symbols.definitions) {
        decl_lines[def.location.line] = name;
    }
    for (const auto& [name, info] : result.semantic.symbols.user_functions) {
        decl_lines[info.location.line] = name;
    }

    const std::string_view src{source};
    std::vector<std::string_view> comment_block;
    int line_num = 1;
    std::size_t pos = 0;
    while (pos <= src.size()) {
        auto eol = src.find('\n', pos);
        if (eol == std::string_view::npos) {
            eol = src.size();
        }
        const std::string_view line_content = src.substr(pos, eol - pos);
        const auto first_char = line_content.find_first_not_of(" \t");
        if (first_char != std::string_view::npos && line_content[first_char] == '#') {
            std::string_view comment_text = line_content.substr(first_char + 1);
            if (!comment_text.empty() && comment_text.front() == ' ') {
                comment_text.remove_prefix(1);
            }
            comment_block.push_back(comment_text);
        } else {
            if (!comment_block.empty()) {
                auto dl_it = decl_lines.find(line_num);
                if (dl_it != decl_lines.end()) {
                    std::string doc;
                    for (const auto& cl : comment_block) {
                        if (!doc.empty()) {
                            doc += "\n";
                        }
                        doc += cl;
                    }
                    result.semantic.symbols.doc_comments[dl_it->second] = std::move(doc);
                }
                comment_block.clear();
            }
        }
        pos = eol + 1;
        ++line_num;
        if (eol == src.size()) {
            break;
        }
    }
}

void LspAnalysisService::call_graph_phase(const Program& program, AnalysisResult& result) {
    collect_call_graph(program.declarations, result);
}

void LspAnalysisService::lint_phase(const Program& program, AnalysisResult& result,
                                    const std::string& source, const std::string& uri,
                                    const std::vector<std::size_t>& line_starts,
                                    FileId prelude_file_id) {
    const auto lint_error = run_phase([&] {
        Linter linter;
        auto lint_warnings = linter.lint(program);
        for (const auto& lw : lint_warnings) {
            if (prelude_file_id != 0 && lw.primary_location().file_id == prelude_file_id) {
                continue;
            }
            result.semantic.diagnostics.push_back(make_diagnostic(lw, source, uri, line_starts));
        }
    });
    if (!lint_error.empty()) {
        callbacks_.log(std::format("Lint phase error for {}: {}", uri, lint_error));
        result.semantic.diagnostics.push_back(make_whole_file_diagnostic(
            std::format("Lint pass failed: {}", lint_error), constants::severity::warning));
    }
}

void LspAnalysisService::build_symbol_origins(AnalysisResult& result) {
    auto& includes = result.semantic.includes;
    if (includes.file_id_to_path.empty()) {
        return;
    }

    const auto origin_for = [&](auto file_id) -> const std::string* {
        if (file_id == 0) {
            return nullptr;
        }
        const auto it = includes.file_id_to_path.find(file_id);
        return it != includes.file_id_to_path.end() ? &it->second : nullptr;
    };

    for (const auto& [name, def] : result.semantic.symbols.definitions) {
        if (const auto* path = origin_for(def.location.file_id)) {
            includes.symbol_origins[name] = *path;
        }
    }
    for (const auto& [name, info] : result.semantic.symbols.user_functions) {
        if (const auto* path = origin_for(info.location.file_id)) {
            includes.symbol_origins[name] = *path;
        }
    }
}

AnalysisResult LspAnalysisService::analyze(const std::string& uri, const std::string& source,
                                           std::chrono::steady_clock::time_point deadline) {
    AnalysisResult result;

    // Build the partial result and return it with a user-visible warning when
    // the analysis deadline has passed between pipeline phases.
    auto timeout_result = [&]() -> AnalysisResult {
        result.semantic.diagnostics.push_back(make_whole_file_diagnostic(
            "Analysis timed out — results may be incomplete", constants::severity::warning));
        callbacks_.log(std::format("Analysis timed out for {}", uri));
        callbacks_.notify(
            "window/showMessage",
            JsonValue(JsonValue::ObjectType{
                {"type", JsonValue(static_cast<int64_t>(constants::message_type::warning))},
                {"message", JsonValue("Luma analysis timed out. Diagnostics may be incomplete.")},
            }));
        build_token_index(result);
        build_identifier_index(result);
        return std::move(result);
    };

    // Build a quiet, stale result when analysis was aborted because a newer
    // edit (or a freshly opened document at startup) requested cancellation.
    // Unlike a real timeout, cancellation is a routine internal event, so it
    // must never inject a warning diagnostic or pop a "timed out" message —
    // the caller re-schedules the URI and skips publishing this result.
    auto cancelled_result = [&]() -> AnalysisResult {
        result.metadata.cancelled = true;
        callbacks_.log(std::format("Analysis cancelled for {}", uri));
        build_token_index(result);
        build_identifier_index(result);
        return std::move(result);
    };

    const auto line_starts = compute_line_starts(source);

    // Retain the analysed source and its line offsets so request/response
    // positions can be translated between the lexer's codepoint columns and
    // LSP's UTF-16 wire columns (see PositionEncoder). Set before the try so
    // every return path — success, timeout (via timeout_result), and the error
    // fall-through — carries them.
    result.metadata.source_text = source;
    result.metadata.line_starts = line_starts;

    try {
        if (run_pipeline_phases(uri, source, deadline, result, line_starts)) {
            return result;
        }
    } catch (const luma::RuntimeError& e) {
        // Convert RuntimeError to a luma::Diagnostic, then to an LSP Diagnostic.
        auto luma_diag = luma::DiagnosticBuilder{luma::Severity::Error, std::string{e.what()}}
                             .category(luma::DiagnosticCategory::Runtime)
                             .primary(e.location())
                             .build();

        if (e.hint()) {
            luma_diag.hint = *e.hint();
        }

        result.semantic.diagnostics.push_back(make_diagnostic(luma_diag, source, uri, line_starts));
    } catch (const std::runtime_error& e) {
        // Timeout exceptions thrown by check_cancellation_and_deadline — return
        // partial results with a warning rather than treating as an internal error.
        const std::string_view msg{e.what()};
        if (msg == "Analysis cancelled") {
            return cancelled_result();
        }
        if (msg.starts_with("Analysis timeout")) {
            return timeout_result();
        }
        callbacks_.log(std::format("Analysis pipeline error for {}: {}", uri, e.what()));
        result.semantic.diagnostics.push_back(make_whole_file_diagnostic(
            std::format("Internal error: {}", e.what()), constants::severity::error));
    } catch (const std::exception& e) {
        callbacks_.log(std::format("Analysis pipeline error for {}: {}", uri, e.what()));
        result.semantic.diagnostics.push_back(make_whole_file_diagnostic(
            std::format("Internal error: {}", e.what()), constants::severity::error));
    } catch (...) {
        const std::string detail = format_current_exception();
        callbacks_.log(std::format("Analysis pipeline unexpected error for {}: {}", uri, detail));
        result.semantic.diagnostics.push_back(make_whole_file_diagnostic(
            "Internal error: unknown exception during analysis", constants::severity::error));
    }

    build_token_index(result);
    build_identifier_index(result);

    return result;
}

bool LspAnalysisService::run_pipeline_phases(const std::string& uri, const std::string& source,
                                             std::chrono::steady_clock::time_point deadline,
                                             AnalysisResult& result,
                                             const std::vector<std::size_t>& line_starts) {
    // Phase 1: Lex.
    check_cancellation_and_deadline(deadline, "lex");
    auto lex_result = lex_phase(source);
    result.semantic.tokens = std::move(lex_result.tokens);

    for (const auto& warning : lex_result.warnings) {
        result.semantic.diagnostics.push_back(make_diagnostic(warning, source, {}, line_starts));
    }

    // Phase 2: Parse.
    check_cancellation_and_deadline(deadline, "parse");
    Parser parser(result.semantic.tokens);
    auto program = parser.parse();

    for (const auto& err : parser.get_errors()) {
        result.semantic.diagnostics.push_back(make_diagnostic(err, source, {}, line_starts));
    }

    if (!parser.get_errors().empty()) {
        // Collect symbols from the valid portion of the AST so that
        // hover, go-to-definition, and completion still work.
        const auto sym_error = run_phase([&] {
            symbol_phase(program, result);
            collect_call_graph(program.declarations, result);
        });
        if (!sym_error.empty()) {
            callbacks_.log(std::format("Symbol collection failed after parse errors for {}: {}",
                                       uri, sym_error));
        }
        result.metadata.cached_program = std::move(program);
        build_token_index(result);
        build_identifier_index(result);
        return true;
    }

    // Phase 3: Include resolution.
    check_cancellation_and_deadline(deadline, "include");
    include_phase(program, uri, result);

    // Phase 4: Symbol collection.
    check_cancellation_and_deadline(deadline, "symbol");
    symbol_phase(program, result);

    // Phase 4a: Doc comment extraction.
    check_cancellation_and_deadline(deadline, "doc-comment");
    doc_comment_phase(result, source);

    // Phase 4a2: Build symbol origins from file_id_to_path.
    build_symbol_origins(result);

    // Phase 4b: Call graph extraction.
    check_cancellation_and_deadline(deadline, "call-graph");
    call_graph_phase(program, result);

    // Phase 4c: no built-in prelude is injected; user code is compiled as-is.
    FileId prelude_file_id = 0;

    // Phase 5: Type checking.
    check_cancellation_and_deadline(deadline, "type-check");
    type_check_phase(program, result, source, uri, prelude_file_id);

    // Phase 6: Lint.
    check_cancellation_and_deadline(deadline, "lint");
    lint_phase(program, result, source, uri, line_starts, prelude_file_id);

    result.metadata.cached_program = std::move(program);

    return false;
}

std::vector<std::size_t> LspAnalysisService::compute_line_starts(const std::string& source) {
    std::vector<std::size_t> line_starts;
    line_starts.push_back(0);

    for (std::size_t i{0}; i < source.size(); ++i) {
        if (source[i] == '\n') {
            line_starts.push_back(i + 1);
        }
    }

    return line_starts;
}

} // namespace luma::lsp
