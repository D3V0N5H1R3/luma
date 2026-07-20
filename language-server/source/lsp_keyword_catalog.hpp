#ifndef LUMA_LSP_KEYWORD_CATALOG_HPP
#define LUMA_LSP_KEYWORD_CATALOG_HPP

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luma::lsp {

// Context filter for keyword completions.
enum class KeywordContext : char {
    Always = 'a',
    AfterBrace = 'b',
    Function = 'f',
    Declaration = 'd'
};

struct KeywordInfo {
    std::string_view name;
    std::string_view hover_doc; // Markdown for hover (empty if no hover)
    std::string_view snippet;   // Completion snippet (empty if no snippet)
    std::string_view detail;    // Short description for completion
    // Context filter:
    //   Always      = always show
    //   AfterBrace  = show only after '}' (else, catch, finally)
    //   Function    = show only inside a function body (return, await, spawn)
    //   Declaration = declaration-level only (function, record, choice, etc.)
    KeywordContext context;
};

// Returns the full keyword catalog.
[[nodiscard]] const std::vector<KeywordInfo>& keyword_catalog();

// Returns type keywords as pairs of (name, description).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> get_type_keywords();

// Returns true if the given name is a Luma reserved keyword or
// built-in type keyword and therefore cannot be used as an identifier.
[[nodiscard]] bool is_reserved_keyword_name(std::string_view name);

// Returns every reserved keyword / built-in type keyword name. Exposed so
// tests can drive the lexer over each entry and confirm this catalog stays
// in sync with core/analysis/lexer/lexer.cpp.
[[nodiscard]] std::span<const std::string_view> reserved_keyword_names();

} // namespace luma::lsp

#endif
