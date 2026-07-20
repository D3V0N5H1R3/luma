// Array module — transform/query: contains, construct, transpose, and search.
// Split from array_transform.cpp for readability.  Registered by
// register_array_transform() via register_array_transform_query().

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numeric>
#include <ranges>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/overflow.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/array_module.hpp"
#include "runtime/stdlib/collections/value_compare.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

[[nodiscard]] Value array_transpose_impl(const std::shared_ptr<ArrayValue>& src,
                                         const Args& /*unused*/,
                                         [[maybe_unused]] SourceLocation loc) {
    const auto& rows = *src->elements;

    if (rows.empty()) {
        return make_success_value(Value{std::make_shared<ArrayValue>()});
    }

    if (!rows[0].is_array()) {
        return make_failure_value(error_msg("Array", "transpose", "elements must be arrays"));
    }

    const auto col_count = rows[0].as_array()->elements->size();

    for (std::size_t i{1}; i < rows.size(); ++i) {
        if (!rows[i].is_array()) {
            return make_failure_value(error_msg("Array", "transpose", "elements must be arrays"));
        }
        if (rows[i].as_array()->elements->size() != col_count) {
            return make_failure_value(
                error_msg("Array", "transpose", "all rows must have equal length"));
        }
    }

    if (col_count > ResourceLimits::max_array_size ||
        rows.size() > ResourceLimits::max_array_size) {
        return make_failure_value(
            error_msg("Array", "transpose", "result exceeds maximum array size"));
    }

    auto result = std::make_shared<ArrayValue>();
    result->elements->reserve(col_count);

    for (std::size_t c{0}; c < col_count; ++c) {
        auto col = std::make_shared<ArrayValue>();
        col->elements->reserve(rows.size());
        for (const auto& row : rows) {
            col->elements->push_back((*row.as_array()->elements)[c]);
        }
        result->elements->emplace_back(std::move(col));
    }
    return make_success_value(Value{std::move(result)});
}

