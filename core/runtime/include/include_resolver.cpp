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

LoadResult load_fresh_source(SourceManager& source_manager, std::string_view path) {
    if (!source_manager.is_loaded(path)) {
        return source_manager.try_load(path);
    }

    // The Source Manager returns the registered entry for a path it has seen,
    // so re-read the file through a scratch manager (same open, size-limit,
    // and BOM handling) and register the fresh text under a new, unique name.
    SourceManager scratch;
    const LoadResult fresh = scratch.try_load(path);

    if (!fresh) {
        return fresh;
    }

    const auto reload_name =
        std::format("{} (reload {})", fresh.file->path, source_manager.files().size() + 1);

    return LoadResult{.file = &source_manager.load_virtual(reload_name, fresh.file->text)};
}

IncludeResolver::IncludeResolver(SourceManager& source_manager)
    : IncludeResolver{source_manager, default_parse_file} {}

IncludeResolver::IncludeResolver(SourceManager& source_manager, ParseFileCallback parse_callback)
    : source_manager_{source_manager}, parse_callback_{std::move(parse_callback)} {
    init_search_paths();
}

void IncludeResolver::set_session_included(StringSet already_included) {
    session_included_ = std::move(already_included);
}

bool IncludeResolver::resolve(Program& program) {
    diagnostics_.clear();
    included_files_.clear();

    // The inclusion stack tracks only nesting depth.  It needs no seed: the
    // entry file already counts as included (it is registered in the Source
    // Manager, or listed in the session set), so any include that loops back
    // to it is skipped by the include-once check in process_include rather
    // than by tracking it here.
    InclusionStack inclusion_stack;

    return resolve_includes(program, inclusion_stack);
}

bool IncludeResolver::is_already_included(const std::string& canonical) const {
    if (session_included_) {
        return session_included_->contains(canonical);
    }

    return source_manager_.is_loaded(canonical);
}

