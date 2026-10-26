#include "runtime/repl/repl.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/diagnostics/renderer.hpp"
#include "analysis/errors/error.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"
#include "analysis/source/source_manager.hpp"
#include "analysis/types/type_checker.hpp"
#include "common/path_utils.hpp"
#include "common/resource_limits.hpp"
#include "common/string_utils.hpp"
#include "common/terminal.hpp"
#include "common/version.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_config.hpp"
#include "runtime/include/include_resolver.hpp"
#include "runtime/repl/line_editor.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"
#include "stdlib/stdlib_catalog.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

namespace {

// Built-in functions that are worth completing in the REPL but are not language
// keywords, so they are absent from the canonical k_keywords table.  Keyword
// completions themselves are derived from k_keywords in build_repl_completions()
// so they can never drift from the spellings the lexer accepts.
constexpr std::array<std::string_view, 3> k_repl_builtin_extras = {
    "print",
    "assert",
    "type_of",
};

// Identifies which REPL command a descriptor stands for; drives the dispatch
// action in dispatch_command().
using CommandKind = repl_detail::ReplCommandKind;

// A REPL command descriptor.  This is the single source of truth for the
// command's spellings, argument, and help text: completion, dispatch, and the
// help listing are all derived from k_repl_commands so they cannot drift apart.
struct ReplCommand {
    CommandKind kind;
    std::string_view name;  // Canonical spelling, e.g. ":quit".
    std::string_view alias; // Short alias, e.g. ":q".
    std::string_view arg;   // Argument placeholder (e.g. "<path>"); empty if none.
    std::string_view help;  // One-line description shown by ":help".
};

constexpr std::array<ReplCommand, 4> k_repl_commands = {{
    {CommandKind::Quit, ":quit", ":q", "", "Exit the REPL"},
    {CommandKind::Help, ":help", ":h", "", "Show this help"},
    {CommandKind::Clear, ":clear", ":c", "", "Reset the environment"},
    {CommandKind::File, ":file", ":f", "<path>", "Load a .luma file"},
}};

// ─── Shared helpers ─────────────────────────────────────────────────────────

// Print error-severity diagnostics from a container to stderr.
void print_errors(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        if (d.severity == Severity::Error) {
            std::cerr << d.message << "\n";
        }
    }
}

// Print error and warning diagnostics (e.g. from include resolution) to stderr,
// prefixing warnings so they are distinguishable from errors.
void print_errors_and_warnings(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        if (d.severity == Severity::Error) {
            std::cerr << d.message << "\n";
        } else if (d.severity == Severity::Warning) {
            std::cerr << term::yellow() << "warning: " << term::reset() << d.message << "\n";
        }
    }
}

// Render a RuntimeError as a diagnostic with source location and hint.
void render_runtime_error(const DiagnosticRenderer& renderer, const RuntimeError& error) {
    auto diag = DiagnosticBuilder{Severity::Error, std::string{error.what()}}
                    .category(DiagnosticCategory::Runtime)
                    .primary(error.location())
                    .build();

    if (const auto& hint = error.hint()) {
        diag.hint = *hint;
    }

    renderer.render(diag);
}

// All parsed programs must be kept alive because the VM holds raw pointers into
// their ASTs.  Programs accumulate across the session until `:clear` is used.
//
// When the count grows very large the user likely forgot about `:clear`.  Emit
// a one-time advisory at the threshold so the session remains responsive.
constexpr std::size_t k_programs_warning_threshold = CompileTimeLimits::max_repl_program_snapshots;

void append_program(std::vector<Program>& programs, Program program) {
    programs.push_back(std::move(program));

    if (programs.size() == k_programs_warning_threshold) {
        std::cerr << term::red() << "warning: " << term::reset() << "the REPL has accumulated "
                  << k_programs_warning_threshold
                  << " program snapshots. Use ':clear' to reset the environment "
                     "and free memory.\n";
    }
}

// ─── Multiline input ────────────────────────────────────────────────────────

