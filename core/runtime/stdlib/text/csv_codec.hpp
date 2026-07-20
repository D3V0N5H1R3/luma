#ifndef LUMA_STDLIB_CSV_CODEC_HPP
#define LUMA_STDLIB_CSV_CODEC_HPP

#include <string>
#include <vector>

// Pure CSV codec layer, free of any Value or environment dependencies.  The
// parser here is the trust-boundary decoder behind Csv.deserialize /
// Csv.read_file and friends — a hand-written RFC 4180 state machine that
// consumes untrusted text read from strings and files.  It is exposed in this
// header — rather than kept file-local in csv_module.cpp — so the fuzz target
// (fuzz/fuzz_csv.cpp) can drive it directly, mirroring how the JSON parser,
// bytecode deserializer and compression codec are fuzzed.
//
// parse_csv never throws: malformed input yields a ParseResult with
// success == false and a human-readable error.  serialize_rows is the matching
// encoder and quotes any field containing the delimiter, quote, CR or LF so
// that the result re-parses to the same rows.

namespace luma::csv {

// Parser/serialiser configuration.  A single-character delimiter and quote
// cover the RFC 4180 dialects the Csv module supports.
struct CsvOptions {
    char delimiter{','};
    char quote{'"'};
};

// Outcome of parse_csv.  On success, rows holds one vector of fields per record.
// On failure, success is false, error explains why, and rows holds whatever was
// decoded before the failure.
struct ParseResult {
    std::vector<std::vector<std::string>> rows;
    bool success{true};
    std::string error;
};

// Parse CSV text into rows of fields following RFC 4180 quoting rules.  Never
// throws; reports malformed input (e.g. an unterminated quoted field) and the
// row-count resource limit through ParseResult::success.
[[nodiscard]] ParseResult parse_csv(const std::string& input, const CsvOptions& opts);

// Serialise rows back to CSV text, quoting fields as required by opts.  Each row
// is terminated with a single '\n'.
[[nodiscard]] std::string serialize_rows(const std::vector<std::vector<std::string>>& rows,
                                         const CsvOptions& opts);

} // namespace luma::csv

#endif // LUMA_STDLIB_CSV_CODEC_HPP
