#include <cstddef>
#include <cstdint>
#include <string>

#include "fuzz_harness.hpp"
#include "runtime/stdlib/io/graphicalui_css.hpp"

// LibFuzzer entry point for the GraphicalUi CSS sanitisation trust boundary.
//
// sanitise_loaded_css (core/runtime/stdlib/io/graphicalui_css.hpp) is the
// allowlist-based filter behind GraphicalUi.load_stylesheet: it tokenises an
// untrusted, user-loaded stylesheet by hand — skipping HTML tags and CSS
// comments, validating at-rules against an allowlist, dropping unknown CSS
// functions, and rejecting unsafe url() schemes — using manual index
// arithmetic (substr, find, fn_end + 1, balanced-paren / balanced-brace
// counting). Arbitrary bytes interpreted as CSS must never crash it, read out
// of bounds, or exhaust memory. The sibling property helpers is_known_css_property
// and suggest_css_property walk arbitrary property keys (the latter runs a
// Levenshtein scan with substr slicing), so they are exercised on the same
// untrusted input. This sits alongside the CSV, path, compression and encoder
// targets as a directly-fuzzed decoder rather than being reached only
// indirectly through the VM by fuzz_structured.
//
// One oracle runs on top of the never-crash contract:
//   * Length monotonicity: the sanitiser only ever drops bytes — every byte it
//     appends is a distinct byte copied from the input, never a synthesised
//     one — so the result can never be longer than the input. A second pass
//     over the sanitiser's own output must likewise not grow it. Any growth is
//     a genuine defect (double-append or synthesised output) that this cheap,
//     sound check pins down without relying on idempotence, which the
//     allowlist tokeniser does not guarantee for constructs such as
//     "@safe-rule(...)".
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            using namespace luma::gui_detail;

            // ── Never-crash: arbitrary bytes are sanitised, never crash. ──
            const auto once = sanitise_loaded_css(input);

            // ── Oracle: the sanitiser only drops bytes, so output never grows. ──
            if (once.size() > input.size()) {
                luma::fuzz::trap(); // sanitiser synthesised or duplicated output.
            }

            // Re-running the sanitiser on its own (already-safe) output must also
            // never crash and never grow it.
            const auto twice = sanitise_loaded_css(once);

            if (twice.size() > once.size()) {
                luma::fuzz::trap(); // second pass grew already-sanitised output.
            }

            // ── Never-crash: the property-name helpers walk arbitrary keys. ──
            // is_known_css_property classifies reserved keys, pseudo-class prefixes
            // and custom properties; suggest_css_property runs a Levenshtein scan
            // with substr slicing over the same untrusted bytes.
            luma::fuzz::do_not_optimize(is_known_css_property(input));
            luma::fuzz::do_not_optimize(suggest_css_property(input));
        });
}
