#include "runtime/stdlib/text/xml_module.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/escape.hpp"
#include "common/resource_limits.hpp"
#include "common/utf8.hpp"
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

// Build an Xml.ParseError record (type_name "ParseError") carrying the failure
// message and its 1-based line/column.  Matches the "Xml.ParseError" record
// registered in stdlib_type_arities.cpp and mirrors Json/Csv parse_detailed.
[[nodiscard]] Value make_xml_parse_error_record(std::string message, std::int64_t line,
                                                std::int64_t column) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "ParseError";
    rec->fields.emplace_back("message", Value{std::move(message)});
    rec->fields.emplace_back("line", Value{line});
    rec->fields.emplace_back("column", Value{column});

    return Value{std::move(rec)};
}

// Convert a byte offset into the source into a 1-based (line, column) pair.  The
// column counts codepoints from the start of the line so it lines up with how an
// editor reports positions (identical to Json/Csv parse_detailed's mapping).  An
// offset of std::string::npos (an unpinpointable failure) clamps to the end.
struct LineColumn {
    std::int64_t line;
    std::int64_t column;
};

[[nodiscard]] LineColumn offset_to_line_column(std::string_view text, std::size_t offset) {
    offset = std::min(offset, text.size());

    std::int64_t line = 1;
    std::size_t line_start = 0;

    for (std::size_t i = 0; i < offset; ++i) {
        if (text[i] == '\n') {
            ++line;
            line_start = i + 1;
        }
    }

    const std::int64_t column = static_cast<std::int64_t>(luma::utf8_codepoint_count(
                                    text.substr(line_start, offset - line_start))) +
                                1;

    return LineColumn{line, column};
}

// Count the direct element children of a node.  Xml.child_count and Xml.children
// only ever see element children, so remove_child / replace_child index into the
// same element-only sequence (a caller who sees N children via Xml.children can
// address indices 0..N-1 here regardless of interspersed text or comment nodes).
[[nodiscard]] std::int64_t element_child_count(const XmlValue& node) {
    return std::ranges::count_if(node.children, [](const auto& child) {
        return child->node_type == XmlValue::NodeType::Element;
    });
}

// Depth-first pre-order search for the first descendant element whose tag matches.
// Unlike Xml.find (which also considers the node itself), this inspects only
// descendants, so a self-tag match never short-circuits the search.  Depth is
// bounded like every other native XML recursion so a programmatically built tree
// cannot overflow the stack.
[[nodiscard]] std::shared_ptr<XmlValue> find_descendant_element(const XmlValue& node,
                                                                std::string_view tag, int depth,
                                                                const SourceLocation& loc) {
    if (depth > CompileTimeLimits::max_xml_depth) {
        throw RuntimeError{"Xml.find_descendant: XML nesting too deep", loc};
    }

    for (const auto& child : node.children) {
        if (child->node_type != XmlValue::NodeType::Element) {
            continue;
        }

        if (child->tag_or_content == tag) {
            return child;
        }

        if (auto found = find_descendant_element(*child, tag, depth + 1, loc)) {
            return found;
        }
    }

    return nullptr;
}

// Collect every descendant element matching `tag` in document (pre-order) order.
void collect_descendant_elements(const XmlValue& node, std::string_view tag,
                                 std::vector<std::shared_ptr<XmlValue>>& out, int depth,
                                 const SourceLocation& loc) {
    if (depth > CompileTimeLimits::max_xml_depth) {
        throw RuntimeError{"Xml.find_all_descendants: XML nesting too deep", loc};
    }

    for (const auto& child : node.children) {
        if (child->node_type != XmlValue::NodeType::Element) {
            continue;
        }

        if (child->tag_or_content == tag) {
            out.push_back(child);
        }

        collect_descendant_elements(*child, tag, out, depth + 1, loc);
    }
}

// Concatenate the text of this node and every descendant text / CDATA node in
// document order — the DOM textContent of the element.  The recursive counterpart
// to Xml.text, which only reads the node's own direct text children.  Comments
// carry no textual content and are skipped, matching textContent.
void collect_inner_text(const XmlValue& node, std::string& out, int depth,
                        const SourceLocation& loc) {
    if (depth > CompileTimeLimits::max_xml_depth) {
        throw RuntimeError{"Xml.inner_text: XML nesting too deep", loc};
    }

    for (const auto& child : node.children) {
        switch (child->node_type) {
            case XmlValue::NodeType::Text:
            case XmlValue::NodeType::CData:
                out += child->tag_or_content;
                break;
            case XmlValue::NodeType::Element:
                collect_inner_text(*child, out, depth + 1, loc);
                break;
            case XmlValue::NodeType::Comment:
                break;
        }
    }
}

