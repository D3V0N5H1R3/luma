#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "fuzz_harness.hpp"
#include "runtime/stdlib/collections/keyvaluestore_codec.hpp"

// LibFuzzer entry point for the KeyValueStore codec
// (core/runtime/stdlib/collections/keyvaluestore_codec.hpp).
//
// parse_store is the trust-boundary parser behind KeyValueStore.open,
// KeyValueStore.open_read_only and KeyValueStore.reload (via read_store): a
// hand-written reader that walks untrusted `.kv` file content one line at a
// time, splitting each record on the first tab and unescaping the key and value
// with the matching escape/unescape codec.  glob_match is the hand-written
// pattern matcher behind KeyValueStore.find_by_pattern, walking an untrusted
// glob pattern against every stored key.  Arbitrary bytes fed to any of these
// must never crash, read out of bounds, or exhaust memory; content that exceeds
// the dictionary-size or string-size resource limits raises luma::RuntimeError,
// which the shared harness treats as the expected outcome for hostile input.
// fuzz_structured only reaches these routines shallowly through the VM, so this
// direct target exercises the parser and codec far more deeply, mirroring how
// fuzz_csv, fuzz_compression and fuzz_datetime drive their codecs.
//
// Two oracles run on top of the never-crash contract:
//   * Codec inverse: unescape(escape(input)) == input for every byte string,
//     because escape only ever expands '\t', '\n' and '\\' into two-character
//     sequences that unescape collapses back exactly.
//   * Parser idempotence on its own output: any entries parse_store accepts
//     must serialise to text that re-parses to identical entries
//     (parse_store(serialize_store(entries)) == entries).  escape renders the
//     tab, newline and backslash bytes harmless, so each record stays on one
//     line and the first literal tab remains the key/value separator.  The
//     fuzz input cap (max_input_size) is far below the dictionary-size and
//     string-size limits, so a successful first parse cannot be re-rejected by
//     a limit on the second pass.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            using namespace luma::kvs;

            // ── Oracle: the escape codec is exactly invertible. ──
            if (unescape(escape(input)) != input) {
                luma::fuzz::trap(); // escape/unescape is not a faithful round-trip.
            }

            // ── Never-crash: arbitrary bytes are parsed, rejected (RuntimeError),
            //    or skipped line-by-line, but never crash.
            const auto entries = parse_store(input);
            luma::fuzz::do_not_optimize(entries.size());

            // ── Oracle: re-serialising accepted entries must round-trip exactly.
            const auto text = serialize_store(entries);
            const auto reparsed = parse_store(text);

            if (reparsed != entries) {
                luma::fuzz::trap(); // serialise → parse is not a faithful round-trip.
            }

            // ── Never-crash: the glob matcher over untrusted pattern and text.
            // Split the input so the pattern and text differ, then also match the
            // whole input against itself to exercise the all-literal fast path.
            const std::string_view view{input};
            const auto mid = view.size() / 2;
            const auto pattern = view.substr(0, mid);
            const auto candidate = view.substr(mid);

            luma::fuzz::do_not_optimize(glob_match(pattern, candidate));
            luma::fuzz::do_not_optimize(glob_match(candidate, pattern));
            luma::fuzz::do_not_optimize(glob_match(view, view));
        });
}
