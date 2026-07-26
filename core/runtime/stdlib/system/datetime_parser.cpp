#include <cctype>
#include <charconv>
#include <cstddef>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "runtime/stdlib/system/datetime_codec.hpp"
#include "runtime/stdlib/system/datetime_internal.hpp"

namespace luma::datetime {

using namespace datetime_detail;

namespace {

// Parse a run of ASCII digits at `pos` as an int (variable width, matching the
// previous stream-extraction semantics), advancing `pos` past the digits.
// Returns false on no digits or integer overflow, so hostile input such as
// "99999999999999999999-01-01" is rejected without throwing.
[[nodiscard]] bool parse_int_field(std::string_view text, std::size_t& pos, int& out_value) {
    const char* const first = text.data() + pos;
    const char* const last = text.data() + text.size();

    int value{0};
    const auto [ptr, ec] = std::from_chars(first, last, value);

    if (ec != std::errc{}) {
        return false;
    }

    pos = static_cast<std::size_t>(ptr - text.data());
    out_value = value;
    return true;
}

// Consume a single expected character at `pos`, advancing only on a match.
[[nodiscard]] bool consume(std::string_view text, std::size_t& pos, char expected) {
    if (pos < text.size() && text[pos] == expected) {
        ++pos;
        return true;
    }

    return false;
}

// Advance `pos` past any ASCII whitespace.
void skip_whitespace(std::string_view text, std::size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
    }
}

} // namespace

IsoParseResult parse_iso8601(std::string_view text) {
    IsoParseResult out;

    int year{0};
    int month{0};
    int day{0};
    int hour{0};
    int min{0};
    int sec{0};

    // Parse ISO 8601: YYYY-MM-DD or YYYY-MM-DDTHH:MM:SS with an optional timezone
    // suffix.  Hand-rolled over the string_view with std::from_chars to avoid
    // constructing a heavyweight std::istringstream (and copying `text`) for this
    // fixed, simple grammar.
    std::size_t pos{0};
    skip_whitespace(text, pos);

    // Empty or whitespace-only input is distinct from a malformed-but-present
    // string, so classify it up front (pos now sits past any leading spaces).
    if (pos == text.size()) {
        out.error = "empty ISO string";
        out.error_kind = IsoParseErrorKind::Empty;
        return out;
    }

    if (!parse_int_field(text, pos, year) || !consume(text, pos, '-') ||
        !parse_int_field(text, pos, month) || !consume(text, pos, '-') ||
        !parse_int_field(text, pos, day)) {
        out.error = "cannot parse ISO string";
        out.error_kind = IsoParseErrorKind::InvalidFormat;
        return out;
    }

    // Optionally parse the time portion after 'T'.
    if (pos < text.size() && (text[pos] == 'T' || text[pos] == 't')) {
        ++pos;

        if (!parse_int_field(text, pos, hour) || !consume(text, pos, ':') ||
            !parse_int_field(text, pos, min) || !consume(text, pos, ':') ||
            !parse_int_field(text, pos, sec)) {
            out.error = "cannot parse time in ISO string";
            out.error_kind = IsoParseErrorKind::InvalidFormat;
            return out;
        }

        // A fractional-second component (e.g. "...:00.5") is a well-formed shape
        // this parser deliberately does not support, so report it as an explicit
        // precision failure rather than lumping it in with generic format errors.
        if (pos < text.size() && text[pos] == '.') {
            out.error = "sub-second precision is not supported";
            out.error_kind = IsoParseErrorKind::UnsupportedPrecision;
            return out;
        }
    }

    // Parse optional timezone suffix: Z, +HH:MM, or -HH:MM.
    double offset_seconds = 0.0;

    if (pos < text.size()) {
        const char zone = text[pos];

        if (zone == 'Z' || zone == 'z') {
            ++pos; // consume 'Z'
        } else if (zone == '+' || zone == '-') {
            ++pos; // consume sign

            int off_h{0};
            int off_m{0};

            if (!parse_int_field(text, pos, off_h) || !consume(text, pos, ':') ||
                !parse_int_field(text, pos, off_m)) {
                out.error = "cannot parse timezone offset in ISO string";
                out.error_kind = IsoParseErrorKind::InvalidFormat;
                return out;
            }

            offset_seconds = ((off_h * 3600.0) + (off_m * 60.0));

            if (zone == '-') {
                offset_seconds = -offset_seconds;
            }
        }
    }

    // Reject any unconsumed trailing characters (e.g. "2020-01-01xyz") so a
    // malformed string is not silently accepted as a valid prefix.  Trailing
    // whitespace is tolerated, matching the previous `iss >> std::ws` behaviour.
    skip_whitespace(text, pos);

    if (pos != text.size()) {
        out.error = "unexpected trailing characters in ISO string";
        out.error_kind = IsoParseErrorKind::InvalidFormat;
        return out;
    }

    // Validate every field explicitly rather than letting timegm/_mkgmtime
    // silently normalise out-of-range components (e.g. 2020-02-30 -> 2020-03-01,
    // or 25:00 -> the next day).  This mirrors DateTime.from_parts'
    // validate_datetime_fields so both construction paths reject the same
    // invalid dates; the year/month bounds also keep the (year - 1900)
    // conversion from signed-overflowing on adversarial input.
    if (year < 1 || year > 9999) {
        out.error = "date out of supported range (year 0001-9999)";
        out.error_kind = IsoParseErrorKind::OutOfRange;
        return out;
    }

    if (month < 1 || month > 12) {
        out.error = "month out of range (1-12)";
        out.error_kind = IsoParseErrorKind::OutOfRange;
        return out;
    }

    if (day < 1 || day > days_in_month_for(month, year)) {
        out.error = "day out of range for month";
        out.error_kind = IsoParseErrorKind::OutOfRange;
        return out;
    }

    if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        out.error = "time field out of range";
        out.error_kind = IsoParseErrorKind::OutOfRange;
        return out;
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

    const auto unix_time = tm_to_unix(tm);

    if (!unix_time) {
        out.error = "cannot parse ISO string";
        out.error_kind = IsoParseErrorKind::OutOfRange;
        return out;
    }

    // Subtract the offset to convert from local time to UTC.
    out.unix_seconds = *unix_time - offset_seconds;
    out.success = true;
    return out;
}

} // namespace luma::datetime
