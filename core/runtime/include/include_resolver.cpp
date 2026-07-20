#include "runtime/include/include_resolver.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

#include "runtime/include/include_resolver_detail.hpp"

// These analysis-module headers are only needed for the default
// ParseFileCallback implementation.  When a custom callback is
// provided, Lexer/Parser are never used by this translation unit.
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"
#include "common/path_utils.hpp"
#include "common/platform_utils.hpp"
#include "common/string_utils.hpp"

namespace luma {

namespace {

// Default ParseFileCallback: lex and parse using the standard Lexer
// and Parser.  Propagates all diagnostics (errors and warnings).
std::optional<Program> default_parse_file(const SourceFile& source_file,
                                          std::vector<Diagnostic>& diagnostics) {
    DiagnosticCollector lexer_diagnostics;
    Lexer lexer{source_file.text, lexer_diagnostics, source_file.file_id};

    auto tokens = lexer.tokenize();

    std::ranges::copy(lexer_diagnostics.diagnostics(), std::back_inserter(diagnostics));

    if (lexer_diagnostics.has_errors()) {
        return std::nullopt;
    }

    Parser parser{std::move(tokens)};

    auto program = parser.parse();

    std::ranges::copy(parser.get_errors(), std::back_inserter(diagnostics));

    if (!parser.get_errors().empty()) {
        return std::nullopt;
    }

    return program;
}

// Canonicalize `path` with weakly_canonical, falling back to `fallback` if the
// filesystem cannot resolve it.  Wraps the non-throwing error_code overload so
// callers never have to repeat the "canonicalize or keep the raw path" dance.
std::filesystem::path canonicalize_or(const std::filesystem::path& path,
                                      std::filesystem::path fallback) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);

    if (ec) {
        return fallback;
    }

    return canonical;
}

} // namespace

ParseFileCallback make_default_parse_callback() {
    return default_parse_file;
}

IncludeResolver::IncludeResolver(SourceManager& source_manager)
    : IncludeResolver{source_manager, default_parse_file} {}

IncludeResolver::IncludeResolver(SourceManager& source_manager, ParseFileCallback parse_callback)
    : source_manager_{source_manager}, parse_callback_{std::move(parse_callback)} {
    init_search_paths();
}

bool IncludeResolver::resolve(Program& program) {
    diagnostics_.clear();

    // The inclusion stack tracks only nesting depth.  It needs no seed: the
    // entry file is registered in the Source Manager before resolution begins,
    // so any include that loops back to it is skipped by the include-once
    // check in process_include rather than by tracking it here.
    InclusionStack inclusion_stack;

    return resolve_includes(program, inclusion_stack);
}

std::optional<std::string>
IncludeResolver::resolve_include_file(const IncludeDeclaration& include_decl) {
    const std::filesystem::path include_fs_path{include_decl.path};

    // Reject directory traversal before any filesystem interaction.
    if (!reject_traversal(include_fs_path, include_decl)) {
        return std::nullopt;
    }

    // Resolve the relative path against the including file's directory.
    const std::filesystem::path resolved_fs_path{
        resolve_path(include_decl.path, include_decl.location.file_id)};

    // Single consolidated symlink check on the entire resolved path chain.
    // This catches both leaf symlinks and intermediate directory symlinks
    // that could redirect the include.
    //
    // Note: true TOCTOU protection is impossible without OS-level guarantees
    // (O_NOFOLLOW etc.).  A single check on the resolved path is the best we
    // can do in portable C++.
    if (!reject_symlinks(resolved_fs_path, include_decl)) {
        return std::nullopt;
    }

    // Canonicalize, falling back to the raw resolved path if the filesystem
    // cannot resolve it (e.g. the file does not exist yet).
    const auto canonical_fs_path = canonicalize_or(resolved_fs_path, resolved_fs_path);

    // Verify the canonical path stays within allowed directories.
    if (!enforce_allowed_root(canonical_fs_path, include_decl)) {
        return std::nullopt;
    }

    return canonical_fs_path.string();
}

bool IncludeResolver::reject_traversal(const std::filesystem::path& include_path,
                                       const IncludeDeclaration& include_decl) {
    if (!has_directory_traversal(include_path)) {
        return true;
    }

    emit_diagnostic(
        Severity::Error,
        std::format("include rejected: '{}' contains directory traversal", include_decl.path),
        include_decl.location, "use paths relative to the current file without '..'");
    return false;
}

