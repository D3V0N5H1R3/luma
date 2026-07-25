#ifndef LUMA_STDLIB_XML_MODULE_HPP
#define LUMA_STDLIB_XML_MODULE_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

struct XmlValue;

// Thrown by the XML parser on malformed input, carrying the byte offset of the
// failure so Xml.deserialize_detailed can report a 1-based line/column.  Derives
// from RuntimeError so every existing catch site (Xml.deserialize / Xml.is_valid,
// wrap_result_operation) treats it exactly like any other parse RuntimeError; the
// message is unchanged.  A position of std::string::npos means "at/after the end
// of input" (or an offset that could not be pinpointed) and is clamped to the end
// when mapped to a line/column.
class XmlParseError : public RuntimeError {
public:
    XmlParseError(std::string_view message, std::size_t position)
        : RuntimeError{message}, position_{position} {}

    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }

private:
    std::size_t position_;
};

void register_xml_ns(const EnvPtr& env);

// Internal sub-registration functions (split for readability).
void register_xml_parser(const EnvPtr& env);
void register_xml_serializer(const EnvPtr& env);

// Internal — parse an XML string into an XmlValue tree, applying the same
// grammar, security rejections (DOCTYPE) and resource limits as
// Xml.deserialize / Xml.is_valid.  Throws luma::RuntimeError (or another
// std::exception) on malformed input.  Exposed for the fuzz_xml trust-boundary
// target; production code reaches the parser through register_xml_parser().
[[nodiscard]] std::shared_ptr<XmlValue> xml_parse_string(std::string_view input);

// Internal — configuration for the XML serializer.  Groups the indent width and
// the pretty-print toggle so the serialize helpers take one options object
// instead of a positional (int indent, …, bool pretty) argument cluster.  The
// per-recursion `depth` is threaded separately.
struct XmlSerializeOptions {
    int indent{0};
    bool pretty{false};
};

// Internal — serialize an XmlValue tree to XML text.  Shared between
// xml_module_serializer.cpp (Xml.serialize / Xml.serialize_pretty /
// Xml.write_file) and the fuzz_xml target.  `options` carries the indent width
// and pretty toggle; `depth` is the starting recursion depth (0 for a root
// node).
void xml_serialize_value(const XmlValue& node, std::string& out, const XmlSerializeOptions& options,
                         int depth = 0);

} // namespace luma

#endif // LUMA_STDLIB_XML_MODULE_HPP
