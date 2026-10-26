// Include resolver unit tests.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"
#include "analysis/source/source_manager.hpp"
#include "common/path_utils.hpp"
#include "runtime/include/include_resolver.hpp"
#include "runtime/include/include_resolver_detail.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Helpers ───

// Load and resolve a program from a real file.
// Asserts that resolution succeeds (no errors).
[[nodiscard]] static Program load_and_resolve(const std::string& path,
                                              SourceManager& source_manager) {
    const auto& source_file = source_manager.load(path);

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    IncludeResolver resolver{source_manager};
    const bool success = resolver.resolve(program);
    ASSERT_TRUE(success);

    return program;
}

// Count declarations of a specific kind in a program.
[[nodiscard]] static int count_declarations(const Program& program, DeclarationKind kind) {
    return static_cast<int>(std::ranges::count_if(
        program.declarations, [kind](const auto& d) { return d->kind == kind; }));
}

// ─── Tests ───

static void test_program_without_includes() {
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile main_file{dir / "no_includes.luma", "@main\n"
                                                 "function void main() {\n"
                                                 "    print(\"hello\")\n"
                                                 "}\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 1);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
}

static void test_single_include() {
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile helper{dir / "helper.luma", "function string greet(string name) {\n"
                                         "    return \"hello \" + name\n"
                                         "}\n"};
    TempFile main_file{dir / "single_include.luma", "include \"helper.luma\"\n"
                                                    "\n"
                                                    "@main\n"
                                                    "function void main() {\n"
                                                    "    print(greet(\"world\"))\n"
                                                    "}\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    // Include declaration should be removed; helper function merged in.
    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 2);
}

static void test_multiple_includes() {
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile file_a{dir / "a.luma", "function integer func_a() { return 1 }\n"};
    TempFile file_b{dir / "b.luma", "function integer func_b() { return 2 }\n"};
    TempFile main_file{dir / "multi_include.luma", "include \"a.luma\"\n"
                                                   "include \"b.luma\"\n"
                                                   "\n"
                                                   "@main\n"
                                                   "function void main() {\n"
                                                   "    print(func_a())\n"
                                                   "    print(func_b())\n"
                                                   "}\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 3);
}

static void test_include_once() {
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile shared{dir / "shared.luma", "function integer shared_func() { return 42 }\n"};
    TempFile main_file{dir / "include_once.luma", "include \"shared.luma\"\n"
                                                  "include \"shared.luma\"\n"
                                                  "\n"
                                                  "@main\n"
                                                  "function void main() {\n"
                                                  "    print(shared_func())\n"
                                                  "}\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    // shared.luma should only be included once despite two include statements.
    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 2);
}

static void test_transitive_include() {
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile base{dir / "base.luma", "function integer base_func() { return 10 }\n"};
    TempFile middle{dir / "middle.luma", "include \"base.luma\"\n"
                                         "\n"
                                         "function integer middle_func() { return 20 }\n"};
    TempFile main_file{dir / "transitive.luma", "include \"middle.luma\"\n"
                                                "\n"
                                                "@main\n"
                                                "function void main() {\n"
                                                "    print(base_func())\n"
                                                "    print(middle_func())\n"
                                                "}\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    // All include declarations removed; all three functions merged.
    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 3);
}

static void test_circular_include_is_safe() {
    // Circular includes are handled by include-once: the second
    // reference to an already-loaded file is simply skipped.
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile file_a{dir / "circ_a.luma", "include \"circ_b.luma\"\n"
                                         "function integer func_a() { return 1 }\n"};
    TempFile file_b{dir / "circ_b.luma", "include \"circ_a.luma\"\n"
                                         "function integer func_b() { return 2 }\n"};

    SourceManager sm;

    const auto program = load_and_resolve(file_a.path_string(), sm);

    // Both functions should be merged; no error.
    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 2);
}

static void test_self_include_is_safe() {
    // Self-include is handled by include-once: the file is already
    // loaded, so the include is silently skipped.
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile self_file{dir / "self.luma", "include \"self.luma\"\n"
                                          "function integer self_func() { return 1 }\n"};

    SourceManager sm;

    const auto program = load_and_resolve(self_file.path_string(), sm);

    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 1);
}

