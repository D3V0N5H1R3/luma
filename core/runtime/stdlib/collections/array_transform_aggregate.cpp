// Array module — transform/query: aggregate and slice operations.
// Split from array_transform.cpp for readability.  Registered by
// register_array_transform() via register_array_transform_aggregate().

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

void register_array_transform_aggregate(const EnvPtr& env) {
    ModuleBuilder{"Array", env}
        .func("sum", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          bool all_int{true};
                          std::int64_t int_sum{0};
                          double dbl_sum{0.0};

                          for (const auto& elem : *src->elements) {
                              if (elem.is_integer()) {
                                  if (all_int && would_overflow_add(int_sum, elem.as_integer())) {
                                      all_int = false;
                                  }
                                  if (all_int) {
                                      int_sum += elem.as_integer();
                                  }
                                  dbl_sum += static_cast<double>(elem.as_integer());
                              } else if (elem.is_number()) {
                                  all_int = false;
                                  dbl_sum += elem.as_number();
                              } else {
                                  return make_failure_value(
                                      error_msg("Array", "sum", "non-numeric element"));
                              }
                          }
                          return make_success_value(all_int ? Value{int_sum} : Value{dbl_sum});
                      })
        // Array.product(array<integer | number>) -> result<integer | number>
        // Multiplies every element; an empty array yields the identity 1.  Fails
        // on a non-numeric element.  Mirrors Array.sum: an all-integer product
        // that overflows int64 promotes to a number rather than failing.
        .func("product", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          bool all_int{true};
                          std::int64_t int_prod{1};
                          double dbl_prod{1.0};

                          for (const auto& elem : *src->elements) {
                              if (elem.is_integer()) {
                                  if (all_int && would_overflow_mul(int_prod, elem.as_integer())) {
                                      all_int = false;
                                  }
                                  if (all_int) {
                                      int_prod *= elem.as_integer();
                                  }
                                  dbl_prod *= static_cast<double>(elem.as_integer());
                              } else if (elem.is_number()) {
                                  all_int = false;
                                  dbl_prod *= elem.as_number();
                              } else {
                                  return make_failure_value(
                                      error_msg("Array", "product", "non-numeric element"));
                              }
                          }
                          return make_success_value(all_int ? Value{int_prod} : Value{dbl_prod});
                      })
        .func("min", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation loc) -> Value {
                          if (src->elements->empty()) {
                              return make_failure_value(error_msg("Array", "min", "empty array"));
                          }
                          // compare_values threads loc and surfaces non-numeric /
                          // NaN / incomparable elements as a result failure (rather
                          // than an uncaught RuntimeError), consistent with sort_by
                          // and binary_search.
                          return apply_with_error_handling([&]() -> Value {
                              const auto it = std::ranges::min_element(
                                  *src->elements, [&loc](const Value& a, const Value& b) {
                                      return compare_values(a, b, loc, "Array.min") < 0;
                                  });
                              return *it;
                          });
                      })
        .func("max", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation loc) -> Value {
                          if (src->elements->empty()) {
                              return make_failure_value(error_msg("Array", "max", "empty array"));
                          }
                          return apply_with_error_handling([&]() -> Value {
                              const auto it = std::ranges::max_element(
                                  *src->elements, [&loc](const Value& a, const Value& b) {
                                      return compare_values(a, b, loc, "Array.max") < 0;
                                  });
                              return *it;
                          });
                      })
        // Array.max_by(array<T>, fn(T) -> number) -> optional<T>
        // Returns the element with the greatest key, or none for an empty array.
        // The first element wins ties (strict >), so the result is stable.
        .func("max_by", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.max_by", loc);

                          const auto& elements = *src->elements;
                          if (elements.empty()) {
                              return Value{NullValue{}};
                          }

                          // Seed from the first element so the tracked index is
                          // always valid, then scan the rest with a strict '>'
                          // so ties keep the earliest element.
                          std::vector<Value> call_args(1);
                          call_args[0] = elements.front();
                          std::size_t best_index{0};
                          double best_key = invoke_callable(args[1], call_args, loc).to_numeric();

                          for (std::size_t i = 1; i < elements.size(); ++i) {
                              call_args[0] = elements[i];
                              const double key =
                                  invoke_callable(args[1], call_args, loc).to_numeric();

                              if (key > best_key) {
                                  best_index = i;
                                  best_key = key;
                              }
                          }

                          return elements[best_index];
                      })
        // Array.min_by(array<T>, fn(T) -> number) -> optional<T>
        // Mirror of max_by using strict < so the first minimum wins ties.
        .func("min_by", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.min_by", loc);

                          const auto& elements = *src->elements;
                          if (elements.empty()) {
                              return Value{NullValue{}};
                          }

                          std::vector<Value> call_args(1);
                          call_args[0] = elements.front();
                          std::size_t best_index{0};
                          double best_key = invoke_callable(args[1], call_args, loc).to_numeric();

                          for (std::size_t i = 1; i < elements.size(); ++i) {
                              call_args[0] = elements[i];
                              const double key =
                                  invoke_callable(args[1], call_args, loc).to_numeric();

                              if (key < best_key) {
                                  best_index = i;
                                  best_key = key;
                              }
                          }

                          return elements[best_index];
                      })
        // Array.sum_by(array<T>, fn(T) -> number) -> number
        // Totals the projected key of every element; an empty array sums to 0.
        .func("sum_by", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.sum_by", loc);

                          double total{0.0};
                          std::vector<Value> call_args(1);

                          for (const auto& elem : *src->elements) {
                              call_args[0] = elem;
                              total += invoke_callable(args[1], call_args, loc).to_numeric();
                          }

                          return Value{total};
                      })
        .func("slice", 3)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto raw_from = expect_integer(args[1], "Array.slice", loc);
                          const auto raw_to = expect_integer(args[2], "Array.slice", loc);

                          if (raw_from < 0) {
                              return make_failure_value(
                                  error_msg("Array", "slice", "'from' index must not be negative"));
                          }
                          if (raw_to < 0) {
                              return make_failure_value(
                                  error_msg("Array", "slice", "'to' index must not be negative"));
                          }
                          if (raw_from > raw_to) {
                              return make_failure_value(error_msg(
                                  "Array", "slice", "'from' index must not exceed 'to' index"));
                          }

                          const auto size = static_cast<std::int64_t>(src->elements->size());
                          const auto from = static_cast<std::size_t>(std::min(raw_from, size));
                          const auto to = static_cast<std::size_t>(std::min(raw_to, size));

                          auto arr = std::make_shared<ArrayValue>();
                          if (from < to) {
                              arr->elements->assign(
                                  src->elements->begin() + static_cast<std::ptrdiff_t>(from),
                                  src->elements->begin() + static_cast<std::ptrdiff_t>(to));
                          }
                          return make_success_value(Value{std::move(arr)});
                      })
        .func("index_of", 2)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation) -> Value {
                const auto& target = args[1];
                const auto it = std::ranges::find_if(
                    *src->elements, [&target](const Value& elem) { return elem.equals(target); });

                if (it == src->elements->end()) {
                    return make_failure_value(error_msg("Array", "index_of", "element not found"));
                }
                return make_success_value(
                    Value{static_cast<std::int64_t>(std::distance(src->elements->begin(), it))});
            })
        .func("chunk", 2)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                const auto raw_n = expect_integer(args[1], "Array.chunk", loc);

                if (raw_n <= 0) {
                    return make_failure_value(
                        error_msg("Array", "chunk",
                                  std::format("chunk size must be positive, got {}", raw_n)));
                }

                const auto n = static_cast<std::size_t>(raw_n);
                const auto& elems = *src->elements;

                auto outer = std::make_shared<ArrayValue>();
                outer->elements->reserve((elems.size() + n - 1) / n);

                for (std::size_t i{0}; i < elems.size(); i += n) {
                    auto chunk = std::make_shared<ArrayValue>();
                    const auto chunk_begin = elems.begin() + static_cast<std::ptrdiff_t>(i);
                    const auto chunk_end =
                        elems.begin() + static_cast<std::ptrdiff_t>(std::min(i + n, elems.size()));
                    chunk->elements->assign(chunk_begin, chunk_end);
                    outer->elements->emplace_back(std::move(chunk));
                }
                return make_success_value(Value{std::move(outer)});
            })
        .func("take", 2)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                const auto raw_n = expect_integer(args[1], "Array.take", loc);
                const auto size = static_cast<std::int64_t>(src->elements->size());
                const auto n = static_cast<std::size_t>(std::clamp(raw_n, std::int64_t{0}, size));

                auto arr = std::make_shared<ArrayValue>();
                arr->elements->assign(src->elements->begin(),
                                      src->elements->begin() + static_cast<std::ptrdiff_t>(n));
                return Value{std::move(arr)};
            })
        .func("drop", 2)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                const auto raw_n = expect_integer(args[1], "Array.drop", loc);
                const auto size = static_cast<std::int64_t>(src->elements->size());
                const auto n = static_cast<std::size_t>(std::clamp(raw_n, std::int64_t{0}, size));

                auto arr = std::make_shared<ArrayValue>();
                arr->elements->assign(src->elements->begin() + static_cast<std::ptrdiff_t>(n),
                                      src->elements->end());
                return Value{std::move(arr)};
            })
        .func("split_at", 2)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                const auto raw_i = expect_integer(args[1], "Array.split_at", loc);
                const auto size = static_cast<std::int64_t>(src->elements->size());
                const auto i = static_cast<std::size_t>(std::clamp(raw_i, std::int64_t{0}, size));
                const auto split = src->elements->begin() + static_cast<std::ptrdiff_t>(i);

                auto head = std::make_shared<ArrayValue>();
                head->elements->assign(src->elements->begin(), split);
                auto tail = std::make_shared<ArrayValue>();
                tail->elements->assign(split, src->elements->end());

                return make_tuple_pair(Value{std::move(head)}, Value{std::move(tail)});
            })
        .func("enumerate", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          auto arr = std::make_shared<ArrayValue>();
                          arr->elements->reserve(src->elements->size());

                          for (std::size_t i{0}; i < src->elements->size(); ++i) {
                              arr->elements->push_back(make_tuple_pair(
                                  Value{static_cast<std::int64_t>(i)}, (*src->elements)[i]));
                          }
                          return Value{std::move(arr)};
                      })
        .func("join", 2)
        .extract_body(
            expect_array, [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                (void)expect_string(args[1], "Array.join", loc);
                const auto& sep = args[1].as_string();

                std::string result{};
                result.reserve(src->elements->size() * 8);
                bool first{true};

                for (const auto& elem : *src->elements) {
                    if (!first) {
                        result += sep;
                    }
                    first = false;
                    if (elem.is_string()) {
                        result += elem.as_string();
                    } else {
                        result += elem.to_string();
                    }

                    if (result.size() > ResourceLimits::max_string_size) {
                        throw RuntimeError{
                            error_msg("Array", "join", "result exceeds maximum string size"), loc,
                            "reduce the number of elements or their size"};
                    }
                }
                return Value{std::move(result)};
            });
}

} // namespace luma
