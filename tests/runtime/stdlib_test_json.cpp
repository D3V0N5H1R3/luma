// Standard library tests: Json (Luma stdlib module).

#include <cstddef>

#include "runtime/stdlib/text/json_module.hpp"
#include "stdlib_test_helpers.hpp"

static void test_json_parses_out_of_double_range_numbers() {
    // Regression: a finite JSON document must still parse when a numeric literal
    // lies outside double's range.  1e-400 underflows to 0.0 and 1e400 overflows
    // to +inf via a saturating conversion, instead of std::stod throwing and the
    // whole document being rejected.
    ASSERT_TRUE(eval(R"(Json.is_valid("{\"v\": 1e-400}"))").is_truthy());
    ASSERT_TRUE(eval(R"(Json.is_valid("{\"v\": 1e400}"))").is_truthy());
}

static void test_json_is_valid() {
    const auto result = eval(R"(
        Json.is_valid("{\"a\": 1}")
    )");

    ASSERT_TRUE(result.is_truthy());
}

static void test_json_is_valid_false() {
    const auto result = eval(R"(
        Json.is_valid("{bad json}")
    )");

    ASSERT_FALSE(result.is_truthy());
}

static void test_json_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Json.serialize"));
    ASSERT_TRUE(env->has("Json.deserialize"));
    ASSERT_TRUE(env->has("Json.is_valid"));
    ASSERT_TRUE(env->has("Json.get"));
    ASSERT_TRUE(env->has("Json.set"));
    ASSERT_TRUE(env->has("Json.merge"));
}

static void test_json_serialize_array() {
    const auto result = eval(R"(
        Json.serialize([1, 2, 3])
    )");

    ASSERT_EQ(result.as_string(), "[1,2,3]");
}

static void test_json_serialize_deserialize() {
    const auto result = eval("Json.serialize(42) |> Json.deserialize() |> Result.unwrap()");

    ASSERT_EQ(result.as_integer(), 42);
}

static void test_json_serialize_string() {
    const auto result = eval(R"(
        Json.serialize("hello")
    )");

    ASSERT_EQ(result.as_string(), "\"hello\"");
}

static void test_json_get_path_simple() {
    const auto v = eval(R"(Json.get_path("{\"a\": 1}", "a") |> Result.unwrap())");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 1);
}

static void test_json_get_path_nested() {
    const auto v = eval(R"(Json.get_path("{\"a\": {\"b\": 42}}", "a.b") |> Result.unwrap())");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 42);
}

static void test_json_get_path_array_index() {
    const auto v =
        eval(R"(Json.get_path("{\"items\": [10, 20, 30]}", "items[1]") |> Result.unwrap())");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 20);
}

static void test_json_get_path_bracket_index_lenient() {
    // Characterization: bracket indices tolerate surrounding whitespace and a
    // leading '+', matching the std::stoll prefix parsing this path historically
    // used (now reproduced by try_parse_index over std::from_chars).
    const auto ws = eval(R"(Json.get_path("[10, 20, 30]", "[ 1 ]") |> Result.unwrap())");

    ASSERT_TRUE(ws.is_integer());
    ASSERT_EQ(ws.as_integer(), 20);

    const auto plus = eval(R"(Json.get_path("[10, 20, 30]", "[+2]") |> Result.unwrap())");

    ASSERT_TRUE(plus.is_integer());
    ASSERT_EQ(plus.as_integer(), 30);
}

static void test_json_get_path_missing() {
    ASSERT_EVAL_FAILURE(R"(Json.get_path("{\"a\": 1}", "b"))");
}

static void test_json_set_path_simple() {
    const auto v = eval(
        R"(Json.set_path("{\"a\": 1}", "a", 99) |> Result.unwrap() |> Json.deserialize() |> Result.unwrap())");

    ASSERT_TRUE(v.is_dictionary());
}

static void test_json_set_path_nested() {
    const auto v = eval(R"(
        Json.set_path("{\"a\": {\"b\": 1}}", "a.b", 42)
        |> Result.unwrap()
        |> Json.get_path("a.b")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 42);
}

// ─── Serialisation (additional value types) ───

static void test_json_serialize_boolean() {
    ASSERT_EQ(eval("Json.serialize(true)").as_string(), "true");
    ASSERT_EQ(eval("Json.serialize(false)").as_string(), "false");
}

static void test_json_serialize_none() {
    ASSERT_EQ(eval("Json.serialize(none)").as_string(), "null");
}

static void test_json_serialize_number() {
    ASSERT_EQ(eval("Json.serialize(3.5)").as_string(), "3.5");
}

static void test_json_serialize_dictionary() {
    // Insertion order is preserved in the serialised output.
    ASSERT_EQ(eval(R"(Json.serialize({"b": 2, "a": 1}))").as_string(), R"({"b":2,"a":1})");
}

static void test_json_serialize_nested() {
    ASSERT_EQ(eval(R"(Json.serialize({"items": [1, 2, 3]}))").as_string(), R"({"items":[1,2,3]})");
}

static void test_json_serialize_string_escapes() {
    // Control characters must be escaped back into their JSON form.
    ASSERT_EQ(eval(R"(Json.serialize("a\tb\nc"))").as_string(), R"("a\tb\nc")");
}

static void test_json_serialize_pretty_layout() {
    ASSERT_EQ(eval("Json.serialize_pretty([1, 2])").as_string(), "[\n  1,\n  2\n]");
}

// ─── Divergent-branch characterization (choice / record / tuple / depth) ───
// These pin the Json module serialiser where it deliberately differs from the
// GraphicalUi serialiser: only nullary choices serialise (to a bare string,
// non-nullary → null), strings do NOT slash-escape, and exceeding the
// nesting-depth limit yields "null" rather than throwing.

static void test_json_serialize_nullary_choice_string() {
    const auto v = eval(R"(choice Color { Red, Green, Blue } Json.serialize(Color.Green))");
    ASSERT_EQ(v.as_string(), R"("Green")");
}

static void test_json_serialize_choice_with_fields_null() {
    const auto v =
        eval(R"(choice Shape { Circle(number radius) } Json.serialize(Shape.Circle(3.5)))");
    ASSERT_EQ(v.as_string(), "null");
}

static void test_json_serialize_record_object() {
    const auto v =
        eval(R"(record Point { integer x, integer y } Json.serialize(Point { x = 1, y = 2 }))");
    ASSERT_EQ(v.as_string(), R"({"x":1,"y":2})");
}

static void test_json_serialize_tuple_array() {
    ASSERT_EQ(eval(R"(Json.serialize((1, 2, 3)))").as_string(), "[1,2,3]");
}

static void test_json_serialize_string_no_slash_escape() {
    // Unlike the GUI serialiser, the Json module leaves forward slashes bare.
    ASSERT_EQ(eval(R"(Json.serialize("</script>"))").as_string(), R"("</script>")");
}

static void test_json_serialize_depth_exceeded_null() {
    // Past the nesting-depth limit (128) the Json serialiser appends "null"
    // rather than throwing.  200 is safely above the limit.
    std::string out;
    luma::json_serialize_value(luma::Value{1.5}, out, 0, 200, false);
    ASSERT_EQ(out, "null");
}

// ─── Deserialisation (additional value types) ───

static void test_json_deserialize_string() {
    const auto v = eval(R"(Json.deserialize("\"hi\"") |> Result.unwrap())");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hi");
}

