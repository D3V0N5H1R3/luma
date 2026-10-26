// json_value_module.cpp — the typed Json.Value ADT and its accessors.
//
// This translation unit registers the statically-typed corner of the Json
// module: the recursive Json.Value choice (JsonObject / JsonArray / JsonString /
// JsonNumber / JsonBool / JsonNull) plus parse / to_string and the result- and
// optional-returning accessors.  Unlike the permissive dynamic API in
// json_module.cpp (deserialize / get / serialize), a Json.Value can be walked
// with an exhaustive `match`, bringing untrusted JSON under static typing.
//
// The runtime shape is backed by shared/json/JsonValue: Json.parse converts a
// parsed JsonValue tree into a Luma ChoiceValue tree, and Json.to_string does
// the reverse and reuses JsonValue's tested serialisation and escaping.

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "common/utf8.hpp"
#include "json/json.hpp"
#include "parse_error.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/native_function_validation.hpp"
#include "runtime/stdlib/common/numeric_helpers.hpp"
#include "runtime/stdlib/common/stdlib_error_helpers.hpp"
#include "runtime/stdlib/text/json_module.hpp"

namespace luma {

namespace {

// Runtime type_name of the Json.Value choice.  Must match the short name of the
// ChoiceDeclaration registered as "Json.Value" in stdlib_type_arities.cpp.
constexpr std::string_view k_json_value_type = "Value";

// Build a Json.Value choice value carrying the given payload fields.
[[nodiscard]] Value make_json_variant(std::string variant, std::vector<Value> fields) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = std::string{k_json_value_type};
    cv->variant = std::move(variant);
    cv->fields = std::move(fields);

    return Value{std::move(cv)};
}

// Convert a parsed shared/json JsonValue into the equivalent Luma Json.Value
// choice tree.  Recursion depth is bounded by json::parse's own max_depth, so
// native recursion here is safe.
[[nodiscard]] Value jsonvalue_to_luma(const json::JsonValue& node) {
    if (node.is_null()) {
        return make_json_variant("JsonNull", {});
    }

    if (node.is_bool()) {
        return make_json_variant("JsonBool", {Value{node.as_bool()}});
    }

    // JSON integers and reals both fold into the single JsonNumber(number)
    // variant — Luma's `number` is a double.
    if (node.is_integer()) {
        return make_json_variant("JsonNumber", {Value{static_cast<double>(node.as_integer())}});
    }

    if (node.is_number()) {
        return make_json_variant("JsonNumber", {Value{node.as_number()}});
    }

    if (node.is_string()) {
        return make_json_variant("JsonString", {Value{node.as_string()}});
    }

    if (node.is_array()) {
        auto array = std::make_shared<ArrayValue>();
        array->elements->reserve(node.as_array().size());

        for (const auto& element : node.as_array()) {
            array->elements->push_back(jsonvalue_to_luma(element));
        }

        return make_json_variant("JsonArray", {Value{std::move(array)}});
    }

    // Object — preserve key order via DictionaryValue::set, which keeps the
    // hash index consistent as each entry is inserted.
    auto dict = std::make_shared<DictionaryValue>();

    for (const auto& [key, value] : node.as_object()) {
        dict->set(key, jsonvalue_to_luma(value));
    }

    return make_json_variant("JsonObject", {Value{std::move(dict)}});
}

// Runtime-configurable nesting bound (LUMA_LIMIT_MAX_JSON_NESTING_DEPTH,
// tightened to 32 under --box), clamped to the compile-time cap that keeps
// json::JsonValue::parse's recursive descent within the native stack. Mirrors
// the clamp applied by json_module_parser.cpp's Json.deserialize so Json.parse
// and Json.parse_detailed cannot bypass the sandbox-configured limit.  Also
// bounds the Json.to_string direction (LumaToJsonConverter below).
[[nodiscard]] std::size_t json_depth_limit() {
    return std::min<std::size_t>(ResourceLimits::max_json_nesting_depth,
                                 static_cast<std::size_t>(CompileTimeLimits::max_json_depth));
}

// Convert a Luma Json.Value choice tree back into a shared/json JsonValue for
// serialisation.  Throws RuntimeError when the value is not a well-formed
// Json.Value (e.g. a bare payload-less variant constant) or exceeds the JSON
// resource limits.
//
// Unlike the parse direction (bounded by json::parse's max_depth), a Json.Value
// can be built at runtime — `v = Json.Value.JsonArray([v])` in a loop — so this
// walk enforces its own limits: nesting depth against json_depth_limit() (an
// unbounded native recursion would overflow the stack, which nothing can
// catch), and the total number of array elements / object members against
// ResourceLimits::max_json_elements (sharing one node twice per level,
// `JsonArray([v, v])` x 40, otherwise expands to 2^40 nodes), and the size of
// the serialised text against ResourceLimits::max_string_size.
class LumaToJsonConverter {
public:
    LumaToJsonConverter(std::string_view func_name, const SourceLocation& loc)
        : func_name_{func_name}, loc_{loc} {}

