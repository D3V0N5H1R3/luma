// Standard library tests: Csv.

#include <cstddef>
#include <cstdint>
#include <string>

#include "common/resource_limits.hpp"
#include "runtime/stdlib/text/csv_codec.hpp"
#include "stdlib_test_helpers.hpp"

static void test_csv_header() {
    const auto result =
        eval("Csv.header(\"name,age\\nAlice,30\") |> Result.unwrap() |> Array.length()");

    ASSERT_EQ(result.as_integer(), 2);
}

static void test_csv_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Csv.deserialize"));
    ASSERT_TRUE(env->has("Csv.deserialize_records"));
    ASSERT_TRUE(env->has("Csv.deserialize_detailed"));
    ASSERT_TRUE(env->has("Csv.serialize"));
    ASSERT_TRUE(env->has("Csv.serialize_records"));
    ASSERT_TRUE(env->has("Csv.header"));
    ASSERT_TRUE(env->has("Csv.count_rows"));
    ASSERT_TRUE(env->has("Csv.default_dialect"));
    ASSERT_TRUE(env->has("Csv.dialect"));
}

static void test_csv_parse() {
    const auto result =
        eval("Csv.deserialize(\"a,b\\n1,2\\n3,4\") |> Result.unwrap() |> Array.length()");

    ASSERT_EQ(result.as_integer(), 3);
}

static void test_csv_serialize() {
    const auto result = eval("Csv.serialize([[\"a\", \"b\"], [\"1\", \"2\"]]) |> Result.unwrap()");

    // CSV uses \r\n line endings per RFC 4180.
    ASSERT_TRUE(result.as_string().find("a,b") != std::string::npos);
    ASSERT_TRUE(result.as_string().find("1,2") != std::string::npos);
}

static void test_csv_count_rows() {
    const auto v = eval("Csv.count_rows(\"name,age\\nAlice,30\\nBob,25\") |> Result.unwrap()");

    ASSERT_EQ(v.as_integer(), 2);
}

static void test_csv_count_rows_empty() {
    const auto v = eval("Csv.count_rows(\"name,age\") |> Result.unwrap()");

    ASSERT_EQ(v.as_integer(), 0);
}

static void test_csv_deserialize_records() {
    const auto v = eval(
        "Csv.deserialize_records(\"name,age\\nAlice,30\") |> Result.unwrap() |> Array.length()");

    ASSERT_EQ(v.as_integer(), 1);
}

static void test_csv_serialize_records() {
    const auto v = eval("Csv.serialize_records([{\"name\": \"Alice\", \"age\": \"30\"}])");

    ASSERT_TRUE(v.as_string().find("name") != std::string::npos);
    ASSERT_TRUE(v.as_string().find("Alice") != std::string::npos);
}

static void test_csv_quoted_fields() {
    // Fields containing commas must be quoted in CSV.
    const auto v = eval("Csv.deserialize(\"a,b\\n\\\"hello, world\\\",2\") |> Result.unwrap()");

    ASSERT_TRUE(v.is_array());
}

static void test_csv_serialize_roundtrip() {
    // Serialize then deserialize should preserve data.
    const auto v =
        eval("string csv = Csv.serialize([[\"x\", \"y\"], [\"1\", \"2\"]]) |> Result.unwrap()\n"
             "Csv.deserialize(csv) |> Result.unwrap() |> Array.length()");

    ASSERT_EQ(v.as_integer(), 2);
}

static void test_csv_header_values() {
    const auto v = eval("Csv.header(\"name,age,city\\nAlice,30,NY\") |> Result.unwrap()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "name");
    ASSERT_EQ((*v.as_array()->elements)[2].as_string(), "city");
}

static void test_csv_header_empty_fails() {
    ASSERT_EVAL_FAILURE("Csv.header(\"\")");
}

static void test_csv_serialize_empty() {
    const auto v = eval("Csv.serialize([]) |> Result.unwrap()");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().empty());
}

static void test_csv_serialize_records_empty() {
    const auto v = eval("Csv.serialize_records([])");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().empty());
}

static void test_csv_deserialize_non_string_throws() {
    ASSERT_THROWS(eval("Csv.deserialize(42)"));
}

// ─── Options: deserialize_with / serialize_with ──────────────────────────────

