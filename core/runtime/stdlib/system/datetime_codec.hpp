#ifndef LUMA_STDLIB_DATETIME_CODEC_HPP
#define LUMA_STDLIB_DATETIME_CODEC_HPP

#include <ctime>
#include <optional>
#include <string>
#include <string_view>

// Pure ISO-8601 date/time codec, free of any Value or environment dependencies.
// parse_iso8601 is the trust-boundary decoder behind DateTime.from_iso_string —
// a hand-written parser that consumes untrusted text read from Luma strings.
// It is exposed in this header — rather than kept file-local in
// datetime_module.cpp — so the fuzz target (fuzz/fuzz_datetime.cpp) can drive it
// directly, mirroring how the CSV (csv_codec.hpp) and JSON (shared/json) parsers
// are fuzzed.
//
// parse_iso8601 never throws: malformed input yields an IsoParseResult with
// success == false and a human-readable error.  format_iso8601 is the matching
// encoder; the canonical "...Z" text it produces always re-parses to the same
// instant, so parse_iso8601(format_iso8601(t)) == t for any in-range t.

namespace luma::datetime {

// Machine-readable category for a parse_iso8601 failure, so callers (notably
// DateTime.from_iso_string_typed) can branch on the cause instead of matching
// the human-readable `error` string.  None means success.
enum class IsoParseErrorKind {
    None,
    Empty,               // input was empty or only whitespace
    InvalidFormat,       // present but not a well-formed ISO-8601 string
    OutOfRange,          // well-formed shape but an impossible field (month 13, …)
    UnsupportedPrecision // valid shape but a precision this parser does not accept
};

// Outcome of parse_iso8601.  On success, unix_seconds holds the UTC Unix
// timestamp.  On failure, success is false, error explains why, and error_kind
// categorises it.
struct IsoParseResult {
    double unix_seconds{0.0};
    bool success{false};
    std::string error;
    IsoParseErrorKind error_kind{IsoParseErrorKind::None};
};

// Parse an ISO-8601 timestamp into a UTC Unix timestamp (seconds since epoch).
// Accepts "YYYY-MM-DD", an optional "THH:MM:SS" time portion, and an optional
// "Z" / "+HH:MM" / "-HH:MM" zone suffix; a zone offset is converted to UTC.
// Years are restricted to the module's supported 0001-9999 calendar range.
// Never throws; malformed or out-of-range input reports success == false.
[[nodiscard]] IsoParseResult parse_iso8601(std::string_view text);

// Render a UTC Unix timestamp as canonical "YYYY-MM-DDTHH:MM:SSZ".
// Returns std::nullopt when the timestamp falls outside the supported
// 0001-9999 range.
[[nodiscard]] std::optional<std::string> format_iso8601(double unix_seconds);

// Render an already broken-down UTC time as "YYYY-MM-DDTHH:MM:SS<suffix>",
// appending `suffix` verbatim (e.g. "Z" or a "+HH:MM" / "-HH:MM" zone).  This
// is the single std::tm -> ISO-text renderer shared by format_iso8601 (which
// passes "Z") and DateTime.to_iso_string_offset (which passes a computed zone).
[[nodiscard]] std::string format_iso8601_with_suffix(const std::tm& tm, std::string_view suffix);

} // namespace luma::datetime

#endif // LUMA_STDLIB_DATETIME_CODEC_HPP