static void test_json_deserialize_number() {
    const auto v = eval(R"(Json.deserialize("3.14") |> Result.unwrap())");

    ASSERT_TRUE(v.is_number());
    ASSERT_NEAR(v.as_number(), 3.14, 0.001);
}

static void test_json_deserialize_boolean() {
    const auto v = eval(R"(Json.deserialize("true") |> Result.unwrap())");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_json_deserialize_null() {
    const auto v = eval(R"(Json.deserialize("null") |> Result.unwrap())");

    ASSERT_TRUE(v.is_null());
}

static void test_json_deserialize_object() {
    const auto v = eval(R"(Json.deserialize("{\"a\": 1, \"b\": 2}") |> Result.unwrap())");

    ASSERT_TRUE(v.is_dictionary());
}

static void test_json_deserialize_unicode_escape() {
    const auto v = eval(R"(Json.deserialize("\"\\u0041\"") |> Result.unwrap())");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "A");
}

static void test_json_roundtrip_dictionary() {
    const auto v = eval(R"(
        Json.serialize({"x": 1, "y": 2})
        |> Json.deserialize()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_dictionary());
}

// ─── get / set / merge (dot-path API) ───

static void test_json_get_dot_array_index() {
    const auto v = eval(R"(Json.get("{\"a\": {\"b\": [7, 8, 9]}}", "a.b.2") |> Result.unwrap())");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 9);
}

