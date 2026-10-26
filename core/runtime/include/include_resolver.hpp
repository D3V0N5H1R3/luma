#ifndef LUMA_INCLUDE_INCLUDE_RESOLVER_HPP
#define LUMA_INCLUDE_INCLUDE_RESOLVER_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/source/source_manager.hpp"
#include "common/string_hash.hpp"

namespace luma {

// Callback that lexes and parses a source file into a Program.
// Decouples IncludeResolver from the concrete Lexer/Parser so that
// tests can supply a mock implementation returning pre-built ASTs.
//
// The callback receives the loaded source file and a diagnostics vector
// to which it should append any errors or warnings produced during
// lexing/parsing.  Returns std::nullopt on failure.
using ParseFileCallback = std::function<std::optional<Program>(
    const SourceFile& source_file, std::vector<Diagnostic>& diagnostics)>;

// Returns the default ParseFileCallback that uses the standard Lexer
// and Parser.  Defined in include_resolver.cpp to keep Lexer/Parser
// includes out of the header.
[[nodiscard]] ParseFileCallback make_default_parse_callback();

// Loads `path` into `source_manager` like SourceManager::try_load(), except
// that a path the manager has already registered is re-read from disk instead
// of returning the registered (possibly stale) text.  The fresh text is
// registered as a virtual file named "<path> (reload N)" with a new file id;
// the suffix contains no path separator, so the file's directory — the anchor
// for its relative includes — is unchanged, and earlier text stays alive for
// diagnostics that point into it.
[[nodiscard]] LoadResult load_fresh_source(SourceManager& source_manager, std::string_view path);

// Forward declarations for implementation-only types.
class InclusionStack;

// ── IncludeResolver ─────────────────────────────────────────────────────────
//
// Resolves `include "path"` declarations in the AST by loading and
// parsing the referenced files, then merging their declarations into
// the program's declaration list.
//
// After resolution, all IncludeDeclaration nodes are removed and replaced
// by the declarations from the included files.
//
// Relative include paths resolve against the including file's directory (or,
// for REPL/stdin input with no file, the current working directory); if the
// file is not found there, the LUMA_PATH directories are searched in order.
//
// ── Responsibilities ──
//
//  1. Path validation  — reject directory traversal, symbolic links, and
//     paths that escape the allowed directory tree.  Entry points:
//     resolve_include_file(), is_within_allowed_directories(), resolve_path().
//
//  2. Depth limiting   — enforce a maximum include nesting depth to guard
//     against runaway recursion.  Circular and self includes need no separate
//     cycle check: the include-once rule prevents any file from being entered
//     twice.  Entry point: enforce_depth_limit().
//
// ── Include-once rule ──
//
// By default a file counts as already included once it is registered in the
// Source Manager, which is right for a single program: the entry file is
// registered before resolution, and every included file as it is entered.
// One input of a multi-input session (the REPL) needs session mode instead —
// see set_session_included().
//
// ── Coupling note ──
//
// The only analysis-module dependency that remains in this header is
// SourceManager.  Lexer and Parser are injected via ParseFileCallback
// (defaulting to the standard implementation), which keeps the header
// decoupled and enables isolated testing with mock parse callbacks.
class IncludeResolver {
public:
    // Maximum nesting depth for includes.
    static constexpr std::size_t k_max_include_depth = 64;

    // Construct with the default Lexer/Parser implementation.
    explicit IncludeResolver(SourceManager& source_manager);

    // Construct with a custom parse callback (for testing or alternative frontends).
    IncludeResolver(SourceManager& source_manager, ParseFileCallback parse_callback);

    // Resolve all include declarations in the program.
    // Loads, parses, and merges each included file.
    // Returns false if any error diagnostics were emitted.
    [[nodiscard]] bool resolve(Program& program);

    // Returns all diagnostics (errors and warnings) emitted during the last resolve() call.
    [[nodiscard]] const std::vector<Diagnostic>& get_diagnostics() const {
        return diagnostics_;
    }

    // Switches the resolver to session mode, for one input of a session whose
    // inputs may be rejected after their includes were resolved (the REPL).
    // A rejected input's included files stay registered in the Source Manager
    // although their declarations never reached the session, so registration
    // cannot be the include-once rule there: only the canonical paths in
    // `already_included` (the files of the session's committed inputs, plus
    // the input's own file) and the files this resolver has already included
    // count as included.  A file the Source Manager has registered before is
    // re-read from disk (load_fresh_source), so edits made since then apply.
    void set_session_included(StringSet already_included);

    // Canonical paths of the files the last resolve() call read, in the order
    // they were included.  A session commits these once its input is kept.
    [[nodiscard]] const std::vector<std::string>& included_files() const noexcept {
        return included_files_;
    }

private:
    // Returns true when the include-once rule says `canonical` was already
    // included (see set_session_included() for session mode).
    [[nodiscard]] bool is_already_included(const std::string& canonical) const;