// A single "tag" or "tag[n]" step of an Xml.get_path expression.  The bracketed
// index is 0-based and selects among the repeated element children sharing `tag`.
struct XmlPathSegment {
    std::string tag;
    std::size_t index;
};

// Parse one path segment.  Returns std::nullopt for a malformed segment such as
// an unterminated or non-numeric "[...]", so navigation can fail cleanly rather
// than silently mis-selecting a child.
[[nodiscard]] std::optional<XmlPathSegment> parse_xml_path_segment(std::string_view seg) {
    const auto bracket = seg.find('[');

    if (bracket == std::string_view::npos) {
        return XmlPathSegment{std::string{seg}, 0};
    }

    if (seg.empty() || seg.back() != ']') {
        return std::nullopt;
    }

    const auto index_text = seg.substr(bracket + 1, seg.size() - bracket - 2);

    if (index_text.empty()) {
        return std::nullopt;
    }

    std::size_t index{0};
    const auto* const first = index_text.data();
    const auto* const last = first + index_text.size();
    const auto [ptr, ec] = std::from_chars(first, last, index);

    if (ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }

    return XmlPathSegment{std::string{seg.substr(0, bracket)}, index};
}

// Follow a "/"-separated tag path starting from `root`'s children (root itself is
// not a segment).  Each segment selects the [index]-th direct element child with
// the matching tag (index 0 by default).  On any missing or malformed segment it
// records a message in `error` and returns nullptr; mirrors Json.get_path's
// segment-at-a-time descent composed of per-segment child lookups.
[[nodiscard]] std::shared_ptr<XmlValue> navigate_xml_get_path(const std::shared_ptr<XmlValue>& root,
                                                              std::string_view path,
                                                              std::string& error) {
    auto current = root;
    bool any_segment{false};
    std::size_t pos{0};

    while (pos <= path.size()) {
        const auto slash = path.find('/', pos);
        const auto seg =
            slash == std::string_view::npos ? path.substr(pos) : path.substr(pos, slash - pos);
        pos = slash == std::string_view::npos ? path.size() + 1 : slash + 1;

        if (seg.empty()) {
            continue;
        }

        any_segment = true;

        const auto parsed = parse_xml_path_segment(seg);

        if (!parsed) {
            error = std::format("invalid path segment '{}'", seg);

            return nullptr;
        }

        std::size_t matched{0};
        std::shared_ptr<XmlValue> next;

        for (const auto& child : current->children) {
            if (child->node_type == XmlValue::NodeType::Element &&
                child->tag_or_content == parsed->tag) {
                if (matched == parsed->index) {
                    next = child;

                    break;
                }

                ++matched;
            }
        }

        if (!next) {
            error = std::format("segment '{}' not found", seg);

            return nullptr;
        }

        current = std::move(next);
    }

    if (!any_segment) {
        error = "empty path";

        return nullptr;
    }

    return current;
}