static void test_include_nonexistent_file() {
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile main_file{dir / "bad_include.luma", "include \"does_not_exist.luma\"\n"
                                                 "\n"
                                                 "@main\n"
                                                 "function void main() {\n"
                                                 "    print(\"unreachable\")\n"
                                                 "}\n"};

    SourceManager sm;

    const auto& source_file = sm.load(main_file.path_string());

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    IncludeResolver resolver{sm};
    const bool success = resolver.resolve(program);

    ASSERT_FALSE(success);

    const auto& diagnostics = resolver.get_diagnostics();
    ASSERT_TRUE(!diagnostics.empty());
    ASSERT_EQ(diagnostics.front().severity, Severity::Error);
}

static void test_diamond_include() {
    // A includes B and C. Both B and C include D.
    // D should only be merged once.
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile d_file{dir / "d.luma", "function integer func_d() { return 4 }\n"};
    TempFile b_file{dir / "diamond_b.luma", "include \"d.luma\"\n"
                                            "function integer func_b() { return 2 }\n"};
    TempFile c_file{dir / "diamond_c.luma", "include \"d.luma\"\n"
                                            "function integer func_c() { return 3 }\n"};
    TempFile main_file{dir / "diamond_main.luma", "include \"diamond_b.luma\"\n"
                                                  "include \"diamond_c.luma\"\n"
                                                  "\n"
                                                  "@main\n"
                                                  "function void main() {\n"
                                                  "    print(func_d())\n"
                                                  "}\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    // main + func_b + func_c + func_d (only once)
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 4);
}