[[nodiscard]] std::optional<std::string> read_multiline_block(std::string_view first_line,
                                                              LineEditor& editor) {
    std::string block{first_line};
    block += '\n';

    int depth{repl_detail::compute_brace_depth_delta(first_line)};

    const std::string continuation_prompt =
        std::string{term::out_cyan()} + "  ... " + std::string{term::out_reset()};

    while (depth > 0) {
        std::string next_line{};

        if (!editor.read_line(continuation_prompt, next_line)) {
            // read_line returns false only on EOF/read failure; a Ctrl+C
            // interrupt makes it return true (handled by the check below), so
            // reaching here always means the input ended unexpectedly.
            std::cerr << "\nUnexpected end of input in multiline block\n";
            return std::nullopt;
        }

        if (editor.was_interrupted()) {
            return std::nullopt;
        }

        block += next_line + "\n";
        depth += repl_detail::compute_brace_depth_delta(next_line);
    }

    return block;
}

// ─── File loading ───────────────────────────────────────────────────────────

// Post-canonicalization symlink re-check to mitigate TOCTOU race conditions.
// Uses the non-throwing std::error_code overloads so that a path the OS cannot
// resolve (e.g. a malformed device path) is rejected cleanly rather than
// throwing a std::filesystem_error out of the REPL command loop.
[[nodiscard]] bool check_symlink(const std::string& file_path) {
    // REPL input is UTF-8; never round-trip it through the ANSI code page.
    const auto fs_path = try_path_from_utf8(file_path);
    std::error_code ec;

    if (!fs_path) {
        std::cerr << "Path rejected: path is not valid UTF-8\n";

        return false;
    }

    const auto canonical = std::filesystem::weakly_canonical(*fs_path, ec);

    if (ec) {
        std::cerr << "Path rejected: unable to resolve path\n";

        return false;
    }

    if (std::filesystem::is_symlink(canonical, ec)) {
        std::cerr << "Path rejected: resolved path is a symbolic "
                     "link\n";

        return false;
    }

    return true;
}

void load_and_execute_file(const std::string& file_path, SourceManager& source_manager,
                           const DiagnosticRenderer& renderer, TypeChecker& checker, VM& vm,
                           std::vector<Program>& programs,
                           repl_detail::ReplIncludeRegistry& includes) {
    try {
        auto file_program = repl_detail::load_file_input(file_path, source_manager, includes);

        if (!file_program) {
            return;
        }

        auto value =
            repl_detail::run_and_keep(std::move(*file_program), checker, vm, programs, includes);

        if (!value) {
            return;
        }

        std::cout << "Loaded: " << file_path << "\n";
    } catch (const RuntimeError& error) {
        render_runtime_error(renderer, error);
    } catch (const std::exception& error) {
        std::cerr << "Error loading file: " << error.what() << "\n";
    }
}

// ─── Command handlers ───────────────────────────────────────────────────────

void handle_help_command() {
    // Render the command listing from k_repl_commands so the help text stays in
    // sync with what dispatch_command actually accepts.  Build each
    // "name / alias" column, then align the descriptions in a second column.
    std::array<std::string, k_repl_commands.size()> columns{};
    std::size_t width{0};

    const auto spelling = [](std::string_view name, std::string_view arg) {
        std::string result{name};
        if (!arg.empty()) {
            result += ' ';
            result += arg;
        }
        return result;
    };

    for (std::size_t i = 0; i < k_repl_commands.size(); ++i) {
        const auto& command = k_repl_commands[i];
        columns[i] =
            spelling(command.name, command.arg) + " / " + spelling(command.alias, command.arg);
        width = std::max(width, columns[i].size());
    }

    std::cout << "REPL commands:\n";

    for (std::size_t i = 0; i < k_repl_commands.size(); ++i) {
        std::cout << "  " << columns[i] << std::string(width - columns[i].size() + 2, ' ')
                  << k_repl_commands[i].help << "\n";
    }
}

void handle_clear_command(EnvPtr& global_env, bool sandbox, VM& vm, std::vector<Program>& programs,
                          SourceManager& source_manager, TypeChecker& checker,
                          repl_detail::ReplIncludeRegistry& includes) {
    global_env = Environment::create();

    register_all(global_env, sandbox);

    vm = VM{global_env};

    // Forget the checker's definitions before the ASTs they point into.
    checker.reset_incremental();

    programs.clear();

    source_manager = SourceManager{};

    includes.clear();

    std::cout << "Environment cleared.\n";
}

