// Pure RFC 4180 CSV codec implementation.
//
// Free of any Value or environment dependencies (see csv_codec.hpp): parse_csv
// is the trust-boundary decoder behind Csv.deserialize / Csv.read_file and
// serialize_rows is its matching encoder.  Exposed so fuzz/fuzz_csv.cpp can
// drive parse_csv directly.  The Value-facing Csv module registration lives in
// csv_module.cpp, mirroring the json parser/serializer split.

#include "runtime/stdlib/text/csv_codec.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "common/resource_limits.hpp"

namespace luma::csv {

namespace {

// Returns the index of the first character that forces the field to be quoted
// (delimiter, quote, CR, or LF), or std::string::npos when the field needs no
// quoting.  Returning the position — rather than a bool — lets serialize_field
// bulk-copy the clean prefix instead of re-scanning it character by character.
[[nodiscard]] std::size_t first_quote_trigger(const std::string& field, const CsvOptions& opts) {
    for (std::size_t i = 0; i < field.size(); ++i) {
        const auto c = field[i];

        if (c == opts.delimiter || c == opts.quote || c == '\n' || c == '\r') {
            return i;
        }
    }

    return std::string::npos;
}

void serialize_field(const std::string& field, const CsvOptions& opts, std::string& out) {
    const auto trigger = first_quote_trigger(field, opts);

    if (trigger == std::string::npos) {
        out += field;

        return;
    }

    out += opts.quote;

    // Every character before the first trigger is an ordinary (non-quote)
    // character, so bulk-copy that prefix instead of re-scanning it; only the
    // remainder needs per-character quote doubling.
    out.append(field, 0, trigger);

    for (std::size_t i = trigger; i < field.size(); ++i) {
        const auto c = field[i];

        if (c == opts.quote) {
            out += opts.quote;
        }

        out += c;
    }

    out += opts.quote;
}

} // namespace

ParseResult parse_csv(const std::string& input, const CsvOptions& opts) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> current_row;
    std::string field;
    bool in_quotes{false};

    // Finalise the current row and append to rows.  Returns false if the row
    // limit has been exceeded.
    auto finalize_row = [&]() -> bool {
        current_row.push_back(std::move(field));
        field.clear();

        if (rows.size() >= ResourceLimits::max_array_size) {
            return false;
        }

        rows.push_back(std::move(current_row));
        current_row.clear();

        return true;
    };

    // Byte offset where an unterminated quoted field opened, so a failure at end
    // of input points at the offending quote rather than the (past-the-end) cursor.
    std::size_t quote_start{0};

    for (std::size_t i{0}; i < input.size(); ++i) {
        const char c = input[i];

        if (in_quotes) {
            if (c == opts.quote) {
                if (i + 1 < input.size() && input[i + 1] == opts.quote) {
                    // Escaped quote.
                    field += opts.quote;

                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == opts.quote) {
                in_quotes = true;
                quote_start = i;
            } else if (c == opts.delimiter) {
                current_row.push_back(std::move(field));

                field.clear();

                // Bound the field count per row: without a newline the row cap
                // in finalize_row is never reached, so a single line of nothing
                // but delimiters would otherwise grow current_row without limit.
                if (current_row.size() >= ResourceLimits::max_array_size) {
                    return {.rows = std::move(rows),
                            .success = false,
                            .error = "CSV has too many fields",
                            .error_offset = i};
                }
            } else if (c == '\r') {
                if (!finalize_row()) {
                    return {.rows = std::move(rows),
                            .success = false,
                            .error = "CSV has too many rows",
                            .error_offset = i};
                }

                // Skip \n if it follows (CRLF pair).
                if (i + 1 < input.size() && input[i + 1] == '\n') {
                    ++i;
                }

                continue;
            } else if (c == '\n') {
                if (!finalize_row()) {
                    return {.rows = std::move(rows),
                            .success = false,
                            .error = "CSV has too many rows",
                            .error_offset = i};
                }
            } else {
                field += c;
            }
        }
    }

    // Handle last field/row.
    if (!field.empty() || !current_row.empty()) {
        current_row.push_back(std::move(field));

        rows.push_back(std::move(current_row));
    }

    if (in_quotes) {
        return {.rows = std::move(rows),
                .success = false,
                .error = "unterminated quoted field",
                .error_offset = quote_start};
    }

    return {.rows = std::move(rows), .success = true, .error = {}, .error_offset = 0};
}

std::string serialize_rows(const std::vector<std::vector<std::string>>& rows,
                           const CsvOptions& opts) {
    std::string out;

    for (const auto& row : rows) {
        for (std::size_t i{0}; i < row.size(); ++i) {
            if (i > 0) {
                out += opts.delimiter;
            }

            serialize_field(row[i], opts, out);
        }

        out += '\n';
    }

    return out;
}

} // namespace luma::csv