    [[nodiscard]] json::JsonValue convert(const Value& value) {
        return convert(value, 0);
    }

    // Defence in depth: the serialised text must never exceed the budget
    // charged during convert().
    void check_serialised_size(const std::string& serialised) const {
        if (serialised.size() > ResourceLimits::max_string_size) {
            throw_output_too_large();
        }
    }

private:
    [[nodiscard]] json::JsonValue convert(const Value& value, std::size_t depth);

    void count_child() {
        if (++element_count_ > ResourceLimits::max_json_elements) {
            throw RuntimeError{std::format("{}: Json.Value exceeds the maximum of {} elements",
                                           func_name_, ResourceLimits::max_json_elements),
                               loc_, "build a smaller Json.Value"};
        }
    }

    // Charge `bytes` of serialised output against max_string_size.  Every node
    // charges exactly what JsonValue::serialise will emit for it — escaped
    // string text, quotes, separators, brackets, and number/literal text — so
    // the serialised string is bounded before it is built (a shared large
    // string, or one full of characters that escape to 2 or 6 bytes, would
    // otherwise expand past the limit).
    void charge_output(std::size_t bytes) {
        if (bytes > ResourceLimits::max_string_size - output_bytes_) {
            throw_output_too_large();
        }

        output_bytes_ += bytes;
    }

    // Charge a quoted, JSON-escaped string (a string value or an object key).
    void charge_string(std::string_view text) {
        std::size_t escaped{2}; // surrounding quotes

        for (const char c : text) {
            escaped += escaped_char_size(c);
        }

        charge_output(escaped);
    }

    // Charge the shortest round-trip text of a number exactly as
    // JsonValue::serialise renders it.
    template <typename Number> void charge_number(Number number) {
        std::array<char, 32> buf{};
        const auto chars = std::to_chars(buf.data(), buf.data() + buf.size(), number);

        charge_output(static_cast<std::size_t>(chars.ptr - buf.data()));
    }

    [[noreturn]] void throw_output_too_large() const {
        throw RuntimeError{std::format("{}: result exceeds maximum string size", func_name_), loc_,
                           "build a smaller Json.Value"};
    }

    // Bytes one input byte occupies after JSON escaping (see JsonEscapePolicy:
    // short escapes for quote/backslash/\n/\r/\t/\b/\f, \u00XX for the other
    // C0 controls, everything else verbatim).
    [[nodiscard]] static std::size_t escaped_char_size(char c) {
        constexpr unsigned char k_last_control = 0x1F;

        switch (c) {
            case '"':
            case '\\':
            case '\n':
            case '\r':
            case '\t':
            case '\b':
            case '\f':
                return 2;
            default:
                return static_cast<unsigned char>(c) <= k_last_control ? 6 : 1;
        }
    }

