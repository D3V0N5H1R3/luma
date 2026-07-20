// Standard library tests: DateTime.

#include <cmath>
#include <limits>

#include "runtime/stdlib/system/datetime_codec.hpp"
#include "stdlib_test_helpers.hpp"

static void test_datetime_add_days() {
    const auto v = eval("DateTime.add_days(1710497400.0, 1.0)");

    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_number(), 1710497400.0 + 86400.0);
}

static void test_datetime_add_hours() {
    const auto v = eval("DateTime.add_hours(1710497400.0, 2.0)");

    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_number(), 1710497400.0 + 7200.0);
}

static void test_datetime_add_months() {
    // Adding 1 month to 2024-03-15 should give 2024-04-15.
    const auto v = eval("DateTime.add_months(1710497400.0, 1)");

    ASSERT_RESULT_SUCCESS(v);
    // Verify the resulting timestamp is larger.
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() > 1710497400.0);
}

static void test_datetime_add_seconds() {
    const auto v = eval("DateTime.add_seconds(1710497400.0, 100.0)");

    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_number(), 1710497500.0);
}

static void test_datetime_add_years() {
    // Adding 1 year to 2024-03-15 should give 2025-03-15.
    const auto v = eval("DateTime.add_years(1710497400.0, 1)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() > 1710497400.0);
}

static void test_datetime_days_in_month() {
    ASSERT_EVAL_INT("DateTime.days_in_month(2024, 2)", 29);

    ASSERT_EVAL_INT("DateTime.days_in_month(2023, 2)", 28);
}

static void test_datetime_days_in_month_invalid() {
    ASSERT_EVAL_FAILURE("DateTime.days_in_month(2024, 13)");

    ASSERT_EVAL_FAILURE("DateTime.days_in_month(2024, 0)");
}

static void test_datetime_difference_days() {
    // 86400 seconds = 1 day.
    const auto v = eval("DateTime.difference_days(1710583800.0, 1710497400.0)");

    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_number(), 1.0);
}

static void test_datetime_difference_hours() {
    // 3600 seconds = 1 hour.
    const auto v = eval("DateTime.difference_hours(1710501000.0, 1710497400.0)");

    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_number(), 1.0);
}

static void test_datetime_difference_months() {
    // 2024-03-15 and 2024-01-15 differ by 2 months.
    ASSERT_EVAL_INT("DateTime.difference_months(1710497400.0, 1705276200.0)", 2);
}

static void test_datetime_difference_seconds() {
    // 1710497400 and 1710497500 differ by 100 seconds.
    const auto v = eval("DateTime.difference_seconds(1710497500.0, 1710497400.0)");

    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_number(), 100.0);
}

static void test_datetime_difference_years() {
    // 2024 and 2020 differ by about 4 years.
    ASSERT_EVAL_INT("DateTime.difference_years(1710497400.0, 1584268200.0)", 4);
}

static void test_datetime_from_iso_string_negative_offset() {
    // 2024-03-15T09:30:00-05:00 → 2024-03-15T14:30:00Z = 1710513000.
    ASSERT_EVAL_NUM(R"(DateTime.from_iso_string("2024-03-15T09:30:00-05:00"))", 1710513000.0);
}

static void test_datetime_from_iso_string_positive_offset() {
    // 2024-03-15T20:00:00+05:30 → 2024-03-15T14:30:00Z = 1710513000.
    ASSERT_EVAL_NUM(R"(DateTime.from_iso_string("2024-03-15T20:00:00+05:30"))", 1710513000.0);
}

static void test_datetime_from_iso_string_utc() {
    // Plain date-only (existing behaviour).
    const auto v1 = eval(R"(DateTime.from_iso_string("2024-03-15"))");

    ASSERT_RESULT_SUCCESS(v1);

    // Date + time + Z suffix.
    ASSERT_EVAL_NUM(R"(DateTime.from_iso_string("2024-03-15T14:30:00Z"))", 1710513000.0);
}

static void test_datetime_from_offset() {
    // Inverse of to_offset: given a local timestamp in UTC+2, recover UTC.
    ASSERT_EVAL_NUM("DateTime.from_offset(1710520200.0, 120.0)", 1710520200.0 - 7200.0);
}

