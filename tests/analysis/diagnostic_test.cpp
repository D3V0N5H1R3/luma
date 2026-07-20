// Diagnostics unit tests.

#include <string>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/renderer.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/source/source_manager.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── DiagnosticCode string tests ───

static void test_error_code_e0001() {
    Diagnostic d;
    d.code = DiagnosticCode::TypeMismatch;
    ASSERT_EQ(d.code_string(), "E0001");
}

static void test_error_code_e0009() {
    Diagnostic d;
    d.code = DiagnosticCode::StdlibArgCount;
    ASSERT_EQ(d.code_string(), "E0009");
}

static void test_error_code_e1001() {
    Diagnostic d;
    d.code = DiagnosticCode::UnexpectedToken;
    ASSERT_EQ(d.code_string(), "E1001");
}

static void test_error_code_e2001() {
    Diagnostic d;
    d.code = DiagnosticCode::CompileError;
    ASSERT_EQ(d.code_string(), "E2001");
}

static void test_warning_code_w0001() {
    Diagnostic d;
    d.code = DiagnosticCode::UnusedVariable;
    ASSERT_EQ(d.code_string(), "W0001");
}

static void test_warning_code_w0012() {
    Diagnostic d;
    d.code = DiagnosticCode::ShadowedVariable;
    ASSERT_EQ(d.code_string(), "W0012");
}

static void test_code_none() {
    Diagnostic d;
    d.code = DiagnosticCode::None;
    ASSERT_TRUE(d.code_string().empty());
}

// ─── Diagnostic code category tests ───

static void test_category_of_type_error() {
    ASSERT_EQ(*category_of(1), DiagnosticCategory::Type);
    ASSERT_EQ(*category_of(999), DiagnosticCategory::Type);
}

static void test_category_of_syntax_error() {
    ASSERT_EQ(*category_of(1001), DiagnosticCategory::Syntax);
    ASSERT_EQ(*category_of(1999), DiagnosticCategory::Syntax);
}

static void test_category_of_compile_error() {
    ASSERT_EQ(*category_of(2001), DiagnosticCategory::Compile);
    ASSERT_EQ(*category_of(2999), DiagnosticCategory::Compile);
}

static void test_category_of_runtime_error() {
    ASSERT_EQ(*category_of(4001), DiagnosticCategory::Runtime);
    ASSERT_EQ(*category_of(4999), DiagnosticCategory::Runtime);
}

static void test_category_of_lint_warning() {
    ASSERT_EQ(*category_of(5001), DiagnosticCategory::Warning);
    ASSERT_EQ(*category_of(5999), DiagnosticCategory::Warning);
}

static void test_category_of_unknown() {
    ASSERT_FALSE(category_of(0).has_value());
    ASSERT_FALSE(category_of(3500).has_value());
    ASSERT_FALSE(category_of(7000).has_value());
}

static void test_category_prefix_errors() {
    ASSERT_EQ(category_prefix(DiagnosticCategory::Type), "E");
    ASSERT_EQ(category_prefix(DiagnosticCategory::Syntax), "E");
    ASSERT_EQ(category_prefix(DiagnosticCategory::Compile), "E");
    ASSERT_EQ(category_prefix(DiagnosticCategory::Runtime), "E");
}

static void test_category_prefix_warning() {
    ASSERT_EQ(category_prefix(DiagnosticCategory::Warning), "W");
}

// ─── Diagnostic builder tests ───