    std::string_view func_name_;
    const SourceLocation& loc_;
    std::size_t element_count_{0};
    std::size_t output_bytes_{0};
};

[[nodiscard]] json::JsonValue LumaToJsonConverter::convert(const Value& value, std::size_t depth) {
    const auto& func_name = func_name_;
    const auto& loc = loc_;

    if (!value.is_choice()) {
        throw RuntimeError{std::format("{}: expected a Json.Value", func_name), loc,
                           "pass a Json.Value, e.g. the result of Json.parse"};
    }

    const auto& choice = value.as_choice();
    const std::string& variant = choice->variant;

    const auto payload = [&]() -> const Value& {
        if (choice->fields.empty()) {
            throw RuntimeError{
                std::format("{}: Json.Value.{} is missing its payload", func_name, variant), loc,
                "build a Json.Value with Json.parse rather than a bare variant"};
        }

        return choice->fields.front();
    };

    const auto check_depth = [&]() {
        if (depth >= json_depth_limit()) {
            throw RuntimeError{std::format("{}: Json.Value nesting exceeds the maximum depth of {}",
                                           func_name, json_depth_limit()),
                               loc, "flatten the Json.Value structure"};
        }
    };

    if (variant == "JsonNull") {
        charge_output(std::string_view{"null"}.size());

        return json::JsonValue{};
    }

    if (variant == "JsonBool") {
        const bool flag = expect_boolean(payload(), func_name, loc);
        charge_output(flag ? std::string_view{"true"}.size() : std::string_view{"false"}.size());

        return json::JsonValue{flag};
    }

    if (variant == "JsonNumber") {
        const double number = expect_numeric(payload(), func_name, loc);

        // Emit integral values without a decimal point for clean JSON output
        // (3 rather than 3.0), matching how most JSON producers render whole
        // numbers.  Guard the int64 cast against non-finite and out-of-range
        // values.
        constexpr double k_int64_limit = 9.2e18;

        if (std::isfinite(number) && std::floor(number) == number &&
            std::abs(number) < k_int64_limit) {
            const auto integral = static_cast<std::int64_t>(number);
            charge_number(integral);

            return json::JsonValue{integral};
        }

        // Non-finite numbers are rejected by the serialiser itself; only a
        // finite one produces output to charge.
        if (std::isfinite(number)) {
            charge_number(number);
        }

        return json::JsonValue{number};
    }

    if (variant == "JsonString") {
        const auto& text = expect_string(payload(), func_name, loc);
        charge_string(text);

        return json::JsonValue{text};
    }

    if (variant == "JsonArray") {
        check_depth();

        json::JsonValue::ArrayType array;
        const auto& elements = *expect_array(payload(), func_name, loc)->elements;
        array.reserve(std::min(elements.size(), ResourceLimits::max_json_elements));

        charge_output(2); // '[' and ']'

        for (const auto& element : elements) {
            count_child();

            if (!array.empty()) {
                charge_output(1); // ','
            }

            array.push_back(convert(element, depth + 1));
        }

        return json::JsonValue{std::move(array)};
    }

    if (variant == "JsonObject") {
        check_depth();

        json::JsonValue::ObjectType object;
        bool first_member{true};

        charge_output(2); // '{' and '}'

        for (const auto& [key, member] : expect_dict(payload(), func_name, loc)->entries) {
            count_child();
            charge_output(first_member ? 1 : 2); // ':' and, after the first member, ','
            first_member = false;
            charge_string(key);
            object.emplace(key, convert(member, depth + 1));
        }

        return json::JsonValue{std::move(object)};
    }

    throw RuntimeError{std::format("{}: unknown Json.Value variant '{}'", func_name, variant), loc,
                       "use a Json.Value produced by Json.parse"};
}

// Return the single payload of `value` when it is a Json.Value whose variant is
// `want` and it carries a payload; nullptr otherwise (wrong type, wrong variant,
// or a payload-less bare variant constant).  Never throws — accessors use it to
// decide success vs failure/none.
[[nodiscard]] const Value* json_payload_of(const Value& value, std::string_view want) {
    if (!value.is_choice()) {
        return nullptr;
    }

    const auto& choice = value.as_choice();

    if (choice->variant != want || choice->fields.empty()) {
        return nullptr;
    }

    return &choice->fields.front();
}

// Whether `value` is a Json.Value whose variant is `want`.  Unlike
// json_payload_of this also matches the payload-less JsonNull unit variant, so it
// underpins the is_* type predicates.
[[nodiscard]] bool json_variant_is(const Value& value, std::string_view want) {
    return value.is_choice() && value.as_choice()->variant == want;
}

// Build a Json.ParseError record (type_name "ParseError") carrying the failure
// message and its 1-based line/column.  Matches the "Json.ParseError" record
// registered in stdlib_type_arities.cpp.
[[nodiscard]] Value make_parse_error_record(std::string message, std::int64_t line,
                                            std::int64_t column) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "ParseError";
    rec->fields.emplace_back("message", Value{std::move(message)});
    rec->fields.emplace_back("line", Value{line});
    rec->fields.emplace_back("column", Value{column});

    return Value{std::move(rec)};
}

// Convert a byte offset into the source into a 1-based (line, column) pair.
// The column counts codepoints from the start of the line so it lines up with
// how an editor reports positions.
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

} // namespace

