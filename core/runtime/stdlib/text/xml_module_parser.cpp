// Xml module — XML parsing and validation.
// Split from xml_module.cpp for readability.  Registered by
// register_xml_parser() called from register_xml_ns().

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "analysis/source/source_location.hpp"
#include "common/escape.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "runtime/stdlib/common/stdlib_error_helpers.hpp"
#include "runtime/stdlib/text/parser_cursor.hpp"
#include "runtime/stdlib/text/xml_module.hpp"
#include "runtime/stdlib/text/xml_module_internal.hpp"

namespace luma {

namespace {

// Error-reporting policy for the XML cursor: every scanner error surfaces as an
// XmlParseError (a RuntimeError carrying the failure's byte offset), matching the
// rest of the XML parser.  unexpected_end / too_deep cannot pinpoint an offset,
// so they report std::string::npos ("at end of input").
struct XmlErrorPolicy {
    [[noreturn]] static void unexpected_end() {
        throw XmlParseError{"unexpected end of XML", std::string::npos};
    }

    [[noreturn]] static void expected(char c, std::size_t position) {
        throw XmlParseError{std::format("expected '{}' at position {}", c, position), position};
    }

    [[noreturn]] static void too_deep() {
        throw XmlParseError{"XML nesting too deep", std::string::npos};
    }
};

class XmlParser : public parser_detail::ParserCursor<XmlErrorPolicy> {
public:
    explicit XmlParser(std::string_view input)
        : parser_detail::ParserCursor<XmlErrorPolicy>{input} {}

    [[nodiscard]] std::shared_ptr<XmlValue> parse() {
        skip_whitespace();

        // Skip XML declaration if present.
        if (starts_with("<?xml")) {
            skip_until("?>");
        }

        skip_whitespace();

        // Reject DOCTYPE declarations — external entity injection risk.
        if (starts_with("<!DOCTYPE") || starts_with("<!doctype")) {
            throw XmlParseError{"XML DOCTYPE declarations are not supported (security: external "
                                "entity injection risk)",
                                pos_};
        }

        auto result = parse_element();

        // Reject trailing content after the root element.
        skip_whitespace();

        if (!at_end()) {
            throw XmlParseError{
                std::format("unexpected content after root element at position {}", pos_), pos_};
        }

        return result;
    }

private:
    using Cursor = parser_detail::ParserCursor<XmlErrorPolicy>;
    using Cursor::advance;
    using Cursor::at_end;
    using Cursor::depth_;
    using Cursor::enter_depth;
    using Cursor::expect;
    using Cursor::input_;
    using Cursor::peek;
    using Cursor::pos_;

    [[nodiscard]] bool starts_with(std::string_view s) const {
        return input_.substr(pos_).starts_with(s);
    }

    void skip_whitespace() {
        while (!at_end() && (std::isspace(static_cast<unsigned char>(input_[pos_])) != 0)) {
            ++pos_;
        }
    }

    void skip_until(std::string_view end) {
        auto found = input_.find(end, pos_);

        if (found == std::string_view::npos) {
            throw XmlParseError{std::format("expected '{}' not found", end), pos_};
        }

        pos_ = found + end.size();
    }

    [[nodiscard]] std::string parse_name() {
        auto start = pos_;

        while (!at_end() && xml_detail::is_xml_name_char(input_[pos_])) {
            ++pos_;
        }

        if (pos_ == start) {
            throw XmlParseError{std::format("expected name at position {}", pos_), pos_};
        }

        return std::string{input_.substr(start, pos_ - start)};
    }

    [[nodiscard]] std::string parse_attribute_value() {
        auto quote_char = advance(); // ' or "

        if (quote_char != '"' && quote_char != '\'') {
            throw XmlParseError{"expected quote for attribute value", pos_ - 1};
        }

        std::string result;

        while (!at_end() && input_[pos_] != quote_char) {
            if (input_[pos_] == '&') {
                result += parse_entity();
            } else {
                result += input_[pos_++];
            }
        }

        expect(quote_char);

        return result;
    }

    [[nodiscard]] std::string parse_text() {
        std::string result;

        while (!at_end() && input_[pos_] != '<') {
            if (result.size() >= ResourceLimits::max_string_size) {
                throw XmlParseError{"XML text content exceeds maximum string size", pos_};
            }

            if (input_[pos_] == '&') {
                result += parse_entity();
            } else {
                result += input_[pos_++];
            }
        }

        return result;
    }

    [[nodiscard]] std::string parse_entity() {
        ++pos_; // skip '&'

        auto start = pos_;

        while (!at_end() && input_[pos_] != ';') {
            ++pos_;
        }

        auto entity = input_.substr(start, pos_ - start);

        if (at_end()) {
            // Unterminated entity reference — return raw text.
            return std::string("&") + std::string(entity);
        }

        ++pos_; // skip ';'

        if (entity == "lt") {
            return "<";
        }

        if (entity == "gt") {
            return ">";
        }

        if (entity == "amp") {
            return "&";
        }

        if (entity == "apos") {
            return "'";
        }

        if (entity == "quot") {
            return "\"";
        }

        return std::string{"&"} + std::string{entity} + ";";
    }

    // Parse attribute key=value pairs until '/' or '>' is encountered.
    void parse_attributes(XmlValue& node) {
        while (!at_end()) {
            skip_whitespace();

            // skip_whitespace() may reach EOF; guard before dereferencing so an
            // unterminated tag does not read past the source view (UB on a
            // non-NUL-terminated string_view).
            if (!at_end() && (input_[pos_] == '/' || input_[pos_] == '>')) {
                break;
            }

            auto attr_name = parse_name();

            skip_whitespace();
            expect('=');
            skip_whitespace();

            auto attr_value = parse_attribute_value();

            node.attributes.emplace_back(std::move(attr_name), std::move(attr_value));
        }
    }

