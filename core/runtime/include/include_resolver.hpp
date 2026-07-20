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
// If LUMA_PATH is set, its directories are searched (in order) when a
// file is not found relative to the including file's directory.
//
// ── Responsibilities ──
//
//  1. Path validation  — reject directory traversal, symbolic links, and
//     paths that escape the allowed directory tree.  Entry points:
//     resolve_include_file(), is_within_allowed_directories(), resolve_path().
//
//  2. Depth limiting   — enforce a maximum include nesting depth to guard
//     against runaway recursion.  Circular and self includes need no separate
//     cycle check: the Source Manager's include-once registry prevents any
//     file from being entered twice.  Entry point: enforce_depth_limit().
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

private:
    // Recursively resolve includes, tracking the inclusion stack for
    // cycle detection.  Returns false if any error was emitted.
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

    // Resolve a relative include path against the directory of the
    // file that contains the include declaration.
    // Falls back to LUMA_PATH directories if not found locally.
    [[nodiscard]] std::string resolve_path(std::string_view include_path, FileId file_id) const;

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
    // Circular and self includes are not detected here: SourceManager's
    // include-once registry (checked before this call) already prevents any
    // file from being entered more than once, so no cycle can recurse.
    [[nodiscard]] bool enforce_depth_limit(const IncludeDeclaration& include_decl,
                                           const InclusionStack& inclusion_stack);

    //=== Include processing helpers ===

    // Load an included file via SourceManager.
    // Returns the loaded SourceFile on success, or nullptr on error.
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
};

} // namespace luma

#endif // LUMA_INCLUDE_INCLUDE_RESOLVER_HPP