[[nodiscard]] Value array_binary_search_impl(const std::shared_ptr<ArrayValue>& src,
                                             const Args& args, SourceLocation loc) {
    const auto& elements = *src->elements;
    const auto& target = args[1];

    std::int64_t lo = 0;
    auto hi = static_cast<std::int64_t>(elements.size()) - 1;

    while (lo <= hi) {
        const auto mid = lo + ((hi - lo) / 2);
        const auto& elem = elements[static_cast<std::size_t>(mid)];

        int cmp = 0;
        try {
            cmp = compare_values(elem, target, loc, "Array.binary_search");
        } catch (const RuntimeError&) {
            return make_failure_value(
                error_msg("Array", "binary_search", "elements are not comparable"));
        }

        if (cmp == 0) {
            return make_success_value(Value{mid});
        }
        if (cmp < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    return make_failure_value(error_msg("Array", "binary_search", "value not found"));
}

} // namespace

void register_array_transform_query(const EnvPtr& env) {
    ModuleBuilder{"Array", env}
        .func("contains", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation) -> Value {
                          return Value{std::ranges::any_of(*src->elements, [&](const Value& elem) {
                              return elem.equals(args[1]);
                          })};
                      })
        .func("concat", 2)
        .extract_body(expect_array,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          const auto& b = *expect_array(args[1], "Array.concat", loc)->elements;

                          auto result = clone_array(a);
                          append_bounded(*result->elements, b, loc, "Array.concat");
                          return Value{std::move(result)};
                      })
        .func("range", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto start = expect_integer(args[0], "Array.range", loc);
            const auto end = expect_integer(args[1], "Array.range", loc);

            auto arr = std::make_shared<ArrayValue>();

            // end <= start yields an empty array (matching std::views::iota,
            // whose end sentinel is reached by incrementing start up to end).
            // When end > start, size and materialise the result.  The element
            // count is computed with UNSIGNED subtraction: `end - start` in
            // signed int64 overflows (undefined behaviour) for extreme inputs
            // such as a start near INT64_MIN with an end near INT64_MAX, whereas
            // the unsigned difference is well-defined and — because end > start
            // here — equals the true magnitude.
            if (end > start) {
                const std::uint64_t count =
                    static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(start);

                if (count > ResourceLimits::max_array_size) {
                    return make_failure_value(
                        error_msg("Array", "range", "range exceeds maximum array size"));
                }

                arr->elements->reserve(static_cast<std::size_t>(count));

                auto int_range = std::views::iota(start, end);
                std::ranges::transform(int_range, std::back_inserter(*arr->elements),
                                       [](std::int64_t i) { return Value{i}; });
            }

            return make_success_value(Value{std::move(arr)});
        })
        .func("repeat", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto count = expect_integer(args[1], "Array.repeat", loc);

            if (count < 0) {
                return make_failure_value(error_msg(
                    "Array", "repeat", std::format("count must not be negative, got {}", count)));
            }

            auto arr = std::make_shared<ArrayValue>();

            if (static_cast<std::size_t>(count) > ResourceLimits::max_array_size) {
                return make_failure_value(
                    error_msg("Array", "repeat", "result exceeds maximum array size"));
            }

            arr->elements->assign(static_cast<std::size_t>(count), args[0]);

            return make_success_value(Value{std::move(arr)});
        })
        // Array.windows(array<T>, size) -> array<array<T>>
        .func("windows", 2)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                const auto size = expect_integer(args[1], "Array.windows", loc);

                if (size <= 0) {
                    return make_failure_value(error_msg(
                        "Array", "windows", std::format("size must be positive, got {}", size)));
                }

                const auto& elems = *src->elements;
                auto result = std::make_shared<ArrayValue>();

                if (static_cast<std::size_t>(size) > elems.size()) {
                    return make_success_value(Value{std::move(result)});
                }

                const auto window_count = elems.size() - static_cast<std::size_t>(size) + 1;

                if (window_count > ResourceLimits::max_array_size) {
                    return make_failure_value(
                        error_msg("Array", "windows", "result exceeds maximum array size"));
                }

                result->elements->reserve(window_count);

                for (std::size_t i{0}; i < window_count; ++i) {
                    auto window = std::make_shared<ArrayValue>();
                    window->elements->reserve(static_cast<std::size_t>(size));
                    for (std::int64_t j{0}; j < size; ++j) {
                        window->elements->push_back(elems[i + static_cast<std::size_t>(j)]);
                    }
                    result->elements->emplace_back(std::move(window));
                }
                return make_success_value(Value{std::move(result)});
            })
        .func("intersperse", 2)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                const auto& elems = *src->elements;

                if (elems.size() <= 1) {
                    auto arr = clone_array(src);
                    return Value{std::move(arr)};
                }

                const auto new_size = (elems.size() * 2) - 1;

                if (new_size > ResourceLimits::max_array_size) {
                    throw RuntimeError{
                        error_msg("Array", "intersperse", "result exceeds maximum array size"), loc,
                        std::format("the maximum array size is {} elements",
                                    ResourceLimits::max_array_size)};
                }

                auto result = std::make_shared<ArrayValue>();
                result->elements->reserve(new_size);

                for (std::size_t i{0}; i < elems.size(); ++i) {
                    if (i > 0) {
                        result->elements->push_back(args[1]);
                    }
                    result->elements->push_back(elems[i]);
                }

                return Value{std::move(result)};
            })
        .func("rotate", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto& elems = *src->elements;
                          const auto n = expect_integer(args[1], "Array.rotate", loc);

                          if (elems.empty()) {
                              auto arr = std::make_shared<ArrayValue>();
                              return Value{std::move(arr)};
                          }

                          auto result = std::make_shared<ArrayValue>();
                          result->elements->reserve(elems.size());

                          const auto len = static_cast<std::int64_t>(elems.size());
                          auto shift = n % len;
                          if (shift < 0) {
                              shift += len;
                          }

                          auto int_range = std::views::iota(std::int64_t{0}, len);
                          std::ranges::transform(int_range, std::back_inserter(*result->elements),
                                                 [&elems, shift, len](std::int64_t i) {
                                                     const auto idx = (i + shift) % len;
                                                     return elems[static_cast<std::size_t>(idx)];
                                                 });
                          return Value{std::move(result)};
                      })
        // Array.transpose(array<array<T>>) -> result<array<array<T>>>
        .func("transpose", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return array_transpose_impl(src, args, loc);
                      })
        // Array.binary_search(arr, value) -> result<integer>
        .func("binary_search", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return array_binary_search_impl(src, args, loc);
                      });
}

} // namespace luma