// Canonical UTF-8 form of a `:file` path — the form the include resolver uses
// for an included file — or the path itself if it cannot be canonicalised.
[[nodiscard]] std::string canonical_file_path(const std::string& file_path) {
    const auto fs_path = try_path_from_utf8(file_path);

    if (!fs_path) {
        return file_path;
    }

    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(*fs_path, ec);

    return ec ? file_path : path_to_utf8(canonical);
}

// Resolves `program`'s includes in session mode (see
// IncludeResolver::set_session_included), printing the diagnostics.  Returns
// the canonical paths of the files read, or std::nullopt on error.
[[nodiscard]] std::optional<std::vector<std::string>>
resolve_session_includes(Program& program, SourceManager& source_manager,
                         StringSet already_included) {
    IncludeResolver include_resolver{source_manager};
    include_resolver.set_session_included(std::move(already_included));

    const bool ok = include_resolver.resolve(program);

    print_errors_and_warnings(include_resolver.get_diagnostics());

    if (!ok) {
        return std::nullopt;
    }

    return include_resolver.included_files();
}

// ─── Command dispatch ───────────────────────────────────────────────────────

// Result of dispatching a REPL command.
enum class CommandResult {
    Continue, // Handled (or rejected with a message); read next input.
    Quit,     // Exit the REPL.
};

// REPL session state passed to command handlers.
struct ReplState {
    EnvPtr& global_env;
    bool sandbox;
    VM& vm;
    std::vector<Program>& programs;
    SourceManager& source_manager;
    const DiagnosticRenderer& renderer;
    TypeChecker& checker;
    LineEditor& editor;
    repl_detail::ReplIncludeRegistry& includes;
};

// Dispatch a `:`-prefixed input line.  Every such line is a REPL command — no
// Luma statement starts with ':' — so a malformed or unknown command is
// reported here instead of falling through to a confusing parse error.
CommandResult dispatch_command(const std::string& line, ReplState& state) {
    const auto parsed = repl_detail::parse_repl_command(line);

    if (!parsed.kind) {
        std::cerr << parsed.error << "\n";
        return CommandResult::Continue;
    }

    switch (*parsed.kind) {
        case CommandKind::Quit:
            return CommandResult::Quit;

        case CommandKind::Help:
            handle_help_command();
            return CommandResult::Continue;

        case CommandKind::Clear:
            handle_clear_command(state.global_env, state.sandbox, state.vm, state.programs,
                                 state.source_manager, state.checker, state.includes);
            return CommandResult::Continue;

        case CommandKind::File:
            if (repl_detail::validate_file_path(parsed.argument)) {
                load_and_execute_file(parsed.argument, state.source_manager, state.renderer,
                                      state.checker, state.vm, state.programs, state.includes);
            }
            return CommandResult::Continue;
    }

    return CommandResult::Continue;
}

} // namespace

