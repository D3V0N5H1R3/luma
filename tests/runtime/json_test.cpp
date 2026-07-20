// JSON parser and serialiser unit tests.

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "json/json.hpp"
#include "json/json_helpers.hpp"
#include "test_framework.hpp"

using namespace luma::json;

// ═══════════════════════════════════════════════════════════
// Construction and type queries
// ═══════════════════════════════════════════════════════════

static void test_null_value() {
    JsonValue v;
    ASSERT_TRUE(v.is_null());
    ASSERT_FALSE(v.is_bool());
    ASSERT_FALSE(v.is_integer());
    ASSERT_FALSE(v.is_string());
    ASSERT_FALSE(v.is_array());
    ASSERT_FALSE(v.is_object());
}

static void test_bool_value() {
    JsonValue v{true};
    ASSERT_TRUE(v.is_bool());
    ASSERT_EQ(v.as_bool(), true);

    JsonValue f{false};
    ASSERT_EQ(f.as_bool(), false);
}

static void test_integer_value() {
    JsonValue v{static_cast<int64_t>(42)};
    ASSERT_TRUE(v.is_integer());
    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_integer(), 42);
    ASSERT_EQ(v.as_number(), 42.0);
}

static void test_double_value() {
    JsonValue v{3.14};
    ASSERT_TRUE(v.is_number());
    ASSERT_FALSE(v.is_integer());
    ASSERT_NEAR(v.as_number(), 3.14, 0.001);
}

static void test_string_value() {
    JsonValue v{std::string{"hello"}};
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello");
}

static void test_cstring_value() {
    JsonValue v{"world"};
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "world");
}

static void test_array_value() {
    JsonValue::ArrayType arr;
    arr.push_back(JsonValue{static_cast<int64_t>(1)});
    arr.push_back(JsonValue{static_cast<int64_t>(2)});

    JsonValue v{arr};
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array().size(), 2U);
    ASSERT_EQ(v.as_array()[0].as_integer(), 1);
}

static void test_object_value() {
    JsonValue::ObjectType obj;
    obj["key"] = JsonValue{std::string{"value"}};

    JsonValue v{obj};
    ASSERT_TRUE(v.is_object());
    ASSERT_TRUE(v.has("key"));
    ASSERT_EQ(v["key"].as_string(), "value");
}

static void test_serialise_deeply_nested_is_depth_bounded() {
    // Regression: JsonValue::serialise() recursed once per nesting level with no
    // depth guard, so a programmatically-built deep value (the parser bounds
    // parse depth, but values can also be constructed directly) could overflow
    // the native stack.  serialise() now collapses nodes past
    // CompileTimeLimits::max_json_depth (128) to null, mirroring the stdlib
    // serializer.  A value nested well beyond the cap must therefore serialise
    // to a truncated string containing the null marker rather than reproducing
    // every level down to the innermost integer.
    JsonValue v{static_cast<int64_t>(1)};

    for (int i = 0; i < 300; ++i) {
        JsonValue::ArrayType arr;
        arr.push_back(std::move(v));
        v = JsonValue{std::move(arr)};
    }

    const auto s = v.to_string();

    ASSERT_TRUE(s.find("null") != std::string::npos); // truncated at the cap
    ASSERT_TRUE(s.find('1') == std::string::npos);    // innermost never reached
}

// ═══════════════════════════════════════════════════════════
// Accessor errors
// ═══════════════════════════════════════════════════════════

static void test_accessor_type_mismatch() {
    JsonValue v{static_cast<int64_t>(42)};
    ASSERT_THROWS(v.as_string());
    ASSERT_THROWS(v.as_bool());
    ASSERT_THROWS(v.as_array());
    ASSERT_THROWS(v.as_object());
}

static void test_object_missing_key() {
    JsonValue::ObjectType obj;
    obj["a"] = JsonValue{static_cast<int64_t>(1)};
    JsonValue v{obj};

    ASSERT_THROWS(v["missing"]);
}

