#include "runtime/stdlib/text/csv_module.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/utf8.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/file_helpers.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "runtime/stdlib/text/csv_codec.hpp"

namespace luma {

// The pure RFC 4180 codec (CsvOptions, ParseResult, parse_csv, serialize_rows)
// lives in namespace luma::csv (declared in csv_codec.hpp) so the fuzz target
// can drive it directly.  Pull the names into luma so the Value-facing helpers
// and registration below read unchanged.
using csv::CsvOptions;
using csv::parse_csv;
using csv::serialize_rows;

namespace {

// ─── Options parsing (Value → CsvOptions) ────────────────────────────────────

[[nodiscard]] CsvOptions parse_options(const DictionaryValue& dict) {
    CsvOptions opts;

    if (const auto* val = dict.find("delimiter")) {
        if (val->is_string()) {
            const auto& s = val->as_string();

            if (!s.empty()) {
                opts.delimiter = s[0];
            }
        }
    }

    if (const auto* val = dict.find("quote")) {
        if (val->is_string()) {
            const auto& s = val->as_string();

            if (!s.empty()) {
                opts.quote = s[0];
            }
        }
    }

    return opts;
}

// Read the delimiter / quote from a Csv.Dialect record { delimiter, quote }.
[[nodiscard]] CsvOptions parse_options(const RecordValue& rec) {
    CsvOptions opts;

    if (const auto* val = rec.find_field("delimiter"); val != nullptr && val->is_string()) {
        const auto& s = val->as_string();

        if (!s.empty()) {
            opts.delimiter = s[0];
        }
    }

    if (const auto* val = rec.find_field("quote"); val != nullptr && val->is_string()) {
        const auto& s = val->as_string();

        if (!s.empty()) {
            opts.quote = s[0];
        }
    }

    return opts;
}

// Accept CSV options as either a typed Csv.Dialect record or the legacy options
// dictionary with "delimiter"/"quote" keys.  Accepting both keeps the typed
// record additive — existing dictionary callers keep working unchanged.
[[nodiscard]] CsvOptions options_from_value(const Value& value, std::string_view func_name,
                                            SourceLocation loc) {
    if (value.is_record()) {
        return parse_options(*value.as_record());
    }

    if (value.is_dictionary()) {
        return parse_options(*value.as_dictionary());
    }

    throw RuntimeError{
        std::format("{}: options must be a Csv.Dialect record or a dictionary", func_name), loc,
        "pass Csv.dialect(delimiter, quote), Csv.default_dialect(), or a dictionary with "
        "\"delimiter\"/\"quote\" keys"};
}

// Build a Csv.Dialect record { delimiter, quote }.  The record is produced by a
// module call (Csv.dialect / Csv.default_dialect) rather than hand-constructed,
// matching how every other stdlib record is returned — so beginners get a typed,
// discoverable options shape without the magic dictionary keys.
[[nodiscard]] Value make_dialect_record(std::string delimiter, std::string quote) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Dialect";
    rec->fields.emplace_back("delimiter", Value{std::move(delimiter)});
    rec->fields.emplace_back("quote", Value{std::move(quote)});