bool IncludeResolver::reject_symlinks(const std::filesystem::path& resolved_path,
                                      const IncludeDeclaration& include_decl) {
    if (!is_symlink_or_contains_symlinks(resolved_path)) {
        return true;
    }

    emit_diagnostic(Severity::Error,
                    std::format("include rejected: '{}' contains a symbolic link in its path chain",
                                include_decl.path),
                    include_decl.location,
                    "symbolic links are not allowed in include paths for security");
    return false;
}

bool IncludeResolver::enforce_allowed_root(const std::filesystem::path& canonical_path,
                                           const IncludeDeclaration& include_decl) {
    const auto* source_file = source_manager_.get_file(include_decl.location.file_id);

    if ((source_file == nullptr) || source_file->path.empty()) {
        // No including file on disk (REPL/stdin) — there is no directory to
        // anchor containment against. Relative includes still resolve against
        // the working directory, but an absolute include path could reach
        // anywhere on the filesystem, so reject those explicitly.
        const std::filesystem::path raw{include_decl.path};

        if (raw.is_absolute() || raw.has_root_path()) {
            emit_diagnostic(
                Severity::Error,
                std::format("include rejected: '{}' is an absolute path", include_decl.path),
                include_decl.location,
                "includes from the REPL or standard input must use relative paths");
            return false;
        }

        return true; // Relative include with no on-disk anchor — allowed.
    }

    const auto base_dir =
        canonicalize_or(std::filesystem::path{source_file->path}.parent_path(), {});

    if (base_dir.empty()) {
        return true; // Base directory unresolvable — skip the enclosure check.
    }

    if (is_within_allowed_directories(canonical_path, base_dir)) {
        return true;
    }

    emit_diagnostic(
        Severity::Error,
        std::format("include rejected: '{}' resolves outside the allowed directory tree",
                    include_decl.path),
        include_decl.location,
        "include paths must resolve within the source directory or LUMA_PATH directories");
    return false;
}

bool IncludeResolver::enforce_depth_limit(const IncludeDeclaration& include_decl,
                                          const InclusionStack& inclusion_stack) {
    if (inclusion_stack.depth() >= k_max_include_depth) {
        emit_diagnostic(Severity::Error,
                        std::format("include depth limit exceeded ({}) while including '{}'",
                                    k_max_include_depth, include_decl.path),
                        include_decl.location, "reduce the depth of nested includes");
        return false;
    }

    return true;
}

bool IncludeResolver::resolve_includes(Program& program, InclusionStack& inclusion_stack) {
    std::vector<DeclarationPtr> merged{};
    merged.reserve(program.declarations.size());

    bool success = true;

    for (auto& decl : program.declarations) {
        if (decl->kind != DeclarationKind::Include) {
            merged.push_back(std::move(decl));

            continue;
        }

        const auto& include_decl = static_cast<const IncludeDeclaration&>(*decl);

        if (!process_include(include_decl, inclusion_stack, merged, program)) {
            success = false;
        }
    }

    program.declarations = std::move(merged);

    return success;
}

bool IncludeResolver::process_include(const IncludeDeclaration& include_decl,
                                      InclusionStack& inclusion_stack,
                                      std::vector<DeclarationPtr>& merged, Program& program) {
    const auto canonical = resolve_include_file(include_decl);

    if (!canonical) {
        return false;
    }

    if (source_manager_.is_loaded(*canonical)) {
        return true;
    }

    if (!enforce_depth_limit(include_decl, inclusion_stack)) {
        return false;
    }

    const auto* source_file = load_include_file(*canonical, include_decl);

    if (source_file == nullptr) {
        return false;
    }

    auto included_program = parse_include_file(*source_file, include_decl);

    if (!included_program) {
        return false;
    }

    return merge_include_ast(*included_program, include_decl, inclusion_stack, merged, program);
}

const SourceFile* IncludeResolver::load_include_file(const std::string& canonical,
                                                     const IncludeDeclaration& include_decl) {
    const LoadResult load_result = source_manager_.try_load(canonical);

    if (!load_result) {
        emit_diagnostic(Severity::Error,
                        std::format("cannot load included file '{}': {}", include_decl.path,
                                    load_result.error.value_or("unknown error")),
                        include_decl.location);
        return nullptr;
    }

    return load_result.file;
}