static void test_csv_deserialize_with_delimiter() {
    const auto v =
        eval("Csv.deserialize_with(\"a;b\\n1;2\", {\"delimiter\": \";\"}) |> Result.unwrap()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);

    const auto& row1 = *(*v.as_array()->elements)[1].as_array()->elements;
    ASSERT_EQ(row1[0].as_string(), "1");
    ASSERT_EQ(row1[1].as_string(), "2");
}

static void test_csv_serialize_with_delimiter() {
    const auto v = eval("Csv.serialize_with([[\"a\", \"b\"], [\"1\", \"2\"]], "
                        "{\"delimiter\": \";\"}) |> Result.unwrap()");

    ASSERT_TRUE(v.as_string().find("a;b") != std::string::npos);
    ASSERT_TRUE(v.as_string().find("1;2") != std::string::npos);
}

static void test_csv_serialize_with_custom_quote() {
    // A field containing the custom delimiter must be wrapped in the custom
    // quote character.
    const auto v = eval(R"(Csv.serialize_with([["a;b", "c"]], )"
                        R"({"delimiter": ";", "quote": "'"}) |> Result.unwrap())");

    ASSERT_TRUE(v.as_string().find("'a;b'") != std::string::npos);
}

// ─── Csv.Dialect typed options ───────────────────────────────────────────────

static void test_csv_default_dialect() {
    // The default dialect is a Csv.Dialect record with RFC 4180 defaults.
    const auto v = eval("Csv.default_dialect()");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Dialect"});
    ASSERT_EQ(v.as_record()->find_field("delimiter")->as_string(), ",");
    ASSERT_EQ(v.as_record()->find_field("quote")->as_string(), "\"");
}

static void test_csv_dialect_custom_fields() {
    // Csv.dialect builds a typed options record from explicit delimiter/quote.
    const auto v = eval(R"(Csv.dialect(";", "'"))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Dialect"});
    ASSERT_EQ(v.as_record()->find_field("delimiter")->as_string(), ";");
    ASSERT_EQ(v.as_record()->find_field("quote")->as_string(), "'");
}

static void test_csv_deserialize_with_dialect() {
    // A Csv.Dialect record is accepted by deserialize_with alongside the dict.
    const auto v =
        eval(R"(Csv.deserialize_with("a;b\n1;2", Csv.dialect(";", "\"")) |> Result.unwrap())");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);

    const auto& row1 = *(*v.as_array()->elements)[1].as_array()->elements;
    ASSERT_EQ(row1[0].as_string(), "1");
    ASSERT_EQ(row1[1].as_string(), "2");
}

static void test_csv_serialize_with_dialect() {
    const auto v = eval(
        R"(Csv.serialize_with([["a", "b"], ["1", "2"]], Csv.dialect(";", "\"")) |> Result.unwrap())");

    ASSERT_TRUE(v.as_string().find("a;b") != std::string::npos);
    ASSERT_TRUE(v.as_string().find("1;2") != std::string::npos);
}

// ─── RFC 4180 quoting / line endings ─────────────────────────────────────────

static void test_csv_deserialize_crlf() {
    const auto v = eval("Csv.deserialize(\"a,b\\r\\n1,2\\r\\n3,4\") |> Result.unwrap()");

    ASSERT_EQ(v.as_array()->elements->size(), 3U);
}

static void test_csv_deserialize_escaped_quote() {
    const auto v = eval(R"(Csv.deserialize("\"she said \"\"hi\"\"\",x") |> Result.unwrap())");

    const auto& row0 = *(*v.as_array()->elements)[0].as_array()->elements;
    ASSERT_EQ(row0[0].as_string(), "she said \"hi\"");
    ASSERT_EQ(row0[1].as_string(), "x");
}

static void test_csv_deserialize_multiline_field() {
    // A newline inside a quoted field is data, not a row separator.
    const auto v = eval(R"(Csv.deserialize("\"a\nb\",c") |> Result.unwrap())");

    ASSERT_EQ(v.as_array()->elements->size(), 1U);

    const auto& row0 = *(*v.as_array()->elements)[0].as_array()->elements;
    ASSERT_EQ(row0[0].as_string(), "a\nb");
    ASSERT_EQ(row0[1].as_string(), "c");
}

