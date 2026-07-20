#include "runtime/repl/repl.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
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
#include "analysis/prelude/gui_prelude.hpp"
#include "analysis/source/source_manager.hpp"
#include "analysis/types/type_checker.hpp"
#include "common/path_utils.hpp"
#include "common/resource_limits.hpp"
#include "runtime/cli/terminal.hpp"
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
enum class CommandKind {
    Quit,
    Help,
    Clear,
    File,
};

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
        std::cerr << term::out_red() << "warning: " << term::out_reset()
                  << "the REPL has accumulated " << k_programs_warning_threshold
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
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(file_path, ec).string();

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
                           std::vector<Program>& programs) {
    try {
        const auto& source_file = source_manager.load(file_path);

        auto file_program = repl_detail::lex_and_parse(source_file.text, source_file.file_id);

        if (!file_program) {
            return;
        }

        IncludeResolver include_resolver{source_manager};

        if (!include_resolver.resolve(*file_program)) {
            print_errors(include_resolver.get_diagnostics());
            return;
        }

        // Make the built-in Solaris surface available to a loaded file that
        // references it, mirroring CLI execution.  Gated on the file's own text
        // (not the whole session, whose virtual prelude file also mentions the
        // trigger) and guarded against double injection inside the prelude.
        if (prelude::source_uses_gui(source_file.text)) {
            prelude::inject_gui_prelude(*file_program, source_manager);
        }

        auto value = repl_detail::compile_and_run(*file_program, checker, vm);

        if (!value) {
            return;
        }

        append_program(programs, std::move(*file_program));

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
                          SourceManager& source_manager) {
    global_env = Environment::create();

    register_all(global_env, sandbox);

    vm = VM{global_env};

    programs.clear();

    // TypeChecker resets its own state at the start of each check() call.

    source_manager = SourceManager{};

    std::cout << "Environment cleared.\n";
}

void handle_expression(const std::string& input, SourceManager& source_manager,
                       const DiagnosticRenderer& renderer, TypeChecker& checker, VM& vm,
                       std::vector<Program>& programs) {
    try {
        auto program = repl_detail::lex_and_parse(input);

        if (!program) {
            return;
        }

        // Make the built-in Solaris surface available to an interactive line
        // that references it, mirroring --eval and file execution.  Each REPL
        // input is an independent program (the type checker resets per line), so
        // the prelude is injected per input; the shared session SourceManager
        // reuses one virtual "<gui-prelude>" file for stable diagnostics.
        if (prelude::source_uses_gui(input)) {
            prelude::inject_gui_prelude(*program, source_manager);
        }

        auto value = repl_detail::compile_and_run(*program, checker, vm);

        if (!value) {
            return;
        }

        if (!value->is_null()) {
            std::cout << term::out_cyan() << "=> " << term::out_reset() << value->to_string()
                      << "\n";
        }

        append_program(programs, std::move(*program));
    } catch (const RuntimeError& error) {
        render_runtime_error(renderer, error);
    } catch (const std::exception& error) {
        std::cerr << "Internal error: " << error.what() << "\n";
    }
}

// ─── Command dispatch ───────────────────────────────────────────────────────

// Result of dispatching a REPL command.
enum class CommandResult {
    Continue, // Handled; read next input.
    Quit,     // Exit the REPL.
    NotFound, // Not a known command; treat as expression.
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
};

CommandResult dispatch_command(const std::string& line, ReplState& state) {
    for (const auto& command : k_repl_commands) {
        bool matched{false};
        std::string argument{};

        if (command.arg.empty()) {
            // No-argument commands match the exact spelling or alias.
            matched = line == command.name || line == command.alias;
        } else {
            // Argument-taking commands match "<name> " / "<alias> " and consume
            // the remainder of the line as the argument.
            const auto name_prefix = std::string{command.name} + ' ';
            const auto alias_prefix = std::string{command.alias} + ' ';

            if (line.starts_with(name_prefix) || line.starts_with(alias_prefix)) {
                matched = true;
                argument = line.substr(line.find(' ') + 1);
            }
        }

        if (!matched) {
            continue;
        }

        switch (command.kind) {
            case CommandKind::Quit:
                return CommandResult::Quit;

            case CommandKind::Help:
                handle_help_command();
                return CommandResult::Continue;

            case CommandKind::Clear:
                handle_clear_command(state.global_env, state.sandbox, state.vm, state.programs,
                                     state.source_manager);
                return CommandResult::Continue;

            case CommandKind::File:
                if (repl_detail::validate_file_path(argument)) {
                    load_and_execute_file(argument, state.source_manager, state.renderer,
                                          state.checker, state.vm, state.programs);
                }
                return CommandResult::Continue;
        }
    }

    return CommandResult::NotFound;
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

std::optional<Value> compile_and_run(Program& program, TypeChecker& checker, VM& vm) {
    // Type check
    {
        auto diagnostics = checker.check(program, /*require_main=*/false);

        bool has_errors = false;

        for (const auto& diag : diagnostics) {
            std::cerr << term::out_red() << "type: " << term::out_reset() << diag.message << "\n";

            if (diag.severity == Severity::Error) {
                has_errors = true;
            }
        }

        if (has_errors) {
            return std::nullopt;
        }
    }

    // Compile
    Compiler compiler;

    auto result = compiler.compile(program, /*repl_mode=*/true);

    if (!result.success) {
        for (const auto& diag : result.diagnostics) {
            std::cerr << diag.message << "\n";
        }

        return std::nullopt;
    }

    // Execute
    return vm.execute_function(result.top_level, result.functions);
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
        if (const auto split = split_module(name)) {
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

    if (!std::filesystem::exists(file_path, ec)) {
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
    std::cout << "Luma 1.0 REPL \u2014 type ':quit' to exit, ':help' for commands\n\n";

    SourceManager source_manager;
    const DiagnosticRenderer renderer{source_manager};

    auto global_env = Environment::create();

    register_all(global_env, sandbox);

    VM vm{global_env};

    // Keep all parsed programs alive so that AST raw pointers remain valid.
    std::vector<Program> programs{};

    // Persistent type checker so definitions from prior inputs are visible.
    TypeChecker repl_checker;

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
                    .editor = editor};

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

        // Dispatch REPL commands (prefixed with ':').
        if (line.starts_with(":")) {
            const auto result = dispatch_command(line, state);

            if (result == CommandResult::Quit) {
                break;
            }

            if (result == CommandResult::Continue) {
                continue;
            }
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

        handle_expression(line, source_manager, renderer, repl_checker, vm, programs);
    }
}

int run_eval(bool sandbox) {
    // Slurp the entire program from standard input. The Playground pipes a
    // snippet here and closes stdin, so reading to EOF returns the whole buffer.
    const std::string source{std::istreambuf_iterator<char>(std::cin),
                             std::istreambuf_iterator<char>()};

    // Fresh global environment with stdlib modules registered, mirroring the
    // REPL and file runner.
    auto global_env = Environment::create();
    register_all(global_env, sandbox);

    VM vm{global_env};

    // The REPL evaluation pipeline type-checks with require_main = false and
    // compiles in REPL mode, so top-level statements run without a @main
    // function — exactly the scratch-pad semantics the Playground expects.
    TypeChecker checker;

    try {
        auto program = repl_detail::lex_and_parse(source);

        if (!program) {
            return exit_code::syntax_error;
        }

        // Make the built-in Solaris surface available to --eval snippets that
        // reference it, mirroring file execution. A local SourceManager gives the
        // injected prelude its own virtual file id; --eval reports diagnostics by
        // message, so no persistent source context is required.
        SourceManager eval_source_manager;

        if (prelude::source_uses_gui(source)) {
            prelude::inject_gui_prelude(*program, eval_source_manager);
        }

        if (!repl_detail::compile_and_run(*program, checker, vm)) {
            return exit_code::type_error;
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
