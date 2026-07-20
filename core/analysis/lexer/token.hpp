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
    SourceLocation location;
    // Location of the token's FIRST character (1-based), i.e. the true start of
    // its source span.  For most tokens the lexeme equals the raw source text,
    // so the start can be reconstructed as `location - lexeme_width`; string
    // literals are the exception — their lexeme is PROCESSED (quotes stripped,
    // escape sequences resolved, triple-quoted bodies dedented), so that
    // reconstruction is wrong and, for multi-line strings, produces negative or
    // oversized coordinates.  The lexer records the real start for string tokens
    // here.  A sentinel line of 0 means "unset"; consumers then fall back to the
    // lexeme-width reconstruction, which is correct for non-string tokens.
    SourceLocation start_location{0, 0, 0};
};

} // namespace luma

#endif // LUMA_LEXER_TOKEN_HPP
