#include <algorithm>
#include <cmath>
#include <cstdint>
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
        {.name = "difference_hours", .scale = 3600.0, .op = ScaleOp::difference},
        {.name = "difference_days", .scale = 86400.0, .op = ScaleOp::difference},
        {.name = "add_milliseconds", .scale = 1.0, .op = ScaleOp::add},
        {.name = "add_seconds", .scale = 1.0, .op = ScaleOp::add},
        {.name = "add_hours", .scale = 3600.0, .op = ScaleOp::add},
        {.name = "add_days", .scale = 86400.0, .op = ScaleOp::add},
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
        });
}

} // namespace luma
