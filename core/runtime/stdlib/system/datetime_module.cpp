#include "runtime/stdlib/system/datetime_module.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/narrow_int.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/system/datetime_codec.hpp"
#include "runtime/stdlib/system/datetime_internal.hpp"
#include "runtime/stdlib/system/platform_time.hpp"

using namespace luma::datetime_detail;

namespace luma {

namespace {

constexpr std::string_view k_offset_range_error = "offset must be between -720 and +840 minutes";

[[nodiscard]] constexpr bool valid_offset(double offset_minutes) noexcept {
    return offset_minutes >= datetime_limits::k_min_offset_minutes &&
           offset_minutes <= datetime_limits::k_max_offset_minutes;
}

// Format a UTC offset as "+HH:MM" or "-HH:MM".
[[nodiscard]] std::string format_offset(double offset_minutes) {
    const char sign = offset_minutes >= 0 ? '+' : '-';
    const int total = static_cast<int>(std::abs(offset_minutes));
    const int hours = total / 60;
    const int mins = total % 60;

    return std::format("{}{:02}:{:02}", sign, hours, mins);
}

// Validate a UTC offset (in minutes) is within the supported range.
// Returns a failure Value when out of range, or std::nullopt on success.
[[nodiscard]] std::optional<Value> check_offset(double offset_minutes, std::string_view func_name) {
    if (!valid_offset(offset_minutes)) {
        return make_failure_value(error_msg("DateTime", func_name, k_offset_range_error));
    }

    return std::nullopt;
}

// Build a raw_body lambda for a DateTime field accessor that extracts
// a single component from a Unix timestamp.
template <typename Extractor>
[[nodiscard]] auto time_field_body(std::string func_name, Extractor extractor) {
    return [name = std::move(func_name), extractor](std::span<const Value> args,
                                                    SourceLocation /*loc*/) -> Value {
        const auto tm = to_tm(args[0].to_numeric());

        if (!tm) {
            return make_failure_value(std::format("{}: {}", name, k_timestamp_range_error));
        }

        return make_success_value(extractor(*tm));
    };
}

struct DatetimeFields {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
};

// Validates and narrows six raw int64 date/time components into a DatetimeFields struct.
// Returns a failure Value if any field is out of range, or std::nullopt on success.
[[nodiscard]] std::optional<Value>
validate_datetime_fields(std::int64_t raw_year, std::int64_t raw_month, std::int64_t raw_day,
                         std::int64_t raw_hour, std::int64_t raw_min, std::int64_t raw_sec,
                         const std::string& func_name, DatetimeFields& out) {
    for (const auto v : {raw_year, raw_month, raw_day, raw_hour, raw_min, raw_sec}) {
        if (!try_narrow_int(v)) {
            return make_failure_value(
                std::format("{}: argument out of 32-bit integer range", func_name));
        }
    }

    const auto year = static_cast<int>(raw_year);
    const auto month = static_cast<int>(raw_month);
    const auto day = static_cast<int>(raw_day);
    const auto hour = static_cast<int>(raw_hour);
    const auto min = static_cast<int>(raw_min);
    const auto sec = static_cast<int>(raw_sec);

    // std::tm stores tm_year as (year - 1900); reject years outside the
    // module's supported 0001-9999 calendar range so that conversion cannot
    // signed-overflow on adversarial input (matching parse_iso8601).
    if (year < 1 || year > 9999) {
        return make_failure_value(std::format("{}: year out of range (1-9999)", func_name));
    }

    if (month < 1 || month > 12) {
        return make_failure_value(std::format("{}: month out of range (1-12)", func_name));
    }

    const int max_day = days_in_month_for(month, year);

    if (day < 1 || day > max_day) {
        return make_failure_value(
            std::format("{}: day out of range (1-{}) for month {}", func_name, max_day, month));
    }

    if (hour < 0 || hour > 23) {
        return make_failure_value(std::format("{}: hour out of range (0-23)", func_name));
    }

    if (min < 0 || min > 59) {
        return make_failure_value(std::format("{}: minute out of range (0-59)", func_name));
    }

    if (sec < 0 || sec > 59) {
        return make_failure_value(std::format("{}: second out of range (0-59)", func_name));
    }

    out = {.year = year, .month = month, .day = day, .hour = hour, .min = min, .sec = sec};
    return std::nullopt;
}

// Builds a std::tm from validated DatetimeFields.
[[nodiscard]] std::tm fields_to_tm(const DatetimeFields& f) {
    std::tm tm{};
    tm.tm_year = f.year - 1900;
    tm.tm_mon = f.month - 1;
    tm.tm_mday = f.day;
    tm.tm_hour = f.hour;
    tm.tm_min = f.min;
    tm.tm_sec = f.sec;
    return tm;
}

// Builds a UTC Unix timestamp from six raw integer date/time components
// (args[0..5]).  Shared by DateTime.from_parts and DateTime.from_parts_offset.
// Returns a failure Value on invalid input, or std::nullopt on success, writing
// the timestamp to out_unix.
[[nodiscard]] std::optional<Value> build_unix_from_parts(std::span<const Value> args,
                                                         const std::string& func_name,
                                                         SourceLocation loc, double& out_unix) {
    const auto raw_year = expect_integer(args[0], func_name, loc);
    const auto raw_month = expect_integer(args[1], func_name, loc);
    const auto raw_day = expect_integer(args[2], func_name, loc);
    const auto raw_hour = expect_integer(args[3], func_name, loc);
    const auto raw_min = expect_integer(args[4], func_name, loc);
    const auto raw_sec = expect_integer(args[5], func_name, loc);

    DatetimeFields fields{};

    if (auto err = validate_datetime_fields(raw_year, raw_month, raw_day, raw_hour, raw_min,
                                            raw_sec, func_name, fields)) {
        return err;
    }

    auto tm = fields_to_tm(fields);
    const auto unix_time = tm_to_unix(tm);

    if (!unix_time) {
        return make_failure_value(std::format("{}: invalid date/time", func_name));
    }

    out_unix = *unix_time;
    return std::nullopt;
}

} // namespace

static void register_datetime_parsing(const EnvPtr& env);

void register_datetime_ns(const EnvPtr& env) {
    static const auto start_time = std::chrono::steady_clock::now();

    // Data-driven registration of DateTime field accessors.
    struct TimeField {
        const char* name;
        Value (*extractor)(const std::tm&);
    };

    const TimeField time_fields[] = {
        {.name = "year",
         .extractor = [](const std::tm& t) -> Value {
             return Value{static_cast<std::int64_t>(t.tm_year + 1900)};
         }},
        {.name = "month",
         .extractor = [](const std::tm& t) -> Value {
             return Value{static_cast<std::int64_t>(t.tm_mon + 1)};
         }},
        {.name = "day_of_month",
         .extractor = [](const std::tm& t) -> Value {
             return Value{static_cast<std::int64_t>(t.tm_mday)};
         }},
        {.name = "hour",
         .extractor = [](const std::tm& t) -> Value {
             return Value{static_cast<std::int64_t>(t.tm_hour)};
         }},
        {.name = "minute",
         .extractor = [](const std::tm& t) -> Value {
             return Value{static_cast<std::int64_t>(t.tm_min)};
         }},
        {.name = "second",
         .extractor = [](const std::tm& t) -> Value {
             return Value{static_cast<std::int64_t>(t.tm_sec)};
         }},
        {.name = "day_of_week",
         .extractor = [](const std::tm& t) -> Value {
             // tm_wday: 0=Sunday. Convert to 1=Monday..7=Sunday.
             const int dow{t.tm_wday == 0 ? 7 : t.tm_wday};
             return Value{static_cast<std::int64_t>(dow)};
         }},
    };

    ModuleBuilder builder{"DateTime", env};

    for (const auto& field : time_fields) {
        builder.func(field.name, 1)
            .raw_body(time_field_body(std::format("DateTime.{}", field.name), field.extractor));
    }

    builder.func("milliseconds_since_start", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            const auto now = std::chrono::steady_clock::now();
            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

            return Value{static_cast<double>(ms)};
        })
        .func("now_unix", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            const auto now = std::chrono::system_clock::now();
            const auto epoch = now.time_since_epoch();
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

            return Value{static_cast<double>(seconds)};
        })
        .func("is_leap_year", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto year = expect_integer(args[0], "DateTime.is_leap_year", loc);

            return Value{is_leap_year(year)};
        })
        .func("days_in_month", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto year = expect_integer(args[0], "DateTime.days_in_month", loc);
            const auto month = expect_integer(args[1], "DateTime.days_in_month", loc);

            const int day_count = days_in_month_for(static_cast<int>(month), year);

            if (day_count == 0) {
                return make_failure_value(error_msg("DateTime", "days_in_month", "invalid month"));
            }

            return make_success_value(Value{static_cast<std::int64_t>(day_count)});
        })
        .func("from_parts", 6)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            double unix_time{};

            if (auto err = build_unix_from_parts(args, "DateTime.from_parts", loc, unix_time)) {
                return *std::move(err);
            }

            return make_success_value(Value{unix_time});
        })
        .func("to_parts", 1)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto tm = to_tm(args[0].to_numeric());

            if (!tm) {
                return make_failure_value(
                    error_msg("DateTime", "to_parts", k_timestamp_range_error));
            }

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "TimeParts";
            rec->fields.emplace_back("year", Value{static_cast<std::int64_t>(tm->tm_year + 1900)});
            rec->fields.emplace_back("month", Value{static_cast<std::int64_t>(tm->tm_mon + 1)});
            rec->fields.emplace_back("day", Value{static_cast<std::int64_t>(tm->tm_mday)});
            rec->fields.emplace_back("hour", Value{static_cast<std::int64_t>(tm->tm_hour)});
            rec->fields.emplace_back("minute", Value{static_cast<std::int64_t>(tm->tm_min)});
            rec->fields.emplace_back("second", Value{static_cast<std::int64_t>(tm->tm_sec)});

            return make_success_value(Value{std::move(rec)});
        })
        .func("to_iso_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto formatted = datetime::format_iso8601(args[0].to_numeric());

            if (!formatted) {
                return make_failure_value(
                    error_msg("DateTime", "to_iso_string", k_timestamp_range_error));
            }

            return make_success_value(Value{*formatted});
        })
        .func("now_iso_string", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            const auto now = std::chrono::system_clock::now();
            const auto epoch = now.time_since_epoch();
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

            const auto formatted = datetime::format_iso8601(static_cast<double>(seconds));

            if (!formatted) {
                return make_failure_value(
                    error_msg("DateTime", "now_iso_string", "current time out of supported range"));
            }

            return make_success_value(Value{*formatted});
        });

    register_datetime_arithmetic(env);
    register_datetime_parsing(env);
}