static void test_include_with_records() {
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile types_file{dir / "types.luma", "record Point {\n"
                                            "    number x,\n"
                                            "    number y\n"
                                            "}\n"};
    TempFile main_file{dir / "use_types.luma", "include \"types.luma\"\n"
                                               "\n"
                                               "@main\n"
                                               "function void main() {\n"
                                               "    Point p = Point { x = 1.0, y = 2.0 }\n"
                                               "    print(p.x)\n"
                                               "}\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Record), 1);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 1);
}

static void test_include_preserves_declaration_order() {
    // Included declarations should appear before the including file's
    // own declarations (at the position of the include statement).
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile helper{dir / "order_helper.luma", "function integer helper() { return 1 }\n"};
    TempFile main_file{dir / "order_main.luma", "function integer before() { return 0 }\n"
                                                "include \"order_helper.luma\"\n"
                                                "function integer after() { return 2 }\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    ASSERT_EQ(program.declarations.size(), static_cast<std::size_t>(3));

    const auto& first = static_cast<const FunctionDeclaration&>(*program.declarations[0]);
    const auto& second = static_cast<const FunctionDeclaration&>(*program.declarations[1]);
    const auto& third = static_cast<const FunctionDeclaration&>(*program.declarations[2]);

    ASSERT_TRUE(first.name == "before");
    ASSERT_TRUE(second.name == "helper");
    ASSERT_TRUE(third.name == "after");
}

static void test_subdirectory_include() {
    const TempDir temp;
    const auto& dir = temp.path();
    const auto sub = dir / "sub";

    std::filesystem::create_directories(sub);

    TempFile sub_file{sub / "sub_helper.luma", "function integer sub_func() { return 99 }\n"};
    TempFile main_file{dir / "subdir_main.luma", "include \"sub/sub_helper.luma\"\n"
                                                 "\n"
                                                 "@main\n"
                                                 "function void main() {\n"
                                                 "    print(sub_func())\n"
                                                 "}\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 2);
}

static void test_warnings_cleared_between_resolves() {
    // Verifies that warnings from a previous resolve() call do not
    // carry over into subsequent calls on the same resolver instance.
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile stmts{dir / "has_stmts.luma", "print(\"side effect\")\n"
                                           "function integer stmts_func() { return 1 }\n"};
    TempFile main_a{dir / "warn_a.luma", "include \"has_stmts.luma\"\n"
                                         "\n"
                                         "@main\n"
                                         "function void main() {\n"
                                         "    print(stmts_func())\n"
                                         "}\n"};
    TempFile main_b{dir / "warn_b.luma", "@main\n"
                                         "function void main() {\n"
                                         "    print(\"no includes\")\n"
                                         "}\n"};

    SourceManager sm_a;
    SourceManager sm_b;

    // Use one resolver for the first call.
    IncludeResolver resolver_a{sm_a};

    const auto& sf_a = sm_a.load(main_a.path_string());
    DiagnosticCollector discarded_a;
    Lexer lexer_a{sf_a.text, discarded_a, sf_a.file_id};
    auto tokens_a = lexer_a.tokenize();
    Parser parser_a{std::move(tokens_a)};
    auto prog_a = parser_a.parse();

    (void)resolver_a.resolve(prog_a);

    ASSERT_EQ(resolver_a.get_diagnostics().size(), static_cast<std::size_t>(1));

    // Use a fresh resolver for the second call — a new resolver should
    // obviously have no warnings.  But also re-resolve the same program
    // on the original resolver to confirm clearing.
    SourceManager sm_c;
    IncludeResolver resolver_b{sm_c};

    const auto& sf_b = sm_c.load(main_b.path_string());
    DiagnosticCollector discarded_b;
    Lexer lexer_b{sf_b.text, discarded_b, sf_b.file_id};
    auto tokens_b = lexer_b.tokenize();
    Parser parser_b{std::move(tokens_b)};
    auto prog_b = parser_b.parse();

    (void)resolver_b.resolve(prog_b);

    ASSERT_EQ(resolver_b.get_diagnostics().size(), static_cast<std::size_t>(0));
}

static void test_transitive_warning_not_misattributed() {
    // A includes B, B includes C. Only C has top-level statements.
    // The warning should be about C, not about B.
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile c_file{dir / "warn_c.luma", "print(\"side effect in C\")\n"
                                         "function integer c_func() { return 3 }\n"};
    TempFile b_file{dir / "warn_b_mid.luma", "include \"warn_c.luma\"\n"
                                             "function integer b_func() { return 2 }\n"};
    TempFile main_file{dir / "warn_main.luma", "include \"warn_b_mid.luma\"\n"
                                               "\n"
                                               "@main\n"
                                               "function void main() {\n"
                                               "    print(b_func())\n"
                                               "}\n"};

    SourceManager sm;

    const auto program = load_and_resolve(main_file.path_string(), sm);

    // Only one warning should be emitted — for C, which actually has
    // top-level statements.  B should NOT produce a spurious warning.
    IncludeResolver resolver{sm};
    SourceManager sm2;
    const auto& sf = sm2.load(main_file.path_string());
    DiagnosticCollector discarded;
    Lexer lexer{sf.text, discarded, sf.file_id};
    auto tokens = lexer.tokenize();
    Parser parser{std::move(tokens)};
    auto prog = parser.parse();

    IncludeResolver resolver2{sm2};
    (void)resolver2.resolve(prog);

    ASSERT_EQ(resolver2.get_diagnostics().size(), static_cast<std::size_t>(1));
}

static void test_traversal_rejected_before_resolve() {
    // Directory traversal should be rejected even when the target path
    // would resolve to a valid file.
    const TempDir temp;
    const auto& dir = temp.path();
    const auto sub = dir / "traverse_sub";

    std::filesystem::create_directories(sub);

    TempFile target{dir / "traverse_target.luma",
                    "function integer traverse_func() { return 1 }\n"};
    TempFile main_file{sub / "traverse_main.luma", "include \"../traverse_target.luma\"\n"
                                                   "\n"
                                                   "@main\n"
                                                   "function void main() {\n"
                                                   "    print(traverse_func())\n"
                                                   "}\n"};

    SourceManager sm;

    const auto& source_file = sm.load(main_file.path_string());

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    IncludeResolver resolver{sm};
    const bool success = resolver.resolve(program);

    ASSERT_FALSE(success);

    const auto& diagnostics = resolver.get_diagnostics();
    ASSERT_TRUE(!diagnostics.empty());
    ASSERT_EQ(diagnostics.front().severity, Severity::Error);
    ASSERT_TRUE(diagnostics.front().message.find("traversal") != std::string::npos);
}

// ─── InclusionStack tests ───

static void test_inclusion_stack_push_pop() {
    InclusionStack stack;

    ASSERT_EQ(stack.depth(), static_cast<std::size_t>(0));

    stack.push();
    ASSERT_EQ(stack.depth(), static_cast<std::size_t>(1));

    stack.push();
    ASSERT_EQ(stack.depth(), static_cast<std::size_t>(2));

    stack.pop();
    ASSERT_EQ(stack.depth(), static_cast<std::size_t>(1));

    stack.pop();
    ASSERT_EQ(stack.depth(), static_cast<std::size_t>(0));
}

static void test_inclusion_guard_raii() {
    InclusionStack stack;

    {
        const InclusionGuard guard{stack};
        ASSERT_EQ(stack.depth(), static_cast<std::size_t>(1));
    }

    // After the guard goes out of scope, the depth should return to zero.
    ASSERT_EQ(stack.depth(), static_cast<std::size_t>(0));
}

static void test_inclusion_guard_nested() {
    InclusionStack stack;

    {
        const InclusionGuard outer{stack};
        {
            const InclusionGuard inner{stack};
            ASSERT_EQ(stack.depth(), static_cast<std::size_t>(2));
        }

        ASSERT_EQ(stack.depth(), static_cast<std::size_t>(1));
    }

    ASSERT_EQ(stack.depth(), static_cast<std::size_t>(0));
}

// ─── ParseFileCallback tests ───

static void test_custom_parse_callback() {
    // A custom callback that returns a program with a single function.
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile helper{dir / "callback_helper.luma", "placeholder content\n"};
    TempFile main_file{dir / "callback_main.luma", "include \"callback_helper.luma\"\n"
                                                   "\n"
                                                   "@main\n"
                                                   "function void main() {\n"
                                                   "    print(\"hello\")\n"
                                                   "}\n"};

    SourceManager sm;

    // Load and parse the main file normally.
    const auto& source_file = sm.load(main_file.path_string());

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    // Use a custom callback that returns an empty program (no declarations).
    auto empty_callback = [](const SourceFile& /*sf*/,
                             std::vector<Diagnostic>& /*diags*/) -> std::optional<Program> {
        return Program{};
    };

    IncludeResolver resolver{sm, empty_callback};
    const bool success = resolver.resolve(program);

    ASSERT_TRUE(success);

    // The include should have been resolved, but the empty callback returns
    // no declarations, so only the main function remains.
    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 1);
}

static void test_make_default_parse_callback() {
    // Verify that make_default_parse_callback() returns a working callback
    // that behaves the same as the default constructor.
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile helper{dir / "default_cb_helper.luma",
                    "function string default_greet() { return \"hi\" }\n"};
    TempFile main_file{dir / "default_cb_main.luma", "include \"default_cb_helper.luma\"\n"
                                                     "\n"
                                                     "@main\n"
                                                     "function void main() {\n"
                                                     "    print(default_greet())\n"
                                                     "}\n"};

    SourceManager sm;

    const auto& source_file = sm.load(main_file.path_string());

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    IncludeResolver resolver{sm, make_default_parse_callback()};
    const bool success = resolver.resolve(program);

    ASSERT_TRUE(success);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 2);
}

static void test_max_include_depth_is_accessible() {
    // Verify the class-level constant is accessible and has the expected value.
    ASSERT_EQ(IncludeResolver::k_max_include_depth, static_cast<std::size_t>(64));
}

// ─── LUMA_PATH search-path resolution ───

static void test_luma_path_include() {
    // A file that exists only on a LUMA_PATH search directory (not in the
    // including file's own directory) should resolve successfully.
    const TempDir temp;
    const auto& dir = temp.path();
    const auto lib = dir / "lp_lib";
    const auto app = dir / "lp_app";

    std::filesystem::create_directories(lib);
    std::filesystem::create_directories(app);

    TempFile lib_file{lib / "lp_helper.luma", "function integer lp_func() { return 7 }\n"};
    TempFile main_file{app / "lp_main.luma", "include \"lp_helper.luma\"\n"
                                             "\n"
                                             "@main\n"
                                             "function void main() {\n"
                                             "    print(lp_func())\n"
                                             "}\n"};

    // Set LUMA_PATH before constructing the resolver (it reads the variable in
    // its constructor). The scoped guard restores the previous environment on
    // scope exit — even if an assertion below throws — so LUMA_PATH cannot leak
    // into later tests.
    const ScopedEnv luma_path{"LUMA_PATH", lib.string()};

    SourceManager sm;

    const auto& source_file = sm.load(main_file.path_string());

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();
    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    // Construct the resolver AFTER setting LUMA_PATH so it is picked up.
    IncludeResolver resolver{sm};
    const bool success = resolver.resolve(program);

    ASSERT_TRUE(success);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Include), 0);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 2);
}