namespace repl_detail {

// ─── Evaluation pipeline ─────────────────────────────────────────────────────
// The real lex → parse → type-check → compile → execute pipeline lives here (not
// in the anonymous namespace) so that repl_test.cpp exercises production code
// rather than a divergent copy.

std::optional<Program> lex_and_parse(const std::string& source, FileId file_id) {
    DiagnosticCollector lexer_diagnostics;
    Lexer lexer{source, lexer_diagnostics, file_id};

    auto tokens = lexer.tokenize();

    if (lexer_diagnostics.has_errors()) {
        print_errors(lexer_diagnostics.diagnostics());
        return std::nullopt;
    }

    Parser parser{std::move(tokens)};

    auto program = parser.parse();

    if (!parser.get_errors().empty()) {
        for (const auto& err : parser.get_errors()) {
            std::cerr << err.message << "\n";
        }
        return std::nullopt;
    }

    return program;
}

std::optional<Value> compile_and_run(Program& program, TypeChecker& checker, VM& vm,
                                     std::span<const Program> earlier_programs) {
    if (!type_check_input(program, checker)) {
        return std::nullopt;
    }

    return compile_and_execute(program, vm, earlier_programs);
}

bool type_check_input(Program& program, TypeChecker& checker) {
    const auto diagnostics = checker.check_incremental(program);

    bool has_errors = false;

    for (const auto& diag : diagnostics) {
        // Diagnostics go to stderr, so colour them by stderr's TTY state.
        std::cerr << term::red() << "type: " << term::reset() << diag.message << "\n";

        if (diag.severity == Severity::Error) {
            has_errors = true;
        }
    }

    return !has_errors;
}

std::optional<Value> compile_and_execute(Program& program, VM& vm,
                                         std::span<const Program> earlier_programs) {
    Compiler compiler;

    auto result = compiler.compile(program, /*repl_mode=*/true, earlier_programs);

    if (!result.success) {
        for (const auto& diag : result.diagnostics) {
            std::cerr << diag.message << "\n";
        }

        return std::nullopt;
    }

    // Execute.  The VM keeps the function table: closures this input binds to
    // globals (function declarations, lambdas) are called by later inputs.
    const auto& functions = vm.retain_compiled_functions(std::move(result.functions));

    return vm.execute_function(result.top_level, functions);
}

bool resolve_input_includes(Program& program, SourceManager& source_manager,
                            ReplIncludeRegistry& includes) {
    includes.discard();

    const bool has_includes = std::ranges::any_of(program.declarations, [](const auto& decl) {
        return decl->kind == DeclarationKind::Include;
    });

    if (!has_includes) {
        return true;
    }

    // The input has no file on disk (FileId 0, or a bracketed "<repl:N>"
    // virtual source), so the resolver treats it as unanchored and resolves
    // relative paths against the current working directory (Manual §25).
    auto files = resolve_session_includes(program, source_manager, includes.committed());

    if (!files) {
        return false;
    }

    includes.stage(std::move(*files));
    return true;
}

std::optional<Program> load_file_input(const std::string& file_path, SourceManager& source_manager,
                                       ReplIncludeRegistry& includes) {
    includes.discard();

    const auto& source_file = load_repl_file_source(source_manager, file_path);

    auto program = lex_and_parse(source_file.text, source_file.file_id);

    if (!program) {
        return std::nullopt;
    }

    // Only the file itself counts as already included, so a loop back to it is
    // skipped while every file it includes is re-read — like the file itself.
    auto entry = canonical_file_path(file_path);
    auto files = resolve_session_includes(*program, source_manager, StringSet{entry});

    if (!files) {
        return std::nullopt;
    }

    files->insert(files->begin(), std::move(entry));
    includes.stage(std::move(*files));

    return program;
}

const SourceFile& register_input(SourceManager& source_manager, std::string text) {
    const auto earlier_inputs = std::ranges::count_if(source_manager.files(), [](const auto& file) {
        return file.is_virtual && file.path.starts_with("<repl:");
    });

    return source_manager.load_virtual(std::format("<repl:{}>", earlier_inputs + 1),
                                       std::move(text));
}

void evaluate_input(const std::string& input, SourceManager& source_manager,
                    const DiagnosticRenderer& renderer, TypeChecker& checker, VM& vm,
                    std::vector<Program>& programs, ReplIncludeRegistry& includes) {
    try {
        // Registering the input gives its locations a file the renderer can
        // show, so a runtime error points at the offending line.
        const auto& source_file = register_input(source_manager, input);

        auto program = lex_and_parse(source_file.text, source_file.file_id);

        if (!program) {
            return;
        }

        if (!resolve_input_includes(*program, source_manager, includes)) {
            return;
        }

        auto value = run_and_keep(std::move(*program), checker, vm, programs, includes);

        if (!value) {
            return;
        }

        if (!value->is_null()) {
            std::cout << term::out_cyan() << "=> " << term::out_reset() << value->to_string()
                      << "\n";
        }
    } catch (const RuntimeError& error) {
        render_runtime_error(renderer, error);
    } catch (const std::exception& error) {
        std::cerr << "Internal error: " << error.what() << "\n";
    }
}

const SourceFile& load_repl_file_source(SourceManager& source_manager,
                                        const std::string& file_path) {
    // A repeated `:file` must not run the text registered by the previous one:
    // load_fresh_source re-reads the file and registers it under a new file id
    // (in the session manager, so runtime errors raised later from its
    // functions still render with the right source).
    const LoadResult loaded = load_fresh_source(source_manager, file_path);

    if (!loaded) {
        throw std::runtime_error{loaded.error.value_or("failed to load source file")};
    }

    return *loaded.file;
}

ParsedReplCommand parse_repl_command(std::string_view line) {
    const auto trimmed = trim_view(line);
    const auto word_end = trimmed.find_first_of(" \t");
    const auto word = trimmed.substr(0, word_end);
    const auto argument = word_end == std::string_view::npos ? std::string_view{}
                                                             : trim_view(trimmed.substr(word_end));

    const auto command =
        std::ranges::find_if(k_repl_commands, [word](const ReplCommand& candidate) {
            return word == candidate.name || word == candidate.alias;
        });

    ParsedReplCommand parsed;

    if (command == k_repl_commands.end()) {
        parsed.error =
            std::format("Unknown command: {} (type ':help' for a list of commands)", word);
        return parsed;
    }

    const bool takes_argument = !command->arg.empty();

    if (takes_argument == argument.empty()) {
        parsed.error = takes_argument
                           ? std::format("Usage: {} {}", command->name, command->arg)
                           : std::format("Usage: {} (takes no arguments)", command->name);
        return parsed;
    }

    parsed.kind = command->kind;
    parsed.argument = std::string{argument};
    return parsed;
}

void ReplIncludeRegistry::commit() {
    for (auto& file : staged_) {
        committed_.insert(std::move(file));
    }

    staged_.clear();
}

void keep_program(std::vector<Program>& programs, Program program, TypeChecker& checker,
                  ReplIncludeRegistry& includes) {
    append_program(programs, std::move(program));
    checker.commit_incremental();
    includes.commit();
}

std::optional<Value> run_and_keep(Program program, TypeChecker& checker, VM& vm,
                                  std::vector<Program>& programs, ReplIncludeRegistry& includes) {
    std::optional<Value> value;

    try {
        value = compile_and_run(program, checker, vm, programs);
    } catch (const RuntimeError&) {
        keep_program(programs, std::move(program), checker, includes);
        throw;
    } catch (...) {
        includes.discard();
        throw;
    }

    if (value) {
        keep_program(programs, std::move(program), checker, includes);
    } else {
        includes.discard();
    }

    return value;
}

std::vector<std::string> build_repl_completions() {
    const auto expected_count = (::luma::stdlib::catalog().size() * 2) // module names + full names
                                + std::size(k_keywords) + k_repl_builtin_extras.size() +
                                (k_repl_commands.size() * 2); // command names + aliases

    std::vector<std::string> completions;
    completions.reserve(expected_count);

    for (const auto& [name, spec] : ::luma::stdlib::catalog()) {
        // Add full "Module.function" name for dot-aware completion.
        completions.emplace_back(name);

        // Also add just the module name prefix.
        if (const auto split = symbols::split_module(name)) {
            completions.emplace_back(split->first);
        }
    }

    // Language keywords — derived from the canonical k_keywords table
    // (token_type.hpp) so the completion list can never drift from the keywords
    // the lexer actually accepts.
    for (const auto& keyword : k_keywords) {
        completions.emplace_back(keyword.spelling);
    }

    completions.insert(completions.end(), k_repl_builtin_extras.begin(),
                       k_repl_builtin_extras.end());

    // REPL commands — both the canonical spelling and the short alias.
    for (const auto& command : k_repl_commands) {
        completions.emplace_back(command.name);
        completions.emplace_back(command.alias);
    }

    std::ranges::sort(completions);
    completions.erase(std::ranges::unique(completions).begin(), completions.end());

    return completions;
}

// ─── File path validation ────────────────────────────────────────────────────

bool validate_file_path(const std::string& file_path) {
    if (file_path.empty()) {
        std::cerr << "Usage: :file <path>\n";

        return false;
    }

    // Validate extension and security (traversal, symlinks).
    const auto result = validate_path(file_path, ".luma");

    if (!result.is_valid) {
        std::cerr << "Only .luma files can be loaded\n";
        return false;
    }

    if (!result.is_secure) {
        std::cerr << "Path rejected: " << result.error_message << "\n";
        return false;
    }

    std::error_code ec;

    // validate_path() above already rejected text that is not valid UTF-8.
    if (!std::filesystem::exists(path_from_utf8(file_path), ec)) {
        std::cerr << "File not found: " << file_path << "\n";

        return false;
    }

    if (!check_symlink(file_path)) {
        return false;
    }

    return true;
}

} // namespace repl_detail