static void test_json_set_replaces_value() {
    const auto v = eval(R"(Json.set("{\"a\": 1}", "a", 5) |> Result.unwrap())");

    ASSERT_EQ(v.as_string(), R"({"a":5})");
}

static void test_json_set_adds_key() {
    const auto v = eval(R"(Json.set("{}", "k", "v") |> Result.unwrap())");

    ASSERT_EQ(v.as_string(), R"({"k":"v"})");
}

static void test_json_merge_combines() {
    const auto v = eval(R"(Json.merge("{\"a\": 1}", "{\"b\": 2}") |> Result.unwrap())");

    ASSERT_EQ(v.as_string(), R"({"a":1,"b":2})");
}

static void test_json_merge_right_wins() {
    const auto v = eval(R"(Json.merge("{\"x\": 1}", "{\"x\": 9}") |> Result.unwrap())");

    ASSERT_EQ(v.as_string(), R"({"x":9})");
}

// ─── get_path / set_path (bracket-index API) ───

static void test_json_get_path_bracket_nested() {
    const auto v = eval(
        R"(Json.get_path("{\"items\": [{\"id\": 10}, {\"id\": 20}]}", "items[1].id") |> Result.unwrap())");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 20);
}

static void test_json_set_path_array_index() {
    const auto v = eval(R"(Json.set_path("[1, 2, 3]", "[0]", 99) |> Result.unwrap())");

    ASSERT_EQ(v.as_string(), "[99,2,3]");
}

static void test_json_is_valid_various() {
    ASSERT_TRUE(eval(R"(Json.is_valid("[1, 2, 3]"))").is_truthy());
    ASSERT_TRUE(eval(R"(Json.is_valid("3.14"))").is_truthy());
    ASSERT_TRUE(eval(R"(Json.is_valid("\"text\""))").is_truthy());
    ASSERT_TRUE(eval(R"(Json.is_valid("null"))").is_truthy());
}

// ═══════════════════════════════════════════════════════════
// Negative tests — malformed input and invalid operations
// ═══════════════════════════════════════════════════════════

static void test_json_deserialize_malformed() {
    ASSERT_EVAL_FAILURE(R"(Json.deserialize("{invalid}"))");
}

static void test_json_deserialize_trailing_content() {
    ASSERT_EVAL_FAILURE(R"(Json.deserialize("1 2"))");
}

static void test_json_deserialize_unterminated_string() {
    ASSERT_EVAL_FAILURE(R"(Json.deserialize("\"abc"))");
}

static void test_json_deserialize_leading_zero() {
    ASSERT_EVAL_FAILURE(R"(Json.deserialize("01"))");
}

static void test_json_deserialize_empty() {
    ASSERT_EVAL_FAILURE(R"(Json.deserialize(""))");
}

static void test_json_deserialize_bad_escape() {
    ASSERT_EVAL_FAILURE(R"(Json.deserialize("\"\\x\""))");
}

static void test_json_merge_non_objects() {
    ASSERT_EVAL_FAILURE(R"(Json.merge("[1]", "[2]"))");
}

static void test_json_merge_invalid_input() {
    ASSERT_EVAL_FAILURE(R"(Json.merge("{invalid}", "{}"))");
}

static void test_json_set_missing_parent() {
    ASSERT_EVAL_FAILURE(R"(Json.set("{\"a\": 1}", "x.y", 1))");
}

static void test_json_set_non_object() {
    ASSERT_EVAL_FAILURE(R"(Json.set("[1, 2]", "a", 1))");
}