static void test_csv_serialize_quotes_special_fields() {
    // A field containing the delimiter must be quoted on output.
    const auto v = eval(R"(Csv.serialize([["a,b", "c"]]) |> Result.unwrap())");

    ASSERT_TRUE(v.as_string().find("\"a,b\"") != std::string::npos);
}

static void test_csv_count_rows_quoted_newline() {
    // The embedded newline lives inside a quoted field, so this is one data row.
    const auto v = eval(R"(Csv.count_rows("a,b\n\"x\ny\",2") |> Result.unwrap())");

    ASSERT_EQ(v.as_integer(), 1);
}

// ─── Records ─────────────────────────────────────────────────────────────────

static void test_csv_deserialize_records_values() {
    const auto v =
        eval("Csv.deserialize_records(\"name,age\\nAlice,30\\nBob,25\") |> Result.unwrap()");

    ASSERT_EQ(v.as_array()->elements->size(), 2U);

    const auto& rec0 = (*v.as_array()->elements)[0];
    ASSERT_EQ(rec0.as_dictionary()->find("name")->as_string(), "Alice");

    const auto& rec1 = (*v.as_array()->elements)[1];
    ASSERT_EQ(rec1.as_dictionary()->find("age")->as_string(), "25");
}

static void test_csv_records_ragged_padding() {
    // A data row shorter than the header is padded with empty fields.
    const auto v = eval("Csv.deserialize_records(\"a,b,c\\n1,2\") |> Result.unwrap()");

    ASSERT_EQ(v.as_array()->elements->size(), 1U);

    const auto& rec = (*v.as_array()->elements)[0];
    ASSERT_EQ(rec.as_dictionary()->find("a")->as_string(), "1");
    ASSERT_EQ(rec.as_dictionary()->find("b")->as_string(), "2");
    ASSERT_EQ(rec.as_dictionary()->find("c")->as_string(), "");
}

static void test_csv_serialize_records_roundtrip() {
    const auto v = eval("string c = Csv.serialize_records([{\"name\": \"Alice\", \"age\": \"30\"}, "
                        "{\"name\": \"Bob\", \"age\": \"25\"}])\n"
                        "Csv.deserialize_records(c) |> Result.unwrap() |> Array.length()");

    ASSERT_EQ(v.as_integer(), 2);
}

// ─── Negative: malformed input yields a failure result ───────────────────────

static void test_csv_deserialize_unterminated_quote_fails() {
    ASSERT_EVAL_FAILURE(R"(Csv.deserialize("\"unterminated"))");
}

static void test_csv_deserialize_records_unterminated_fails() {
    ASSERT_EVAL_FAILURE(R"(Csv.deserialize_records("\"oops"))");
}

static void test_csv_header_unterminated_fails() {
    ASSERT_EVAL_FAILURE(R"(Csv.header("\"oops"))");
}

static void test_csv_count_rows_unterminated_fails() {
    ASSERT_EVAL_FAILURE(R"(Csv.count_rows("\"oops"))");
}

static void test_csv_read_file_missing_fails() {
    ASSERT_EVAL_FAILURE("Csv.read_file(\"nonexistent_csv_file_xyz.csv\")");
}

static void test_csv_read_file_rejects_oversized_file() {
    // read_file slurps the whole file into memory (via file_helpers) before
    // parsing.  A file larger than the maximum string size must be rejected up
    // front, yielding a failure result rather than an unbounded allocation.
    // The file is created under the default cap, then the cap is lowered so the
    // test need not materialise a 256 MB file.
    const LumaTempFile file{"_test_csv_oversize.csv", std::string(64, 'x')};
    const LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(16)};

    ASSERT_EVAL_FAILURE("Csv.read_file(\"_test_csv_oversize.csv\")");
}

// ─── Negative: wrong argument types raise a runtime error ────────────────────

static void test_csv_serialize_non_array_arg_throws() {
    ASSERT_THROWS(eval("Csv.serialize(\"not an array\")"));
}

static void test_csv_serialize_row_not_array_throws() {
    ASSERT_THROWS(eval("Csv.serialize([1, 2])"));
}

static void test_csv_serialize_with_row_not_array_throws() {
    ASSERT_THROWS(eval("Csv.serialize_with([1], {\"delimiter\": \",\"})"));
}