void run_repl(bool sandbox) {
    std::cout << "Luma " << luma_version
              << " REPL \u2014 type ':quit' to exit, ':help' for commands\n\n";

    SourceManager source_manager;
    const DiagnosticRenderer renderer{source_manager};

    auto global_env = Environment::create();

    register_all(global_env, sandbox);

    VM vm{global_env};

    // Keep all parsed programs alive so that AST raw pointers remain valid.
    std::vector<Program> programs{};

    // Persistent type checker so definitions from prior inputs are visible.
    TypeChecker repl_checker;

    // Include-once registry: the files included by kept inputs.
    repl_detail::ReplIncludeRegistry includes;

    // Set up line editor with tab completion.
    LineEditor editor;
    editor.set_completions(repl_detail::build_repl_completions());

    ReplState state{.global_env = global_env,
                    .sandbox = sandbox,
                    .vm = vm,
                    .programs = programs,
                    .source_manager = source_manager,
                    .renderer = renderer,
                    .checker = repl_checker,
                    .editor = editor,
                    .includes = includes};

    const std::string prompt =
        std::string{term::out_bold()} + "luma> " + std::string{term::out_reset()};
    std::string line{};

    while (true) {
        if (!editor.read_line(prompt, line)) {
            std::cout << "\n";
            break;
        }

        if (editor.was_interrupted()) {
            continue;
        }

        if (line.empty()) {
            continue;
        }

        // Dispatch REPL commands (prefixed with ':').  Every such line is a
        // command, handled (or rejected with a message) by dispatch_command.
        if (line.starts_with(":")) {
            if (dispatch_command(line, state) == CommandResult::Quit) {
                break;
            }

            continue;
        }

        // Add non-command lines to history.
        editor.add_history(line);

        // Incomplete block detection: a positive brace depth delta means
        // the input has unclosed braces — switch to multi-line mode.
        if (repl_detail::compute_brace_depth_delta(line) > 0) {
            if (auto block = read_multiline_block(line, editor)) {
                line = std::move(*block);
            } else {
                continue;
            }
        }

        repl_detail::evaluate_input(line, source_manager, renderer, repl_checker, vm, programs,
                                    includes);
    }
}

