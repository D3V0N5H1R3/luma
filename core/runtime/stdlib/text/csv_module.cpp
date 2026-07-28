#include "runtime/stdlib/text/csv_module.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
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

// ─── Csv.Table (headers + positional rows as one shape) ──────────────────────

// Build an array<string> Value from a list of strings.
[[nodiscard]] Value make_string_array(const std::vector<std::string>& items) {
    auto arr = std::make_shared<ArrayValue>();

    for (const auto& s : items) {
        arr->elements->emplace_back(s);
    }

    return Value{std::move(arr)};
}

// Build a Csv.Table record { headers: array<string>, rows: array<array<string>> }
// (type_name "Table").  Carries the header row once plus positional data rows,
// preserving column order — the shape deserialize_records loses.
[[nodiscard]] Value make_table_record(const std::vector<std::string>& headers,
                                      const std::vector<std::vector<std::string>>& rows) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Table";
    rec->fields.emplace_back("headers", make_string_array(headers));
    rec->fields.emplace_back("rows", rows_to_value(rows));

    return Value{std::move(rec)};
}

// The header names and positional data rows read from a Csv.Table argument.
struct TableParts {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
};

// Read a Csv.Table argument.  Returns std::nullopt when the value is not a
// table-shaped record so the caller can raise a typed failure.
[[nodiscard]] std::optional<TableParts> read_table(const Value& value) {
    if (!value.is_record()) {
        return std::nullopt;
    }

    const auto& rec = value.as_record();
    const Value* headers = rec->find_field("headers");
    const Value* rows = rec->find_field("rows");

    if (headers == nullptr || rows == nullptr || !headers->is_array() || !rows->is_array()) {
        return std::nullopt;
    }

    TableParts parts;

    for (const auto& h : *headers->as_array()->elements) {
        parts.headers.push_back(h.to_string());
    }

    for (const auto& row_val : *rows->as_array()->elements) {
        if (!row_val.is_array()) {
            return std::nullopt;
        }

        std::vector<std::string> row;

        for (const auto& field : *row_val.as_array()->elements) {
            row.push_back(field.to_string());
        }

        parts.rows.push_back(std::move(row));
    }

    return parts;
}