static void test_has_on_non_object() {
    JsonValue v{static_cast<int64_t>(42)};
    ASSERT_FALSE(v.has("x"));
}

// A JSON integer that does not fit in a C++ int must yield nullopt / the
// supplied default from the safe accessors, never a throw. The DAP/LSP read
// loops rely on this when extracting client-supplied fields (e.g. a request
// "seq"); a throw there would crash the server on malformed input.
static void test_try_as_int_out_of_range_returns_nullopt() {
    const auto too_big = static_cast<int64_t>(std::numeric_limits<int>::max()) + 1;

    JsonValue big{too_big};
    ASSERT_TRUE(big.is_integer());
    ASSERT_FALSE(big.try_as<int>().has_value());
    // int64_t extraction is unaffected — the value fits.
    ASSERT_TRUE(big.try_as<int64_t>().has_value());

    JsonValue::ObjectType obj;
    obj["seq"] = big;
    JsonValue v{obj};
    ASSERT_FALSE(v.try_get<int>("seq").has_value());
    ASSERT_EQ(v.get_or<int>("seq", 7), 7);

    // An in-range integer is still extracted normally.
    JsonValue ok{static_cast<int64_t>(42)};
    const auto extracted = ok.try_as<int>();
    ASSERT_TRUE(extracted.has_value());
    ASSERT_EQ(extracted.value(), 42);
}

// ═══════════════════════════════════════════════════════════
// json_helpers.hpp — try_extract_field free-function facade
// ═══════════════════════════════════════════════════════════

// try_extract_field is the spelling the LSP and DAP servers use to read typed
// fields off request objects; it delegates to JsonValue::try_get<T>().  These
// tests are its caller of record: a present field of the right type yields the
// value, and every "not extractable" case yields nullopt rather than throwing.

static void test_try_extract_field_present() {
    JsonValue::ObjectType obj;
    obj["name"] = JsonValue{std::string{"luma"}};
    obj["count"] = JsonValue{static_cast<int64_t>(5)};
    obj["ratio"] = JsonValue{2.5};
    obj["flag"] = JsonValue{true};
    const JsonValue v{obj};

    const auto name = try_extract_field<std::string>(v, "name");
    ASSERT_TRUE(name.has_value());
    ASSERT_EQ(*name, std::string{"luma"});

    const auto count = try_extract_field<int>(v, "count");
    ASSERT_TRUE(count.has_value());
    ASSERT_EQ(*count, 5);

    const auto ratio = try_extract_field<double>(v, "ratio");
    ASSERT_TRUE(ratio.has_value());
    ASSERT_NEAR(*ratio, 2.5, 0.001);

    const auto flag = try_extract_field<bool>(v, "flag");
    ASSERT_TRUE(flag.has_value());
    ASSERT_EQ(*flag, true);
}

static void test_try_extract_field_absent_returns_nullopt() {
    JsonValue::ObjectType obj;
    obj["present"] = JsonValue{static_cast<int64_t>(1)};
    const JsonValue v{obj};

    ASSERT_FALSE(try_extract_field<int>(v, "missing").has_value());
    ASSERT_FALSE(try_extract_field<std::string>(v, "missing").has_value());
}

static void test_try_extract_field_wrong_type_returns_nullopt() {
    JsonValue::ObjectType obj;
    obj["n"] = JsonValue{static_cast<int64_t>(7)};
    const JsonValue v{obj};

    // Field exists but holds an integer — a string extraction must decline.
    ASSERT_FALSE(try_extract_field<std::string>(v, "n").has_value());
    // The matching type still extracts.
    ASSERT_EQ(try_extract_field<int>(v, "n").value_or(-1), 7);
}

static void test_try_extract_field_on_non_object_returns_nullopt() {
    const JsonValue not_an_object{static_cast<int64_t>(42)};
    ASSERT_FALSE(try_extract_field<int>(not_an_object, "anything").has_value());
}

