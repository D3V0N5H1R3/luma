#include "runtime/stdlib/text/csv_module.hpp"

#include <cstdint>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "analysis/source/source_location.hpp"
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
    auto [rows, success, error] = parse_csv(input, opts);

    if (!success) {
        return failure_msg("Csv", func_name, error, error_codes::parse_error);
    }

    return make_success_value(rows_to_value(rows));
}

// Parse CSV and return a Luma result Value containing an array of dictionaries.
// Handles the common parse + error-check + rows_to_records pattern.
[[nodiscard]] Value parse_csv_to_records_result(const std::string& input, const CsvOptions& opts,
                                                std::string_view func_name) {
    auto [rows, success, error] = parse_csv(input, opts);

    if (!success) {
        return failure_msg("Csv", func_name, error, error_codes::parse_error);
    }

    return make_success_value(rows_to_records(rows));
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
        .func("deserialize_records", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.deserialize_records", loc);

            return parse_csv_to_records_result(args[0].as_string(), CsvOptions{},
                                               "deserialize_records");
        })
        .func("deserialize_with", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.deserialize_with", loc);
            (void)expect_dict(args[1], "Csv.deserialize_with", loc);

            return parse_csv_to_rows_result(
                args[0].as_string(), parse_options(*args[1].as_dictionary()), "deserialize_with");
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
            const auto opts = parse_options(*expect_dict(args[1], "Csv.serialize_with", loc));

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
            auto [rows, success, error] = parse_csv(*content, opts);

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
            auto [rows, success, error] = parse_csv(args[0].as_string(), opts);

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
            auto [rows, success, error] = parse_csv(args[0].as_string(), opts);

            if (!success) {
                return failure_msg("Csv", "count_rows", error, error_codes::parse_error);
            }

            // Exclude header row.
            const auto count = rows.size() > 1 ? rows.size() - 1 : 0;

            return make_success_value(Value{static_cast<std::int64_t>(count)});
        });
}

} // namespace luma