int run_eval(bool sandbox) {
    // Slurp the entire program from standard input. A caller pipes a snippet
    // here and closes stdin, so reading to EOF returns the whole buffer.
    const std::string source{std::istreambuf_iterator<char>(std::cin),
                             std::istreambuf_iterator<char>()};

    // Fresh global environment with stdlib modules registered, mirroring the
    // REPL and file runner.
    auto global_env = Environment::create();
    register_all(global_env, sandbox);

    VM vm{global_env};

    // The REPL evaluation pipeline type-checks with require_main = false and
    // compiles in REPL mode, so top-level statements run without a @main
    // function — exactly the scratch-pad semantics stdin evaluation expects.
    TypeChecker checker;

    // Owns the text of any included files for as long as the program's AST
    // (which points into it) is alive.
    SourceManager source_manager;

    // A single input: nothing is committed for a later one.
    repl_detail::ReplIncludeRegistry includes;

    try {
        auto program = repl_detail::lex_and_parse(source);

        if (!program) {
            return exit_code::syntax_error;
        }

        // Include resolution failures are compile errors (exit 4), exactly as
        // when running a file.
        if (!repl_detail::resolve_input_includes(*program, source_manager, includes)) {
            return exit_code::compile_error;
        }

        // Map each rejecting stage to its own exit code: a type error exits 2,
        // a bytecode compile error exits 4 (see the Manual's exit-code table).
        if (!repl_detail::type_check_input(*program, checker)) {
            return exit_code::type_error;
        }

        if (!repl_detail::compile_and_execute(*program, vm, {})) {
            return exit_code::compile_error;
        }

        return exit_code::success;
    } catch (const RuntimeError& error) {
        std::cerr << error.what() << "\n";

        if (const auto& hint = error.hint()) {
            std::cerr << "hint: " << *hint << "\n";
        }

        return exit_code::runtime_error;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";

        return exit_code::runtime_error;
    }
}

} // namespace luma
