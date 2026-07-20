#include "lsp_hover_literals.hpp"

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
        // ── Type keywords (matched by token type, not lexeme) ──
        {TokenType::ArrayType,
         "```luma\narray<T>\n```\n\nOrdered, homogeneous collection of type `T`."},
        {TokenType::BinaryTreeType,
         "```luma\nbinary_tree\n```\n\nImmutable binary search tree with unique elements."},
        {TokenType::BooleanType,
         "```luma\nboolean\n```\n\nPrimitive type representing `true` or `false`."},
        {TokenType::ChannelType,
         "```luma\nchannel<T>\n```\n\nA typed message channel for communicating between tasks."},
        {TokenType::DictionaryType,
         "```luma\ndictionary<V>\n```\n\nString-keyed map with values of type `V`."},
        {TokenType::GraphType, "```luma\ngraph\n```\n\nDirected or undirected graph of named "
                               "vertices with weighted edges."},
        {TokenType::HashSetType, "```luma\nhash_set\n```\n\nUnordered set with O(1) lookup."},
        {TokenType::IntegerType, "```luma\ninteger\n```\n\n64-bit signed integer type."},
        {TokenType::KeyValueStoreType,
         "```luma\nkey_value_store\n```\n\nPersistent key-value store."},
        {TokenType::LinkedListType, "```luma\nlinked_list\n```\n\nImmutable singly-linked list."},
        {TokenType::NumberType, "```luma\nnumber\n```\n\n64-bit IEEE 754 floating-point type."},
        {TokenType::OptionalType,
         "```luma\noptional<T>\n```\n\nRepresents either `some(T)` or `none`."},
        {TokenType::ResultType,
         "```luma\nresult<T>\n```\n\nRepresents either `success(T)` or `failure(string)`."},
        {TokenType::QueueType, "```luma\nqueue\n```\n\nImmutable FIFO queue."},
        {TokenType::SetType, "```luma\nset\n```\n\nImmutable ordered set with unique elements."},
        {TokenType::StackType, "```luma\nstack\n```\n\nImmutable LIFO stack."},
        {TokenType::StringType, "```luma\nstring\n```\n\nUTF-8 text type. Supports interpolation: "
                                "`\"value is ${expr}\"`."},
        {TokenType::TaskType,
         "```luma\ntask<T>\n```\n\nA concurrently executing computation that produces a "
         "value of type `T`."},
        {TokenType::ReferenceType,
         "```luma\nreference<T>\n```\n\nA shared reference to a value of type `T`."},
        {TokenType::SocketType,
         "```luma\nsocket\n```\n\nA network socket for TCP/UDP communication."},
        {TokenType::WidgetType,
         "```luma\nwidget\n```\n\nA graphical UI widget for building user interfaces."},
        {TokenType::XmlType,
         "```luma\nxml\n```\n\nAn XML document node for structured data processing."},
    };
    return map;
}

} // namespace luma::lsp