    // Recursively resolve includes, tracking the include nesting depth for
    // the depth limit (cycles need no tracking — see enforce_depth_limit).
    // Returns false if any error was emitted.
    [[nodiscard]] bool resolve_includes(Program& program, InclusionStack& inclusion_stack);

    // Process a single include declaration: validate, resolve, parse,
    // and merge the included file's declarations into `merged`.
    // Returns false if an error diagnostic was emitted.
    [[nodiscard]] bool process_include(const IncludeDeclaration& include_decl,
                                       InclusionStack& inclusion_stack,
                                       std::vector<DeclarationPtr>& merged, Program& program);

    //=== Path validation ===

    // Validate the include path, resolve it to a canonical filesystem path,
    // and verify symlink and directory-escape safety.
    // Returns the canonical path, or std::nullopt on error.
    [[nodiscard]] std::optional<std::string>
    resolve_include_file(const IncludeDeclaration& include_decl);

    // Reject an include whose declared path contains directory-traversal
    // components (. or ..).  Emits a diagnostic and returns false on rejection.
    [[nodiscard]] bool reject_traversal(const std::filesystem::path& include_path,
                                        const IncludeDeclaration& include_decl);

    // Reject an include whose resolved path chain contains a symbolic link.
    // Emits a diagnostic and returns false on rejection.
    [[nodiscard]] bool reject_symlinks(const std::filesystem::path& resolved_path,
                                       const IncludeDeclaration& include_decl);

    // Enforce that the canonical include path stays within the allowed
    // directory tree (the including file's base directory or a LUMA_PATH
    // entry).  Emits a diagnostic and returns false on rejection.
    [[nodiscard]] bool enforce_allowed_root(const std::filesystem::path& canonical_path,
                                            const IncludeDeclaration& include_decl);

    // Resolve the include path against the directory of the file that
    // contains the include declaration — or, for REPL/stdin input that has no
    // file, the current working directory — then against each LUMA_PATH
    // directory in order.  Returns the first existing candidate, or
    // std::nullopt when none exists (a file-anchored include never falls back
    // to the working directory).
    [[nodiscard]] std::optional<std::filesystem::path>
    resolve_path(const std::filesystem::path& include_path, FileId file_id) const;

    // The directory relative includes in `file_id` resolve against, or
    // std::nullopt when the include comes from REPL/stdin input with no file.
    // An empty path means the directory could not be determined, which makes
    // the containment check fail closed.
    [[nodiscard]] std::optional<std::filesystem::path> including_directory(FileId file_id) const;

    // Returns true if the canonical path is within the base directory of the
    // including file or any of the configured search paths (LUMA_PATH).
    [[nodiscard]] bool is_within_allowed_directories(const std::filesystem::path& canonical_path,
                                                     const std::filesystem::path& base_dir) const;

    // Populate search_paths_ from the LUMA_PATH environment variable.
    void init_search_paths();

    //=== Depth limiting ===

    // Enforce the maximum include nesting depth.  Returns false and emits a
    // diagnostic when the current depth reaches k_max_include_depth.
    //
    // Circular and self includes are not detected here: the include-once rule
    // (checked before this call) already prevents any file from being entered
    // more than once, so no cycle can recurse.
    [[nodiscard]] bool enforce_depth_limit(const IncludeDeclaration& include_decl,
                                           const InclusionStack& inclusion_stack);

    //=== Include processing helpers ===

    // Load an included file via SourceManager (re-read from disk in session
    // mode).  Returns the loaded SourceFile on success, or nullptr on error.
    [[nodiscard]] const SourceFile* load_include_file(const std::string& canonical,
                                                      const IncludeDeclaration& include_decl);

    // Parse an included file into a Program using the parse callback.
    // Returns the parsed Program on success, or std::nullopt on error.
    [[nodiscard]] std::optional<Program> parse_include_file(const SourceFile& source_file,
                                                            const IncludeDeclaration& include_decl);

    // Recursively resolve nested includes and merge declarations/statements.
    // Returns false if an error was emitted.
    [[nodiscard]] bool merge_include_ast(Program& included_program,
                                         const IncludeDeclaration& include_decl,
                                         InclusionStack& inclusion_stack,
                                         std::vector<DeclarationPtr>& merged,
                                         Program& parent_program);

    //=== Diagnostics ===

    // Emit a diagnostic with the given severity, message, location, and optional hint.
    void emit_diagnostic(Severity severity, std::string_view message,
                         const SourceLocation& location, std::string_view hint = {});

    SourceManager& source_manager_;
    ParseFileCallback parse_callback_;
    std::vector<Diagnostic> diagnostics_;
    std::vector<std::filesystem::path>
        search_paths_; // Pre-computed fs::path objects from LUMA_PATH.
    // Session mode only: the canonical paths that count as already included,
    // grown by every file this resolver includes.
    std::optional<StringSet> session_included_;
    std::vector<std::string> included_files_; // Files read by the last resolve().
};

} // namespace luma

#endif // LUMA_INCLUDE_INCLUDE_RESOLVER_HPP