std::optional<Program> IncludeResolver::parse_include_file(const SourceFile& source_file,
                                                           const IncludeDeclaration& include_decl) {
    std::vector<Diagnostic> parse_diagnostics;
    auto result = parse_callback_(source_file, parse_diagnostics);

    // Propagate all diagnostics from the included file so that users
    // see every error/warning on the first run rather than one at a time.
    for (auto& d : parse_diagnostics) {
        diagnostics_.push_back(std::move(d));
    }

    if (!result) {
        emit_diagnostic(Severity::Error,
                        std::format("errors in included file '{}'", include_decl.path),
                        include_decl.location, "fix the errors in the included file");
    }

    return result;
}

bool IncludeResolver::merge_include_ast(Program& included_program,
                                        const IncludeDeclaration& include_decl,
                                        InclusionStack& inclusion_stack,
                                        std::vector<DeclarationPtr>& merged,
                                        Program& parent_program) {
    const bool has_own_statements = !included_program.statements.empty();

    // RAII guard ensures the stack is popped even on early return.
    {
        const InclusionGuard guard{inclusion_stack};
        const bool nested_ok = resolve_includes(included_program, inclusion_stack);

        if (!nested_ok) {
            return false;
        }
    }

    if (has_own_statements) {
        const auto& first_stmt = included_program.statements.front();

        emit_diagnostic(Severity::Warning,
                        std::format("included file '{}' contains top-level statements "
                                    "that will execute before @main",
                                    include_decl.path),
                        first_stmt->location);
    }

    std::ranges::move(included_program.declarations, std::back_inserter(merged));
    std::ranges::move(included_program.statements, std::back_inserter(parent_program.statements));

    return true;
}

std::string IncludeResolver::resolve_path(std::string_view include_path, FileId file_id) const {
    std::error_code ec;

    // Return the first existing candidate formed by joining `dir` with the
    // requested include path.  Non-throwing: a filesystem error simply means
    // the candidate does not resolve here, so the caller tries the next dir.
    const auto try_dir = [&](const std::filesystem::path& dir) -> std::optional<std::string> {
        const auto candidate = dir / include_path;

        if (std::filesystem::exists(candidate, ec)) {
            return candidate.string();
        }

        return std::nullopt;
    };

    // First, the including file's own directory.
    const auto* source_file = source_manager_.get_file(file_id);

    if ((source_file != nullptr) && !source_file->path.empty()) {
        if (auto found = try_dir(std::filesystem::path{source_file->path}.parent_path())) {
            return *found;
        }
    }

    // Then each LUMA_PATH search directory, in order.
    for (const auto& dir : search_paths_) {
        if (auto found = try_dir(dir)) {
            return *found;
        }
    }

    // Fallback: treat as relative to the current working directory.
    return std::string{include_path};
}

bool IncludeResolver::is_within_allowed_directories(const std::filesystem::path& canonical_path,
                                                    const std::filesystem::path& base_dir) const {
    // Both canonical_path and base_dir are already canonical — skip re-canonicalization.
    if (!canonical_escapes_root(canonical_path, base_dir)) {
        return true;
    }

    // Allow paths within any LUMA_PATH search directory.  These are
    // pre-canonicalized in init_search_paths(), so the canonical comparison
    // needs no per-include filesystem access.
    for (const auto& search_dir : search_paths_) {
        if (!canonical_escapes_root(canonical_path, search_dir)) {
            return true;
        }
    }

    return false;
}

void IncludeResolver::init_search_paths() {
    const auto path_value = safe_getenv("LUMA_PATH");

    if (!path_value) {
        return;
    }

    // Split on platform-specific path separator.
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif

    for_each_split(*path_value, separator, [this](std::string_view token) {
        if (token.empty()) {
            return;
        }

        std::error_code ec;
        const std::filesystem::path dir{token};

        if (!std::filesystem::is_directory(dir, ec)) {
            return; // Skip missing or inaccessible LUMA_PATH entries.
        }

        // Store the canonical form so the enclosure checks in
        // is_within_allowed_directories() require no filesystem access.
        search_paths_.push_back(canonicalize_or(dir, dir));
    });
}

void IncludeResolver::emit_diagnostic(Severity severity, std::string_view message,
                                      const SourceLocation& location, std::string_view hint) {
    auto builder = DiagnosticBuilder{severity, std::string{message}}
                       .category(DiagnosticCategory::Compile)
                       .primary(location);

    if (!hint.empty()) {
        builder.hint(std::string{hint});
    }

    diagnostics_.push_back(builder.build());
}

} // namespace luma