static void test_json_set_array_mid_path() {
    // Characterization: Json.set traverses dictionaries only.  An array in the
    // middle of the dot-path is not navigable, so the operation fails rather
    // than indexing into the array (indexing mid-path is Json.set_path's job).
    ASSERT_EVAL_FAILURE(R"(Json.set("{\"a\": [1, 2]}", "a.b.c", 9))");
}

static void test_json_get_path_out_of_bounds() {
    ASSERT_EVAL_FAILURE(R"(Json.get_path("[1, 2, 3]", "[9]"))");
}

static void test_json_get_path_into_scalar() {
    ASSERT_EVAL_FAILURE(R"(Json.get_path("5", "a"))");
}

static void test_json_set_path_out_of_bounds() {
    ASSERT_EVAL_FAILURE(R"(Json.set_path("[1, 2]", "[5]", 9))");
}

static void test_json_deserialize_non_string_throws() {
    ASSERT_TRUE(luma::test::eval_throws("Json.deserialize(42)"));
}

static void test_json_is_valid_non_string_throws() {
    ASSERT_TRUE(luma::test::eval_throws("Json.is_valid(42)"));
}

// ── Json.Value typed ADT (parse / to_string / accessors) ────────────────────

static void test_json_value_parse_object() {
    const auto v = eval(R"(Json.parse("{\"a\": 1}"))");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "Value");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "JsonObject");
}

static void test_json_value_parse_array() {
    const auto v = eval(R"(Json.parse("[1, 2, 3]"))");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "JsonArray");
}

static void test_json_value_parse_string() {
    const auto v = eval(R"(Json.parse("\"hello\""))");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "JsonString");
}

static void test_json_value_parse_number() {
    const auto v = eval(R"(Json.parse("42"))");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "JsonNumber");
}

static void test_json_value_parse_boolean() {
    const auto v = eval(R"(Json.parse("true"))");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "JsonBool");
}

static void test_json_value_parse_null() {
    const auto v = eval(R"(Json.parse("null"))");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "JsonNull");
}

static void test_json_value_parse_invalid_fails() {
    ASSERT_EVAL_FAILURE(R"(Json.parse("{bad}"))");
}

static void test_json_parse_detailed_success() {
    const auto v = eval(R"(Json.parse_detailed("{\"a\": 1}"))");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "Value");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "JsonObject");
}

static void test_json_parse_detailed_failure_has_location() {
    // A malformed object: the parser reports a structured ParseError record with
    // the failing line and column, not a bare string.
    const auto v = eval(R"(Json.parse_detailed("{ bad"))");
    ASSERT_TRUE(v.is_result());
    ASSERT_FALSE(v.as_result()->is_success);

    const auto& err = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(err->type_name, std::string{"ParseError"});
    ASSERT_EQ(err->find_field("line")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_TRUE(err->find_field("column")->as_integer() >= 1);
    ASSERT_FALSE(err->find_field("message")->as_string().empty());
}

static void test_json_parse_detailed_reports_line_on_multiline() {
    // The error is on the third line; the record must report line 3.
    const auto v = eval(R"(Json.parse_detailed("[\n  1,\n  bad\n]"))");
    ASSERT_FALSE(v.as_result()->is_success);

    const auto& err = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(err->find_field("line")->as_integer(), static_cast<std::int64_t>(3));
}

static void test_json_value_as_string_success() {
    const auto v =
        eval(R"(Json.parse("\"hi\"") |> Result.unwrap() |> Json.as_string() |> Result.unwrap())");
    ASSERT_EQ(v.as_string(), "hi");
}

static void test_json_value_as_string_wrong_type_fails() {
    ASSERT_EVAL_FAILURE(R"(Json.parse("42") |> Result.unwrap() |> Json.as_string())");
}

static void test_json_value_as_number_success() {
    const auto v =
        eval(R"(Json.parse("42") |> Result.unwrap() |> Json.as_number() |> Result.unwrap())");
    ASSERT_NEAR(v.to_numeric(), 42.0, 1e-9);
}

static void test_json_value_as_boolean_success() {
    const auto v =
        eval(R"(Json.parse("true") |> Result.unwrap() |> Json.as_boolean() |> Result.unwrap())");
    ASSERT_TRUE(v.as_bool());
}

static void test_json_value_as_array_success() {
    const auto v =
        eval(R"(Json.parse("[1, 2, 3]") |> Result.unwrap() |> Json.as_array() |> Result.unwrap())");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), static_cast<std::size_t>(3));
}

