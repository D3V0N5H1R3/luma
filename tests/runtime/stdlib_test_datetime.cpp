// Standard library tests: DateTime.

#include <cmath>
#include <limits>
#include <string>

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

// --- DateTime.Period (N03) ---

static void test_datetime_period_constructs() {
    const auto v = eval("DateTime.period(1, 2, 3)");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Period"});
    ASSERT_EQ(v.as_record()->find_field("years")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(v.as_record()->find_field("months")->as_integer(), static_cast<std::int64_t>(2));
    ASSERT_EQ(v.as_record()->find_field("days")->as_integer(), static_cast<std::int64_t>(3));
}

static void test_datetime_add_period_advances() {
    // 2024-03-15 + 1 year 2 months 3 days (no day clamping) advances the instant.
    const auto v = eval("DateTime.add_period(1710497400.0, DateTime.period(1, 2, 3))");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() > 1710497400.0);
}

static void test_datetime_between_dates_roundtrips() {
    // between_dates recovers exactly the period add_period applied (day-clamp-free).
    const auto v =
        eval("DateTime.between_dates(1710497400.0, "
             "Result.unwrap(DateTime.add_period(1710497400.0, DateTime.period(1, 2, 3))))");

    ASSERT_RESULT_SUCCESS(v);
    const auto& rec = *v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec.type_name, std::string{"Period"});
    ASSERT_EQ(rec.find_field("years")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(rec.find_field("months")->as_integer(), static_cast<std::int64_t>(2));
    ASSERT_EQ(rec.find_field("days")->as_integer(), static_cast<std::int64_t>(3));
}