// Build a header-keyed dictionary<string> from one data row.  Short rows are
// padded with "" so every header is present (mirroring Csv.column's padding).
[[nodiscard]] Value row_to_record(const std::vector<std::string>& headers,
                                  const std::vector<std::string>& row) {
    auto dict = std::make_shared<DictionaryValue>();
    dict->rebuild_index();

    for (std::size_t i{0}; i < headers.size(); ++i) {
        dict->set(headers[i], Value{i < row.size() ? row[i] : std::string{}});
    }

    return Value{std::move(dict)};
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
        // ── Csv.Table (headers + positional rows) ────────────────────────────
        // Parse into a Csv.Table: the first row becomes the header, the rest the
        // positional rows.  Preserves column order and carries the header even
        // when there are zero data rows.  Located failure via Csv.ParseError,
        // mirroring deserialize_detailed.
        .func("deserialize_table", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Csv.deserialize_table", loc);

            const auto& input = args[0].as_string();
            auto [rows, success, error, error_offset] = parse_csv(input, CsvOptions{});

            if (!success) {
                const auto pos = offset_to_line_column(input, error_offset);

                return Value{
                    ResultValue::failure(make_parse_error_record(error, pos.line, pos.column))};
            }

            std::vector<std::string> headers;
            std::vector<std::vector<std::string>> data;

            if (!rows.empty()) {
                headers = rows.front();
                data.assign(rows.begin() + 1, rows.end());
            }

            return make_success_value(make_table_record(headers, data));
        })
        // Serialize a Csv.Table back to CSV text: the header row is emitted
        // first, then every positional row.  Returns result<string>.
        .func("serialize_table", 1)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto table = read_table(args[0]);

            if (!table) {
                return failure_msg("Csv", "serialize_table", "expected a Csv.Table record",
                                   error_codes::type_mismatch);
            }

            std::vector<std::vector<std::string>> rows;
            rows.reserve(table->rows.size() + 1);
            rows.push_back(table->headers);

            for (const auto& row : table->rows) {
                rows.push_back(row);
            }

            return make_success_value(Value{serialize_rows(rows, CsvOptions{})});
        })
        // Extract one named column from a Csv.Table as array<string> (short cells
        // are padded with "").  Fails with not_found when no header matches.
        .func("column", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto table = read_table(args[0]);

            if (!table) {
                return failure_msg("Csv", "column", "expected a Csv.Table record",
                                   error_codes::type_mismatch);
            }

            (void)expect_string(args[1], "Csv.column", loc);
            const auto& name = args[1].as_string();

            std::size_t index{0};
            bool found{false};

            for (std::size_t i{0}; i < table->headers.size(); ++i) {
                if (table->headers[i] == name) {
                    index = i;
                    found = true;
                    break;
                }
            }

            if (!found) {
                return failure_msg("Csv", "column", std::format("no column named '{}'", name),
                                   error_codes::not_found);
            }

            std::vector<std::string> column;
            column.reserve(table->rows.size());

            for (const auto& row : table->rows) {
                column.push_back(index < row.size() ? row[index] : "");
            }

            return make_success_value(make_string_array(column));
        })
        // Extract one data row (0-based) from a Csv.Table as a header-keyed
        // dictionary<string> (short rows padded with "").  Fails on out-of-bounds.
        .func("row", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto table = read_table(args[0]);

            if (!table) {
                return failure_msg("Csv", "row", "expected a Csv.Table record",
                                   error_codes::type_mismatch);
            }

            const auto index = expect_integer(args[1], "Csv.row", loc);
            const auto count = static_cast<std::int64_t>(table->rows.size());

            if (index < 0 || index >= count) {
                return failure_msg(
                    "Csv", "row", std::format("row index {} out of bounds (size {})", index, count),
                    error_codes::index_out_of_bounds);
            }

            return make_success_value(
                row_to_record(table->headers, table->rows[static_cast<std::size_t>(index)]));
        })
        // Project a subset of columns (in the given order) into a new Csv.Table.
        // Fails with not_found if any requested name is absent from the header.
        .func("select", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto table = read_table(args[0]);

            if (!table) {
                return failure_msg("Csv", "select", "expected a Csv.Table record",
                                   error_codes::type_mismatch);
            }

            const auto& names_arr = expect_array(args[1], "Csv.select", loc);

            std::vector<std::string> names;
            std::vector<std::size_t> indices;
            names.reserve(names_arr->elements->size());
            indices.reserve(names_arr->elements->size());

            for (const auto& name_val : *names_arr->elements) {
                const auto& name = name_val.to_string();

                bool found{false};
                for (std::size_t i{0}; i < table->headers.size(); ++i) {
                    if (table->headers[i] == name) {
                        indices.push_back(i);
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    return failure_msg("Csv", "select", std::format("no column named '{}'", name),
                                       error_codes::not_found);
                }

                names.push_back(name);
            }

            std::vector<std::vector<std::string>> rows;
            rows.reserve(table->rows.size());

            for (const auto& row : table->rows) {
                std::vector<std::string> projected;
                projected.reserve(indices.size());
                for (const auto idx : indices) {
                    projected.push_back(idx < row.size() ? row[idx] : std::string{});
                }
                rows.push_back(std::move(projected));
            }

            return make_success_value(make_table_record(names, rows));
        })
        // Keep only the data rows for which fn(row-record) returns true,
        // preserving the headers.  Fails if the predicate throws.
        .func("filter_rows", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto table = read_table(args[0]);

            if (!table) {
                return failure_msg("Csv", "filter_rows", "expected a Csv.Table record",
                                   error_codes::type_mismatch);
            }

            expect_callable(args[1], "Csv.filter_rows", loc);

            return apply_with_error_handling([&]() -> Value {
                std::vector<std::vector<std::string>> kept;
                std::vector<Value> call_args(1);

                for (const auto& row : table->rows) {
                    call_args[0] = row_to_record(table->headers, row);
                    if (invoke_callable(args[1], call_args, loc).is_truthy()) {
                        kept.push_back(row);
                    }
                }

                return make_table_record(table->headers, kept);
            });
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
