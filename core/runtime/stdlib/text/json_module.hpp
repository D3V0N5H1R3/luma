#ifndef LUMA_STDLIB_JSON_MODULE_HPP
#define LUMA_STDLIB_JSON_MODULE_HPP

#include <string>
#include <string_view>

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

class Value;

void register_json_ns(const EnvPtr& env);

// Internal sub-registration (split for readability).
void register_json_parser(const EnvPtr& env);

// Internal sub-registration — the typed Json.Value ADT (parse / to_string /
// accessors).  Split into json_value_module.cpp.
void register_json_value(const EnvPtr& env);

// Internal — serialize a Value to JSON.  Shared between
// json_module.cpp and json_module_parser.cpp.
void json_serialize_value(const Value& val, std::string& out, int indent, int depth, bool pretty);

// Internal — parse a JSON string into a Value, applying the same grammar and
// resource limits as Json.deserialize.  Throws std::runtime_error (or a
// subclass) on malformed input.  Exposed for the fuzz_json_stdlib trust-boundary
// target; production code reaches the parser through register_json_parser().
[[nodiscard]] Value json_parse_string(std::string_view input);

} // namespace luma

#endif // LUMA_STDLIB_JSON_MODULE_HPP
