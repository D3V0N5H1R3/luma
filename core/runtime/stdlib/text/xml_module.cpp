#include "runtime/stdlib/text/xml_module.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/text/xml_module_internal.hpp"

namespace luma {

namespace {

// Clone an XML node and apply a modification function, returning the modified clone.
template <typename ModifyFn>
[[nodiscard]] Value modify_xml_clone(const std::shared_ptr<XmlValue>& src, ModifyFn&& modifier) {
    auto clone = src->deep_clone();
    modifier(*clone);
    return Value{std::move(clone)};
}

// Validate that a string is a well-formed XML name.  Delegates the per-character
// grammar to xml_detail::is_xml_name_char, the single predicate the parser's
// parse_name also uses, so any name that passes here serialises to XML the
// parser can read back.  Without this guard a name such as "bad tag" would
// serialise to malformed markup (e.g. <bad tag/>) that fails to re-parse and
// could inject structure.
[[nodiscard]] bool is_valid_xml_name(std::string_view name) {
    if (name.empty()) {
        return false;
    }

    return std::ranges::all_of(name, [](char c) { return xml_detail::is_xml_name_char(c); });
}

void validate_xml_name(std::string_view name, std::string_view function,
                       const SourceLocation& loc) {
    if (!is_valid_xml_name(name)) {
        throw RuntimeError{std::format("{}: invalid XML name '{}'", function, name), loc};
    }
}

// Convert an XmlValue tree into the equivalent Xml.Node choice tree.  Element
// carries its tag, an attribute dictionary, and its ordered children (every node
// type — text, comment, and CDATA included — so a match over the ADT sees the
// full document, unlike Xml.children which keeps only element children).  Text /
// Comment / CData carry their raw content string.  Depth is bounded exactly like
// deep_clone / the serializer: a programmatically built tree is not limited by
// the parser's nesting cap, so guard the native recursion with a catchable error.
[[nodiscard]] Value xml_to_node(const XmlValue& node, int depth, const SourceLocation& loc) {
    if (depth > CompileTimeLimits::max_xml_depth) {
        throw RuntimeError{"Xml.to_node: XML nesting too deep", loc};
    }

    const auto make_node = [](std::string variant, std::vector<Value> fields) {
        auto cv = std::make_shared<ChoiceValue>();
        cv->type_name = "Node";
        cv->variant = std::move(variant);
        cv->fields = std::move(fields);

        return Value{std::move(cv)};
    };

    switch (node.node_type) {
    case XmlValue::NodeType::Text:
        return make_node("Text", {Value{node.tag_or_content}});
    case XmlValue::NodeType::Comment:
        return make_node("Comment", {Value{node.tag_or_content}});
    case XmlValue::NodeType::CData:
        return make_node("CData", {Value{node.tag_or_content}});
    case XmlValue::NodeType::Element:
        break;
    }

    auto attributes = std::make_shared<DictionaryValue>();
    // Pre-build the empty hash index so each set() below is O(1).
    attributes->rebuild_index();

    for (const auto& [key, value] : node.attributes) {
        attributes->set(key, Value{value});
    }

    auto children = std::make_shared<ArrayValue>();
    children->elements->reserve(node.children.size());

    for (const auto& child : node.children) {
        children->elements->push_back(xml_to_node(*child, depth + 1, loc));
    }

    return make_node("Element", {Value{node.tag_or_content}, Value{std::move(attributes)},
                                 Value{std::move(children)}});
}

} // namespace

void register_xml_ns(const EnvPtr& env) {
    ModuleBuilder{"Xml", env}
        .func("element", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& tag = args[0].as_string();
            validate_xml_name(tag, "Xml.element", loc);

            auto node = std::make_shared<XmlValue>();
            node->node_type = XmlValue::NodeType::Element;
            node->tag_or_content = tag;

            return Value{std::move(node)};
        })
        .func("set_attribute", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_xml(args[0], "Xml.set_attribute", loc);

            const auto& name = args[1].as_string();
            const auto& value = args[2].as_string();

            validate_xml_name(name, "Xml.set_attribute", loc);

            return modify_xml_clone(src, [&name, &value](XmlValue& node) {
                // Update or add attribute.
                auto it = std::ranges::find_if(
                    node.attributes, [&name](const auto& attr) { return attr.first == name; });

                if (it != node.attributes.end()) {
                    it->second = value;
                } else {
                    node.attributes.emplace_back(name, value);
                }
            });
        })
        .func("remove_attribute", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_xml(args[0], "Xml.remove_attribute", loc);

            const auto& name = args[1].as_string();

            return modify_xml_clone(src, [&name](XmlValue& node) {
                std::erase_if(node.attributes,
                              [&name](const auto& attr) { return attr.first == name; });
            });
        })
        .func("set_text", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_xml(args[0], "Xml.set_text", loc);

            auto content = args[1].as_string();

            return modify_xml_clone(src, [&content](XmlValue& node) {
                // Remove existing text children and add new one.
                std::erase_if(node.children, [](const auto& c) {
                    return c->node_type == XmlValue::NodeType::Text;
                });

                auto text = std::make_shared<XmlValue>();
                text->node_type = XmlValue::NodeType::Text;
                text->tag_or_content = content;

                node.children.insert(node.children.begin(), std::move(text));
            });
        })
        .func("set_cdata", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_xml(args[0], "Xml.set_cdata", loc);

            auto content = args[1].as_string();

            return modify_xml_clone(src, [&content](XmlValue& node) {
                // Remove existing CDATA children.
                std::erase_if(node.children, [](const auto& c) {
                    return c->node_type == XmlValue::NodeType::CData;
                });

                auto cdata = std::make_shared<XmlValue>();
                cdata->node_type = XmlValue::NodeType::CData;
                cdata->tag_or_content = content;

                node.children.push_back(std::move(cdata));
            });
        })
        .func("add_child", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_xml(args[0], "Xml.add_child", loc);
            auto child_clone = expect_xml(args[1], "Xml.add_child", loc)->deep_clone();

            return modify_xml_clone(src, [&child_clone](XmlValue& node) {
                node.children.push_back(std::move(child_clone));
            });
        })
        .func("add_comment", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_xml(args[0], "Xml.add_comment", loc);

            auto content = args[1].as_string();

            return modify_xml_clone(src, [&content](XmlValue& node) {
                auto comment = std::make_shared<XmlValue>();
                comment->node_type = XmlValue::NodeType::Comment;
                comment->tag_or_content = content;

                node.children.push_back(std::move(comment));
            });
        })
        .func("tag", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.tag", loc);

            return Value{node->tag_or_content};
        })
        .func("text", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.text", loc);
            auto content = xml_detail::get_text_content(*node);

            if (content.empty()) {
                return failure_msg("Xml", "text", "no text content", error_codes::not_found);
            }

            return make_success_value(Value{std::move(content)});
        })
        .func("attribute", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.attribute", loc);

            const auto& name = args[1].as_string();
            auto value = xml_detail::find_attribute(*node, name);

            if (value) {
                return make_success_value(Value{*value});
            }

            return failure_msg("Xml", "attribute", std::format("'{}' not found", name),
                               error_codes::not_found);
        })
        .func("attributes", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.attributes", loc);
            auto dict = std::make_shared<DictionaryValue>();
            // Pre-build the empty hash index so each set() below is O(1), keeping
            // the build O(n) rather than O(n^2).
            dict->rebuild_index();

            for (const auto& [k, v] : node->attributes) {
                dict->set(k, Value{v});
            }

            return Value{std::move(dict)};
        })
        .func("children", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.children", loc);
            auto arr = std::make_shared<ArrayValue>();

            for (const auto& child : node->children) {
                if (child->node_type == XmlValue::NodeType::Element) {
                    arr->elements->emplace_back(child->deep_clone());
                }
            }

            return Value{std::move(arr)};
        })
        .func("child_count", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.child_count", loc);
            std::int64_t count{0};

            for (const auto& child : node->children) {
                if (child->node_type == XmlValue::NodeType::Element) {
                    ++count;
                }
            }

            return Value{count};
        })
        .func("is_leaf", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.is_leaf", loc);

            for (const auto& child : node->children) {
                if (child->node_type == XmlValue::NodeType::Element) {
                    return Value{false};
                }
            }

            return Value{true};
        })
        .func("has_attribute", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.has_attribute", loc);

            const auto& name = args[1].as_string();

            return Value{xml_detail::has_attribute(*node, name)};
        })
        .func("to_dictionary", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.to_dictionary", loc);
            auto dict = std::make_shared<DictionaryValue>();
            // Pre-build the empty hash index so each set() below is O(1), keeping
            // the build O(n) rather than O(n^2).
            dict->rebuild_index();

            for (const auto& child : node->children) {
                if (child->node_type == XmlValue::NodeType::Element) {
                    dict->set(child->tag_or_content, Value{xml_detail::get_text_content(*child)});
                }
            }

            return Value{std::move(dict)};
        })
        .func("to_node", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.to_node", loc);

            return xml_to_node(*node, 0, loc);
        })
        .func("from_dictionary", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_dict(args[1], "Xml.from_dictionary", loc);

            const auto& root_tag = args[0].as_string();
            validate_xml_name(root_tag, "Xml.from_dictionary", loc);

            auto root = std::make_shared<XmlValue>();
            root->node_type = XmlValue::NodeType::Element;
            root->tag_or_content = root_tag;

            for (const auto& [k, v] : args[1].as_dictionary()->entries) {
                validate_xml_name(k, "Xml.from_dictionary", loc);

                auto child = std::make_shared<XmlValue>();
                child->node_type = XmlValue::NodeType::Element;
                child->tag_or_content = k;

                auto text = std::make_shared<XmlValue>();
                text->node_type = XmlValue::NodeType::Text;
                text->tag_or_content = v.to_string();

                child->children.push_back(std::move(text));

                root->children.push_back(std::move(child));
            }

            return Value{std::move(root)};
        })
        .func("set_tag", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_xml(args[0], "Xml.set_tag", loc);

            auto tag = args[1].as_string();

            validate_xml_name(tag, "Xml.set_tag", loc);

            return modify_xml_clone(src, [&tag](XmlValue& node) { node.tag_or_content = tag; });
        })
        .func("children_by_tag", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.children_by_tag", loc);

            const auto& tag = args[1].as_string();
            auto matches = xml_detail::children_by_tag(*node, tag);
            auto arr = std::make_shared<ArrayValue>();

            for (auto& child : matches) {
                arr->elements->emplace_back(std::move(child));
            }

            return Value{std::move(arr)};
        })
        .func("has_child", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.has_child", loc);

            const auto& tag = args[1].as_string();

            return Value{xml_detail::any_child_with_tag(*node, tag)};
        });

    register_xml_parser(env);
    register_xml_serializer(env);
}

} // namespace luma