// ─── Top-level statement merging ───

static void test_included_statements_merged_into_parent() {
    // Top-level statements in an included file must be moved into the
    // parent program's statement list and produce a single warning.
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile helper{dir / "stmt_helper.luma", "print(\"included side effect\")\n"
                                              "function integer stmt_func() { return 1 }\n"};
    TempFile main_file{dir / "stmt_main.luma", "include \"stmt_helper.luma\"\n"
                                               "\n"
                                               "@main\n"
                                               "function void main() {\n"
                                               "    print(stmt_func())\n"
                                               "}\n"};

    SourceManager sm;

    const auto& source_file = sm.load(main_file.path_string());

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();
    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    // The main file has no top-level statements of its own.
    ASSERT_EQ(program.statements.size(), static_cast<std::size_t>(0));

    IncludeResolver resolver{sm};
    const bool success = resolver.resolve(program);

    ASSERT_TRUE(success);

    // The included statement should now live in the parent program.
    ASSERT_EQ(program.statements.size(), static_cast<std::size_t>(1));

    // Exactly one warning about top-level statements should be emitted.
    const auto& diagnostics = resolver.get_diagnostics();
    ASSERT_EQ(diagnostics.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(diagnostics.front().severity, Severity::Warning);
}

// ─── Negative: parse errors in an included file ───

static void test_include_with_parse_errors() {
    // An included file containing a syntax error must cause resolution to
    // fail rather than silently dropping the file.
    const TempDir temp;
    const auto& dir = temp.path();

    TempFile broken{dir / "broken_helper.luma", "function integer broken( {\n"
                                                "    return 1\n"
                                                "}\n"};
    TempFile main_file{dir / "broken_main.luma", "include \"broken_helper.luma\"\n"
                                                 "\n"
                                                 "@main\n"
                                                 "function void main() {\n"
                                                 "    print(\"unreachable\")\n"
                                                 "}\n"};

    SourceManager sm;

    const auto& source_file = sm.load(main_file.path_string());

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();
    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    IncludeResolver resolver{sm};
    const bool success = resolver.resolve(program);

    ASSERT_FALSE(success);

    const auto& diagnostics = resolver.get_diagnostics();
    ASSERT_TRUE(!diagnostics.empty());

    // The parser error and the wrapping "errors in included file" diagnostic
    // should both surface as errors.
    const bool has_error = std::ranges::any_of(
        diagnostics, [](const auto& d) { return d.severity == Severity::Error; });
    ASSERT_TRUE(has_error);
}

// ─── Negative: absolute path escaping the allowed tree ───

static void test_absolute_path_escape_rejected() {
    // An absolute include path that resolves outside the including file's
    // directory tree (and is not on LUMA_PATH) must be rejected, even
    // though it contains no ".." traversal component.
    const TempDir temp;
    const auto& dir = temp.path();
    const auto inside = dir / "abs_inside";
    const auto outside = dir / "abs_outside";

    std::filesystem::create_directories(inside);
    std::filesystem::create_directories(outside);

    TempFile target{outside / "abs_target.luma", "function integer abs_func() { return 1 }\n"};

    // Use a forward-slash absolute path so the Luma string literal does not
    // contain backslash escape sequences on Windows.
    const std::string abs_path = (outside / "abs_target.luma").generic_string();

    TempFile main_file{inside / "abs_main.luma", "include \"" + abs_path +
                                                     "\"\n"
                                                     "\n"
                                                     "@main\n"
                                                     "function void main() {\n"
                                                     "    print(abs_func())\n"
                                                     "}\n"};

    SourceManager sm;

    const auto& source_file = sm.load(main_file.path_string());

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();
    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    IncludeResolver resolver{sm};
    const bool success = resolver.resolve(program);

    ASSERT_FALSE(success);

    const auto& diagnostics = resolver.get_diagnostics();
    ASSERT_TRUE(!diagnostics.empty());
    ASSERT_EQ(diagnostics.front().severity, Severity::Error);
    ASSERT_TRUE(diagnostics.front().message.find("outside") != std::string::npos);
}

// ─── Negative: include depth limit ───

static void test_include_depth_limit_exceeded() {
    // A linear include chain deeper than k_max_include_depth must be
    // rejected with a depth-limit diagnostic.
    const TempDir temp;
    const auto& dir = temp.path();
    const auto chain = dir / "depth_chain";

    std::filesystem::create_directories(chain);

    // Generate file_0 → file_1 → ... → file_N, each including the next, so
    // the nesting depth exceeds the limit.
    const std::size_t count = IncludeResolver::k_max_include_depth + 8;

    std::vector<std::unique_ptr<TempFile>> files;
    files.reserve(count + 1);

    for (std::size_t i = 0; i <= count; ++i) {
        std::string content;

        if (i < count) {
            content = std::format("include \"file_{}.luma\"\n", i + 1);
        }

        content += std::format("function integer depth_func_{}() {{ return {} }}\n", i,
                               static_cast<int>(i));

        files.push_back(
            std::make_unique<TempFile>(chain / std::format("file_{}.luma", i), content));
    }

    SourceManager sm;

    const auto& source_file = sm.load(files.front()->path_string());

    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();
    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    IncludeResolver resolver{sm};
    const bool success = resolver.resolve(program);

    ASSERT_FALSE(success);

    const auto& diagnostics = resolver.get_diagnostics();
    const bool has_depth_error = std::ranges::any_of(diagnostics, [](const auto& d) {
        return d.severity == Severity::Error && d.message.find("depth limit") != std::string::npos;
    });
    ASSERT_TRUE(has_depth_error);
}

// ─── main ───

// Parse `source` as input with no file of its own (FileId 0), as the REPL and
// `--eval` do.
[[nodiscard]] static Program parse_unanchored(const std::string& source) {
    DiagnosticCollector discarded;
    Lexer lexer{source, discarded, FileId{0}};
    auto tokens = lexer.tokenize();
    Parser parser{std::move(tokens)};
    return parser.parse();
}

// Parse a file already loaded into `sm`.
[[nodiscard]] static Program parse_loaded(const SourceFile& source_file) {
    DiagnosticCollector discarded;
    Lexer lexer{source_file.text, discarded, source_file.file_id};
    auto tokens = lexer.tokenize();
    Parser parser{std::move(tokens)};
    return parser.parse();
}

namespace {

// Switches the working directory for the guard's lifetime.
class ScopedCwd {
public:
    explicit ScopedCwd(const std::filesystem::path& dir)
        : previous_{std::filesystem::current_path()} {
        std::filesystem::current_path(dir);
    }

    ~ScopedCwd() noexcept {
        std::error_code ec;
        std::filesystem::current_path(previous_, ec);
    }

    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;
    ScopedCwd(ScopedCwd&&) = delete;
    ScopedCwd& operator=(ScopedCwd&&) = delete;

private:
    std::filesystem::path previous_;
};

[[nodiscard]] bool any_message_contains(const std::vector<Diagnostic>& diagnostics,
                                        std::string_view needle) {
    return std::ranges::any_of(diagnostics, [needle](const Diagnostic& d) {
        return d.message.find(needle) != std::string::npos;
    });
}

} // namespace

// ─── Regression: a file-anchored include must not fall back to the CWD ───

static void test_anchored_include_does_not_fall_back_to_cwd() {
    // Previously a missing include was retried relative to the working
    // directory: it could load an unrelated file of the same name there, or
    // misreport a simple missing file as escaping the allowed tree.
    const TempDir temp;
    const auto app = temp.path() / "anchor_app";
    const auto elsewhere = temp.path() / "anchor_elsewhere";
    std::filesystem::create_directories(app);
    std::filesystem::create_directories(elsewhere);

    TempFile decoy{elsewhere / "anchor_only_in_cwd.luma",
                   "function integer anchor_decoy() { return 1 }\n"};
    TempFile main_file{app / "anchor_main.luma", "include \"anchor_only_in_cwd.luma\"\n"};

    const ScopedCwd cwd{elsewhere};
    SourceManager sm;
    auto program = parse_loaded(sm.load(main_file.path_string()));

    IncludeResolver resolver{sm};
    ASSERT_FALSE(resolver.resolve(program));
    ASSERT_TRUE(any_message_contains(resolver.get_diagnostics(), "file not found"));
    ASSERT_FALSE(any_message_contains(resolver.get_diagnostics(), "outside"));
}

// ─── Regression: unanchored (REPL/stdin) includes search the CWD first ───

static void test_unanchored_include_searches_cwd_before_luma_path() {
    const TempDir temp;
    const auto cwd_dir = temp.path() / "order_cwd";
    const auto lib_dir = temp.path() / "order_lib";
    std::filesystem::create_directories(cwd_dir);
    std::filesystem::create_directories(lib_dir);

    TempFile in_cwd{cwd_dir / "order_helper.luma", "function integer order_value() { return 1 }\n"};
    TempFile in_lib{lib_dir / "order_helper.luma", "function integer order_value() { return 2 }\n"};

    const ScopedEnv luma_path{"LUMA_PATH", lib_dir.string()};
    const ScopedCwd cwd{cwd_dir};

    SourceManager sm;
    auto program = parse_unanchored("include \"order_helper.luma\"\n");

    IncludeResolver resolver{sm};
    ASSERT_TRUE(resolver.resolve(program));
    ASSERT_EQ(sm.files().size(), 1U);
    ASSERT_TRUE(sm.files().front().text.find("return 1") != std::string::npos);
}

static void test_unanchored_include_falls_back_to_luma_path() {
    const TempDir temp;
    const auto cwd_dir = temp.path() / "fallback_cwd";
    const auto lib_dir = temp.path() / "fallback_lib";
    std::filesystem::create_directories(cwd_dir);
    std::filesystem::create_directories(lib_dir);

    TempFile in_lib{lib_dir / "fallback_helper.luma", "function integer fb() { return 2 }\n"};

    const ScopedEnv luma_path{"LUMA_PATH", lib_dir.string()};
    const ScopedCwd cwd{cwd_dir};

    SourceManager sm;
    auto program = parse_unanchored("include \"fallback_helper.luma\"\n");

    IncludeResolver resolver{sm};
    ASSERT_TRUE(resolver.resolve(program));
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 1);
}