static void test_json_value_as_object_success() {
    const auto v = eval(
        R"(Json.parse("{\"a\": 1}") |> Result.unwrap() |> Json.as_object() |> Result.unwrap())");
    ASSERT_TRUE(v.is_dictionary());
}

static void test_json_value_field_some() {
    // field returns optional<Json.Value>; the "some" runtime shape is the bare
    // Json.Value choice value.
    const auto v = eval(R"(Json.parse("{\"a\": \"x\"}") |> Result.unwrap() |> Json.field("a"))");
    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->variant, "JsonString");
}

static void test_json_value_field_missing_key_none() {
    // A missing key yields none, whose runtime shape is null.
    const auto v = eval(R"(Json.parse("{\"a\": 1}") |> Result.unwrap() |> Json.field("missing"))");
    ASSERT_TRUE(v.is_null());
}

static void test_json_value_field_on_non_object_none() {
    const auto v = eval(R"(Json.parse("[1, 2]") |> Result.unwrap() |> Json.field("a"))");
    ASSERT_TRUE(v.is_null());
}

static void test_json_value_index_some() {
    const auto v = eval(R"(Json.parse("[10, 20, 30]") |> Result.unwrap() |> Json.index(1))");
    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->variant, "JsonNumber");
}

static void test_json_value_index_out_of_bounds_none() {
    const auto v = eval(R"(Json.parse("[10, 20]") |> Result.unwrap() |> Json.index(5))");
    ASSERT_TRUE(v.is_null());
}

static void test_json_value_index_negative_none() {
    const auto v = eval(R"(Json.parse("[10, 20]") |> Result.unwrap() |> Json.index(-1))");
    ASSERT_TRUE(v.is_null());
}

static void test_json_value_index_on_non_array_none() {
    const auto v = eval(R"(Json.parse("{\"a\": 1}") |> Result.unwrap() |> Json.index(0))");
    ASSERT_TRUE(v.is_null());
}

static void test_json_value_to_string_roundtrip_object() {
    // Keys are emitted in sorted order (shared JsonValue uses std::map); integral
    // numbers render without a decimal point.
    const auto v =
        eval(R"(Json.parse("{\"b\": [2, 3], \"a\": 1}") |> Result.unwrap() |> Json.to_string())");
    ASSERT_EQ(v.as_string(), R"({"a":1,"b":[2,3]})");
}

static void test_json_value_to_string_string_quotes() {
    const auto v = eval(R"(Json.parse("\"hi\"") |> Result.unwrap() |> Json.to_string())");
    ASSERT_EQ(v.as_string(), "\"hi\"");
}

static void test_json_value_to_string_fraction_preserved() {
    const auto v = eval(R"(Json.parse("1.5") |> Result.unwrap() |> Json.to_string())");
    ASSERT_EQ(v.as_string(), "1.5");
}

static void test_json_value_module_registered() {
    const auto env = luma::test::make_std_env();
    ASSERT_TRUE(env->has("Json.parse"));
    ASSERT_TRUE(env->has("Json.to_string"));
    ASSERT_TRUE(env->has("Json.as_string"));
    ASSERT_TRUE(env->has("Json.as_number"));
    ASSERT_TRUE(env->has("Json.as_boolean"));
    ASSERT_TRUE(env->has("Json.as_array"));
    ASSERT_TRUE(env->has("Json.as_object"));
    ASSERT_TRUE(env->has("Json.field"));
    ASSERT_TRUE(env->has("Json.index"));
}

