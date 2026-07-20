#include <cstddef>
#include <cstdint>
#include <string>

#include "fuzz_harness.hpp"
#include "runtime/stdlib/text/csv_codec.hpp"

// LibFuzzer entry point for the Csv module's RFC 4180 codec
// (core/runtime/stdlib/text/csv_codec.hpp).
//
// parse_csv is the trust-boundary parser behind Csv.deserialize,
// Csv.deserialize_records, Csv.read_file, Csv.header and Csv.count_rows — a
// hand-written state machine that walks untrusted text read from strings and
// files, tracking quote state, escaped quotes, and CR / LF / CRLF row
// terminators by hand.  Arbitrary bytes must never crash it, read out of
// bounds, or exhaust memory.  This sits alongside the JSON parser, bytecode
// deserializer and compression codec as a directly-fuzzed decoder, rather than
// being reached only indirectly through the VM by fuzz_structured.
//
// One oracle runs on top of the never-crash contract:
//   * Idempotence on the parser's own output: any rows parse_csv accepts must
//     serialise to text that re-parses to identical rows
//     (parse(serialize(rows)) == rows).  serialize_rows quotes every field
//     containing the delimiter, quote, CR or LF, so the round-trip is exact.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            using namespace luma::csv;

            const CsvOptions default_opts;

            // ── Never-crash: arbitrary bytes are parsed or rejected, never crash.
            const auto first = parse_csv(input, default_opts);
            luma::fuzz::do_not_optimize(first.success);

            // Drive the option-dependent branches with a distinct delimiter and
            // quote so non-default quoting paths are exercised too.
            CsvOptions alt_opts;
            alt_opts.delimiter = ';';
            alt_opts.quote = '\'';
            const auto alt = parse_csv(input, alt_opts);
            luma::fuzz::do_not_optimize(alt.success);

            // ── Oracle: re-serialising accepted rows must round-trip exactly.
            // The fuzz input cap (max_input_size) is far below the parser's
            // row-count limit (ResourceLimits::max_array_size), so a successful
            // parse here cannot be re-rejected by that limit on the second pass.
            if (first.success) {
                const auto text = serialize_rows(first.rows, default_opts);
                const auto second = parse_csv(text, default_opts);

                if (!second.success || second.rows != first.rows) {
                    luma::fuzz::trap(); // serialise → parse is not a faithful round-trip.
                }
            }
        });
}
