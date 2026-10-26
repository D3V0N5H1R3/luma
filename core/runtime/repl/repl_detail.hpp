#ifndef LUMA_REPL_REPL_DETAIL_HPP
#define LUMA_REPL_REPL_DETAIL_HPP

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/ast/ast_fwd.hpp"            // Program
#include "analysis/source/source_location.hpp" // FileId
#include "analysis/types/type_checker_fwd.hpp" // TypeChecker
#include "runtime/interpreter/value_fwd.hpp"   // Value

namespace luma {

class VM;

namespace repl_detail {

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
// layouts.
[[nodiscard]] std::optional<Value> compile_and_run(Program& program, TypeChecker& checker, VM& vm,
                                                   std::span<const Program> earlier_programs = {});

// Keeps a successfully evaluated Program alive for the rest of the session and
// commits its definitions to `checker`, making them visible to later inputs.
void keep_program(std::vector<Program>& programs, Program program, TypeChecker& checker);

// Runs `program` through compile_and_run and keeps it (keep_program) unless it
// was rejected before running.  An input that fails at runtime is kept too —
// the statements before the error have already changed the VM's globals — and
// the RuntimeError is rethrown.
[[nodiscard]] std::optional<Value> run_and_keep(Program program, TypeChecker& checker, VM& vm,
                                                std::vector<Program>& programs);

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
