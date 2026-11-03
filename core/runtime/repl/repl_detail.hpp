#ifndef LUMA_REPL_REPL_DETAIL_HPP
#define LUMA_REPL_REPL_DETAIL_HPP

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/ast/ast_fwd.hpp"            // Program
#include "analysis/source/source_location.hpp" // FileId
#include "analysis/types/type_checker_fwd.hpp" // TypeChecker
#include "common/string_hash.hpp"              // StringSet
#include "runtime/interpreter/value_fwd.hpp"   // Value

namespace luma {

class VM;
class SourceManager;
class DiagnosticRenderer;
struct SourceFile;

namespace repl_detail {

// Include-once registry of a REPL session.
//
// An input's includes are resolved — their files read and registered in the
// session's SourceManager — before the input is type-checked, but they only
// count as included once the input itself is kept (keep_program).  A rejected
// input's files therefore stay eligible: the next input that includes them
// re-reads them from disk instead of skipping them as already included, which
// would leave their definitions missing from the session.
class ReplIncludeRegistry {
public:
    // Canonical paths of the files included by the session's kept inputs.
    [[nodiscard]] const StringSet& committed() const noexcept {
        return committed_;
    }

    // Records the files the current input read (its includes, and for `:file`
    // the file itself), replacing anything staged by an earlier input.
    void stage(std::vector<std::string> files) {
        staged_ = std::move(files);
    }

    // The current input was kept: its staged files now count as included.
    void commit();

    // The current input was rejected: its staged files do not count.
    void discard() noexcept {
        staged_.clear();
    }