// ─── Regression: include paths are UTF-8 end to end (MSVC used the ANSI
// code page, turning "café.luma" into mojibake or failing outright) ───

static void test_non_ascii_include_path_resolves() {
    const TempDir temp;
    const auto dir = temp.path() / "utf8_inc";
    std::filesystem::create_directories(dir);

    // "café_日本_helper.luma": 'é' is in Windows-1252 but the CJK characters
    // are in no single-byte ANSI code page, so any remaining ANSI conversion
    // fails here regardless of the machine's code page.
    const std::string utf8_name = "caf\xC3\xA9_\xE6\x97\xA5\xE6\x9C\xAC_helper.luma";
    TempFile helper{dir / path_from_utf8(utf8_name), "function integer cafe() { return 3 }\n"};
    TempFile main_file{dir / "utf8_main.luma", "include \"" + utf8_name + "\"\n"};

    SourceManager sm;
    auto program = parse_loaded(sm.load(path_to_utf8(main_file.path())));

    IncludeResolver resolver{sm};
    const bool resolved = resolver.resolve(program);

    for (const auto& d : resolver.get_diagnostics()) {
        std::cerr << "diagnostic: " << d.message << "\n";
    }

    ASSERT_TRUE(resolved);
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 1);

    // The included file is registered under its UTF-8 path.
    const auto included = std::ranges::find_if(sm.files(), [](const SourceFile& file) {
        return file.text.find("cafe()") != std::string::npos;
    });
    ASSERT_TRUE(included != sm.files().end());
    ASSERT_TRUE(included->path.find(utf8_name) != std::string::npos);
}

