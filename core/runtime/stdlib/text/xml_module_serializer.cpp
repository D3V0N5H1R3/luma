// Xml module — serialization, search, and navigation operations.
// Split from xml_module.cpp for readability.  Registered by
// register_xml_serializer() called from register_xml_ns().

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/escape.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "runtime/stdlib/text/xml_module.hpp"
#include "runtime/stdlib/text/xml_module_internal.hpp"

namespace luma {

namespace {

void serialize_cdata(const XmlValue& node, std::string& out) {
    out += "<![CDATA[";

    // Split at "]]>" boundaries to prevent CDATA injection.
    const std::string_view content{node.tag_or_content};
    std::size_t pos = 0;

    while (pos < content.size()) {
        auto found = content.find("]]>", pos);

        if (found == std::string_view::npos) {
            out += content.substr(pos);
            break;
        }

        out += content.substr(pos, found - pos);
        out += "]]]]><![CDATA[>";
        pos = found + 3;
    }

    out += "]]>";
}

void serialize_comment(const XmlValue& node, std::string& out) {
    const auto& content = node.tag_or_content;

    out += "<!--";

    // Replace every "--" in the comment text with "- -" to produce valid XML.
    // Stream directly into out in a single left-to-right pass rather than
    // repeatedly replace()-ing a temporary, which shifts the tail on every match
    // and is O(n^2) on pathological "--" runs (mirrors serialize_cdata).
    std::size_t search{0};
    std::size_t match{0};

    while ((match = content.find("--", search)) != std::string::npos) {
        out.append(content, search, match - search);
        out += "- -";
        search = match + 2;
    }

    out.append(content, search, std::string::npos);

    out += "-->";
}

void serialize_xml(const XmlValue& node, std::string& out, const XmlSerializeOptions& options,
                   int depth);

void serialize_element(const XmlValue& node, std::string& out, const XmlSerializeOptions& options,
                       int depth) {
    if (depth > CompileTimeLimits::max_xml_depth) {
        throw RuntimeError{"XML nesting too deep", {}};
    }

    out += '<';
    out += node.tag_or_content;

    for (const auto& [k, v] : node.attributes) {
        out += ' ';
        out += k;
        out += "=\"";

        xml_escape_string(v, out);

        out += '"';
    }

    if (node.children.empty()) {
        out += "/>";

        return;
    }

    out += '>';

    // Check if only text children.
    bool text_only{true};

    for (const auto& child : node.children) {
        if (child->node_type == XmlValue::NodeType::Element) {
            text_only = false;

            break;
        }
    }

    for (const auto& child : node.children) {
        if (text_only) {
            serialize_xml(*child, out, XmlSerializeOptions{}, 0);
        } else {
            serialize_xml(*child, out, options, depth + 1);
        }
    }

    if (options.pretty && !text_only) {
        out += '\n';

        out.append(static_cast<std::size_t>(options.indent) * static_cast<std::size_t>(depth), ' ');
    }

    out += "</";
    out += node.tag_or_content;
    out += '>';
}

void serialize_xml(const XmlValue& node, std::string& out, const XmlSerializeOptions& options,
                   int depth) {
    if (options.pretty && depth > 0) {
        out += '\n';

        out.append(static_cast<std::size_t>(options.indent) * static_cast<std::size_t>(depth), ' ');
    }

    switch (node.node_type) {
        case XmlValue::NodeType::Text:
            xml_escape_string(node.tag_or_content, out);

            break;
        case XmlValue::NodeType::CData:
            serialize_cdata(node, out);

            break;
        case XmlValue::NodeType::Comment:
            serialize_comment(node, out);

            break;
        case XmlValue::NodeType::Element:
            serialize_element(node, out, options, depth);

            break;
    }
}

template <typename Predicate>
void find_xml_nodes(const std::shared_ptr<XmlValue>& node, const Predicate& predicate,
                    std::vector<std::shared_ptr<XmlValue>>& results, int depth = 0) {
    if (depth > CompileTimeLimits::max_xml_depth) {
        throw RuntimeError{"XML nesting too deep", {}};
    }

    if (predicate(node)) {
        results.push_back(node);
    }

    for (const auto& child : node->children) {
        find_xml_nodes(child, predicate, results, depth + 1);
    }
}

void find_by_tag(const std::shared_ptr<XmlValue>& node, const std::string& tag,
                 std::vector<std::shared_ptr<XmlValue>>& results) {
    find_xml_nodes(
        node,
        [&tag](const std::shared_ptr<XmlValue>& n) {
            return n->node_type == XmlValue::NodeType::Element && n->tag_or_content == tag;
        },
        results);
}

void find_by_attr(const std::shared_ptr<XmlValue>& node, const std::string& attr_name,
                  const std::string& attr_value, std::vector<std::shared_ptr<XmlValue>>& results) {
    find_xml_nodes(
        node,
        [&attr_name, &attr_value](const std::shared_ptr<XmlValue>& n) {
            if (n->node_type != XmlValue::NodeType::Element) {
                return false;
            }

            auto val = xml_detail::find_attribute(*n, attr_name);

            return val.has_value() && *val == attr_value;
        },
        results);
}

[[nodiscard]] std::shared_ptr<XmlValue> navigate_path(const std::shared_ptr<XmlValue>& root,
                                                      const std::string& path) {
    auto current = root;
    std::string segment;
    bool first_segment{true};

    for (std::size_t i{0}; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (segment.empty()) {
                segment.clear();
                continue;
            }

            // Skip root only if the first segment matches root tag.
            if (first_segment && current->tag_or_content == segment) {
                first_segment = false;
                segment.clear();
                continue;
            }

            first_segment = false;

            bool found{false};

            for (const auto& child : current->children) {
                if (child->node_type == XmlValue::NodeType::Element &&
                    child->tag_or_content == segment) {
                    current = child;
                    found = true;

                    break;
                }
            }

            if (!found) {
                return nullptr;
            }

            segment.clear();
        } else {
            segment += path[i];
        }
    }

