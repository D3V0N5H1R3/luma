// json_module_serializer.cpp — JSON serializer (Value → JSON string).
//
// Extracted from json_module.cpp for readability.  Handles arrays,
// objects, primitives, nested structures, and pretty-printing.

#include <string>
#include <string_view>

#include "common/escape.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/text/json_module.hpp"
#include "runtime/stdlib/text/json_value_writer.hpp"

namespace luma {

namespace {

// The traversal itself lives in json_writer::write_value (json_value_writer.hpp).
// ModuleJsonPolicy carries the Json module's behaviour: no slash-escaping,
// "null" (not a throw) past the nesting-depth limit, and only nullary choices
// serialise (as a bare string; choices with fields become null).
struct ModuleJsonPolicy {
    static void escape(std::string_view s, std::string& out) {
        // JsonEscapePolicy<false> — forward slashes are left bare.
        json_escape_string(s, out);
    }

    static bool depth_exceeded(int depth) {
        return depth > CompileTimeLimits::max_json_depth;
    }

    static void on_depth_exceeded(std::string& out) {
        out += "null";
    }

    static constexpr json_writer::JsonChoiceMode choice_mode =
        json_writer::JsonChoiceMode::nullary_string;
    static constexpr bool result_increments_depth = false;
};

} // namespace

void json_serialize_value(const Value& val, std::string& out, int indent, int depth, bool pretty) {
    json_writer::write_value<ModuleJsonPolicy>(val, out, indent, depth, pretty);
}

} // namespace luma