// DateTime ISO parsing, timezone offsets, formatting, and comparisons.
static void register_datetime_parsing(const EnvPtr& env) {
    ModuleBuilder builder{"DateTime", env};

    builder.func("from_iso_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "DateTime.from_iso_string", loc);

            const auto& s = args[0].as_string();
            const auto parsed = datetime::parse_iso8601(s);

            if (!parsed.success) {
                return make_failure_value(error_msg("DateTime", "from_iso_string",
                                                    std::format("{}: {}", parsed.error, s)));
            }

            return make_success_value(Value{parsed.unix_seconds});
        })
        .func("to_offset", 2)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto ts = args[0].to_numeric();
            const auto offset_minutes = args[1].to_numeric();

            if (auto err = check_offset(offset_minutes, "to_offset")) {
                return *std::move(err);
            }

            return make_success_value(Value{ts + (offset_minutes * 60.0)});
        })
        .func("from_offset", 2)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto local_ts = args[0].to_numeric();
            const auto offset_minutes = args[1].to_numeric();

            if (auto err = check_offset(offset_minutes, "from_offset")) {
                return *std::move(err);
            }

            return make_success_value(Value{local_ts - (offset_minutes * 60.0)});
        })
        .func("to_iso_string_offset", 2)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const auto ts = args[0].to_numeric();
            const auto offset_minutes = args[1].to_numeric();

            if (auto err = check_offset(offset_minutes, "to_iso_string_offset")) {
                return *std::move(err);
            }

            const auto adjusted = ts + (offset_minutes * 60.0);
            const auto tm = to_tm(adjusted);

            if (!tm) {
                return make_failure_value(
                    error_msg("DateTime", "to_iso_string_offset", k_timestamp_range_error));
            }

            const auto suffix =
                (offset_minutes == 0.0) ? std::string{"Z"} : format_offset(offset_minutes);

            return make_success_value(Value{datetime::format_iso8601_with_suffix(*tm, suffix)});
        })
        .func("from_parts_offset", 7)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto offset_minutes = args[6].to_numeric();

            if (auto err = check_offset(offset_minutes, "from_parts_offset")) {
                return *std::move(err);
            }

            double unix_time{};

            if (auto err =
                    build_unix_from_parts(args, "DateTime.from_parts_offset", loc, unix_time)) {
                return *std::move(err);
            }

            // Convert from local (offset) time to UTC.
            return make_success_value(Value{unix_time - (offset_minutes * 60.0)});
        })
        .func("offset_hours", 1)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            return Value{args[0].to_numeric() * 60.0};
        })
        .func("format", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[1], "DateTime.format", loc);

            const auto tm = to_tm(args[0].to_numeric());

            if (!tm) {
                return make_failure_value(error_msg("DateTime", "format", k_timestamp_range_error));
            }

            auto result = args[1].as_string();

            // Replace placeholders from longest to shortest to avoid partial matches.
            const auto replace_all = [](std::string& str, const std::string& from,
                                        const std::string& to) {
                std::size_t pos{0};

                while ((pos = str.find(from, pos)) != std::string::npos) {
                    str.replace(pos, from.size(), to);
                    pos += to.size();
                }
            };

            replace_all(result, "YYYY", std::format("{:04}", tm->tm_year + 1900));
            replace_all(result, "MM", std::format("{:02}", tm->tm_mon + 1));
            replace_all(result, "DD", std::format("{:02}", tm->tm_mday));
            replace_all(result, "hh", std::format("{:02}", tm->tm_hour));
            replace_all(result, "mm", std::format("{:02}", tm->tm_min));
            replace_all(result, "ss", std::format("{:02}", tm->tm_sec));

            return make_success_value(Value{std::move(result)});
        })
        .func("is_before", 2)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            return Value{args[0].to_numeric() < args[1].to_numeric()};
        })
        .func("is_after", 2)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            return Value{args[0].to_numeric() > args[1].to_numeric()};
        })
        .func("break_duration", 1)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            const double total = args[0].to_numeric();
            const double abs_total = std::abs(total);

            // Round to the nearest millisecond.  Non-finite or astronomically
            // large inputs saturate to INT64_MAX so the conversion below is
            // always defined behaviour.
            const double ms_d = std::isfinite(abs_total) ? std::round(abs_total * 1000.0) : 0.0;
            // static_cast<double>(INT64_MAX) rounds up to 2^63, which is *not*
            // representable as an int64_t — so compare with >= and saturate
            // rather than casting a clamped 2^63 (which would be undefined).
            constexpr double k_ms_ceiling =
                static_cast<double>(std::numeric_limits<std::int64_t>::max());
            const std::int64_t total_ms = (ms_d >= k_ms_ceiling)
                                              ? std::numeric_limits<std::int64_t>::max()
                                              : static_cast<std::int64_t>(ms_d);

            const std::int64_t days = total_ms / 86'400'000;
            const std::int64_t hours = (total_ms / 3'600'000) % 24;
            const std::int64_t minutes = (total_ms / 60'000) % 60;
            const std::int64_t seconds = (total_ms / 1'000) % 60;
            const std::int64_t milliseconds = total_ms % 1'000;

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "Duration";
            rec->fields.emplace_back("days", Value{days});
            rec->fields.emplace_back("hours", Value{hours});
            rec->fields.emplace_back("minutes", Value{minutes});
            rec->fields.emplace_back("seconds", Value{seconds});
            rec->fields.emplace_back("milliseconds", Value{milliseconds});
            rec->fields.emplace_back("negative", Value{total < 0.0 && total_ms != 0});

            return Value{std::move(rec)};
        })
        .func("format_duration", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record()) {
                throw RuntimeError{"DateTime.format_duration: expected a DateTime.Duration record",
                                   loc};
            }

            const auto& rec = args[0].as_record();
            const auto component = [&rec](std::string_view name) -> std::int64_t {
                const Value* v = rec->find_field(name);
                return (v != nullptr && v->is_integer()) ? v->as_integer() : 0;
            };

            const std::int64_t days = component("days");
            const std::int64_t hours = component("hours");
            const std::int64_t minutes = component("minutes");
            const std::int64_t seconds = component("seconds");
            const std::int64_t milliseconds = component("milliseconds");

            const Value* neg = rec->find_field("negative");
            const bool negative = neg != nullptr && neg->is_bool() && neg->as_bool();

            std::vector<std::string> parts;
            if (days > 0) {
                parts.push_back(std::format("{}d", days));
            }
            if (hours > 0) {
                parts.push_back(std::format("{}h", hours));
            }
            if (minutes > 0) {
                parts.push_back(std::format("{}m", minutes));
            }
            if (seconds > 0) {
                parts.push_back(std::format("{}s", seconds));
            }
            if (milliseconds > 0) {
                parts.push_back(std::format("{}ms", milliseconds));
            }

            std::string out;
            if (parts.empty()) {
                out = "0s";
            } else {
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    if (i > 0) {
                        out += ' ';
                    }
                    out += parts[i];
                }
            }
            if (negative) {
                out = "-" + out;
            }

            return Value{std::move(out)};
        });
}

} // namespace luma