static void test_datetime_between_dates_negative_when_reversed() {
    // start after end yields a negative span.
    const auto v = eval("DateTime.between_dates(Result.unwrap(DateTime.add_period(1710497400.0, "
                        "DateTime.period(1, 0, 0))), 1710497400.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_record()->find_field("years")->as_integer(),
              static_cast<std::int64_t>(-1));
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
    ASSERT_TRUE(env->has("DateTime.day_of_year"));
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

static void test_datetime_day_of_year_valid() {
    // 2024 is a leap year: 2024-03-15 is day 31 + 29 + 15 = 75.
    ASSERT_EVAL_INT("DateTime.day_of_year(1710497400.0)", 75);
    // 2024-01-01 is day 1.
    ASSERT_EVAL_INT("DateTime.day_of_year(1704067200.0)", 1);
    // Out-of-range timestamp fails cleanly like the other accessors.
    ASSERT_EVAL_FAILURE("DateTime.day_of_year(-99999999999.0)");
}

static void test_datetime_weekday_returns_choice() {
    // 2024-03-15 is a Friday → DateTime.Weekday.Friday.
    const auto v = eval("DateTime.weekday(1710497400.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "Weekday");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "Friday");
}

static void test_datetime_weekday_out_of_range() {
    // A timestamp outside the supported year 0001-9999 range fails.
    ASSERT_EVAL_FAILURE("DateTime.weekday(-99999999999.0)");
}

static void test_datetime_weekday_from_number_valid() {
    const auto mon = eval("DateTime.weekday_from_number(1)");
    ASSERT_RESULT_SUCCESS(mon);
    ASSERT_EQ(mon.as_result()->owned_inner->as_choice()->type_name, "Weekday");
    ASSERT_EQ(mon.as_result()->owned_inner->as_choice()->variant, "Monday");

    const auto sun = eval("DateTime.weekday_from_number(7)");
    ASSERT_RESULT_SUCCESS(sun);
    ASSERT_EQ(sun.as_result()->owned_inner->as_choice()->variant, "Sunday");
}

static void test_datetime_weekday_from_number_invalid() {
    // Only 1..7 are valid ISO-8601 weekday numbers.
    ASSERT_EVAL_FAILURE("DateTime.weekday_from_number(0)");
    ASSERT_EVAL_FAILURE("DateTime.weekday_from_number(8)");
}

static void test_datetime_weekday_number() {
    ASSERT_EQ(eval("DateTime.weekday_number(DateTime.Weekday.Monday)").as_integer(), 1);
    ASSERT_EQ(eval("DateTime.weekday_number(DateTime.Weekday.Friday)").as_integer(), 5);
    ASSERT_EQ(eval("DateTime.weekday_number(DateTime.Weekday.Sunday)").as_integer(), 7);
}

static void test_datetime_weekday_name() {
    ASSERT_EQ(eval("DateTime.weekday_name(DateTime.Weekday.Wednesday)").as_string(), "Wednesday");
    ASSERT_EQ(eval("DateTime.weekday_name(DateTime.Weekday.Sunday)").as_string(), "Sunday");
}

static void test_datetime_weekday_roundtrip() {
    // weekday_number ∘ weekday_from_number is the identity on 1..7.
    for (int n = 1; n <= 7; ++n) {
        const auto expr = "DateTime.weekday_number(Result.unwrap(DateTime.weekday_from_number(" +
                          std::to_string(n) + ")))";
        ASSERT_EQ(eval(expr).as_integer(), n);
    }
}

static void test_datetime_weekday_accessors_reject_non_weekday() {
    // The choice-consuming accessors reject a non-choice and a foreign choice.
    ASSERT_TRUE(luma::test::eval_throws("DateTime.weekday_number(5)"));
    ASSERT_TRUE(luma::test::eval_throws(R"(DateTime.weekday_name("Monday"))"));
    ASSERT_TRUE(luma::test::eval_throws("DateTime.weekday_number(Log.Level.Information)"));
}

static void test_datetime_month_of_returns_choice() {
    // 2024-03-15 is in March → DateTime.Month.March.
    const auto v = eval("DateTime.month_of(1710497400.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "Month");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "March");
}

static void test_datetime_month_of_out_of_range() {
    // A timestamp outside the supported year 0001-9999 range fails.
    ASSERT_EVAL_FAILURE("DateTime.month_of(-99999999999.0)");
}

static void test_datetime_month_from_number_valid() {
    const auto jan = eval("DateTime.month_from_number(1)");
    ASSERT_RESULT_SUCCESS(jan);
    ASSERT_EQ(jan.as_result()->owned_inner->as_choice()->type_name, "Month");
    ASSERT_EQ(jan.as_result()->owned_inner->as_choice()->variant, "January");

    const auto dec = eval("DateTime.month_from_number(12)");
    ASSERT_RESULT_SUCCESS(dec);
    ASSERT_EQ(dec.as_result()->owned_inner->as_choice()->variant, "December");
}

static void test_datetime_month_from_number_invalid() {
    // Only 1..12 are valid month numbers.
    ASSERT_EVAL_FAILURE("DateTime.month_from_number(0)");
    ASSERT_EVAL_FAILURE("DateTime.month_from_number(13)");
}

static void test_datetime_month_number() {
    ASSERT_EQ(eval("DateTime.month_number(DateTime.Month.January)").as_integer(), 1);
    ASSERT_EQ(eval("DateTime.month_number(DateTime.Month.March)").as_integer(), 3);
    ASSERT_EQ(eval("DateTime.month_number(DateTime.Month.December)").as_integer(), 12);
}

static void test_datetime_month_name() {
    ASSERT_EQ(eval("DateTime.month_name(DateTime.Month.April)").as_string(), "April");
    ASSERT_EQ(eval("DateTime.month_name(DateTime.Month.December)").as_string(), "December");
}

static void test_datetime_month_roundtrip() {
    // month_number ∘ month_from_number is the identity on 1..12.
    for (int n = 1; n <= 12; ++n) {
        const auto expr = "DateTime.month_number(Result.unwrap(DateTime.month_from_number(" +
                          std::to_string(n) + ")))";
        ASSERT_EQ(eval(expr).as_integer(), n);
    }
}

static void test_datetime_month_accessors_reject_non_month() {
    // The choice-consuming accessors reject a non-choice and a foreign choice.
    ASSERT_TRUE(luma::test::eval_throws("DateTime.month_number(3)"));
    ASSERT_TRUE(luma::test::eval_throws(R"(DateTime.month_name("March"))"));
    ASSERT_TRUE(luma::test::eval_throws("DateTime.month_number(DateTime.Weekday.Monday)"));
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

static void test_datetime_break_duration_components() {
    // 3725 s = 1 h 2 m 5 s.
    const auto v = eval("DateTime.break_duration(3725.0)");

    ASSERT_TRUE(v.is_record());

    const auto& rec = *v.as_record();

    ASSERT_EQ(rec.find_field("days")->as_integer(), 0);
    ASSERT_EQ(rec.find_field("hours")->as_integer(), 1);
    ASSERT_EQ(rec.find_field("minutes")->as_integer(), 2);
    ASSERT_EQ(rec.find_field("seconds")->as_integer(), 5);
    ASSERT_EQ(rec.find_field("milliseconds")->as_integer(), 0);
    ASSERT_FALSE(rec.find_field("negative")->as_bool());
}

static void test_datetime_break_duration_fractional_and_days() {
    // 90061.5 s = 1 d 1 h 1 m 1 s 500 ms.
    const auto v = eval("DateTime.break_duration(90061.5)");
    const auto& rec = *v.as_record();

    ASSERT_EQ(rec.find_field("days")->as_integer(), 1);
    ASSERT_EQ(rec.find_field("hours")->as_integer(), 1);
    ASSERT_EQ(rec.find_field("minutes")->as_integer(), 1);
    ASSERT_EQ(rec.find_field("seconds")->as_integer(), 1);
    ASSERT_EQ(rec.find_field("milliseconds")->as_integer(), 500);
}

static void test_datetime_break_duration_negative() {
    const auto v = eval("DateTime.break_duration(-3725.0)");
    const auto& rec = *v.as_record();

    ASSERT_TRUE(rec.find_field("negative")->as_bool());
    ASSERT_EQ(rec.find_field("hours")->as_integer(), 1);
    ASSERT_EQ(rec.find_field("minutes")->as_integer(), 2);
    ASSERT_EQ(rec.find_field("seconds")->as_integer(), 5);
}

static void test_datetime_break_duration_zero() {
    const auto v = eval("DateTime.break_duration(0.0)");
    const auto& rec = *v.as_record();

    ASSERT_EQ(rec.find_field("days")->as_integer(), 0);
    ASSERT_EQ(rec.find_field("milliseconds")->as_integer(), 0);
    // A zero span is not negative.
    ASSERT_FALSE(rec.find_field("negative")->as_bool());
}

static void test_datetime_format_duration() {
    ASSERT_EQ(eval("DateTime.format_duration(DateTime.break_duration(3725.0))").as_string(),
              std::string("1h 2m 5s"));
    ASSERT_EQ(eval("DateTime.format_duration(DateTime.break_duration(90.0))").as_string(),
              std::string("1m 30s"));
    ASSERT_EQ(eval("DateTime.format_duration(DateTime.break_duration(0.5))").as_string(),
              std::string("500ms"));
    ASSERT_EQ(eval("DateTime.format_duration(DateTime.break_duration(90061.5))").as_string(),
              std::string("1d 1h 1m 1s 500ms"));
}

static void test_datetime_format_duration_zero_and_negative() {
    ASSERT_EQ(eval("DateTime.format_duration(DateTime.break_duration(0.0))").as_string(),
              std::string("0s"));
    ASSERT_EQ(eval("DateTime.format_duration(DateTime.break_duration(-3725.0))").as_string(),
              std::string("-1h 2m 5s"));
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

// ─── DateTime.from_iso_string_typed — result<number, DateTime.ParseError> ────

static void test_datetime_from_iso_string_typed_success() {
    const auto v = eval(R"(DateTime.from_iso_string_typed("2024-03-15T14:30:00Z"))");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 1710513000.0, 0.001);
}

static void assert_parse_error_variant(const char* expr, const char* variant) {
    const auto v = eval(expr);

    ASSERT_RESULT_FAILURE(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "ParseError");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, variant);
}

static void test_datetime_from_iso_string_typed_classifies_failures() {
    // Empty / whitespace-only input.
    assert_parse_error_variant(R"(DateTime.from_iso_string_typed(""))", "Empty");
    assert_parse_error_variant(R"(DateTime.from_iso_string_typed("   "))", "Empty");
    // Present but malformed.
    assert_parse_error_variant(R"(DateTime.from_iso_string_typed("not-a-date"))", "InvalidFormat");
    assert_parse_error_variant(R"(DateTime.from_iso_string_typed("2024/03/15"))", "InvalidFormat");
    // Well-formed shape but an impossible field.
    assert_parse_error_variant(R"(DateTime.from_iso_string_typed("2024-13-01"))", "OutOfRange");
    assert_parse_error_variant(R"(DateTime.from_iso_string_typed("2024-02-30"))", "OutOfRange");
    assert_parse_error_variant(R"(DateTime.from_iso_string_typed("99999-01-01"))", "OutOfRange");
    // Valid shape but sub-second precision this parser does not accept.
    assert_parse_error_variant(R"(DateTime.from_iso_string_typed("2024-03-15T14:30:00.5Z"))",
                               "UnsupportedPrecision");
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

// ─── DateTime.Interval: validating range record + helpers ────────

static void test_datetime_interval_valid() {
    // A well-formed interval (end >= start) succeeds and carries start/end as
    // plain number timestamps in a DateTime.Interval record.
    const auto v = eval("DateTime.interval(100.0, 200.0)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;
    ASSERT_TRUE(inner.is_record());
    ASSERT_EQ(inner.as_record()->type_name, "Interval");
    ASSERT_EQ(inner.as_record()->find_field("start")->as_number(), 100.0);
    ASSERT_EQ(inner.as_record()->find_field("end")->as_number(), 200.0);
}

static void test_datetime_interval_empty_is_valid() {
    // A zero-length interval (start == end) is permitted.
    const auto v = eval("DateTime.interval(50.0, 50.0)");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_record()->find_field("start")->as_number(), 50.0);
}

static void test_datetime_interval_end_before_start_fails() {
    // end < start is a domain error surfaced as a result failure, not a throw.
    ASSERT_EVAL_FAILURE("DateTime.interval(200.0, 100.0)");
}

static void test_datetime_interval_duration() {
    const auto v =
        eval("DateTime.interval_duration(Result.unwrap(DateTime.interval(100.0, 250.0)))");
    ASSERT_EQ(v.as_number(), 150.0);
}

static void test_datetime_interval_contains_closed() {
    // Intervals are closed: both endpoints are contained.
    ASSERT_EQ(eval("DateTime.interval_contains(Result.unwrap(DateTime.interval(100.0, 200.0)), "
                   "100.0)")
                  .as_bool(),
              true);
    ASSERT_EQ(eval("DateTime.interval_contains(Result.unwrap(DateTime.interval(100.0, 200.0)), "
                   "200.0)")
                  .as_bool(),
              true);
    ASSERT_EQ(eval("DateTime.interval_contains(Result.unwrap(DateTime.interval(100.0, 200.0)), "
                   "150.0)")
                  .as_bool(),
              true);
    ASSERT_EQ(eval("DateTime.interval_contains(Result.unwrap(DateTime.interval(100.0, 200.0)), "
                   "99.0)")
                  .as_bool(),
              false);
    ASSERT_EQ(eval("DateTime.interval_contains(Result.unwrap(DateTime.interval(100.0, 200.0)), "
                   "201.0)")
                  .as_bool(),
              false);
}

static void test_datetime_intervals_overlap() {
    const auto overlap = [](const std::string& a, const std::string& b) {
        return eval("DateTime.intervals_overlap(Result.unwrap(DateTime.interval(" + a +
                    ")), Result.unwrap(DateTime.interval(" + b + ")))")
            .as_bool();
    };

    ASSERT_EQ(overlap("100.0, 200.0", "150.0, 250.0"), true);  // partial overlap
    ASSERT_EQ(overlap("100.0, 200.0", "120.0, 180.0"), true);  // containment
    ASSERT_EQ(overlap("100.0, 200.0", "200.0, 300.0"), true);  // touching (closed) counts
    ASSERT_EQ(overlap("100.0, 200.0", "201.0, 300.0"), false); // disjoint
}

static void test_datetime_zoned_valid_and_iso() {
    // A well-formed Zoned carries the instant and its offset in minutes.
    const auto v = eval("DateTime.zoned(0.0, 60)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;
    ASSERT_TRUE(inner.is_record());
    ASSERT_EQ(inner.as_record()->type_name, "Zoned");
    ASSERT_EQ(inner.as_record()->find_field("timestamp")->as_number(), 0.0);
    ASSERT_EQ(inner.as_record()->find_field("offset_minutes")->as_integer(),
              static_cast<std::int64_t>(60));

    // Epoch 0 at +60 renders 1970-01-01T01:00:00+01:00.
    const auto iso = eval("DateTime.zoned_to_iso_string(Result.unwrap(DateTime.zoned(0.0, 60)))");
    ASSERT_RESULT_SUCCESS(iso);
    ASSERT_EQ(iso.as_result()->owned_inner->as_string(), "1970-01-01T01:00:00+01:00");

    // Zero offset renders a trailing Z.
    const auto utc = eval("DateTime.zoned_to_iso_string(Result.unwrap(DateTime.zoned(0.0, 0)))");
    ASSERT_EQ(utc.as_result()->owned_inner->as_string(), "1970-01-01T00:00:00Z");
}

static void test_datetime_zoned_invalid_offset_fails() {
    ASSERT_EVAL_FAILURE("DateTime.zoned(0.0, 900)");
    ASSERT_EVAL_FAILURE("DateTime.zoned(0.0, -800)");
}

static void test_datetime_zoned_to_parts() {
    const auto v = eval("DateTime.zoned_to_parts(Result.unwrap(DateTime.zoned(0.0, 60)))");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, "TimeParts");
    ASSERT_EQ(rec->find_field("year")->as_integer(), static_cast<std::int64_t>(1970));
    ASSERT_EQ(rec->find_field("hour")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(rec->find_field("minute")->as_integer(), static_cast<std::int64_t>(0));
}

// ─── DateTime.Date / DateTime.Time (partial calendar/wall-clock records) ──────

static void test_datetime_date_validates_and_builds() {
    const auto v = eval("DateTime.date(2024, 2, 29)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, "Date");
    ASSERT_EQ(rec->find_field("year")->as_integer(), static_cast<std::int64_t>(2024));
    ASSERT_EQ(rec->find_field("month")->as_integer(), static_cast<std::int64_t>(2));
    ASSERT_EQ(rec->find_field("day")->as_integer(), static_cast<std::int64_t>(29));
}

static void test_datetime_date_rejects_invalid() {
    // Feb 29 in a non-leap year, month 0, and day 0 are all rejected.
    ASSERT_EVAL_FAILURE("DateTime.date(2023, 2, 29)");
    ASSERT_EVAL_FAILURE("DateTime.date(2024, 0, 1)");
    ASSERT_EVAL_FAILURE("DateTime.date(2024, 1, 0)");
}

static void test_datetime_time_validates_and_builds() {
    const auto v = eval("DateTime.time(13, 30, 45)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, "Time");
    ASSERT_EQ(rec->find_field("hour")->as_integer(), static_cast<std::int64_t>(13));
    ASSERT_EQ(rec->find_field("minute")->as_integer(), static_cast<std::int64_t>(30));
    ASSERT_EQ(rec->find_field("second")->as_integer(), static_cast<std::int64_t>(45));
}

static void test_datetime_time_rejects_invalid() {
    ASSERT_EVAL_FAILURE("DateTime.time(24, 0, 0)");
    ASSERT_EVAL_FAILURE("DateTime.time(0, 60, 0)");
    ASSERT_EVAL_FAILURE("DateTime.time(0, 0, 60)");
}

static void test_datetime_date_of_time_of_extract() {
    // 1970-01-01T00:01:05 UTC = 65 seconds after the epoch.
    const auto date = eval("DateTime.date_of(65.0)");
    ASSERT_RESULT_SUCCESS(date);
    ASSERT_EQ(date.as_result()->owned_inner->as_record()->find_field("year")->as_integer(),
              static_cast<std::int64_t>(1970));
    ASSERT_EQ(date.as_result()->owned_inner->as_record()->find_field("day")->as_integer(),
              static_cast<std::int64_t>(1));

    const auto time = eval("DateTime.time_of(65.0)");
    ASSERT_RESULT_SUCCESS(time);
    ASSERT_EQ(time.as_result()->owned_inner->as_record()->find_field("minute")->as_integer(),
              static_cast<std::int64_t>(1));
    ASSERT_EQ(time.as_result()->owned_inner->as_record()->find_field("second")->as_integer(),
              static_cast<std::int64_t>(5));
}

static void test_datetime_combine_roundtrips_with_date_of() {
    // combine(date_of(t), time_of(t)) == t for an in-range instant.
    const auto v = eval("DateTime.combine(Result.unwrap(DateTime.date(2024, 6, 15)), "
                        "Result.unwrap(DateTime.time(9, 30, 0)))");
    ASSERT_RESULT_SUCCESS(v);

    const auto ts = v.as_result()->owned_inner->to_numeric();
    const auto day = eval("DateTime.date_of(" + std::to_string(ts) + ")");
    ASSERT_EQ(day.as_result()->owned_inner->as_record()->find_field("month")->as_integer(),
              static_cast<std::int64_t>(6));
    ASSERT_EQ(day.as_result()->owned_inner->as_record()->find_field("day")->as_integer(),
              static_cast<std::int64_t>(15));
}

static void test_datetime_combine_rejects_foreign_records() {
    // combine requires a Date then a Time, in that order.
    ASSERT_TRUE(luma::test::eval_throws("DateTime.combine(Result.unwrap(DateTime.time(1, 2, 3)), "
                                        "Result.unwrap(DateTime.date(2024, 1, 1)))"));
}

int main() {
    RUN(test_datetime_add_hours);
    RUN(test_datetime_add_months);
    RUN(test_datetime_add_seconds);
    RUN(test_datetime_add_years);
    RUN(test_datetime_period_constructs);
    RUN(test_datetime_add_period_advances);
    RUN(test_datetime_between_dates_roundtrips);
    RUN(test_datetime_between_dates_negative_when_reversed);
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
    RUN(test_datetime_day_of_year_valid);
    RUN(test_datetime_weekday_returns_choice);
    RUN(test_datetime_weekday_out_of_range);
    RUN(test_datetime_weekday_from_number_valid);
    RUN(test_datetime_weekday_from_number_invalid);
    RUN(test_datetime_weekday_number);
    RUN(test_datetime_weekday_name);
    RUN(test_datetime_weekday_roundtrip);
    RUN(test_datetime_weekday_accessors_reject_non_weekday);
    RUN(test_datetime_month_of_returns_choice);
    RUN(test_datetime_month_of_out_of_range);
    RUN(test_datetime_month_from_number_valid);
    RUN(test_datetime_month_from_number_invalid);
    RUN(test_datetime_month_number);
    RUN(test_datetime_month_name);
    RUN(test_datetime_month_roundtrip);
    RUN(test_datetime_month_accessors_reject_non_month);
    RUN(test_datetime_components_out_of_range);
    RUN(test_datetime_from_parts_valid);
    RUN(test_datetime_from_parts_invalid_fields);
    RUN(test_datetime_from_parts_feb_29);
    RUN(test_datetime_to_parts_values);
    RUN(test_datetime_to_parts_out_of_range);
    RUN(test_datetime_break_duration_components);
    RUN(test_datetime_break_duration_fractional_and_days);
    RUN(test_datetime_break_duration_negative);
    RUN(test_datetime_break_duration_zero);
    RUN(test_datetime_format_duration);
    RUN(test_datetime_format_duration_zero_and_negative);
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
    RUN(test_datetime_from_iso_string_typed_success);
    RUN(test_datetime_from_iso_string_typed_classifies_failures);
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
    RUN(test_datetime_interval_valid);
    RUN(test_datetime_interval_empty_is_valid);
    RUN(test_datetime_interval_end_before_start_fails);
    RUN(test_datetime_interval_duration);
    RUN(test_datetime_interval_contains_closed);
    RUN(test_datetime_intervals_overlap);
    RUN(test_datetime_zoned_valid_and_iso);
    RUN(test_datetime_zoned_invalid_offset_fails);
    RUN(test_datetime_zoned_to_parts);
    RUN(test_datetime_date_validates_and_builds);
    RUN(test_datetime_date_rejects_invalid);
    RUN(test_datetime_time_validates_and_builds);
    RUN(test_datetime_time_rejects_invalid);
    RUN(test_datetime_date_of_time_of_extract);
    RUN(test_datetime_combine_roundtrips_with_date_of);
    RUN(test_datetime_combine_rejects_foreign_records);
    return SUMMARY();
}