    return current;
}

} // namespace

void xml_serialize_value(const XmlValue& node, std::string& out, const XmlSerializeOptions& options,
                         int depth) {
    serialize_xml(node, out, options, depth);
}

void register_xml_serializer(const EnvPtr& env) {
    ModuleBuilder{"Xml", env}
        .func("find", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.find", loc);
            std::vector<std::shared_ptr<XmlValue>> results;

            find_by_tag(node, args[1].as_string(), results);

            if (results.empty()) {
                return failure_msg("Xml", "find",
                                   std::format("element '{}' not found", args[1].as_string()),
                                   error_codes::not_found);
            }

            return make_success_value(Value{results[0]->deep_clone()});
        })
        .func("find_all", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.find_all", loc);
            std::vector<std::shared_ptr<XmlValue>> results;

            find_by_tag(node, args[1].as_string(), results);

            auto arr = std::make_shared<ArrayValue>();

            for (const auto& r : results) {
                arr->elements->emplace_back(r->deep_clone());
            }

            return Value{std::move(arr)};
        })
        .func("find_by_attribute", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.find_by_attribute", loc);
            std::vector<std::shared_ptr<XmlValue>> results;

            find_by_attr(node, args[1].as_string(), args[2].as_string(), results);

            if (results.empty()) {
                return failure_msg("Xml", "find_by_attribute",
                                   std::format("no element with attribute '{}' = '{}' found",
                                               args[1].as_string(), args[2].as_string()),
                                   error_codes::not_found);
            }

            return make_success_value(Value{results[0]->deep_clone()});
        })
        .func("at", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.at", loc);
            auto result = navigate_path(node, args[1].as_string());

            if (!result) {
                return failure_msg("Xml", "at",
                                   std::format("path '{}' not found", args[1].as_string()),
                                   error_codes::not_found);
            }

            return make_success_value(Value{result->deep_clone()});
        })
        .func("text_at", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.text_at", loc);
            auto result = navigate_path(node, args[1].as_string());

            if (!result) {
                return failure_msg("Xml", "text_at",
                                   std::format("path '{}' not found", args[1].as_string()),
                                   error_codes::not_found);
            }

            auto content = xml_detail::get_text_content(*result);

            if (content.empty()) {
                return failure_msg("Xml", "text_at",
                                   std::format("no text content at '{}'", args[1].as_string()),
                                   error_codes::not_found);
            }

            return make_success_value(Value{std::move(content)});
        })
        .func("serialize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.serialize", loc);
            std::string out;

            xml_serialize_value(*node, out, {});

            return Value{std::move(out)};
        })
        .func("serialize_pretty", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.serialize_pretty", loc);
            std::string out;

            xml_serialize_value(*node, out, {.indent = 2, .pretty = true});

            return Value{std::move(out)};
        })
        .func("write_file", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto safe_path = validate_path(args[0].as_string(), loc);

            auto node = expect_xml(args[1], "Xml.write_file", loc);
            std::string out;

            xml_serialize_value(*node, out, {.indent = 2, .pretty = true});

            std::ofstream file{safe_path};

            if (!file.is_open()) {
                return failure_msg("Xml", "write_file",
                                   std::format("cannot open '{}'", safe_path.string()),
                                   error_codes::io_error);
            }

            file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            file << out;
            file.flush();
            file.close();

            return make_success_value(Value{NullValue{}});
        });
}

} // namespace luma