static void test_datetime_from_parts_offset() {
    // Build 2024-03-15 20:00:00 at UTC+05:30 → should equal
    // 2024-03-15 14:30:00 UTC = 1710513000.
    ASSERT_EVAL_NUM("DateTime.from_parts_offset(2024, 3, 15, 20, 0, 0, 330.0)", 1710513000.0);
}

static void test_datetime_is_leap_year() {
    ASSERT_EQ(eval("DateTime.is_leap_year(2024)").as_bool(), true);
    ASSERT_EQ(eval("DateTime.is_leap_year(2023)").as_bool(), false);
}

static void test_datetime_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("DateTime.milliseconds_since_start"));
    ASSERT_TRUE(env->has("DateTime.now_unix"));
    ASSERT_TRUE(env->has("DateTime.now_iso_string"));
    ASSERT_TRUE(env->has("DateTime.year"));
    ASSERT_TRUE(env->has("DateTime.month"));
    ASSERT_TRUE(env->has("DateTime.day_of_month"));
    ASSERT_TRUE(env->has("DateTime.is_leap_year"));
    ASSERT_TRUE(env->has("DateTime.to_iso_string"));
    ASSERT_TRUE(env->has("DateTime.difference_seconds"));
    ASSERT_TRUE(env->has("DateTime.difference_hours"));
    ASSERT_TRUE(env->has("DateTime.difference_days"));
    ASSERT_TRUE(env->has("DateTime.difference_months"));
    ASSERT_TRUE(env->has("DateTime.difference_years"));
    ASSERT_TRUE(env->has("DateTime.add_seconds"));
    ASSERT_TRUE(env->has("DateTime.add_hours"));
    ASSERT_TRUE(env->has("DateTime.add_days"));
    ASSERT_TRUE(env->has("DateTime.add_months"));
    ASSERT_TRUE(env->has("DateTime.add_years"));
}

static void test_datetime_now_iso_string() {
    const auto v = eval("DateTime.now_iso_string()");

    ASSERT_RESULT_SUCCESS(v);
    // Should contain a 'T' separator.
    ASSERT_TRUE(v.as_result()->owned_inner->as_string().find('T') != std::string::npos);
}

static void test_datetime_now_ms() {
    const auto v = eval("DateTime.milliseconds_since_start()");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 0);
}

static void test_datetime_offset_hours() {
    const auto v = eval("DateTime.offset_hours(5.5)");

    ASSERT_EQ(v.as_number(), 330.0);
}

static void test_datetime_to_iso_string_offset() {
    // 2024-03-15 14:30:00 UTC formatted with +05:30 offset.
    // 14:30 + 5:30 = 20:00, so expect "2024-03-15T20:00:00+05:30".
    ASSERT_EVAL_STR("DateTime.to_iso_string_offset(1710513000.0, 330.0)",
                    "2024-03-15T20:00:00+05:30");
}

static void test_datetime_to_iso_string_offset_negative() {
    // 2024-03-15 14:30:00 UTC formatted with -05:00 offset.
    // 14:30 - 5:00 = 09:30, so expect "2024-03-15T09:30:00-05:00".
    ASSERT_EVAL_STR("DateTime.to_iso_string_offset(1710513000.0, -300.0)",
                    "2024-03-15T09:30:00-05:00");
}

static void test_datetime_to_iso_string_offset_zero() {
    // Zero offset should produce "Z" suffix.
    ASSERT_EVAL_STR("DateTime.to_iso_string_offset(1710513000.0, 0.0)", "2024-03-15T14:30:00Z");
}

