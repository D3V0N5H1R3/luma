// ─────────────────────────────────────────────────────────────────────────────
// Type Annotation Parser
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Parse Luma type annotations from the Parser's token stream.
//
// Extracted from Parser to reduce the size of the monolithic parser class.
// Parser::parse_type_annotation() and related methods now delegate here.
// TypeAnnotationParser is declared friend of Parser to retain access to
// the private token-stream helpers without changing Parser's public API.
//
// Key entry points:
//   - parse()                   — parse a full type annotation
//   - parse_type_param_list()   — parse a <T: Bound, U> parameter list
//   - parse_optional_type_params() — parse an optional parameter list
//   - close_generic()           — consume the closing '>' of a generic list
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_PARSER_TYPE_ANNOTATION_PARSER_HPP
#define LUMA_PARSER_TYPE_ANNOTATION_PARSER_HPP

#include <vector>

#include "analysis/ast/declaration.hpp"

namespace luma {

class Parser;

// Ownership modifiers (`unique` / `borrow`) parsed ahead of a type annotation
// and threaded into the concrete type parsers.  Bundling the two flags into one
// value keeps the call sites self-documenting and removes the same-type boolean
// parameter pair that was trivially swappable.
struct TypeOwnership {
    bool is_unique{false};
    bool is_borrow{false};
};

// Parses Luma type annotations on behalf of the Parser.
// All methods require a Parser reference so they share the same token stream
// state.  TypeAnnotationParser is a friend of Parser.
class TypeAnnotationParser {
public:
    [[nodiscard]] static TypeAnnotation parse(Parser& parser);
    [[nodiscard]] static std::vector<TypeParam> parse_type_param_list(Parser& parser);
    [[nodiscard]] static std::vector<TypeParam> parse_optional_type_params(Parser& parser);

    // Consume the closing '>' of a generic type argument list.
    // Handles the '>>' token produced when lexing nested generics like
    // array<array<T>>: splits it so the outer generic can consume the
    // remaining '>'.
    static void close_generic(Parser& parser);

private:
    // parse() sub-cases, dispatched on the leading token.
    [[nodiscard]] static TypeAnnotation parse_tuple_type(Parser& parser, TypeOwnership ownership);
    [[nodiscard]] static TypeAnnotation parse_function_type(Parser& parser,
                                                            TypeOwnership ownership);
    [[nodiscard]] static TypeAnnotation parse_named_type(Parser& parser, TypeOwnership ownership);
};

} // namespace luma

#endif // LUMA_PARSER_TYPE_ANNOTATION_PARSER_HPP