static void test_try_extract_field_int_out_of_range_returns_nullopt() {
    const auto too_big = static_cast<int64_t>(std::numeric_limits<int>::max()) + 1;

    JsonValue::ObjectType obj;
    obj["big"] = JsonValue{too_big};
    const JsonValue v{obj};

    // Out of int range → nullopt for int, but the int64_t view still succeeds.
    ASSERT_FALSE(try_extract_field<int>(v, "big").has_value());
    ASSERT_EQ(try_extract_field<int64_t>(v, "big").value_or(0), too_big);
}

// ═══════════════════════════════════════════════════════════
// Parsing
// ═══════════════════════════════════════════════════════════

static void test_parse_null() {
    auto v = JsonValue::parse("null");
    ASSERT_TRUE(v.is_null());
}

static void test_parse_true() {
    auto v = JsonValue::parse("true");
    ASSERT_TRUE(v.is_bool());
    ASSERT_EQ(v.as_bool(), true);
}

static void test_parse_false() {
    auto v = JsonValue::parse("false");
    ASSERT_TRUE(v.is_bool());
    ASSERT_EQ(v.as_bool(), false);
}

static void test_parse_integer() {
    auto v = JsonValue::parse("42");
    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 42);
}

static void test_parse_negative_integer() {
    auto v = JsonValue::parse("-7");
    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), -7);
}

static void test_parse_double() {
    auto v = JsonValue::parse("3.14");
    ASSERT_TRUE(v.is_number());
    ASSERT_NEAR(v.as_number(), 3.14, 0.001);
}

static void test_parse_string() {
    auto v = JsonValue::parse("\"hello world\"");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello world");
}

static void test_parse_string_escapes() {
    auto v = JsonValue::parse("\"a\\nb\\tc\"");
    ASSERT_EQ(v.as_string(), "a\nb\tc");
}

static void test_parse_string_unicode_escape() {
    auto v = JsonValue::parse("\"\\u0041\""); // 'A'
    ASSERT_EQ(v.as_string(), "A");
}

static void test_parse_empty_array() {
    auto v = JsonValue::parse("[]");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array().size(), 0U);
}

static void test_parse_array() {
    auto v = JsonValue::parse("[1, 2, 3]");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array().size(), 3U);
    ASSERT_EQ(v.as_array()[0].as_integer(), 1);
    ASSERT_EQ(v.as_array()[2].as_integer(), 3);
}

static void test_parse_nested_array() {
    auto v = JsonValue::parse("[[1, 2], [3]]");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array().size(), 2U);
    ASSERT_TRUE(v.as_array()[0].is_array());
    ASSERT_EQ(v.as_array()[0].as_array().size(), 2U);
}

static void test_parse_empty_object() {
    auto v = JsonValue::parse("{}");
    ASSERT_TRUE(v.is_object());
    ASSERT_EQ(v.as_object().size(), 0U);
}

static void test_parse_object() {
    auto v = JsonValue::parse("{\"name\": \"luma\", \"version\": 1}");
    ASSERT_TRUE(v.is_object());
    ASSERT_EQ(v["name"].as_string(), "luma");
    ASSERT_EQ(v["version"].as_integer(), 1);
}

static void test_parse_nested_object() {
    auto v = JsonValue::parse("{\"a\": {\"b\": true}}");
    ASSERT_TRUE(v["a"].is_object());
    ASSERT_EQ(v["a"]["b"].as_bool(), true);
}

static void test_parse_whitespace() {
    auto v = JsonValue::parse("  \n\t  42  \n  ");
    ASSERT_EQ(v.as_integer(), 42);
}

// ─── Parse errors ───

static void test_parse_empty_input() {
    ASSERT_THROWS(JsonValue::parse(""));
}

static void test_parse_trailing_content() {
    ASSERT_THROWS(JsonValue::parse("42 abc"));
}

static void test_parse_unterminated_string() {
    ASSERT_THROWS(JsonValue::parse("\"hello"));
}

static void test_parse_invalid_token() {
    ASSERT_THROWS(JsonValue::parse("undefined"));
}