    // Parse a single child node (comment, CDATA, nested element, or text).
    void parse_child_node(XmlValue& parent) {
        if (starts_with("<!--")) {
            pos_ += 4;

            auto comment = std::make_shared<XmlValue>();
            comment->node_type = XmlValue::NodeType::Comment;

            auto end = input_.find("-->", pos_);

            if (end == std::string_view::npos) {
                throw XmlParseError{"unterminated comment", pos_};
            }

            comment->tag_or_content = std::string{input_.substr(pos_, end - pos_)};

            pos_ = end + 3;

            parent.children.push_back(std::move(comment));
            return;
        }

        if (starts_with("<![CDATA[")) {
            pos_ += 9;

            auto cdata = std::make_shared<XmlValue>();
            cdata->node_type = XmlValue::NodeType::CData;

            auto end = input_.find("]]>", pos_);

            if (end == std::string_view::npos) {
                throw XmlParseError{"unterminated CDATA", pos_};
            }

            cdata->tag_or_content = std::string{input_.substr(pos_, end - pos_)};

            pos_ = end + 3;

            parent.children.push_back(std::move(cdata));
            return;
        }

        if (!at_end() && input_[pos_] == '<') {
            parent.children.push_back(parse_element());
            return;
        }

        auto text = parse_text();

        // Trim whitespace-only text nodes.
        const bool all_ws =
            std::ranges::all_of(text, [](unsigned char c) { return std::isspace(c); });

        if (!all_ws) {
            auto text_node = std::make_shared<XmlValue>();
            text_node->node_type = XmlValue::NodeType::Text;
            text_node->tag_or_content = std::move(text);

            parent.children.push_back(std::move(text_node));
        }
    }

    [[nodiscard]] std::shared_ptr<XmlValue> parse_element() {
        enter_depth(CompileTimeLimits::max_xml_depth);

        expect('<');

        auto name = parse_name();

        auto node = std::make_shared<XmlValue>();
        node->node_type = XmlValue::NodeType::Element;
        node->tag_or_content = name;

        parse_attributes(*node);

        // Self-closing tag.
        if (!at_end() && input_[pos_] == '/') {
            ++pos_;

            expect('>');

            --depth_;

            return node;
        }

        expect('>');

        // Parse children and text.
        while (!at_end()) {
            skip_whitespace();

            if (starts_with("</")) {
                break;
            }

            if (node->children.size() >= ResourceLimits::max_array_size) {
                throw XmlParseError{"XML element has too many children", pos_};
            }

            parse_child_node(*node);
        }

        // Parse closing tag.
        expect('<');
        expect('/');

        auto close_name = parse_name();

        if (close_name != name) {
            throw XmlParseError{
                std::format("mismatched closing tag: expected </{}>, got </{}>", name, close_name),
                pos_};
        }

        skip_whitespace();
        expect('>');

        --depth_;

        return node;
    }
};

} // namespace

[[nodiscard]] std::shared_ptr<XmlValue> xml_parse_string(std::string_view input) {
    XmlParser parser{input};

    return parser.parse();
}

void register_xml_parser(const EnvPtr& env) {
    ModuleBuilder{"Xml", env}
        .func("deserialize", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return wrap_result_operation(
                "Xml", "deserialize",
                [&]() -> Value {
                    return make_success_value(Value{xml_parse_string(args[0].as_string())});
                },
                error_codes::parse_error);
        })
        .func("deserialize_file", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto safe_path = validate_path(args[0].as_string(), loc);

            // Bound the on-disk size before slurping the whole file into memory,
            // the same guard FileSystem.read_file and Hash.*_file use.  Fail
            // closed when the size cannot be determined (e.g. a FIFO or device
            // node).
            std::error_code size_ec;
            const auto file_bytes = std::filesystem::file_size(safe_path, size_ec);
            if (size_ec) {
                return failure_msg("Xml", "deserialize_file",
                                   std::format("cannot determine the size of '{}': {}",
                                               safe_path.string(), size_ec.message()),
                                   error_codes::io_error);
            }
            if (file_bytes > ResourceLimits::max_string_size) {
                return failure_msg("Xml", "deserialize_file",
                                   std::format("file '{}' exceeds the maximum size of {} bytes",
                                               safe_path.string(), ResourceLimits::max_string_size),
                                   error_codes::io_error);
            }

            const std::ifstream file{safe_path};

            if (!file.is_open()) {
                return failure_msg("Xml", "deserialize_file",
                                   std::format("cannot open '{}'", safe_path.string()),
                                   error_codes::io_error);
            }

            std::ostringstream ss;
            ss << file.rdbuf();

            // Mirror the post-read guard in file_helpers::read_file_contents: a
            // mid-read I/O failure is an I/O error, not malformed XML, so report
            // it as such instead of letting truncated content fall through to the
            // parser and be misreported as a parse error.
            if (file.bad()) {
                return failure_msg("Xml", "deserialize_file",
                                   std::format("error reading '{}'", safe_path.string()),
                                   error_codes::io_error);
            }

            const std::string content = ss.str();

            return wrap_result_operation(
                "Xml", "deserialize_file",
                [&]() -> Value { return make_success_value(Value{xml_parse_string(content)}); },
                error_codes::parse_error);
        })
        .func("is_valid", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            try {
                static_cast<void>(xml_parse_string(args[0].as_string()));

                return Value{true};
            } catch (...) {
                return Value{false};
            }
        });
}

} // namespace luma