void register_json_value(const EnvPtr& env) {
    ModuleBuilder{"Json", env}
        .func("parse", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Json.parse", loc);

            if (args[0].as_string().size() > ResourceLimits::max_string_size) {
                return make_failure_value(std::string{"Json.parse: input too large"});
            }

            return wrap_result_operation("Json", "parse", [&]() -> Value {
                const json::JsonValue parsed = json::parse(args[0].as_string(), json_depth_limit());

                return make_success_value(jsonvalue_to_luma(parsed));
            });
        })
        .func("parse_detailed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& text = expect_string(args[0], "Json.parse_detailed", loc);

            if (text.size() > ResourceLimits::max_string_size) {
                return Value{ResultValue::failure(
                    make_parse_error_record("Json.parse_detailed: input too large", 1, 1))};
            }

            // Unlike Json.parse (a string-error result), parse_detailed reports a
            // structured Json.ParseError { message, line, column } so a caller can
            // point at the offending byte in a large document.
            try {
                const json::JsonValue parsed = json::parse(text, json_depth_limit());

                return make_success_value(jsonvalue_to_luma(parsed));
            } catch (const luma::JsonParseError& e) {
                const auto pos = offset_to_line_column(text, e.position());

                return Value{
                    ResultValue::failure(make_parse_error_record(e.what(), pos.line, pos.column))};
            } catch (const std::exception& e) {
                // Non-positional failure (e.g. nesting too deep): report line 1,
                // column 1 rather than guessing an offset.
                return Value{ResultValue::failure(make_parse_error_record(e.what(), 1, 1))};
            }
        })
        .func("to_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            LumaToJsonConverter converter{"Json.to_string", loc};
            const json::JsonValue converted = converter.convert(args[0]);
            std::string serialised = converted.to_string();
            converter.check_serialised_size(serialised);

            return Value{std::move(serialised)};
        })
        .func("as_string", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            if (const Value* payload = json_payload_of(args[0], "JsonString")) {
                return make_success_value(*payload);
            }

            return make_failure_value(error_msg("Json", "as_string", "value is not a JSON string"));
        })
        .func("as_number", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            if (const Value* payload = json_payload_of(args[0], "JsonNumber")) {
                return make_success_value(*payload);
            }

            return make_failure_value(error_msg("Json", "as_number", "value is not a JSON number"));
        })
        .func("as_boolean", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            if (const Value* payload = json_payload_of(args[0], "JsonBool")) {
                return make_success_value(*payload);
            }

            return make_failure_value(
                error_msg("Json", "as_boolean", "value is not a JSON boolean"));
        })
        .func("as_array", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            if (const Value* payload = json_payload_of(args[0], "JsonArray")) {
                return make_success_value(*payload);
            }

            return make_failure_value(error_msg("Json", "as_array", "value is not a JSON array"));
        })
        .func("as_object", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            if (const Value* payload = json_payload_of(args[0], "JsonObject")) {
                return make_success_value(*payload);
            }

            return make_failure_value(error_msg("Json", "as_object", "value is not a JSON object"));
        })
        .func("field", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& key = expect_string(args[1], "Json.field", loc);
            const Value* payload = json_payload_of(args[0], "JsonObject");

            if (payload == nullptr) {
                return Value{NullValue{}}; // none — not a JSON object
            }

            const Value* found = payload->as_dictionary()->find(key);

            if (found == nullptr) {
                return Value{NullValue{}}; // none — key absent
            }

            return *found;
        })
        .func("index", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::int64_t index = expect_integer(args[1], "Json.index", loc);
            const Value* payload = json_payload_of(args[0], "JsonArray");

            if (payload == nullptr) {
                return Value{NullValue{}}; // none — not a JSON array
            }

            const auto& elements = *payload->as_array()->elements;

            if (index < 0 || static_cast<std::size_t>(index) >= elements.size()) {
                return Value{NullValue{}}; // none — out of bounds
            }

            return elements[static_cast<std::size_t>(index)];
        })
        .func("as_integer", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            // JSON has one number type; as_integer extracts it as a Luma integer,
            // failing if the value is not a whole number (respecting the
            // integer/number distinction that as_number ignores).
            const Value* payload = json_payload_of(args[0], "JsonNumber");

            if (payload == nullptr) {
                return make_failure_value(
                    error_msg("Json", "as_integer", "value is not a JSON number"));
            }

            const double d = payload->to_numeric();

            if (const auto i = stdlib::safe_to_int64(d); i && static_cast<double>(*i) == d) {
                return make_success_value(Value{*i});
            }

            return make_failure_value(
                error_msg("Json", "as_integer", "value is not a whole number"));
        })
        .func("is_object", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{json_variant_is(args[0], "JsonObject")};
        })
        .func("is_array", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{json_variant_is(args[0], "JsonArray")};
        })
        .func("is_string", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{json_variant_is(args[0], "JsonString")};
        })
        .func("is_number", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{json_variant_is(args[0], "JsonNumber")};
        })
        .func("is_boolean", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{json_variant_is(args[0], "JsonBool")};
        })
        .func("is_null", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{json_variant_is(args[0], "JsonNull")};
        });
}

} // namespace luma