// ─── Regression: containment fails closed when the including file's
// directory cannot be canonicalised (previously the check was skipped) ───

static void test_containment_fails_closed_for_unresolvable_base() {
#ifdef _WIN32
    // A device path the OS cannot resolve: weakly_canonical reports an error
    // for its parent directory.  The include itself is found on LUMA_PATH, so
    // only the containment check stands between it and being accepted.
    const TempDir temp;
    const auto lib_dir = temp.path() / "closed_lib";
    std::filesystem::create_directories(lib_dir);
    TempFile in_lib{lib_dir / "closed_helper.luma", "function integer closed() { return 4 }\n"};

    const ScopedEnv luma_path{"LUMA_PATH", lib_dir.string()};

    SourceManager sm;
    const auto& source_file = sm.load_virtual("\\\\.\\nul\\closed_dir\\closed_main.luma",
                                              "include \"closed_helper.luma\"\n");
    auto program = parse_loaded(source_file);

    IncludeResolver resolver{sm};
    ASSERT_FALSE(resolver.resolve(program));
    ASSERT_TRUE(any_message_contains(resolver.get_diagnostics(), "cannot resolve the directory"));
#endif
}

// ─── Session mode (regression: the REPL used SourceManager registration as its
// include-once rule, so a file read by a rejected input was skipped forever) ───

