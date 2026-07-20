#include <cstddef>
#include <cstdint>
#include <string>

#include "fuzz_harness.hpp"
#include "runtime/stdlib/io/http_url_parser.hpp"

// LibFuzzer entry point for the Http module's URL parser
// (core/runtime/stdlib/io/http_url_parser.hpp).
//
// parse_url is the trust-boundary parser behind Http.parse_url and every
// Http.get / Http.post / ... request: it splits an untrusted URL string into
// scheme, host, port, path and query entirely by hand — lowercasing the scheme,
// honouring an optional "://", carving the authority off at the first '/',
// pulling a query off at the first '?', and reading a port with std::stoi
// (guarded against std::invalid_argument / std::out_of_range), including the
// bracketed-IPv6 "[2001:db8::1]:8080" form.  It sits alongside the JSON, CSV,
// datetime and encoder decoders as a directly-fuzzed parser rather than being
// reached only indirectly through the VM by fuzz_structured.  Arbitrary bytes
// must never crash it, read out of bounds, or exhaust memory.
//
// One oracle runs on top of the never-crash contract:
//   * Idempotence: re-serialising the parsed parts into a canonical URL and
//     parsing again must reach a fixpoint.  parse_url normalises its input — it
//     omits a port equal to the scheme default, collapses a missing path to
//     "/", and folds an unparseable port back to that default — so the first
//     normalisation round may legitimately change the fields, but a second round
//     over the already-canonical string must reproduce them exactly.  Comparing
//     the second and third parses (rather than the raw first one) keeps the
//     oracle robust against that one-shot normalisation while still flagging any
//     genuine non-convergence in the parser.
namespace {

// Re-serialise parsed parts into a canonical "scheme://host[:port]path[?query]"
// URL.  A port equal to the scheme default is omitted so the form matches what
// parse_url itself normalises to.
[[nodiscard]] std::string reconstruct(const luma::ParsedUrl& u) {
    std::string out = u.scheme + "://" + u.host;

    if (u.port != luma::default_port_for_scheme(u.scheme)) {
        out += ":" + std::to_string(u.port);
    }

    out += u.path;

    if (!u.query.empty()) {
        out += "?" + u.query;
    }

    return out;
}

[[nodiscard]] bool same_parts(const luma::ParsedUrl& a, const luma::ParsedUrl& b) {
    return a.scheme == b.scheme && a.host == b.host && a.port == b.port && a.path == b.path &&
           a.query == b.query;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            // ── Never-crash: arbitrary bytes are parsed into parts, never crash.
            const auto p1 = luma::parse_url(input);
            luma::fuzz::do_not_optimize(p1.port);

            // ── Oracle: parse → reconstruct → parse reaches a fixpoint.
            const auto p2 = luma::parse_url(reconstruct(p1));
            const auto p3 = luma::parse_url(reconstruct(p2));

            if (!same_parts(p2, p3)) {
                luma::fuzz::trap(); // parse → reconstruct → parse does not converge.
            }
        });
}
