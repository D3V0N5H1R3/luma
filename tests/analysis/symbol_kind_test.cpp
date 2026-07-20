// Unit tests for shared/symbols/symbol_kind.hpp.
//
// Covers the LSP-aligned integer mapping and the human-readable name/stream
// helpers. The name-lookup and streaming pair exist for diagnostics; these
// tests are their caller of record (see audit item R04).

#include <sstream>
#include <string>
#include <string_view>

#include "symbols/symbol_kind.hpp"
#include "test_framework.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// to_lsp_symbol_kind — integer values match the LSP spec
// ═══════════════════════════════════════════════════════════

static void test_to_lsp_symbol_kind_values() {
    ASSERT_EQ(to_lsp_symbol_kind(SymbolKind::Namespace), 3);
    ASSERT_EQ(to_lsp_symbol_kind(SymbolKind::Field), 8);
    ASSERT_EQ(to_lsp_symbol_kind(SymbolKind::Enum), 10);
    ASSERT_EQ(to_lsp_symbol_kind(SymbolKind::Interface), 11);
    ASSERT_EQ(to_lsp_symbol_kind(SymbolKind::Function), 12);
    ASSERT_EQ(to_lsp_symbol_kind(SymbolKind::Variable), 13);
    ASSERT_EQ(to_lsp_symbol_kind(SymbolKind::Constant), 14);
    ASSERT_EQ(to_lsp_symbol_kind(SymbolKind::TypeAlias), 19);
    ASSERT_EQ(to_lsp_symbol_kind(SymbolKind::Struct), 23);
}

static void test_to_lsp_symbol_kind_is_constexpr() {
    static_assert(to_lsp_symbol_kind(SymbolKind::Function) == 12);
    static_assert(to_lsp_symbol_kind(SymbolKind::Struct) == 23);
}

// ═══════════════════════════════════════════════════════════
// symbol_kind_name — human-readable names
// ═══════════════════════════════════════════════════════════

static void test_symbol_kind_name_values() {
    ASSERT_EQ(symbol_kind_name(SymbolKind::Namespace), std::string_view{"Namespace"});
    ASSERT_EQ(symbol_kind_name(SymbolKind::Field), std::string_view{"Field"});
    ASSERT_EQ(symbol_kind_name(SymbolKind::Enum), std::string_view{"Enum"});
    ASSERT_EQ(symbol_kind_name(SymbolKind::Interface), std::string_view{"Interface"});
    ASSERT_EQ(symbol_kind_name(SymbolKind::Function), std::string_view{"Function"});
    ASSERT_EQ(symbol_kind_name(SymbolKind::Variable), std::string_view{"Variable"});
    ASSERT_EQ(symbol_kind_name(SymbolKind::Constant), std::string_view{"Constant"});
    ASSERT_EQ(symbol_kind_name(SymbolKind::Struct), std::string_view{"Struct"});
    ASSERT_EQ(symbol_kind_name(SymbolKind::TypeAlias), std::string_view{"TypeAlias"});
}

static void test_symbol_kind_name_is_constexpr() {
    static_assert(symbol_kind_name(SymbolKind::Function) == "Function");
    static_assert(symbol_kind_name(SymbolKind::TypeAlias) == "TypeAlias");
}

// ═══════════════════════════════════════════════════════════
// operator<< — streams the human-readable name
// ═══════════════════════════════════════════════════════════

static void test_operator_stream() {
    std::ostringstream oss;
    oss << SymbolKind::Function;
    ASSERT_EQ(oss.str(), std::string{"Function"});
}

static void test_operator_stream_struct() {
    std::ostringstream oss;
    oss << SymbolKind::Struct << ":" << SymbolKind::Namespace;
    ASSERT_EQ(oss.str(), std::string{"Struct:Namespace"});
}

int main() {
    using namespace luma::test;
    print_suite_header("symbol_kind");

    RUN(test_to_lsp_symbol_kind_values);
    RUN(test_to_lsp_symbol_kind_is_constexpr);
    RUN(test_symbol_kind_name_values);
    RUN(test_symbol_kind_name_is_constexpr);
    RUN(test_operator_stream);
    RUN(test_operator_stream_struct);

    return SUMMARY();
}
