#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "common/narrow_int.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/system/datetime_internal.hpp"
#include "runtime/stdlib/system/datetime_module.hpp"

using namespace luma::datetime_detail;

namespace luma {

namespace {

// Rebuilds a std::tm from a new (year, month, day) — using the tm_* convention
// (year - 1900, 0-indexed month) — combined with the time-of-day fields of an
// existing tm, then converts to a UTC Unix timestamp.  Shared tail of
// DateTime.add_months and DateTime.add_years.  Returns a failure Value when the
// result is out of range, or std::nullopt on success, writing to out_unix.
[[nodiscard]] std::optional<Value> rebuild_to_unix(int new_year, int new_mon, int new_mday,
                                                   const std::tm& time_of_day,
                                                   std::string_view func_name, double& out_unix) {
    std::tm result{};
    result.tm_year = new_year;
    result.tm_mon = new_mon;
    result.tm_mday = new_mday;
    result.tm_hour = time_of_day.tm_hour;
    result.tm_min = time_of_day.tm_min;
    result.tm_sec = time_of_day.tm_sec;

    const auto unix_time = tm_to_unix(result);

    if (!unix_time) {
        return make_failure_value(error_msg("DateTime", func_name, "resulting date is invalid"));
    }

    out_unix = *unix_time;
    return std::nullopt;
}

// Builds a raw_body lambda computing abs(a - b) / scale — the difference between
// two same-unit timestamps expressed in a coarser unit.
[[nodiscard]] auto scaled_difference_body(double scale) {
    return [scale](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
        return Value{std::abs(args[0].to_numeric() - args[1].to_numeric()) / scale};
    };
}

// Builds a raw_body lambda computing a + b * scale — adding a scaled quantity to
// a same-unit timestamp.
[[nodiscard]] auto scaled_add_body(double scale) {
    return [scale](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
        return Value{args[0].to_numeric() + (args[1].to_numeric() * scale)};
    };
}

// Build a DateTime.Period record value (type_name "Period").  All three
// components are whole counts, stored as integer Values.
[[nodiscard]] Value make_period_record(std::int64_t years, std::int64_t months, std::int64_t days) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Period";
    rec->fields.emplace_back("years", Value{years});
    rec->fields.emplace_back("months", Value{months});
    rec->fields.emplace_back("days", Value{days});

    return Value{std::move(rec)};
}

// The integer years/months/days of a DateTime.Period argument.
struct PeriodParts {
    std::int64_t years;
    std::int64_t months;
    std::int64_t days;
};

// Read a DateTime.Period argument.  Returns std::nullopt when the value is not a
// period-shaped record so the caller can raise a typed failure.
[[nodiscard]] std::optional<PeriodParts> read_period(const Value& value) {
    if (!value.is_record()) {
        return std::nullopt;
    }

    const auto& rec = value.as_record();
    const auto read = [&rec](std::string_view name, std::int64_t& out) -> bool {
        const Value* v = rec->find_field(name);
        if (v == nullptr || !(v->is_integer() || v->is_number())) {
            return false;
        }
        out = v->is_integer() ? v->as_integer() : static_cast<std::int64_t>(v->to_numeric());
        return true;
    };

    PeriodParts parts{};
    if (!read("years", parts.years) || !read("months", parts.months) || !read("days", parts.days)) {
        return std::nullopt;
    }

    return parts;
}

// Proleptic-month index (year * 12 + zero-indexed month) for a 1-indexed month,
// the monotonic key that makes calendar month arithmetic a plain subtraction.
[[nodiscard]] std::int64_t proleptic_month(std::int64_t year, int month_1indexed) {
    return (year * 12) + (month_1indexed - 1);
}

// The (year, 1-indexed month, day) reached by adding a signed month delta to a
// date, clamping the day to the target month's length (so 31 Jan + 1 month = 28
// or 29 Feb).  Returns std::nullopt when the result leaves the 0001-9999 range.
struct Ymd {
    int year;
    int month; // 1-indexed
    int day;
};

[[nodiscard]] std::optional<Ymd> add_months_to_date(std::int64_t year, int month_1indexed, int day,
                                                    std::int64_t delta) {
    const std::int64_t total = proleptic_month(year, month_1indexed) + delta;
    std::int64_t new_year = total / 12;
    std::int64_t new_month = total % 12;

    if (new_month < 0) {
        new_month += 12;
        new_year -= 1;
    }

    if (new_year < 1 || new_year > 9999) {
        return std::nullopt;
    }

    const int month_out = static_cast<int>(new_month) + 1;
    const int max_day = days_in_month_for(month_out, new_year);

    return Ymd{static_cast<int>(new_year), month_out, std::min(day, max_day)};
}

// Whole days since 1970-01-01 for a calendar date (midnight UTC), used to count
// the trailing-day remainder of DateTime.between_dates exactly.  Returns
// std::nullopt when the date is outside the supported range.
[[nodiscard]] std::optional<std::int64_t> epoch_day(int year, int month_1indexed, int day) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month_1indexed - 1;
    t.tm_mday = day;