static void test_csv_serialize_records_non_dict_throws() {
    ASSERT_THROWS(eval("Csv.serialize_records([1, 2])"));
}

static void test_csv_deserialize_with_non_dict_throws() {
    ASSERT_THROWS(eval("Csv.deserialize_with(\"a,b\", 5)"));
}

static void test_csv_parse_rejects_too_many_fields() {
    // Regression: parse_csv capped the row count but not the field count per
    // row.  A single line of nothing but delimiters (no newline) never reaches
    // the row cap, so current_row grew without bound — an out-of-memory DoS on
    // hostile input.  The per-row field count is now bounded by the same
    // max_array_size limit.  Lower the limit so the test stays small, and
    // restore it afterwards even if an assertion throws.
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(8)};

    const std::string input(64, ',');
    const auto result = luma::csv::parse_csv(input, luma::csv::CsvOptions{});

    ASSERT_FALSE(result.success);
    ASSERT_TRUE(result.error.find("too many fields") != std::string::npos);
}

// ─── Csv.deserialize_detailed / Csv.ParseError ───────────────────────────────

static void test_csv_deserialize_detailed_success() {
    const auto v =
        eval(R"(Csv.deserialize_detailed("a,b\n1,2") |> Result.unwrap() |> Array.length())");

    ASSERT_EQ(v.as_integer(), 2);
}

static void test_csv_deserialize_detailed_failure_message() {
    const auto v = eval(R"(Csv.deserialize_detailed("\"oops"))");
    ASSERT_TRUE(v.is_result());
    ASSERT_FALSE(v.as_result()->is_success);

    const auto& err = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(err->type_name, std::string{"ParseError"});
    ASSERT_TRUE(err->find_field("message")->as_string().find("unterminated") != std::string::npos);
}

static void test_csv_deserialize_detailed_failure_location() {
    // The unterminated quote opens on the second line, so the located error must
    // point at line 2, column 1.
    const auto v = eval(R"(Csv.deserialize_detailed("a,b\n\"oops"))");
    ASSERT_TRUE(v.is_result());
    ASSERT_FALSE(v.as_result()->is_success);

    const auto& err = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(err->type_name, std::string{"ParseError"});
    ASSERT_EQ(err->find_field("line")->as_integer(), static_cast<std::int64_t>(2));
    ASSERT_TRUE(err->find_field("column")->as_integer() >= 1);
}

static void test_csv_codec_reports_error_offset() {
    // The pure codec records the byte offset of the failure so the located error
    // can be derived.  The unterminated quote sits at index 4.
    const auto result = luma::csv::parse_csv("a,b\n\"oops", luma::csv::CsvOptions{});

    ASSERT_FALSE(result.success);
    ASSERT_EQ(result.error_offset, static_cast<std::size_t>(4));
}

// --- Csv.Table (N05) ---

static void test_csv_deserialize_table_shape() {
    const auto v = eval(R"(Csv.deserialize_table("name,age\nAlice,30\nBob,25"))");
    ASSERT_TRUE(v.is_result());
    ASSERT_TRUE(v.as_result()->is_success);

    const auto& rec = *v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec.type_name, std::string{"Table"});

    const auto& headers = *rec.find_field("headers")->as_array()->elements;
    ASSERT_EQ(headers.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(headers[0].as_string(), std::string{"name"});

    const auto& rows = *rec.find_field("rows")->as_array()->elements;
    ASSERT_EQ(rows.size(), static_cast<std::size_t>(2));
    ASSERT_EQ((*rows[1].as_array()->elements)[0].as_string(), std::string{"Bob"});
}

static void test_csv_deserialize_table_empty_keeps_header() {
    // A header-only CSV yields headers but zero rows — the shape the record form
    // preserves where deserialize_records cannot.
    const auto v = eval(R"(Csv.deserialize_table("name,age"))");
    ASSERT_TRUE(v.as_result()->is_success);

    const auto& rec = *v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec.find_field("headers")->as_array()->elements->size(), static_cast<std::size_t>(2));
    ASSERT_EQ(rec.find_field("rows")->as_array()->elements->size(), static_cast<std::size_t>(0));
}

