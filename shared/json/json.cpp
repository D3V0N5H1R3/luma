#include "json/json.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <stdexcept>

#include "common/escape.hpp"
#include "common/overloaded.hpp"
#include "common/resource_limits.hpp"
#include "json/json_parser.hpp"

namespace luma::json {

namespace {

// Append a JSON-escaped string (with surrounding quotes) to the output buffer.
//
// This serializer targets the LSP/DAP wire protocol (Content-Length framed JSON
// over stdio), so it emits only the escapes RFC 8259 requires: quote, backslash,
// and the C0 control range.  It deliberately does NOT escape '/', U+2028, or
// U+2029 — those matter only when JSON is embedded in an HTML <script> element
// or evaluated as a JavaScript literal, which never happens on the wire.  A
// caller embedding output in HTML/JS must use the slash-escaping variant
// (luma::json_escape) instead.
void escape_json_string(std::string_view s, std::string& out) {
    out.reserve(out.size() + s.size() + 2);
    out += '"';
    luma::json_escape_string(s, out);
    out += '"';
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// get_as helpers
// ═══════════════════════════════════════════════════════════

template <typename T> const T& JsonValue::get_as(const char* type_name) const {
    if (const auto* v = std::get_if<T>(&data_)) {
        return *v;
    }

    throw std::runtime_error(std::format("JSON value is not {}", type_name));
}

template <typename T> T& JsonValue::get_as_mut(const char* type_name) {
    if (auto* v = std::get_if<T>(&data_)) {
        return *v;
    }

    throw std::runtime_error(std::format("JSON value is not {}", type_name));
}

// ═══════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════

JsonValue::JsonValue() = default;

JsonValue::JsonValue(bool value) : data_{value} {}

JsonValue::JsonValue(int value) : data_{static_cast<int64_t>(value)} {}

JsonValue::JsonValue(int64_t value) : data_{value} {}

JsonValue::JsonValue(double value) : data_{value} {}

JsonValue::JsonValue(std::string value) noexcept : data_{std::move(value)} {}

JsonValue::JsonValue(const char* value) : data_{std::string{value}} {}

JsonValue::JsonValue(ArrayType value) noexcept : data_{std::move(value)} {}

JsonValue::JsonValue(ObjectType value) noexcept : data_{std::move(value)} {}

// ═══════════════════════════════════════════════════════════
// Type queries
// ═══════════════════════════════════════════════════════════

bool JsonValue::is_null() const noexcept {
    return std::holds_alternative<std::monostate>(data_);
}

bool JsonValue::is_bool() const noexcept {
    return std::holds_alternative<bool>(data_);
}

bool JsonValue::is_integer() const noexcept {
    return std::holds_alternative<int64_t>(data_);
}

bool JsonValue::is_number() const noexcept {
    return std::holds_alternative<double>(data_) || std::holds_alternative<int64_t>(data_);
}

bool JsonValue::is_string() const noexcept {
    return std::holds_alternative<std::string>(data_);
}

bool JsonValue::is_array() const noexcept {
    return std::holds_alternative<ArrayType>(data_);
}

bool JsonValue::is_object() const noexcept {
    return std::holds_alternative<ObjectType>(data_);
}

// ═══════════════════════════════════════════════════════════
// Value accessors
// ═══════════════════════════════════════════════════════════

bool JsonValue::as_bool() const {
    return get_as<bool>("a boolean");
}

int64_t JsonValue::as_integer() const {
    return get_as<int64_t>("an integer");
}

// NOTE: as_number() converts int64_t → double, which may lose precision
// for integer values larger than 2^53.  Use as_integer() or try_as<int64_t>()
// when exact integer precision is required.
double JsonValue::as_number() const {
    if (const auto* v = std::get_if<double>(&data_)) {
        return *v;
    }

    if (const auto* v = std::get_if<int64_t>(&data_)) {
        return static_cast<double>(*v);
    }

    throw std::runtime_error("JSON value is not a number");
}

const std::string& JsonValue::as_string() const {
    return get_as<std::string>("a string");
}

const JsonValue::ArrayType& JsonValue::as_array() const {
    return get_as<ArrayType>("an array");
}

const JsonValue::ObjectType& JsonValue::as_object() const {
    return get_as<ObjectType>("an object");
}

JsonValue::ArrayType& JsonValue::as_array() {
    return get_as_mut<ArrayType>("an array");
}

JsonValue::ObjectType& JsonValue::as_object() {
    return get_as_mut<ObjectType>("an object");
}

const JsonValue& JsonValue::operator[](std::string_view key) const {
    if (const auto* obj = std::get_if<ObjectType>(&data_)) {
        auto it = obj->find(key);

        if (it != obj->end()) {
            return it->second;
        }

        throw std::runtime_error(std::format("JSON object has no key '{}'", key));
    }

    throw std::runtime_error("JSON value is not an object");
}

bool JsonValue::has(std::string_view key) const noexcept {
    if (const auto* obj = std::get_if<ObjectType>(&data_)) {
        return obj->contains(key);
    }

    return false;
}

const JsonValue& JsonValue::get(std::string_view key, const JsonValue& default_val) const {
    if (const auto* obj = std::get_if<ObjectType>(&data_)) {
        auto it = obj->find(key);
        if (it != obj->end()) {
            return it->second;
        }
    }
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter): the rvalue overload
    // is deleted, so default_val can never bind to a temporary and cannot dangle.
    return default_val;
}

const JsonValue& JsonValue::get(std::string_view key) const {
    return get(key, null_sentinel());
}

const JsonValue& JsonValue::null_sentinel() {
    static const JsonValue instance;
    return instance;
}

// ═══════════════════════════════════════════════════════════
// Serialisation
// ═══════════════════════════════════════════════════════════

std::string JsonValue::to_string() const {
    std::string out;
    out.reserve(256);

    serialise(out);

    return out;
}

void JsonValue::serialise(std::string& out, int depth) const {
    // Bound recursion so a pathologically deep value cannot overflow the native
    // stack.  Mirrors the parser's depth guard and the stdlib serializer
    // (json_module_serializer.cpp); over-deep nodes collapse to null, matching
    // that serializer's behaviour.
    if (depth > CompileTimeLimits::max_json_depth) {
        out += "null";
        return;
    }

    std::visit(luma::overloaded{[&out](std::monostate) { out += "null"; },
                                [&out](bool v) { out += v ? "true" : "false"; },
                                [&out](int64_t v) {
                                    // std::to_chars writes directly into a stack
                                    // buffer — no throwaway std::string per number.
                                    // 20 digits + sign fits any int64_t.
                                    char buf[24];
                                    const auto result = std::to_chars(buf, buf + sizeof(buf), v);
                                    out.append(buf, static_cast<std::size_t>(result.ptr - buf));
                                },
                                [&out](double v) {
                                    if (std::isnan(v) || std::isinf(v)) {
                                        throw std::runtime_error(
                                            "Cannot serialise NaN or Infinity as JSON");
                                    }

                                    // std::to_chars gives the shortest round-trip
                                    // representation (matching std::format's default)
                                    // with no heap allocation.
                                    char buf[32];
                                    const auto result = std::to_chars(buf, buf + sizeof(buf), v);
                                    out.append(buf, static_cast<std::size_t>(result.ptr - buf));
                                },
                                [&out](const std::string& v) { escape_json_string(v, out); },
                                [&out, depth](const ArrayType& v) {
                                    out += '[';

                                    bool first{true};

                                    for (const auto& elem : v) {
                                        if (!first) {
                                            out += ',';
                                        }

                                        first = false;

                                        elem.serialise(out, depth + 1);
                                    }

                                    out += ']';
                                },
                                [&out, depth](const ObjectType& v) {
                                    out += '{';

                                    bool first{true};

                                    for (const auto& [key, val] : v) {
                                        if (!first) {
                                            out += ',';
                                        }

                                        first = false;

                                        escape_json_string(key, out);

                                        out += ':';

                                        val.serialise(out, depth + 1);
                                    }

                                    out += '}';
                                }},
               data_);
}

JsonValue JsonValue::parse(std::string_view input, std::size_t max_depth) {
    JsonParser parser(input, max_depth);

    return parser.parse();
}

} // namespace luma::json