    const auto unix_time = tm_to_unix(t);
    if (!unix_time) {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(std::floor(*unix_time / 86400.0));
}

} // namespace

// DateTime difference_* and add_* arithmetic over second/millisecond timestamps.
void register_datetime_arithmetic(const EnvPtr& env) {
    ModuleBuilder builder{"DateTime", env};

    // Data-driven registration of the scale-factor difference_*/add_* handlers.
    // Each differs only by a constant scale factor and whether it computes a
    // difference (abs(a - b) / scale) or an addition (a + b * scale).  The
    // millisecond and second rows both use scale 1 but are kept distinct: they
    // operate on different units (milliseconds since program start vs. Unix
    // seconds), so collapsing them would conflate two separate APIs.
    enum class ScaleOp {
        difference,
        add
    };

    struct ScaleFunc {
        const char* name;
        double scale;
        ScaleOp op;
    };

    const ScaleFunc scale_funcs[] = {
        {.name = "difference_milliseconds", .scale = 1.0, .op = ScaleOp::difference},
        {.name = "difference_seconds", .scale = 1.0, .op = ScaleOp::difference},
        {.name = "difference_minutes", .scale = 60.0, .op = ScaleOp::difference},
        {.name = "difference_hours", .scale = 3600.0, .op = ScaleOp::difference},
        {.name = "difference_days", .scale = 86400.0, .op = ScaleOp::difference},
        {.name = "difference_weeks", .scale = 604800.0, .op = ScaleOp::difference},
        {.name = "add_milliseconds", .scale = 1.0, .op = ScaleOp::add},
        {.name = "add_seconds", .scale = 1.0, .op = ScaleOp::add},
        {.name = "add_minutes", .scale = 60.0, .op = ScaleOp::add},
        {.name = "add_hours", .scale = 3600.0, .op = ScaleOp::add},
        {.name = "add_days", .scale = 86400.0, .op = ScaleOp::add},
        {.name = "add_weeks", .scale = 604800.0, .op = ScaleOp::add},
    };

    for (const auto& f : scale_funcs) {
        if (f.op == ScaleOp::difference) {
            builder.func(f.name, 2).raw_body(scaled_difference_body(f.scale));
        } else {
            builder.func(f.name, 2).raw_body(scaled_add_body(f.scale));
        }
    }

    // Calendar-aware difference/add handlers need broken-down time and day
    // clamping, so they stay hand-written rather than joining the scale table.
    builder.func("difference_months", 2)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto tm1 = to_tm(args[0].to_numeric());
            const auto tm2 = to_tm(args[1].to_numeric());

            if (!tm1 || !tm2) {
                return make_failure_value(
                    error_msg("DateTime", "difference_months", k_timestamp_range_error));
            }

            const int months1 = ((tm1->tm_year + 1900) * 12) + tm1->tm_mon;
            const int months2 = ((tm2->tm_year + 1900) * 12) + tm2->tm_mon;

            return make_success_value(
                Value{static_cast<std::int64_t>(std::abs(months1 - months2))});
        })
        .func("difference_years", 2)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto tm1 = to_tm(args[0].to_numeric());
            const auto tm2 = to_tm(args[1].to_numeric());

            if (!tm1 || !tm2) {
                return make_failure_value(
                    error_msg("DateTime", "difference_years", k_timestamp_range_error));
            }

            const int year1 = tm1->tm_year + 1900;
            const int year2 = tm2->tm_year + 1900;

            return make_success_value(Value{static_cast<std::int64_t>(std::abs(year1 - year2))});
        })
        .func("add_months", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto tm = to_tm(args[0].to_numeric());

            if (!tm) {
                return make_failure_value(
                    error_msg("DateTime", "add_months", k_timestamp_range_error));
            }

            const auto months_to_add =
                try_narrow_int(expect_integer(args[1], "DateTime.add_months", loc));

            if (!months_to_add) {
                return make_failure_value(
                    error_msg("DateTime", "add_months", "month count out of 32-bit integer range"));
            }

            // Use int64_t to avoid signed overflow when adding to tm_mon.
            auto total_months =
                (static_cast<std::int64_t>(tm->tm_year) * 12) + tm->tm_mon + *months_to_add;

            // Euclidean division: month stays in [0, 11].
            auto new_year_64 = total_months / 12;
            auto new_month_64 = total_months % 12;

            if (new_month_64 < 0) {
                new_month_64 += 12;
                new_year_64 -= 1;
            }

            // Check the resulting year is within int range for std::tm.
            const auto new_year = try_narrow_int(new_year_64);

            if (!new_year) {
                return make_failure_value(
                    error_msg("DateTime", "add_months", "resulting date is out of range"));
            }

            const int new_month = static_cast<int>(new_month_64);

            // Clamp day to valid range for the new month.
            // new_month is 0-indexed here (tm_mon convention).
            const int max_day = days_in_month_for(new_month + 1, *new_year + 1900);
            const int clamped_day = std::min(tm->tm_mday, max_day);

            double unix_time{};

            if (auto err = rebuild_to_unix(*new_year, new_month, clamped_day, *tm, "add_months",
                                           unix_time)) {
                return *std::move(err);
            }

            return make_success_value(Value{unix_time});
        })
        .func("add_years", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto tm = to_tm(args[0].to_numeric());

            if (!tm) {
                return make_failure_value(
                    error_msg("DateTime", "add_years", k_timestamp_range_error));
            }

            const auto years_to_add =
                try_narrow_int(expect_integer(args[1], "DateTime.add_years", loc));

            if (!years_to_add) {
                return make_failure_value(
                    error_msg("DateTime", "add_years", "year count out of 32-bit integer range"));
            }

            // Use int64_t to avoid signed overflow when adding to tm_year.
            const auto new_year_64 = static_cast<std::int64_t>(tm->tm_year) + *years_to_add;
            const auto new_year = try_narrow_int(new_year_64);

            if (!new_year) {
                return make_failure_value(
                    error_msg("DateTime", "add_years", "resulting date is out of range"));
            }

            // Clamp day for Feb 29 → non-leap year.
            int max_day = tm->tm_mday;

            if (tm->tm_mon == 1 && tm->tm_mday == 29 &&
                !is_leap_year(static_cast<std::int64_t>(*new_year) + 1900)) {
                max_day = 28;
            }

            double unix_time{};

            if (auto err =
                    rebuild_to_unix(*new_year, tm->tm_mon, max_day, *tm, "add_years", unix_time)) {
                return *std::move(err);
            }

            return make_success_value(Value{unix_time});
        })
        // ── DateTime.Period (calendar-aware span) ────────────────────────────
        // Total constructor: a calendar span is just three whole counts and may
        // be negative, so it never fails.
        .func("period", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto years = expect_integer(args[0], "DateTime.period", loc);
            const auto months = expect_integer(args[1], "DateTime.period", loc);
            const auto days = expect_integer(args[2], "DateTime.period", loc);

            return make_period_record(years, months, days);
        })
        // Apply a calendar span to an instant: add the year+month component with
        // month-length day clamping (reusing the add_months/add_years rules),
        // then add the whole-day component as seconds.  Fails when the timestamp
        // or the result leaves the supported 0001-9999 range.
        .func("add_period", 2)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto tm = to_tm(args[0].to_numeric());

            if (!tm) {
                return make_failure_value(
                    error_msg("DateTime", "add_period", k_timestamp_range_error));
            }

            const auto period = read_period(args[1]);

            if (!period) {
                return make_failure_value(
                    error_msg("DateTime", "add_period", "expected a DateTime.Period record"));
            }

            // Bound the year/month components before combining them so the month
            // arithmetic below cannot overflow int64 — anything this large is far
            // outside the calendar range and fails the same way as add_months.
            constexpr std::int64_t k_max_years = 100000;
            constexpr std::int64_t k_max_months = 1200000;

            if (std::abs(period->years) > k_max_years || std::abs(period->months) > k_max_months) {
                return make_failure_value(
                    error_msg("DateTime", "add_period", "resulting date is out of range"));
            }

            const std::int64_t month_delta = (period->years * 12) + period->months;
            const auto shifted = add_months_to_date(static_cast<std::int64_t>(tm->tm_year) + 1900,
                                                    tm->tm_mon + 1, tm->tm_mday, month_delta);

            if (!shifted) {
                return make_failure_value(
                    error_msg("DateTime", "add_period", "resulting date is out of range"));
            }

            double unix_time{};

            if (auto err = rebuild_to_unix(shifted->year - 1900, shifted->month - 1, shifted->day,
                                           *tm, "add_period", unix_time)) {
                return *std::move(err);
            }

            // The day component is a plain wall-clock offset, matching add_days.
            return make_success_value(
                Value{unix_time + (static_cast<double>(period->days) * 86400.0)});
        })
        // Measure the calendar span from start to end as a DateTime.Period, using
        // the date components only (time-of-day is ignored) with Java
        // Period.between borrow semantics.  A start after end yields a negative
        // span.  Fails when either instant leaves the supported range.
        .func("between_dates", 2)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto start = to_tm(args[0].to_numeric());
            const auto end = to_tm(args[1].to_numeric());

            if (!start || !end) {
                return make_failure_value(
                    error_msg("DateTime", "between_dates", k_timestamp_range_error));
            }

            const std::int64_t start_year = static_cast<std::int64_t>(start->tm_year) + 1900;
            const std::int64_t end_year = static_cast<std::int64_t>(end->tm_year) + 1900;
            const int start_month = start->tm_mon + 1;
            const int end_month = end->tm_mon + 1;
            const int start_day = start->tm_mday;
            const int end_day = end->tm_mday;

            std::int64_t total_months =
                proleptic_month(end_year, end_month) - proleptic_month(start_year, start_month);
            std::int64_t days = static_cast<std::int64_t>(end_day) - start_day;

            if (total_months > 0 && days < 0) {
                total_months -= 1;
                // Recount the trailing days exactly from the borrowed anchor date.
                const auto anchor =
                    add_months_to_date(start_year, start_month, start_day, total_months);
                const auto anchor_epoch =
                    anchor ? epoch_day(anchor->year, anchor->month, anchor->day) : std::nullopt;
                const auto end_epoch = epoch_day(static_cast<int>(end_year), end_month, end_day);

                if (!anchor_epoch || !end_epoch) {
                    return make_failure_value(
                        error_msg("DateTime", "between_dates", "resulting span is out of range"));
                }

                days = *end_epoch - *anchor_epoch;
            } else if (total_months < 0 && days > 0) {
                total_months += 1;
                days -= days_in_month_for(end_month, end_year);
            }

            return make_success_value(
                make_period_record(total_months / 12, total_months % 12, days));
        });
}

} // namespace luma