    return Value{std::move(rec)};
}

// RFC 4180 defaults: comma delimiter, double quote.
[[nodiscard]] Value make_default_dialect() {
    return make_dialect_record(",", "\"");
}

[[nodiscard]] Value rows_to_value(const std::vector<std::vector<std::string>>& rows) {
    auto arr = std::make_shared<ArrayValue>();

    for (const auto& row : rows) {
        auto row_arr = std::make_shared<ArrayValue>();

        for (const auto& field : row) {
            row_arr->elements->emplace_back(field);
        }

        arr->elements->emplace_back(std::move(row_arr));
    }

    return Value{std::move(arr)};
}

[[nodiscard]] Value rows_to_records(const std::vector<std::vector<std::string>>& rows) {
    if (rows.empty()) {
        return Value{std::make_shared<ArrayValue>()};
    }

    const auto& header = rows[0];

    auto arr = std::make_shared<ArrayValue>();

    for (std::size_t i{1}; i < rows.size(); ++i) {
        auto dict = std::make_shared<DictionaryValue>();
        // Pre-build the empty hash index so each set() below is O(1), keeping the
        // per-row build O(columns) rather than O(columns^2).
        dict->rebuild_index();

        for (std::size_t j{0}; j < header.size(); ++j) {
            const auto& value = j < rows[i].size() ? rows[i][j] : "";

            dict->set(header[j], Value{value});
        }

        arr->elements->emplace_back(std::move(dict));
    }

    return Value{std::move(arr)};
}

[[nodiscard]] std::vector<std::vector<std::string>>
records_to_rows(std::span<const Value> records, std::string_view func_name, SourceLocation loc) {
    if (records.empty()) {
        return {};
    }

    if (!records[0].is_dictionary()) {
        throw RuntimeError{std::format("{}: each record must be a dictionary", func_name), loc,
                           "ensure every element in the array is a dictionary"};
    }

    std::vector<std::string> header;

    for (const auto& [k, v] : records[0].as_dictionary()->entries) {
        header.push_back(k);
    }

    std::vector<std::vector<std::string>> rows;
    rows.push_back(header);

    for (const auto& rec : records) {
        if (!rec.is_dictionary()) {
            throw RuntimeError{std::format("{}: each record must be a dictionary", func_name), loc,
                               "ensure every element in the array is a dictionary"};
        }

        std::vector<std::string> row;

        for (const auto& key : header) {
            const auto* val = rec.as_dictionary()->find(key);
            row.push_back((val != nullptr) ? val->to_string() : "");
        }

        rows.push_back(std::move(row));
    }

    return rows;
}

// Convert a Luma array-of-arrays Value into rows of stringified fields.  Throws
// if any row element is not an array — a static-type violation the runtime
// still guards defensively.  Shared by Csv.serialize and Csv.serialize_with.
[[nodiscard]] std::vector<std::vector<std::string>>
array_to_rows(std::span<const Value> data, std::string_view func_name, SourceLocation loc) {
    std::vector<std::vector<std::string>> rows;
    rows.reserve(data.size());

    for (const auto& row_val : data) {
        if (!row_val.is_array()) {
            throw RuntimeError{std::format("{}: each row must be an array", func_name), loc,
                               "pass an array of arrays"};
        }

        std::vector<std::string> row;

        for (const auto& field : *row_val.as_array()->elements) {
            row.push_back(field.to_string());
        }

        rows.push_back(std::move(row));
    }

    return rows;
}

// Parse CSV and return a Luma result Value containing an array of arrays.
// Handles the common parse + error-check + rows_to_value pattern.
[[nodiscard]] Value parse_csv_to_rows_result(const std::string& input, const CsvOptions& opts,
                                             std::string_view func_name) {
    [[maybe_unused]] auto [rows, success, error, error_offset] = parse_csv(input, opts);

    if (!success) {
        return failure_msg("Csv", func_name, error, error_codes::parse_error);
    }

    return make_success_value(rows_to_value(rows));
}

// Parse CSV and return a Luma result Value containing an array of dictionaries.
// Handles the common parse + error-check + rows_to_records pattern.
[[nodiscard]] Value parse_csv_to_records_result(const std::string& input, const CsvOptions& opts,
                                                std::string_view func_name) {
    [[maybe_unused]] auto [rows, success, error, error_offset] = parse_csv(input, opts);

    if (!success) {
        return failure_msg("Csv", func_name, error, error_codes::parse_error);
    }

    return make_success_value(rows_to_records(rows));
}

// Build a Csv.ParseError record (type_name "ParseError") carrying the failure
// message and its 1-based line/column.  Matches the "Csv.ParseError" record
// registered in stdlib_type_arities.cpp and mirrors Json.parse_detailed's
// located-failure record.
[[nodiscard]] Value make_parse_error_record(std::string message, std::int64_t line,
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
// editor reports positions (identical to Json.parse_detailed's mapping).
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

// Parse CSV and return result<array<array<string>>, Csv.ParseError>: on failure
// the error carries the located reason instead of a bare string, so a caller can
// point at the row/column that broke.  Mirrors Json.parse_detailed.
[[nodiscard]] Value parse_csv_to_detailed_result(const std::string& input, const CsvOptions& opts) {
    auto [rows, success, error, error_offset] = parse_csv(input, opts);

    if (!success) {
        const auto pos = offset_to_line_column(input, error_offset);

        return Value{ResultValue::failure(make_parse_error_record(error, pos.line, pos.column))};
    }

    return make_success_value(rows_to_value(rows));
}

} // namespace

// ─── Registration ────────────────────────────────────────────────────────────

void register_csv_ns(const EnvPtr& env) {
    ModuleBuilder{"Csv", env}
        .func("deserialize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.deserialize", loc);

            return parse_csv_to_rows_result(args[0].as_string(), CsvOptions{}, "deserialize");
        })
        .func("deserialize_detailed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.deserialize_detailed", loc);

            return parse_csv_to_detailed_result(args[0].as_string(), CsvOptions{});
        })
        .func("deserialize_records", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.deserialize_records", loc);

            return parse_csv_to_records_result(args[0].as_string(), CsvOptions{},
                                               "deserialize_records");
        })
        .func("deserialize_with", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.deserialize_with", loc);

            const auto opts = options_from_value(args[1], "Csv.deserialize_with", loc);

            return parse_csv_to_rows_result(args[0].as_string(), opts, "deserialize_with");
        })
        .func("default_dialect", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return make_default_dialect();
        })
        .func("dialect", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& delimiter = expect_string(args[0], "Csv.dialect", loc);
            const auto& quote = expect_string(args[1], "Csv.dialect", loc);

            return make_dialect_record(delimiter, quote);
        })
        .func("serialize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& data = *expect_array(args[0], "Csv.serialize", loc)->elements;

            const auto rows = array_to_rows(data, "Csv.serialize", loc);

            return make_success_value(Value{serialize_rows(rows, CsvOptions{})});
        })
        .func("serialize_records", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& records = *expect_array(args[0], "Csv.serialize_records", loc)->elements;

            const auto rows = records_to_rows(records, "Csv.serialize_records", loc);

            return Value{serialize_rows(rows, CsvOptions{})};
        })
        .func("serialize_with", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& data = *expect_array(args[0], "Csv.serialize_with", loc)->elements;
            const auto opts = options_from_value(args[1], "Csv.serialize_with", loc);

            const auto rows = array_to_rows(data, "Csv.serialize_with", loc);

            return make_success_value(Value{serialize_rows(rows, opts)});
        })
        .func("read_file", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.read_file", loc);

            const auto safe_path = validate_path(args[0].as_string(), loc);

            const auto content = file_helpers::read_file_contents(safe_path);

            if (!content) {
                return failure_msg("Csv", "read_file",
                                   std::format("cannot open '{}'", safe_path.string()),
                                   error_codes::io_error);
            }

            const CsvOptions opts;
            [[maybe_unused]] auto [rows, success, error, error_offset] = parse_csv(*content, opts);

            if (!success) {
                return failure_msg("Csv", "read_file", error, error_codes::parse_error);
            }

            return make_success_value(rows_to_records(rows));
        })
        .func("write_file", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.write_file", loc);

            const auto safe_path = validate_path(args[0].as_string(), loc);

            const auto& records = *expect_array(args[1], "Csv.write_file", loc)->elements;

            const auto rows = records_to_rows(records, "Csv.write_file", loc);

            const auto csv_text = serialize_rows(rows, CsvOptions{});

            if (!file_helpers::write_file_contents(safe_path, csv_text)) {
                return failure_msg("Csv", "write_file",
                                   std::format("cannot open '{}'", safe_path.string()),
                                   error_codes::io_error);
            }

            return make_success_value(Value{true});
        })
        .func("header", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.header", loc);

            const CsvOptions opts;
            [[maybe_unused]] auto [rows, success, error, error_offset] =
                parse_csv(args[0].as_string(), opts);

            if (!success) {
                return failure_msg("Csv", "header", error, error_codes::parse_error);
            }

            if (rows.empty()) {
                return failure_msg("Csv", "header", "empty CSV", error_codes::empty_container);
            }

            auto arr = std::make_shared<ArrayValue>();

            for (const auto& field : rows[0]) {
                arr->elements->emplace_back(field);
            }

            return make_success_value(Value{std::move(arr)});
        })
        .func("count_rows", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.count_rows", loc);

            const CsvOptions opts;
            [[maybe_unused]] auto [rows, success, error, error_offset] =
                parse_csv(args[0].as_string(), opts);

            if (!success) {
                return failure_msg("Csv", "count_rows", error, error_codes::parse_error);
            }

            // Exclude header row.
            const auto count = rows.size() > 1 ? rows.size() - 1 : 0;

            return make_success_value(Value{static_cast<std::int64_t>(count)});
        });
}

} // namespace luma