static void test_diagnostic_builder() {
    auto d = diag::error("something went wrong")
                 .category(DiagnosticCategory::Type)
                 .error_code(DiagnosticCode::TypeMismatch)
                 .primary(SourceLocation{.file_id = 1, .line = 5},
                          SourceLocation{.file_id = 1, .line = 10}, "here")
                 .hint("try this instead")
                 .build();

    ASSERT_EQ(d.severity, Severity::Error);
    ASSERT_EQ(d.category, DiagnosticCategory::Type);
    ASSERT_EQ(d.code, DiagnosticCode::TypeMismatch);
    ASSERT_EQ(d.message, "something went wrong");
    ASSERT_EQ(d.spans.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(d.spans[0].is_primary);
    ASSERT_TRUE(d.hint.has_value());
    ASSERT_EQ(*d.hint, "try this instead");
}

static void test_diagnostic_warning_builder() {
    auto d = diag::warning("unused variable")
                 .category(DiagnosticCategory::Warning)
                 .error_code(DiagnosticCode::UnusedVariable)
                 .build();

    ASSERT_EQ(d.severity, Severity::Warning);
    ASSERT_EQ(d.code, DiagnosticCode::UnusedVariable);
    ASSERT_EQ(d.code_string(), "W0001");
}

// ─── Severity and category coverage ───

static void test_primary_location() {
    auto d = diag::error("test")
                 .primary(SourceLocation{.line = 3, .column = 7},
                          SourceLocation{.line = 3, .column = 12}, "here")
                 .build();

    auto loc = d.primary_location();
    ASSERT_EQ(loc.line, 3);
    ASSERT_EQ(loc.column, 7);
}

static void test_primary_location_empty() {
    const Diagnostic d;
    auto loc = d.primary_location();
    ASSERT_EQ(loc.line, 1);
    ASSERT_EQ(loc.column, 1);
}

static void test_has_errors_and_warnings() {
    auto err = diag::error("test error").build();
    ASSERT_EQ(err.severity, Severity::Error);
    ASSERT_EQ(err.message, "test error");

    auto warn = diag::warning("test warning").build();
    ASSERT_EQ(warn.severity, Severity::Warning);
    ASSERT_EQ(warn.message, "test warning");
}

// ─── Fix helper tests ───

static void test_fix_replace() {
    auto fix = Fix::replace(SourceLocation{.line = 1, .column = 1},
                            SourceLocation{.line = 1, .column = 5}, "new_text", "replace it");
    ASSERT_EQ(fix.replacement, "new_text");
    ASSERT_EQ(fix.description, "replace it");
}

static void test_fix_insert() {
    auto fix = Fix::insert(SourceLocation{.line = 2, .column = 3}, "inserted", "add it");
    ASSERT_EQ(fix.replacement, "inserted");
    ASSERT_EQ(fix.start.line, 2);
    ASSERT_EQ(fix.start.column, 3);
    ASSERT_EQ(fix.end.line, fix.start.line);
    ASSERT_EQ(fix.end.column, fix.start.column);
}

// ─── Renderer tests ───

static void test_renderer_format_produces_output() {
    const SourceManager sm;
    const DiagnosticRenderer renderer{sm};

    auto d = diag::error("type mismatch")
                 .category(DiagnosticCategory::Type)
                 .error_code(DiagnosticCode::TypeMismatch)
                 .build();

    const auto output = renderer.format(d);
    ASSERT_FALSE(output.empty());
    ASSERT_TRUE(output.find("type mismatch") != std::string::npos);
}

static void test_renderer_format_includes_code() {
    const SourceManager sm;
    const DiagnosticRenderer renderer{sm};

    auto d = diag::error("wrong count")
                 .category(DiagnosticCategory::Type)
                 .error_code(DiagnosticCode::WrongArgCount)
                 .build();

    const auto output = renderer.format(d);
    ASSERT_TRUE(output.find("E0002") != std::string::npos);
}

static void test_renderer_caret_hostile_column_is_bounded() {
    // Regression: a span column can originate from a corrupt or adversarial
    // bytecode cache (the source-map column is deserialized as a raw u32 with no
    // range check).  render_caret_line must clamp it so a negative or enormous
    // column cannot drive an unbounded native allocation (std::bad_alloc /
    // length_error) for the caret indent or the underline width.
    const TempFile file{"let x = 1\n"};
    SourceManager sm;
    const auto file_id = sm.load(file.path().string()).file_id;

    const DiagnosticRenderer renderer{sm};

    // Negative start column wraps to a huge size_t when cast; the huge end column
    // makes end - start overflow signed int and the underline width enormous.
    auto d =
        diag::error("hostile span")
            .category(DiagnosticCategory::Runtime)
            .primary(SourceLocation{.file_id = file_id, .line = 1, .column = -2147483647},
                     SourceLocation{.file_id = file_id, .line = 1, .column = 2147483647}, "here")
            .build();

    // Must return promptly with bounded output — no multi-gigabyte allocation.
    const auto output = renderer.format(d);
    ASSERT_TRUE(output.find("hostile span") != std::string::npos);
    ASSERT_TRUE(output.size() < static_cast<std::size_t>(64 * 1024));
}

static void test_renderer_hostile_line_is_safe() {
    // Regression: a span line can also originate from a corrupt or adversarial
    // bytecode cache.  render_span renders the "after" context as line + 1; for
    // line == INT_MAX that addition is signed-overflow UB (trapped under UBSan).
    // The renderer must guard it, mirroring the existing line > 1 guard on the
    // "before" context.
    const TempFile file{"let x = 1\n"};
    SourceManager sm;
    const auto file_id = sm.load(file.path().string()).file_id;

    const DiagnosticRenderer renderer{sm};

    auto d =
        diag::error("hostile line")
            .category(DiagnosticCategory::Runtime)
            .primary(SourceLocation{.file_id = file_id, .line = 2147483647, .column = 1},
                     SourceLocation{.file_id = file_id, .line = 2147483647, .column = 2}, "here")
            .build();

    // Must render without triggering line + 1 overflow; output stays bounded.
    const auto output = renderer.format(d);
    ASSERT_TRUE(output.find("hostile line") != std::string::npos);
    ASSERT_TRUE(output.size() < static_cast<std::size_t>(64 * 1024));
}

// ─── Snapshot tests ───
//
// These verify the exact rendered output of diagnostics against stored
// baseline files in tests/analysis/snapshots/.  Run with the
// environment variable UPDATE_SNAPSHOTS=1 to create or refresh them.

static const char* const kTestFile = __FILE__;

static void test_snapshot_error_type_mismatch() {
    const SourceManager sm;
    const DiagnosticRenderer renderer{sm};

    auto d = diag::error("expected 'integer' but got 'string'")
                 .category(DiagnosticCategory::Type)
                 .error_code(DiagnosticCode::TypeMismatch)
                 .build();

    ASSERT_SNAPSHOT("error_type_mismatch", renderer.format(d), kTestFile);
}

static void test_snapshot_error_with_hint() {
    const SourceManager sm;
    const DiagnosticRenderer renderer{sm};

    auto d = diag::error("cannot assign to immutable variable 'x'")
                 .category(DiagnosticCategory::Type)
                 .error_code(DiagnosticCode::ImmutableAssignment)
                 .hint("declare with 'mutable' to allow assignment")
                 .build();

    ASSERT_SNAPSHOT("error_with_hint", renderer.format(d), kTestFile);
}

static void test_snapshot_warning_unused_variable() {
    const SourceManager sm;
    const DiagnosticRenderer renderer{sm};

    auto d = diag::warning("variable 'temp' is declared but never used")
                 .category(DiagnosticCategory::Warning)
                 .error_code(DiagnosticCode::UnusedVariable)
                 .build();

    ASSERT_SNAPSHOT("warning_unused_variable", renderer.format(d), kTestFile);
}

static void test_snapshot_error_with_fix() {
    const SourceManager sm;
    const DiagnosticRenderer renderer{sm};

    auto d = diag::error("unknown type 'intger'")
                 .category(DiagnosticCategory::Type)
                 .error_code(DiagnosticCode::TypeMismatch)
                 .fix(Fix::replace(SourceLocation{.line = 1, .column = 10},
                                   SourceLocation{.line = 1, .column = 16}, "integer",
                                   "did you mean 'integer'?"))
                 .build();

    ASSERT_SNAPSHOT("error_with_fix", renderer.format(d), kTestFile);
}

int main() {
    RUN(test_error_code_e0001);
    RUN(test_error_code_e0009);
    RUN(test_error_code_e1001);
    RUN(test_error_code_e2001);
    RUN(test_warning_code_w0001);
    RUN(test_warning_code_w0012);
    RUN(test_code_none);
    RUN(test_category_of_type_error);
    RUN(test_category_of_syntax_error);
    RUN(test_category_of_compile_error);
    RUN(test_category_of_runtime_error);
    RUN(test_category_of_lint_warning);
    RUN(test_category_of_unknown);
    RUN(test_category_prefix_errors);
    RUN(test_category_prefix_warning);
    RUN(test_diagnostic_builder);
    RUN(test_diagnostic_warning_builder);
    RUN(test_primary_location);
    RUN(test_primary_location_empty);
    RUN(test_has_errors_and_warnings);
    RUN(test_fix_replace);
    RUN(test_fix_insert);
    RUN(test_renderer_format_produces_output);
    RUN(test_renderer_format_includes_code);
    RUN(test_renderer_caret_hostile_column_is_bounded);
    RUN(test_renderer_hostile_line_is_safe);
    RUN(test_snapshot_error_type_mismatch);
    RUN(test_snapshot_error_with_hint);
    RUN(test_snapshot_warning_unused_variable);
    RUN(test_snapshot_error_with_fix);
    return SUMMARY();
}
