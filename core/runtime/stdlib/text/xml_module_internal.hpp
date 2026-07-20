#ifndef LUMA_STDLIB_XML_MODULE_INTERNAL_HPP
#define LUMA_STDLIB_XML_MODULE_INTERNAL_HPP

// ═══════════════════════════════════════════════════════════
// Shared XML module internals
// ═══════════════════════════════════════════════════════════
//
// Internal helpers shared across the Xml module's split translation
// units (xml_module.cpp, xml_module_parser.cpp, xml_module_serializer.cpp).
// These are not part of the public API and live in the luma::xml_detail
// namespace so they cannot leak across module boundaries.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "runtime/interpreter/value.hpp"

namespace luma::xml_detail {

// Single source of truth for the XML name grammar: ASCII alphanumerics plus
// '_', '-', '.', ':'.  The writer's validator (is_valid_xml_name) and the
// parser's parse_name must accept exactly the same characters so any name that
// serialises can also be re-parsed; sharing this predicate keeps them in
// lock-step.  Locale-independent by design — XML names must not depend on the
// C locale as std::isalnum would.
[[nodiscard]] constexpr bool is_xml_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.' || c == ':';
}

// Recursively extract text content from an XML node tree.
[[nodiscard]] inline std::string get_text_content(const XmlValue& node) {
    std::string result;

    for (const auto& child : node.children) {
        if (child->node_type == XmlValue::NodeType::Text ||
            child->node_type == XmlValue::NodeType::CData) {
            result += child->tag_or_content;
        }
    }

    return result;
}

// Find an attribute value by name.  Returns std::nullopt if not found.
[[nodiscard]] inline std::optional<std::string> find_attribute(const XmlValue& node,
                                                               std::string_view name) {
    for (const auto& [k, v] : node.attributes) {
        if (k == name) {
            return v;
        }
    }

    return std::nullopt;
}

// Check if an XML node has a specific attribute.
[[nodiscard]] inline bool has_attribute(const XmlValue& node, std::string_view name) {
    return find_attribute(node, name).has_value();
}

// Collect child elements that match a given tag name.
[[nodiscard]] inline std::vector<std::shared_ptr<XmlValue>> children_by_tag(const XmlValue& node,
                                                                            std::string_view tag) {
    std::vector<std::shared_ptr<XmlValue>> result;

    for (const auto& child : node.children) {
        if (child->node_type == XmlValue::NodeType::Element && child->tag_or_content == tag) {
            result.push_back(child->deep_clone());
        }
    }

    return result;
}

// Return true if any child element matches `tag`, without cloning subtrees.
// Used by Xml.has_child, which only needs a boolean answer — cloning every
// matching subtree (as children_by_tag does) just to test emptiness is wasteful.
[[nodiscard]] inline bool any_child_with_tag(const XmlValue& node, std::string_view tag) {
    return std::ranges::any_of(node.children, [tag](const auto& child) {
        return child->node_type == XmlValue::NodeType::Element && child->tag_or_content == tag;
    });
}

} // namespace luma::xml_detail

#endif // LUMA_STDLIB_XML_MODULE_INTERNAL_HPP