// ═══════════════════════════════════════════════════════════
// Serialisation
// ═══════════════════════════════════════════════════════════

static void test_serialise_null() {
    JsonValue v;
    ASSERT_EQ(v.to_string(), "null");
}

static void test_serialise_bool() {
    JsonValue t{true};
    ASSERT_EQ(t.to_string(), "true");

    JsonValue f{false};
    ASSERT_EQ(f.to_string(), "false");
}

static void test_serialise_integer() {
    JsonValue v{static_cast<int64_t>(42)};
    ASSERT_EQ(v.to_string(), "42");
}

static void test_serialise_string_escape() {
    JsonValue v{std::string{"hello\nworld"}};
    ASSERT_EQ(v.to_string(), "\"hello\\nworld\"");
}

static void test_serialise_array() {
    JsonValue::ArrayType arr;
    arr.push_back(JsonValue{static_cast<int64_t>(1)});
    arr.push_back(JsonValue{static_cast<int64_t>(2)});

    JsonValue v{arr};
    ASSERT_EQ(v.to_string(), "[1,2]");
}

static void test_serialise_empty_object() {
    JsonValue::ObjectType obj;
    JsonValue v{obj};
    ASSERT_EQ(v.to_string(), "{}");
}

// ─── Round-trip ───

static void test_round_trip_complex() {
    const auto input = "{\"arr\":[1,2,3],\"flag\":true,\"name\":\"test\",\"nested\":{\"x\":null}}";
    auto parsed = JsonValue::parse(input);
    auto output = parsed.to_string();
    auto reparsed = JsonValue::parse(output);

    ASSERT_EQ(reparsed["name"].as_string(), "test");
    ASSERT_EQ(reparsed["flag"].as_bool(), true);
    ASSERT_TRUE(reparsed["nested"]["x"].is_null());
    ASSERT_EQ(reparsed["arr"].as_array().size(), 3U);
}

// ─── main ───

int main() {
    // Construction.
    RUN(test_null_value);
    RUN(test_bool_value);
    RUN(test_integer_value);
    RUN(test_double_value);
    RUN(test_string_value);
    RUN(test_cstring_value);
    RUN(test_array_value);
    RUN(test_object_value);
    RUN(test_serialise_deeply_nested_is_depth_bounded);

    // Accessor errors.
    RUN(test_accessor_type_mismatch);
    RUN(test_object_missing_key);
    RUN(test_has_on_non_object);
    RUN(test_try_as_int_out_of_range_returns_nullopt);

    // json_helpers — try_extract_field.
    RUN(test_try_extract_field_present);
    RUN(test_try_extract_field_absent_returns_nullopt);
    RUN(test_try_extract_field_wrong_type_returns_nullopt);
    RUN(test_try_extract_field_on_non_object_returns_nullopt);
    RUN(test_try_extract_field_int_out_of_range_returns_nullopt);

    // Parsing.
    RUN(test_parse_null);
    RUN(test_parse_true);
    RUN(test_parse_false);
    RUN(test_parse_integer);
    RUN(test_parse_negative_integer);
    RUN(test_parse_double);
    RUN(test_parse_string);
    RUN(test_parse_string_escapes);
    RUN(test_parse_string_unicode_escape);
    RUN(test_parse_empty_array);
    RUN(test_parse_array);
    RUN(test_parse_nested_array);
    RUN(test_parse_empty_object);
    RUN(test_parse_object);
    RUN(test_parse_nested_object);
    RUN(test_parse_whitespace);

    // Parse errors.
    RUN(test_parse_empty_input);
    RUN(test_parse_trailing_content);
    RUN(test_parse_unterminated_string);
    RUN(test_parse_invalid_token);

    // Serialisation.
    RUN(test_serialise_null);
    RUN(test_serialise_bool);
    RUN(test_serialise_integer);
    RUN(test_serialise_string_escape);
    RUN(test_serialise_array);
    RUN(test_serialise_empty_object);

    // Round-trip.
    RUN(test_round_trip_complex);
    return SUMMARY();
}