static void test_session_mode_rereads_registered_file() {
    const TempDir temp;
    TempFile main_file{temp.path() / "session_main.luma", "include \"session_helper.luma\"\n"};
    TempFile helper{temp.path() / "session_helper.luma",
                    "function integer session_value() { return 1 }\n"};

    SourceManager sm;
    (void)load_and_resolve(main_file.path_string(), sm);

    {
        std::ofstream out{helper.path(), std::ios::binary | std::ios::trunc};
        out << "function integer session_value() { return 2 }\n";
    }

    // Default mode: the registered helper counts as included and is skipped.
    auto skipped = parse_loaded(sm.load(main_file.path_string()));
    IncludeResolver default_resolver{sm};
    ASSERT_TRUE(default_resolver.resolve(skipped));
    ASSERT_EQ(count_declarations(skipped, DeclarationKind::Function), 0);
    ASSERT_TRUE(default_resolver.included_files().empty());

    // Session mode: only the entry file counts; the helper is re-read from disk.
    const auto entry = sm.load(main_file.path_string()).path;
    auto reread = parse_loaded(sm.load(main_file.path_string()));
    IncludeResolver session_resolver{sm};
    session_resolver.set_session_included(StringSet{entry});
    ASSERT_TRUE(session_resolver.resolve(reread));
    ASSERT_EQ(count_declarations(reread, DeclarationKind::Function), 1);
    ASSERT_EQ(session_resolver.included_files().size(), 1U);
    ASSERT_TRUE(sm.files().back().text.find("return 2") != std::string::npos);
}