int main() {
    RUN(test_json_get_path_array_index);
    RUN(test_json_get_path_bracket_index_lenient);
    RUN(test_json_get_path_missing);
    RUN(test_json_get_path_nested);
    RUN(test_json_get_path_simple);
    RUN(test_json_is_valid);
    RUN(test_json_is_valid_false);
    RUN(test_json_module);
    RUN(test_json_serialize_array);
    RUN(test_json_serialize_deserialize);
    RUN(test_json_serialize_string);
    RUN(test_json_set_path_nested);
    RUN(test_json_set_path_simple);

    // Serialisation — additional value types.
    RUN(test_json_serialize_boolean);
    RUN(test_json_serialize_none);
    RUN(test_json_serialize_number);
    RUN(test_json_serialize_dictionary);
    RUN(test_json_serialize_nested);
    RUN(test_json_serialize_string_escapes);
    RUN(test_json_serialize_pretty_layout);

    // Serialisation — divergent-branch characterization (choice/record/tuple/depth).
    RUN(test_json_serialize_nullary_choice_string);
    RUN(test_json_serialize_choice_with_fields_null);
    RUN(test_json_serialize_record_object);
    RUN(test_json_serialize_tuple_array);
    RUN(test_json_serialize_string_no_slash_escape);
    RUN(test_json_serialize_depth_exceeded_null);

    // Deserialisation — additional value types.
    RUN(test_json_deserialize_string);
    RUN(test_json_deserialize_number);
    RUN(test_json_deserialize_boolean);
    RUN(test_json_deserialize_null);
    RUN(test_json_deserialize_object);
    RUN(test_json_deserialize_unicode_escape);
    RUN(test_json_roundtrip_dictionary);

    // get / set / merge (dot-path API).
    RUN(test_json_get_dot_array_index);
    RUN(test_json_set_replaces_value);
    RUN(test_json_set_adds_key);
    RUN(test_json_merge_combines);
    RUN(test_json_merge_right_wins);

    // get_path / set_path (bracket-index API).
    RUN(test_json_get_path_bracket_nested);
    RUN(test_json_set_path_array_index);
    RUN(test_json_is_valid_various);

    // Negative — malformed input and invalid operations.
    RUN(test_json_deserialize_malformed);
    RUN(test_json_deserialize_trailing_content);
    RUN(test_json_deserialize_unterminated_string);
    RUN(test_json_deserialize_leading_zero);
    RUN(test_json_deserialize_empty);
    RUN(test_json_deserialize_bad_escape);
    RUN(test_json_merge_non_objects);
    RUN(test_json_merge_invalid_input);
    RUN(test_json_set_missing_parent);
    RUN(test_json_set_non_object);
    RUN(test_json_set_array_mid_path);
    RUN(test_json_get_path_out_of_bounds);
    RUN(test_json_get_path_into_scalar);
    RUN(test_json_set_path_out_of_bounds);
    RUN(test_json_deserialize_non_string_throws);
    RUN(test_json_is_valid_non_string_throws);
    RUN(test_json_parses_out_of_double_range_numbers);

    // Json.Value typed ADT — parse, accessors, field/index, round-trip.
    RUN(test_json_value_module_registered);
    RUN(test_json_value_parse_object);
    RUN(test_json_value_parse_array);
    RUN(test_json_value_parse_string);
    RUN(test_json_value_parse_number);
    RUN(test_json_value_parse_boolean);
    RUN(test_json_value_parse_null);
    RUN(test_json_value_parse_invalid_fails);
    RUN(test_json_parse_detailed_success);
    RUN(test_json_parse_detailed_failure_has_location);
    RUN(test_json_parse_detailed_reports_line_on_multiline);
    RUN(test_json_value_as_string_success);
    RUN(test_json_value_as_string_wrong_type_fails);
    RUN(test_json_value_as_number_success);
    RUN(test_json_value_as_boolean_success);
    RUN(test_json_value_as_array_success);
    RUN(test_json_value_as_object_success);
    RUN(test_json_value_field_some);
    RUN(test_json_value_field_missing_key_none);
    RUN(test_json_value_field_on_non_object_none);
    RUN(test_json_value_index_some);
    RUN(test_json_value_index_out_of_bounds_none);
    RUN(test_json_value_index_negative_none);
    RUN(test_json_value_index_on_non_array_none);
    RUN(test_json_value_to_string_roundtrip_object);
    RUN(test_json_value_to_string_string_quotes);
    RUN(test_json_value_to_string_fraction_preserved);
    return SUMMARY();
}