static void test_csv_deserialize_table_failure() {
    const auto v = eval(R"(Csv.deserialize_table("\"oops"))");
    ASSERT_FALSE(v.as_result()->is_success);
    ASSERT_EQ(v.as_result()->owned_inner->as_record()->type_name, std::string{"ParseError"});
}

static void test_csv_column_extracts_by_name() {
    const auto v = eval(
        R"(Csv.deserialize_table("name,age\nAlice,30\nBob,25") |> Result.unwrap() |> Csv.column("age"))");
    ASSERT_TRUE(v.as_result()->is_success);

    const auto& col = *v.as_result()->owned_inner->as_array()->elements;
    ASSERT_EQ(col.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(col[0].as_string(), std::string{"30"});
    ASSERT_EQ(col[1].as_string(), std::string{"25"});
}

static void test_csv_column_unknown_fails() {
    const auto v = eval(
        R"(Csv.deserialize_table("name,age\nAlice,30") |> Result.unwrap() |> Csv.column("missing"))");
    ASSERT_FALSE(v.as_result()->is_success);
}

static void test_csv_serialize_table_roundtrips() {
    const auto v = eval(
        R"(Csv.deserialize_table("name,age\nAlice,30\nBob,25") |> Result.unwrap() |> Csv.serialize_table())");
    ASSERT_TRUE(v.as_result()->is_success);

    const auto& text = v.as_result()->owned_inner->as_string();
    ASSERT_TRUE(text.find("name,age") != std::string::npos);
    ASSERT_TRUE(text.find("Bob,25") != std::string::npos);
}

int main() {
    RUN(test_csv_count_rows);
    RUN(test_csv_count_rows_empty);
    RUN(test_csv_count_rows_quoted_newline);
    RUN(test_csv_count_rows_unterminated_fails);
    RUN(test_csv_default_dialect);
    RUN(test_csv_deserialize_crlf);
    RUN(test_csv_deserialize_detailed_success);
    RUN(test_csv_deserialize_detailed_failure_message);
    RUN(test_csv_deserialize_detailed_failure_location);
    RUN(test_csv_codec_reports_error_offset);
    RUN(test_csv_deserialize_table_shape);
    RUN(test_csv_deserialize_table_empty_keeps_header);
    RUN(test_csv_deserialize_table_failure);
    RUN(test_csv_column_extracts_by_name);
    RUN(test_csv_column_unknown_fails);
    RUN(test_csv_serialize_table_roundtrips);
    RUN(test_csv_deserialize_escaped_quote);
    RUN(test_csv_deserialize_multiline_field);
    RUN(test_csv_deserialize_non_string_throws);
    RUN(test_csv_deserialize_records);
    RUN(test_csv_deserialize_records_unterminated_fails);
    RUN(test_csv_deserialize_records_values);
    RUN(test_csv_deserialize_unterminated_quote_fails);
    RUN(test_csv_deserialize_with_delimiter);
    RUN(test_csv_deserialize_with_dialect);
    RUN(test_csv_deserialize_with_non_dict_throws);
    RUN(test_csv_dialect_custom_fields);
    RUN(test_csv_header);
    RUN(test_csv_header_empty_fails);
    RUN(test_csv_header_unterminated_fails);
    RUN(test_csv_header_values);
    RUN(test_csv_module);
    RUN(test_csv_parse);
    RUN(test_csv_parse_rejects_too_many_fields);
    RUN(test_csv_quoted_fields);
    RUN(test_csv_read_file_missing_fails);
    RUN(test_csv_read_file_rejects_oversized_file);
    RUN(test_csv_records_ragged_padding);
    RUN(test_csv_serialize);
    RUN(test_csv_serialize_empty);
    RUN(test_csv_serialize_non_array_arg_throws);
    RUN(test_csv_serialize_quotes_special_fields);
    RUN(test_csv_serialize_records);
    RUN(test_csv_serialize_records_empty);
    RUN(test_csv_serialize_records_non_dict_throws);
    RUN(test_csv_serialize_records_roundtrip);
    RUN(test_csv_serialize_row_not_array_throws);
    RUN(test_csv_serialize_roundtrip);
    RUN(test_csv_serialize_with_custom_quote);
    RUN(test_csv_serialize_with_delimiter);
    RUN(test_csv_serialize_with_dialect);
    RUN(test_csv_serialize_with_row_not_array_throws);
    return SUMMARY();
}