static void test_session_mode_skips_listed_and_repeated_files() {
    const TempDir temp;
    TempFile main_file{temp.path() / "listed_main.luma",
                       "include \"listed_a.luma\"\ninclude \"listed_b.luma\"\n"
                       "include \"listed_b.luma\"\n"};
    TempFile a{temp.path() / "listed_a.luma", "function integer listed_a() { return 1 }\n"};
    TempFile b{temp.path() / "listed_b.luma",
               "include \"listed_main.luma\"\nfunction integer listed_b() { return 2 }\n"};

    SourceManager sm;
    const auto& source_file = sm.load(main_file.path_string());
    auto program = parse_loaded(source_file);

    // listed_a counts as already included; listed_b is entered once, and its
    // include of the entry file is skipped.
    std::error_code ec;
    const auto canonical_a = path_to_utf8(std::filesystem::weakly_canonical(a.path(), ec));

    IncludeResolver resolver{sm};
    resolver.set_session_included(StringSet{source_file.path, canonical_a});
    ASSERT_TRUE(resolver.resolve(program));
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 1);
    ASSERT_EQ(resolver.included_files().size(), 1U);
}

static void test_bracketed_virtual_source_is_unanchored() {
    // A REPL input registered as "<repl:N>" resolves includes against the
    // working directory, exactly like unregistered (file id 0) input.
    const TempDir temp;
    TempFile helper{temp.path() / "bracket_helper.luma",
                    "function integer bracket_value() { return 1 }\n"};
    const ScopedCwd cwd{temp.path()};

    SourceManager sm;
    const auto& input = sm.load_virtual("<repl:1>", "include \"bracket_helper.luma\"\n");
    auto program = parse_loaded(input);

    IncludeResolver resolver{sm};
    ASSERT_TRUE(resolver.resolve(program));
    ASSERT_EQ(count_declarations(program, DeclarationKind::Function), 1);

    // An absolute path is still rejected, as for any input with no file.
    // Forward slashes: a backslash would be an escape in the Luma string.
    auto absolute_path = path_to_utf8(helper.path());
    std::ranges::replace(absolute_path, '\\', '/');
    const auto absolute = std::format("include \"{}\"\n", absolute_path);
    const auto& absolute_input = sm.load_virtual("<repl:2>", absolute);
    auto absolute_program = parse_loaded(absolute_input);

    IncludeResolver absolute_resolver{sm};
    ASSERT_FALSE(absolute_resolver.resolve(absolute_program));
    ASSERT_TRUE(any_message_contains(absolute_resolver.get_diagnostics(), "absolute path"));
}

int main() {
    RUN(test_program_without_includes);
    RUN(test_single_include);
    RUN(test_multiple_includes);
    RUN(test_include_once);
    RUN(test_transitive_include);
    RUN(test_circular_include_is_safe);
    RUN(test_self_include_is_safe);
    RUN(test_include_nonexistent_file);
    RUN(test_diamond_include);
    RUN(test_include_with_records);
    RUN(test_include_preserves_declaration_order);
    RUN(test_subdirectory_include);
    RUN(test_warnings_cleared_between_resolves);
    RUN(test_transitive_warning_not_misattributed);
    RUN(test_traversal_rejected_before_resolve);
    RUN(test_inclusion_stack_push_pop);
    RUN(test_inclusion_guard_raii);
    RUN(test_inclusion_guard_nested);
    RUN(test_custom_parse_callback);
    RUN(test_make_default_parse_callback);
    RUN(test_max_include_depth_is_accessible);
    RUN(test_luma_path_include);
    RUN(test_included_statements_merged_into_parent);
    RUN(test_include_with_parse_errors);
    RUN(test_absolute_path_escape_rejected);
    RUN(test_include_depth_limit_exceeded);
    RUN(test_anchored_include_does_not_fall_back_to_cwd);
    RUN(test_unanchored_include_searches_cwd_before_luma_path);
    RUN(test_unanchored_include_falls_back_to_luma_path);
    RUN(test_non_ascii_include_path_resolves);
    RUN(test_containment_fails_closed_for_unresolvable_base);
    RUN(test_session_mode_rereads_registered_file);
    RUN(test_session_mode_skips_listed_and_repeated_files);
    RUN(test_bracketed_virtual_source_is_unanchored);
    return SUMMARY();
}
