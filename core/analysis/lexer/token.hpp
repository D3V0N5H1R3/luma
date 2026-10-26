#ifndef LUMA_LEXER_TOKEN_HPP
#define LUMA_LEXER_TOKEN_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

// Pre-computed literal value for numeric/string/boolean tokens.
using TokenLiteral = std::variant<std::monostate, std::int64_t, double, bool, std::string>;

struct Token {
    // Fail-fast default: an unset token reads as the invalid/recovery type rather
    // than silently masquerading as a valid BooleanLiteral (the zero-valued enumerator).
    TokenType type{TokenType::Error};
    // String interning deferred: lexeme lifetimes are tied to token ownership;
    // interning would require global state and complicate the lexer's threading
    // model.  Revisit only if profiling shows lexeme allocation as a bottleneck.
    std::string lexeme;
    std::optional<TokenLiteral> literal;
    // Location of the position ONE PAST the token's last character (1-based).
    // AST nodes and the language server are built on this end-position
    // convention, so it stays the token's `location`.  The lexer also stamps
    // the token's start into `location.start_line`/`start_column`, so every
    // copy of this location (AST nodes, source maps) can still find the start
    // and diagnostics put the caret on the token itself.
    SourceLocation location;
    // Location of the token's FIRST character (1-based), i.e. the true start of
    // its source span.  The lexer records it for every token it emits.  It is
    // essential for string literals, whose lexeme is PROCESSED (quotes stripped,
    // escape sequences resolved, triple-quoted bodies dedented), so their start
    // cannot be reconstructed as `location - lexeme_width`.  A sentinel line of
    // 0 means "unset" (only for tokens synthesised outside the lexer); consumers
    // then fall back to the lexeme-width reconstruction.
    SourceLocation start_location{0, 0, 0};
    // True when at least one newline separates this token from the previous
    // one.  Luma has no statement terminator, so the parser uses this to stop a
    // `(` or `[` at the start of a line from continuing the previous expression
    // as a call or index.
    bool preceded_by_newline{false};
};

} // namespace luma

#endif // LUMA_LEXER_TOKEN_HPP