std::optional<std::string>
IncludeResolver::resolve_include_file(const IncludeDeclaration& include_decl) {
    // Include paths come from UTF-8 source text; decode them as UTF-8 rather
    // than in the ANSI code page std::filesystem::path{std::string} uses on MSVC.
    const auto decoded_path = try_path_from_utf8(include_decl.path);

    if (!decoded_path) {
        emit_diagnostic(
            Severity::Error,
            std::format("include rejected: '{}' is not a valid UTF-8 path", include_decl.path),
            include_decl.location, "save the source file as UTF-8");
        return std::nullopt;
    }

    const auto& include_fs_path = *decoded_path;

    // Reject directory traversal before any filesystem interaction.
    if (!reject_traversal(include_fs_path, include_decl)) {
        return std::nullopt;
    }

    // Resolve against the including file's directory (or the working directory
    // for REPL/stdin input), then LUMA_PATH.
    const auto resolved_fs_path = resolve_path(include_fs_path, include_decl.location.file_id);

    if (!resolved_fs_path) {
        const bool anchored = including_directory(include_decl.location.file_id).has_value();

        emit_diagnostic(
            Severity::Error,
            std::format("cannot load included file '{}': file not found", include_decl.path),
            include_decl.location,
            anchored ? "searched the including file's directory and LUMA_PATH"
                     : "searched the current working directory and LUMA_PATH");
        return std::nullopt;
    }

    // Single consolidated symlink check on the entire resolved path chain.
    // This catches both leaf symlinks and intermediate directory symlinks
    // that could redirect the include.
    //
    // Note: true TOCTOU protection is impossible without OS-level guarantees
    // (O_NOFOLLOW etc.).  A single check on the resolved path is the best we
    // can do in portable C++.
    if (!reject_symlinks(*resolved_fs_path, include_decl)) {
        return std::nullopt;
    }

    // Canonicalize, falling back to the raw resolved path if the filesystem
    // cannot resolve it.
    const auto canonical_fs_path = canonicalize_or(*resolved_fs_path, *resolved_fs_path);

    // Verify the canonical path stays within allowed directories.
    if (!enforce_allowed_root(canonical_fs_path, include_decl)) {
        return std::nullopt;
    }

    // The Source Manager keys and opens files by UTF-8 path strings.
    return path_to_utf8(canonical_fs_path);
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
    const auto including_dir = including_directory(include_decl.location.file_id);

    if (!including_dir) {
        // No including file on disk (REPL/stdin) — there is no directory to
        // anchor containment against. Relative includes still resolve against
        // the working directory, but an absolute include path could reach
        // anywhere on the filesystem, so reject those explicitly.
        const auto raw = path_from_utf8(include_decl.path);

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

    const auto base_dir = canonicalize_or(*including_dir, {});

    if (base_dir.empty()) {
        // Fail closed: without a canonical base directory the enclosure check
        // cannot be performed, and skipping it would accept an absolute
        // include from anywhere on the filesystem.
        emit_diagnostic(
            Severity::Error,
            std::format("include rejected: cannot resolve the directory of the file including '{}'",
                        include_decl.path),
            include_decl.location, "the including file's directory must exist and be accessible");
        return false;
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

    if (is_already_included(*canonical)) {
        return true;
    }

    if (!enforce_depth_limit(include_decl, inclusion_stack)) {
        return false;
    }

    const auto* source_file = load_include_file(*canonical, include_decl);

    if (source_file == nullptr) {
        return false;
    }

    // Record the file before resolving its own includes, so an include that
    // loops back to it is skipped.  (Loading already registered it in the
    // Source Manager, which is the include-once rule outside session mode.)
    if (session_included_) {
        session_included_->insert(*canonical);
    }

    included_files_.push_back(*canonical);

    auto included_program = parse_include_file(*source_file, include_decl);

    if (!included_program) {
        return false;
    }

    return merge_include_ast(*included_program, include_decl, inclusion_stack, merged, program);
}

const SourceFile* IncludeResolver::load_include_file(const std::string& canonical,
                                                     const IncludeDeclaration& include_decl) {
    // In session mode a registered path may hold the text of an earlier,
    // rejected input; re-read it so the include sees the file as it is now.
    const LoadResult load_result = session_included_ ? load_fresh_source(source_manager_, canonical)
                                                     : source_manager_.try_load(canonical);

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

std::optional<std::filesystem::path> IncludeResolver::including_directory(FileId file_id) const {
    const auto* source_file = source_manager_.get_file(file_id);

    if ((source_file == nullptr) || source_file->path.empty()) {
        return std::nullopt; // REPL / stdin input (FileId 0) has no file.
    }

    // A bracketed virtual source such as "<repl:3>" — an input the REPL
    // registered so runtime errors can show its text — has no file either.
    // (A real path never normalises to a bracketed name; a virtual reload of a
    // file keeps its real directory and stays anchored.)
    if (source_file->is_virtual && source_file->path.starts_with('<')) {
        return std::nullopt;
    }

    // The Source Manager stores paths as UTF-8.  A name that does not decode
    // yields an empty directory, which makes the containment check fail closed.
    const auto file_path = try_path_from_utf8(source_file->path);
    auto dir = file_path ? file_path->parent_path() : std::filesystem::path{};

    if (dir.empty() && file_path) {
        // A bare file name (e.g. a virtual source) lives in the working
        // directory.  If even that cannot be determined, the empty path makes
        // the containment check fail closed.
        std::error_code ec;
        dir = std::filesystem::current_path(ec);

        if (ec) {
            dir.clear();
        }
    }

    return dir;
}

std::optional<std::filesystem::path>
IncludeResolver::resolve_path(const std::filesystem::path& include_path, FileId file_id) const {
    std::error_code ec;

    // Return the first existing candidate formed by joining `dir` with the
    // requested include path.  Non-throwing: a filesystem error simply means
    // the candidate does not resolve here, so the caller tries the next dir.
    const auto try_dir =
        [&](const std::filesystem::path& dir) -> std::optional<std::filesystem::path> {
        auto candidate = dir / include_path;

        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }

        return std::nullopt;
    };

    // First the base directory: the including file's own directory, or — for
    // REPL/stdin input, which has no file — the current working directory
    // (Manual §25).  A file-anchored include never falls back to the working
    // directory: that would load a different file than the including file's
    // location implies, or misreport a missing file as escaping the tree.
    if (const auto dir = including_directory(file_id)) {
        if (auto found = try_dir(*dir)) {
            return found;
        }
    } else {
        const auto cwd = std::filesystem::current_path(ec);

        if (!ec) {
            if (auto found = try_dir(cwd)) {
                return found;
            }
        }
    }

    // Then each LUMA_PATH search directory, in order.
    for (const auto& dir : search_paths_) {
        if (auto found = try_dir(dir)) {
            return found;
        }
    }

    return std::nullopt;
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
        // safe_getenv returns UTF-8, so decode the entry as UTF-8.
        const auto dir = path_from_utf8(token);

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