static void test_datetime_to_iso_string_valid() {
    const auto v = eval("DateTime.to_iso_string(1710497400.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_string().find("2024") != std::string::npos);
}

static void test_datetime_to_offset() {
    // UTC+2:00 = 120 minutes. 2024-03-15 14:30:00 UTC = 1710513000.
    // Adjusted timestamp is 2 hours ahead.
    ASSERT_EVAL_NUM("DateTime.to_offset(1710513000.0, 120.0)", 1710513000.0 + 7200.0);
}

static void test_datetime_to_offset_invalid() {
    // Offset beyond valid range (> +840 minutes) should fail.
    ASSERT_EVAL_FAILURE("DateTime.to_offset(1710513000.0, 900.0)");
}

static void test_datetime_to_parts_valid() {
    const auto v = eval("DateTime.to_parts(1710497400.0)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_TRUE(inner.is_record());
    ASSERT_EQ(inner.as_record()->type_name, "TimeParts");
    ASSERT_TRUE(inner.as_record()->find_field("year") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("month") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("day") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("hour") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("minute") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("second") != nullptr);
}

static void test_datetime_year_out_of_range() {
    // Timestamp far before year 0001
    ASSERT_EVAL_FAILURE("DateTime.year(-99999999999.0)");
}

static void test_datetime_year_valid() {
    // Unix timestamp 1710497400 = 2024-03-15 10:10:00 UTC
    ASSERT_EVAL_INT("DateTime.year(1710497400.0)", 2024);
}

// ─── Additional positive component accessors ─────────────────────
// 1710497400 = 2024-03-15 10:10:00 UTC (a Friday).

static void test_datetime_month_valid() {
    ASSERT_EVAL_INT("DateTime.month(1710497400.0)", 3);
}

static void test_datetime_day_of_month_valid() {
    ASSERT_EVAL_INT("DateTime.day_of_month(1710497400.0)", 15);
}

static void test_datetime_hour_minute_second_valid() {
    const auto h = eval("DateTime.hour(1710497400.0)");
    const auto m = eval("DateTime.minute(1710497400.0)");
    const auto s = eval("DateTime.second(1710497400.0)");

    ASSERT_RESULT_SUCCESS(h);
    ASSERT_RESULT_SUCCESS(m);
    ASSERT_RESULT_SUCCESS(s);
    ASSERT_EQ(h.as_result()->owned_inner->as_integer(), 10);
    ASSERT_EQ(m.as_result()->owned_inner->as_integer(), 10);
    ASSERT_EQ(s.as_result()->owned_inner->as_integer(), 0);
}

static void test_datetime_day_of_week_valid() {
    // 2024-03-15 is a Friday → 5 (1 = Monday .. 7 = Sunday).
    ASSERT_EVAL_INT("DateTime.day_of_week(1710497400.0)", 5);
}

static void test_datetime_components_out_of_range() {
    // Every component accessor rejects a timestamp outside year 0001-9999.
    ASSERT_EVAL_FAILURE("DateTime.month(-99999999999.0)");
    ASSERT_EVAL_FAILURE("DateTime.day_of_month(-99999999999.0)");
    ASSERT_EVAL_FAILURE("DateTime.hour(-99999999999.0)");
    ASSERT_EVAL_FAILURE("DateTime.minute(-99999999999.0)");
    ASSERT_EVAL_FAILURE("DateTime.second(-99999999999.0)");
    ASSERT_EVAL_FAILURE("DateTime.day_of_week(-99999999999.0)");
}

static void test_datetime_from_parts_valid() {
    // 2024-03-15 10:10:00 UTC = 1710497400.
    ASSERT_EVAL_NUM("DateTime.from_parts(2024, 3, 15, 10, 10, 0)", 1710497400.0);
}

static void test_datetime_from_parts_invalid_fields() {
    ASSERT_EVAL_FAILURE("DateTime.from_parts(2024, 0, 1, 0, 0, 0)");  // month 0
    ASSERT_EVAL_FAILURE("DateTime.from_parts(2024, 13, 1, 0, 0, 0)"); // month 13
    ASSERT_EVAL_FAILURE("DateTime.from_parts(2024, 1, 0, 0, 0, 0)");  // day 0
    ASSERT_EVAL_FAILURE("DateTime.from_parts(2024, 1, 32, 0, 0, 0)"); // day 32
    ASSERT_EVAL_FAILURE("DateTime.from_parts(2024, 1, 1, 24, 0, 0)"); // hour 24
    ASSERT_EVAL_FAILURE("DateTime.from_parts(2024, 1, 1, 0, 60, 0)"); // minute 60
    ASSERT_EVAL_FAILURE("DateTime.from_parts(2024, 1, 1, 0, 0, 60)"); // second 60

    // Extreme years outside the supported 0001-9999 calendar range must fail
    // rather than signed-overflow the internal `year - 1900` conversion.
    ASSERT_EVAL_FAILURE("DateTime.from_parts(-2147483648, 1, 1, 0, 0, 0)");
    ASSERT_EVAL_FAILURE("DateTime.from_parts(10000, 1, 1, 0, 0, 0)");
}

static void test_datetime_from_parts_feb_29() {
    // Feb 29 is valid on a leap year, invalid otherwise.
    ASSERT_RESULT_SUCCESS(eval("DateTime.from_parts(2024, 2, 29, 0, 0, 0)"));
    ASSERT_EVAL_FAILURE("DateTime.from_parts(2023, 2, 29, 0, 0, 0)");
}

static void test_datetime_to_parts_values() {
    const auto v = eval("DateTime.to_parts(1710497400.0)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = *v.as_result()->owned_inner->as_record();

    ASSERT_EQ(rec.find_field("year")->as_integer(), 2024);
    ASSERT_EQ(rec.find_field("month")->as_integer(), 3);
    ASSERT_EQ(rec.find_field("day")->as_integer(), 15);
    ASSERT_EQ(rec.find_field("hour")->as_integer(), 10);
    ASSERT_EQ(rec.find_field("minute")->as_integer(), 10);
    ASSERT_EQ(rec.find_field("second")->as_integer(), 0);
}

static void test_datetime_to_parts_out_of_range() {
    ASSERT_EVAL_FAILURE("DateTime.to_parts(-99999999999.0)");
}

static void test_datetime_add_milliseconds() {
    const auto v = eval("DateTime.add_milliseconds(1000.0, 500)");

    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_number(), 1500.0);
}

static void test_datetime_difference_milliseconds() {
    const auto v = eval("DateTime.difference_milliseconds(1500.0, 1000.0)");

    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_number(), 500.0);
}

static void test_datetime_now_unix() {
    const auto v = eval("DateTime.now_unix()");

    ASSERT_TRUE(v.is_number());
    // Any moment after 2020-01-01 (1577836800) confirms a real clock reading.
    ASSERT_TRUE(v.as_number() > 1577836800.0);
}

static void test_datetime_format_valid() {
    ASSERT_EVAL_STR(R"(DateTime.format(1710497400.0, "YYYY-MM-DD hh:mm:ss"))",
                    "2024-03-15 10:10:00");
}

static void test_datetime_format_out_of_range() {
    ASSERT_EVAL_FAILURE(R"(DateTime.format(-99999999999.0, "YYYY"))");
}

static void test_datetime_is_before_after() {
    ASSERT_EQ(eval("DateTime.is_before(1000.0, 2000.0)").as_bool(), true);
    ASSERT_EQ(eval("DateTime.is_before(2000.0, 1000.0)").as_bool(), false);
    ASSERT_EQ(eval("DateTime.is_before(1000.0, 1000.0)").as_bool(), false);
    ASSERT_EQ(eval("DateTime.is_after(2000.0, 1000.0)").as_bool(), true);
    ASSERT_EQ(eval("DateTime.is_after(1000.0, 2000.0)").as_bool(), false);
    ASSERT_EQ(eval("DateTime.is_after(1000.0, 1000.0)").as_bool(), false);
}

static void test_datetime_to_iso_string_out_of_range() {
    ASSERT_EVAL_FAILURE("DateTime.to_iso_string(-99999999999.0)");
}

static void test_datetime_from_iso_string_date_time_components() {
    // Parse a full date-time and confirm each component round-trips.
    ASSERT_EVAL_INT(
        R"(DateTime.year(Result.unwrap(DateTime.from_iso_string("2024-03-15T14:30:45Z"))))", 2024);
    ASSERT_EVAL_INT(
        R"(DateTime.second(Result.unwrap(DateTime.from_iso_string("2024-03-15T14:30:45Z"))))", 45);
}

static void test_datetime_from_iso_string_invalid() {
    ASSERT_EVAL_FAILURE(R"(DateTime.from_iso_string("not-a-date"))");
    ASSERT_EVAL_FAILURE(R"(DateTime.from_iso_string(""))");
    ASSERT_EVAL_FAILURE(R"(DateTime.from_iso_string("2024/03/15"))");
    ASSERT_EVAL_FAILURE(R"(DateTime.from_iso_string("2024-03-15T14:30"))");
}

static void test_datetime_from_iso_string_year_out_of_range() {
    // Regression: extreme years must fail cleanly rather than overflow std::tm
    // (tm_year = year - 1900).  See datetime_codec.hpp.
    ASSERT_EVAL_FAILURE(R"(DateTime.from_iso_string("99999-01-01"))");
    ASSERT_EVAL_FAILURE(R"(DateTime.from_iso_string("0000-01-01"))");
}

static void test_datetime_from_parts_offset_invalid() {
    ASSERT_EVAL_FAILURE("DateTime.from_parts_offset(2024, 3, 15, 12, 0, 0, 900.0)"); // bad offset
    ASSERT_EVAL_FAILURE("DateTime.from_parts_offset(2024, 13, 15, 12, 0, 0, 60.0)"); // bad month
}

static void test_datetime_from_offset_invalid() {
    ASSERT_EVAL_FAILURE("DateTime.from_offset(1710513000.0, 900.0)");
}

static void test_datetime_to_iso_string_offset_invalid() {
    ASSERT_EVAL_FAILURE("DateTime.to_iso_string_offset(1710513000.0, 900.0)");
}

static void test_datetime_add_months_out_of_range() {
    // Pushing the year past 9999 must fail rather than produce an invalid date.
    ASSERT_EVAL_FAILURE("DateTime.add_months(1710497400.0, 2147483647)");
}

static void test_datetime_add_years_out_of_range() {
    ASSERT_EVAL_FAILURE("DateTime.add_years(1710497400.0, 2147483647)");
}

// ─── Direct ISO codec tests (datetime_codec.hpp) ─────────────────

static void test_iso_codec_parse_date_only() {
    const auto r = luma::datetime::parse_iso8601("2024-03-15");

    ASSERT_TRUE(r.success);
    // 2024-03-15 00:00:00 UTC = 1710460800.
    ASSERT_EQ(r.unix_seconds, 1710460800.0);
}

static void test_iso_codec_parse_with_zone() {
    // Z, positive and negative offsets all normalise to the same UTC instant.
    const auto z = luma::datetime::parse_iso8601("2024-03-15T14:30:00Z");
    const auto plus = luma::datetime::parse_iso8601("2024-03-15T20:00:00+05:30");
    const auto minus = luma::datetime::parse_iso8601("2024-03-15T09:30:00-05:00");

    ASSERT_TRUE(z.success);
    ASSERT_TRUE(plus.success);
    ASSERT_TRUE(minus.success);
    ASSERT_EQ(z.unix_seconds, 1710513000.0);
    ASSERT_EQ(plus.unix_seconds, 1710513000.0);
    ASSERT_EQ(minus.unix_seconds, 1710513000.0);
}

static void test_iso_codec_rejects_out_of_range_and_trailing() {
    // Regression: parse_iso8601 must validate each field explicitly instead of
    // letting timegm/_mkgmtime silently normalise out-of-range components
    // (Feb 30 -> Mar 1, 25:00 -> next day), and must reject unconsumed trailing
    // characters — matching DateTime.from_parts.  Valid dates still parse.
    ASSERT_FALSE(luma::datetime::parse_iso8601("2020-02-30").success);
    ASSERT_FALSE(luma::datetime::parse_iso8601("2024-03-15T25:00:00Z").success);
    ASSERT_FALSE(luma::datetime::parse_iso8601("2024-03-15T14:60:00Z").success);
    ASSERT_FALSE(luma::datetime::parse_iso8601("2020-01-01xyz").success);
    ASSERT_TRUE(luma::datetime::parse_iso8601("2020-01-01").success);
    ASSERT_TRUE(luma::datetime::parse_iso8601("2024-02-29").success);
}

static void test_iso_codec_parse_failures() {
    ASSERT_FALSE(luma::datetime::parse_iso8601("not-a-date").success);
    ASSERT_FALSE(luma::datetime::parse_iso8601("").success);
    ASSERT_FALSE(luma::datetime::parse_iso8601("2024-03-15T99").success);
    // Out-of-range year / month are rejected (overflow guard).
    ASSERT_FALSE(luma::datetime::parse_iso8601("99999-01-01").success);
    ASSERT_FALSE(luma::datetime::parse_iso8601("0000-01-01").success);
    ASSERT_FALSE(luma::datetime::parse_iso8601("2024-13-01").success);
}

static void test_iso_codec_format() {
    const auto s = luma::datetime::format_iso8601(1710513000.0);

    ASSERT_TRUE(s.has_value());
    ASSERT_EQ(*s, "2024-03-15T14:30:00Z");

    // Outside the supported range there is no representation.
    ASSERT_FALSE(luma::datetime::format_iso8601(1.0e20).has_value());
    ASSERT_FALSE(luma::datetime::format_iso8601(-1.0e20).has_value());
}

static void test_iso_codec_format_non_finite() {
    // NaN compares false against both range bounds, so it must be rejected
    // explicitly — otherwise it would reach static_cast<time_t>(NaN), which is
    // undefined behaviour.  Infinities are likewise non-representable.
    ASSERT_FALSE(
        luma::datetime::format_iso8601(std::numeric_limits<double>::quiet_NaN()).has_value());
    ASSERT_FALSE(
        luma::datetime::format_iso8601(std::numeric_limits<double>::infinity()).has_value());
    ASSERT_FALSE(
        luma::datetime::format_iso8601(-std::numeric_limits<double>::infinity()).has_value());
}

static void test_iso_codec_round_trip() {
    // parse(format(t)) == t for any instant the platform can represent.
    // (Windows gmtime_s rejects negative time_t, so pre-1970 instants such as
    // the year-0001 lower bound have no representation there; skip those rather
    // than assert a round-trip that the platform cannot perform.)
    for (const double t :
         {0.0, 946684800.0, 1710513000.0, 4102444800.0, -62135596800.0, 253402300799.0}) {
        const auto text = luma::datetime::format_iso8601(t);

        if (!text) {
            continue;
        }

        const auto back = luma::datetime::parse_iso8601(*text);

        ASSERT_TRUE(back.success);
        ASSERT_EQ(back.unix_seconds, t);
    }
}

// Deterministic mirror of fuzz/fuzz_datetime.cpp's contract, run against the
// MSVC-built codec (libFuzzer is Clang-only).  It guards both halves of the
// trust boundary: parse_iso8601 must survive hostile text without throwing, and
// whenever it accepts input that the formatter can represent, the format → parse
// round-trip must reproduce the exact instant.  format_iso8601 is also swept
// with hostile doubles so the range / NaN / infinity guards stay enforced.
static void test_iso_codec_oracle_corpus() {
    constexpr std::string_view inputs[] = {"",
                                           " ",
                                           "Z",
                                           "T",
                                           "+",
                                           "-",
                                           "::",
                                           "---",
                                           "not-a-date",
                                           "2024",
                                           "2024-",
                                           "2024-03",
                                           "2024-03-15T",
                                           "2024-03-15T14",
                                           "2024-03-15T14:30",
                                           "2024-03-15",
                                           "2024-03-15T14:30:00",
                                           "2024-03-15T14:30:00Z",
                                           "2024-03-15t14:30:00z",
                                           "2024-03-15T20:00:00+05:30",
                                           "2024-03-15T09:30:00-05:00",
                                           "2024-03-15T14:30:00+99:99",
                                           "0001-01-01T00:00:00Z",
                                           "9999-12-31T23:59:59Z",
                                           "0000-01-01",
                                           "10000-01-01",
                                           "2024-13-01",
                                           "2024-00-01",
                                           "2024-02-30",
                                           "2024-01-99T99:99:99Z",
                                           "  2024-03-15  ",
                                           "2024-03-15Tgarbage",
                                           "99999999999999999999-01-01",
                                           "+-+-+-",
                                           "2024-03-15T14:30:00+",
                                           "2024-03-15T14:30:00+05",
                                           "\x01\x02\xff\xfe",
                                           "\xc2\xb2\xc2\xb3"};

    for (const std::string_view in : inputs) {
        const auto parsed = luma::datetime::parse_iso8601(in);

        if (!parsed.success) {
            continue;
        }

        const auto text = luma::datetime::format_iso8601(parsed.unix_seconds);

        if (!text) {
            continue; // Instant outside the formattable range — nothing to check.
        }

        const auto reparsed = luma::datetime::parse_iso8601(*text);

        ASSERT_TRUE(reparsed.success);
        ASSERT_EQ(reparsed.unix_seconds, parsed.unix_seconds);
    }

    // Hostile doubles: format must never crash and must only emit text for
    // finite, in-range instants — which must then round-trip.
    const double k_min = -62135596800.0;
    const double k_max = 253402300799.0;
    const double doubles[] = {0.0,
                              -0.0,
                              1.0e-300,
                              1.0e300,
                              -1.0e300,
                              std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity(),
                              k_min,
                              k_max,
                              k_min - 1.0,
                              k_max + 1.0,
                              1710513000.0};

    for (const double d : doubles) {
        const auto text = luma::datetime::format_iso8601(d);

        if (!text) {
            continue;
        }

        // format_iso8601 renders whole seconds, so the round-trip recovers the
        // timestamp truncated toward zero rather than the exact fractional input.
        const auto reparsed = luma::datetime::parse_iso8601(*text);

        ASSERT_TRUE(reparsed.success);
        ASSERT_EQ(reparsed.unix_seconds, std::trunc(d));
    }
}

int main() {
    RUN(test_datetime_add_days);
    RUN(test_datetime_add_hours);
    RUN(test_datetime_add_months);
    RUN(test_datetime_add_seconds);
    RUN(test_datetime_add_years);
    RUN(test_datetime_days_in_month);
    RUN(test_datetime_days_in_month_invalid);
    RUN(test_datetime_difference_days);
    RUN(test_datetime_difference_hours);
    RUN(test_datetime_difference_months);
    RUN(test_datetime_difference_seconds);
    RUN(test_datetime_difference_years);
    RUN(test_datetime_from_iso_string_negative_offset);
    RUN(test_datetime_from_iso_string_positive_offset);
    RUN(test_datetime_from_iso_string_utc);
    RUN(test_datetime_from_offset);
    RUN(test_datetime_from_parts_offset);
    RUN(test_datetime_is_leap_year);
    RUN(test_datetime_module);
    RUN(test_datetime_now_iso_string);
    RUN(test_datetime_now_ms);
    RUN(test_datetime_offset_hours);
    RUN(test_datetime_to_iso_string_offset);
    RUN(test_datetime_to_iso_string_offset_negative);
    RUN(test_datetime_to_iso_string_offset_zero);
    RUN(test_datetime_to_iso_string_valid);
    RUN(test_datetime_to_offset);
    RUN(test_datetime_to_offset_invalid);
    RUN(test_datetime_to_parts_valid);
    RUN(test_datetime_year_out_of_range);
    RUN(test_datetime_year_valid);
    RUN(test_datetime_month_valid);
    RUN(test_datetime_day_of_month_valid);
    RUN(test_datetime_hour_minute_second_valid);
    RUN(test_datetime_day_of_week_valid);
    RUN(test_datetime_components_out_of_range);
    RUN(test_datetime_from_parts_valid);
    RUN(test_datetime_from_parts_invalid_fields);
    RUN(test_datetime_from_parts_feb_29);
    RUN(test_datetime_to_parts_values);
    RUN(test_datetime_to_parts_out_of_range);
    RUN(test_datetime_add_milliseconds);
    RUN(test_datetime_difference_milliseconds);
    RUN(test_datetime_now_unix);
    RUN(test_datetime_format_valid);
    RUN(test_datetime_format_out_of_range);
    RUN(test_datetime_is_before_after);
    RUN(test_datetime_to_iso_string_out_of_range);
    RUN(test_datetime_from_iso_string_date_time_components);
    RUN(test_datetime_from_iso_string_invalid);
    RUN(test_datetime_from_iso_string_year_out_of_range);
    RUN(test_datetime_from_parts_offset_invalid);
    RUN(test_datetime_from_offset_invalid);
    RUN(test_datetime_to_iso_string_offset_invalid);
    RUN(test_datetime_add_months_out_of_range);
    RUN(test_datetime_add_years_out_of_range);
    RUN(test_iso_codec_parse_date_only);
    RUN(test_iso_codec_parse_with_zone);
    RUN(test_iso_codec_parse_failures);
    RUN(test_iso_codec_rejects_out_of_range_and_trailing);
    RUN(test_iso_codec_format);
    RUN(test_iso_codec_format_non_finite);
    RUN(test_iso_codec_round_trip);
    RUN(test_iso_codec_oracle_corpus);
    return SUMMARY();
}
