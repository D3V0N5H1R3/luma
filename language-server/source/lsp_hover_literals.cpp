#include "lsp_hover_literals.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

#include "analysis/lexer/token_type.hpp"

namespace luma::lsp {

const std::unordered_map<TokenType, std::string_view, TokenTypeHash>& get_literal_hover_map() {
    static const std::unordered_map<TokenType, std::string_view, TokenTypeHash> map = {
        {TokenType::IntegerLiteral, "```luma\ninteger\n```"},
        {TokenType::NoneLiteral, "```luma\nnone\n```\n\nThe absence of an optional value."},
        {TokenType::NumberLiteral, "```luma\nnumber\n```"},
        {TokenType::StringLiteral, "```luma\nstring\n```"},
        // ── Reserved type keywords (matched by token type, not lexeme) ──
        {TokenType::ArrayType,
         "```luma\narray<T>\n```\n\nOrdered, homogeneous collection of type `T`."},
        {TokenType::BooleanType,
         "```luma\nboolean\n```\n\nPrimitive type representing `true` or `false`."},
        {TokenType::DecimalType,
         "```luma\ndecimal\n```\n\nExact base-10 decimal for correct money arithmetic "
         "(unlike `number`, `0.1 + 0.2` is exactly `0.3`). Built and operated on with the "
         "`Decimal` module."},
        {TokenType::DictionaryType,
         "```luma\ndictionary<V>\n```\n\nString-keyed map with values of type `V`."},
        {TokenType::IntegerType, "```luma\ninteger\n```\n\n64-bit signed integer type."},
        {TokenType::NumberType, "```luma\nnumber\n```\n\n64-bit IEEE 754 floating-point type."},
        {TokenType::OptionalType,
         "```luma\noptional<T>\n```\n\nRepresents either `some(T)` or `none`."},
        {TokenType::ResultType,
         "```luma\nresult<T>\n```\n\nRepresents either `success(T)` or `failure(string)`."},
        {TokenType::StringType, "```luma\nstring\n```\n\nUTF-8 text type. Supports interpolation: "
                                "`\"value is ${expr}\"`."},
    };
    return map;
}

const std::unordered_map<std::string_view, std::string_view>& get_builtin_type_name_hover_map() {
    // The container/handle types demoted from reserved keywords to ordinary
    // identifiers (R02).  They lex as identifiers now, so hover matches them by
    // lexeme rather than by token type.
    static const std::unordered_map<std::string_view, std::string_view> map = {
        {"binary_tree",
         "```luma\nbinary_tree\n```\n\nImmutable binary search tree with unique elements."},
        {"channel",
         "```luma\nchannel<T>\n```\n\nA typed message channel for communicating between tasks."},
        {"key_value_store", "```luma\nkey_value_store\n```\n\nPersistent key-value store."},
        {"queue", "```luma\nqueue\n```\n\nImmutable FIFO queue."},
        {"reference", "```luma\nreference<T>\n```\n\nA shared reference to a value of type `T`."},
        {"set", "```luma\nset\n```\n\nImmutable ordered set with unique elements."},
        {"socket", "```luma\nsocket\n```\n\nA network socket for TCP/UDP communication."},
        {"stack", "```luma\nstack\n```\n\nImmutable LIFO stack."},
        {"task", "```luma\ntask<T>\n```\n\nA concurrently executing computation that produces a "
                 "value of type `T`."},
        {"widget", "```luma\nwidget\n```\n\nA graphical UI widget for building user interfaces."},
        {"xml", "```luma\nxml\n```\n\nAn XML document node for structured data processing."},
    };
    return map;
}

} // namespace luma::lsp