// Decode the five predefined XML entities plus optional numeric (decimal and
// hexadecimal) character references into `out`.  Returns false and sets `error`
// on any malformed reference — an unterminated "&amp", an unknown name "&foo;",
// or an out-of-range code point — so Xml.unescape can surface a typed failure.
[[nodiscard]] bool decode_xml_entities(std::string_view input, std::string& out,
                                       std::string& error) {
    out.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '&') {
            out += input[i];

            continue;
        }

        const auto semicolon = input.find(';', i);

        if (semicolon == std::string_view::npos) {
            error = "unterminated entity reference";

            return false;
        }

        const auto entity = input.substr(i + 1, semicolon - i - 1);

        if (entity == "amp") {
            out += '&';
        } else if (entity == "lt") {
            out += '<';
        } else if (entity == "gt") {
            out += '>';
        } else if (entity == "quot") {
            out += '"';
        } else if (entity == "apos") {
            out += '\'';
        } else if (!entity.empty() && entity.front() == '#') {
            const bool hex = entity.size() >= 2 && (entity[1] == 'x' || entity[1] == 'X');
            const auto digits = entity.substr(hex ? 2 : 1);

            std::uint32_t code_point{0};
            const auto* const first = digits.data();
            const auto* const last = first + digits.size();
            const auto [ptr, ec] = std::from_chars(first, last, code_point, hex ? 16 : 10);

            if (digits.empty() || ec != std::errc{} || ptr != last) {
                error = std::format("invalid numeric entity '&{};'", entity);

                return false;
            }

            const auto encoded = utf8_encode(code_point);

            if (encoded.empty()) {
                error = std::format("invalid code point in entity '&{};'", entity);

                return false;
            }

            out += encoded;
        } else {
            error = std::format("unknown entity '&{};'", entity);

            return false;
        }

        i = semicolon;
    }

    return true;
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
        .func("deserialize_detailed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& text = expect_string(args[0], "Xml.deserialize_detailed", loc);

            // Unlike Xml.deserialize (a string-error result over the opaque xml
            // handle), deserialize_detailed returns the typed Xml.Node tree on
            // success and a structured Xml.ParseError { message, line, column } on
            // failure, so a caller can point at the offending byte.  Mirrors
            // Json.parse_detailed.
            try {
                auto tree = xml_parse_string(text);

                return make_success_value(xml_to_node(*tree, 0, loc));
            } catch (const XmlParseError& e) {
                const auto pos = offset_to_line_column(text, e.position());

                return Value{ResultValue::failure(
                    make_xml_parse_error_record(e.what(), pos.line, pos.column))};
            } catch (const std::exception& e) {
                // Non-positional failure (e.g. Xml.to_node depth guard): report
                // line 1, column 1 rather than guessing an offset.
                return Value{ResultValue::failure(make_xml_parse_error_record(e.what(), 1, 1))};
            }
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
        })
        .func("find_descendant", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.find_descendant", loc);

            const auto& tag = args[1].as_string();
            auto found = find_descendant_element(*node, tag, 0, loc);

            if (!found) {
                return failure_msg("Xml", "find_descendant",
                                   std::format("descendant '{}' not found", tag),
                                   error_codes::not_found);
            }

            return make_success_value(Value{found->deep_clone()});
        })
        .func("find_all_descendants", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.find_all_descendants", loc);

            const auto& tag = args[1].as_string();
            std::vector<std::shared_ptr<XmlValue>> results;

            collect_descendant_elements(*node, tag, results, 0, loc);

            auto arr = std::make_shared<ArrayValue>();
            arr->elements->reserve(results.size());

            for (const auto& r : results) {
                arr->elements->emplace_back(r->deep_clone());
            }

            return Value{std::move(arr)};
        })
        .func("get_path", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.get_path", loc);

            const auto& path = args[1].as_string();
            std::string error;
            auto result = navigate_xml_get_path(node, path, error);

            if (!result) {
                return failure_msg("Xml", "get_path", std::format("path '{}': {}", path, error),
                                   error_codes::not_found);
            }

            return make_success_value(Value{result->deep_clone()});
        })
        .func("remove_child", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_xml(args[0], "Xml.remove_child", loc);

            const auto index = args[1].as_integer();
            const auto count = element_child_count(*src);

            if (index < 0 || index >= count) {
                return failure_msg("Xml", "remove_child",
                                   std::format("index {} out of bounds", index),
                                   error_codes::index_out_of_bounds);
            }

            auto clone = src->deep_clone();
            std::int64_t seen{0};

            for (auto it = clone->children.begin(); it != clone->children.end(); ++it) {
                if ((*it)->node_type == XmlValue::NodeType::Element) {
                    if (seen == index) {
                        clone->children.erase(it);

                        break;
                    }

                    ++seen;
                }
            }

            return make_success_value(Value{std::move(clone)});
        })
        .func("replace_child", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_xml(args[0], "Xml.replace_child", loc);

            const auto index = args[1].as_integer();
            const auto count = element_child_count(*src);

            if (index < 0 || index >= count) {
                return failure_msg("Xml", "replace_child",
                                   std::format("index {} out of bounds", index),
                                   error_codes::index_out_of_bounds);
            }

            auto new_child = expect_xml(args[2], "Xml.replace_child", loc)->deep_clone();
            auto clone = src->deep_clone();
            std::int64_t seen{0};

            for (auto& child : clone->children) {
                if (child->node_type == XmlValue::NodeType::Element) {
                    if (seen == index) {
                        child = std::move(new_child);

                        break;
                    }

                    ++seen;
                }
            }

            return make_success_value(Value{std::move(clone)});
        })
        .func("inner_text", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto node = expect_xml(args[0], "Xml.inner_text", loc);
            std::string out;

            collect_inner_text(*node, out, 0, loc);

            return Value{std::move(out)};
        })
        .func("escape", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& text = expect_string(args[0], "Xml.escape", loc);
            std::string out;

            xml_escape_string(text, out);

            return Value{std::move(out)};
        })
        .func("unescape", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& text = expect_string(args[0], "Xml.unescape", loc);
            std::string out;
            std::string error;

            if (!decode_xml_entities(text, out, error)) {
                return failure_msg("Xml", "unescape", error, error_codes::invalid_argument);
            }

            return make_success_value(Value{std::move(out)});
        });

    register_xml_parser(env);
    register_xml_serializer(env);
}

} // namespace luma