    // Forgets every file (`:clear`, which also resets the SourceManager).
    void clear() noexcept {
        committed_.clear();
        staged_.clear();
    }

private:
    StringSet committed_;
    std::vector<std::string> staged_;
};

// Returns the net change in brace depth for a single line of source code,
// accounting for string literals, comments, and ${…} interpolation.
// Exposed in this detail header for unit testing.
[[nodiscard]] int compute_brace_depth_delta(std::string_view line) noexcept;

// Lexes and parses REPL source text into a Program.  Returns std::nullopt and
// prints diagnostics to stderr on a lexer or parser error.  This is the real
// front half of the REPL evaluation pipeline, exposed here so tests exercise
// production code rather than a copy.
[[nodiscard]] std::optional<Program> lex_and_parse(const std::string& source,
                                                   FileId file_id = FileId{0});

// Type-checks, compiles (in REPL mode), and executes a parsed Program, returning
// the VM result value.  Returns std::nullopt and prints diagnostics on a type or
// compile error; runtime errors propagate as RuntimeError exceptions.  This is
// the real back half of the REPL evaluation pipeline, exposed here so tests
// exercise production code rather than a copy.  The input is type-checked
// against every input kept by keep_program(), so earlier definitions are
// visible; `earlier_programs` are those kept inputs, for the compiler's record
// layouts.  Equivalent to type_check_input() followed by compile_and_execute().
[[nodiscard]] std::optional<Value> compile_and_run(Program& program, TypeChecker& checker, VM& vm,
                                                   std::span<const Program> earlier_programs = {});

// First stage of compile_and_run: type-checks `program` incrementally against
// `checker`, printing every diagnostic.  Returns false when any is an error.
[[nodiscard]] bool type_check_input(Program& program, TypeChecker& checker);

// Second stage of compile_and_run: compiles an already type-checked `program`
// in REPL mode and executes it.  Returns std::nullopt (after printing the
// diagnostics) on a compile error; runtime errors propagate as RuntimeError.
[[nodiscard]] std::optional<Value> compile_and_execute(Program& program, VM& vm,
                                                       std::span<const Program> earlier_programs);

// Resolves `include` declarations in typed REPL input or `--eval` input.  The
// input has no file of its own, so relative include paths resolve against the
// current working directory (then LUMA_PATH); included files are registered in
// `source_manager`, which must outlive the program.  A file in
// `includes.committed()` is skipped (include-once across kept inputs); any
// other file is read from disk even if an earlier, rejected input read it.  On
// success the files read are staged in `includes`.  Prints the resolver's
// diagnostics (errors and warnings) to stderr and returns false on error.
[[nodiscard]] bool resolve_input_includes(Program& program, SourceManager& source_manager,
                                          ReplIncludeRegistry& includes);

// Front half of `:file`: (re)reads `file_path` (load_repl_file_source), parses
// it, and resolves its includes.  A `:file` reloads the whole file: every file
// it includes is re-read from disk too, even one an earlier kept input
// included, so edits to included files take effect (each file still at most
// once per load).  On success the file and its includes are staged in
// `includes`, so a later `include` of them is skipped once the load is kept.
// Prints diagnostics and returns std::nullopt on a parse or include error;
// throws std::runtime_error when the file cannot be read.
[[nodiscard]] std::optional<Program> load_file_input(const std::string& file_path,
                                                     SourceManager& source_manager,
                                                     ReplIncludeRegistry& includes);

// Registers the text of a typed REPL input in `source_manager` as a virtual
// file named "<repl:N>" (N counts the session's inputs), so a runtime error
// raised by it renders with its line and a caret.  Its includes still resolve
// against the working directory, like any input with no file.
[[nodiscard]] const SourceFile& register_input(SourceManager& source_manager, std::string text);

// Evaluates one typed REPL input end to end — register_input, lex_and_parse,
// resolve_input_includes, run_and_keep — printing a non-null result as
// "=> value" and every error to stderr (runtime errors through `renderer`).
void evaluate_input(const std::string& input, SourceManager& source_manager,
                    const DiagnosticRenderer& renderer, TypeChecker& checker, VM& vm,
                    std::vector<Program>& programs, ReplIncludeRegistry& includes);

// Loads the source of a `:file` command into `source_manager`.  The first load
// of a path reads it like any other file; a repeated `:file` of the same path
// re-reads it from disk and registers the fresh text under a new file id, so
// edits made since the previous load take effect.  Throws std::runtime_error
// when the file cannot be read.
[[nodiscard]] const SourceFile& load_repl_file_source(SourceManager& source_manager,
                                                      const std::string& file_path);

// ─── REPL commands ───

// The commands the REPL understands (see k_repl_commands in repl.cpp).
enum class ReplCommandKind {
    Quit,
    Help,
    Clear,
    File,
};

// Result of parsing a `:`-prefixed REPL input line.  Exactly one of `kind` and
// `error` is meaningful: `kind` is set when the line is a well-formed command
// (with its trimmed `argument`, empty for commands that take none), otherwise
// `error` holds the usage or unknown-command message to show the user.
struct ParsedReplCommand {
    std::optional<ReplCommandKind> kind;
    std::string argument;
    std::string error;
};

// Parses a REPL command line such as ":file  app.luma".  The command word must
// match a command's name or alias exactly (":filex" is unknown, not ":file"),
// surrounding whitespace is ignored, an argument-taking command without an
// argument yields its usage message, and a command given an argument it does
// not take is rejected likewise.
[[nodiscard]] ParsedReplCommand parse_repl_command(std::string_view line);

// ─── Line-editor cursor helpers (UTF-8 aware) ───

// Byte offset of the character boundary after the character starting at
// `pos` (buf.size() when `pos` is at or past the end).  An ill-formed byte is
// a one-byte character, so the result is always a valid split point.
[[nodiscard]] std::size_t next_char_boundary(std::string_view buf, std::size_t pos) noexcept;

// Byte offset of the start of the character that ends at `pos` (0 at the
// start of the buffer), so Backspace/Left step over a whole multi-byte
// character rather than one of its bytes.
[[nodiscard]] std::size_t prev_char_boundary(std::string_view buf, std::size_t pos) noexcept;

// Number of terminal columns `text` occupies, counted as one column per
// codepoint (ill-formed bytes count as one each).
[[nodiscard]] std::size_t display_width(std::string_view text) noexcept;

// Keeps a successfully evaluated Program alive for the rest of the session and
// commits its definitions to `checker` and its staged includes to `includes`,
// making them visible to later inputs.
void keep_program(std::vector<Program>& programs, Program program, TypeChecker& checker,
                  ReplIncludeRegistry& includes);

// Runs `program` through compile_and_run and keeps it (keep_program) unless it
// was rejected before running, in which case its staged includes are
// discarded.  An input that fails at runtime is kept too — the statements
// before the error have already changed the VM's globals — and the
// RuntimeError is rethrown.
[[nodiscard]] std::optional<Value> run_and_keep(Program program, TypeChecker& checker, VM& vm,
                                                std::vector<Program>& programs,
                                                ReplIncludeRegistry& includes);

// Returns the completions matching `prefix`, sorted ascending.  An exact
// prefix match is preferred; if none exists, a case-insensitive substring
// (fuzzy) match is used as a fallback.  An empty prefix yields no matches.
// Exposed in this detail header for unit testing.
[[nodiscard]] std::vector<std::string>
match_completions(const std::vector<std::string>& completions, std::string_view prefix);

// Builds the sorted, de-duplicated completion list offered by the REPL:
// stdlib module names, fully-qualified stdlib functions, language keywords,
// and REPL commands.  Exposed in this detail header for unit testing.
[[nodiscard]] std::vector<std::string> build_repl_completions();

// Validates a `:file` argument before it is loaded: rejects an empty path, a
// non-".luma" extension, directory traversal, symbolic links, and paths the OS
// cannot resolve or that do not exist.  Diagnostics are printed to stderr.
// Uses non-throwing filesystem queries so a malformed path is reported cleanly
// rather than throwing out of the REPL loop.  Exposed here for unit testing.
[[nodiscard]] bool validate_file_path(const std::string& file_path);

} // namespace repl_detail
} // namespace luma

#endif // LUMA_REPL_REPL_DETAIL_HPP
