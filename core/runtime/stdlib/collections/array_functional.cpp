// Array module — functional operations.
// Split from array_module.cpp for readability.  Registered by
// register_array_functional() called from register_array_ns().
//
// Note on ContainerOps / ContainerModuleBuilder:
// Array cannot use the generic ContainerOps<ArrayValue> helpers (container_map,
// container_filter) because ArrayValue stores its elements in a
// `std::shared_ptr<std::vector<Value>>` (COW semantics) rather than a plain
// `std::vector<Value> elements` member.  ContainerOps assumes the latter, so
// `container_map<ArrayValue>` would not compile (it would try
// `result->elements.push_back()` instead of `result->elements->push_back()`).
//
// Array.map and Array.filter use `map_with_error_handling` which is aware of
// the shared_ptr indirection.  Array.reduce, Array.each, and Array.partition
// already delegate to the shared helpers (container_reduce, container_each,
// container_partition) by dereferencing the shared_ptr or providing a custom
// inserter — so those operations are already consolidated.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <utility>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/array_module.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

void register_array_functional(const EnvPtr& env) {
    ModuleBuilder{"Array", env}
        .func("map", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.map", loc);
                          return map_with_error_handling(
                              *src->elements, args[1],
                              [](auto& out, const auto&, const Value& fn,
                                 std::vector<Value>& call_args, const SourceLocation& loc) {
                                  out.push_back(invoke_callable(fn, call_args, loc));
                              },
                              loc);
                      })
        .func("filter", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.filter", loc);
                          return map_with_error_handling(
                              *src->elements, args[1],
                              [](auto& out, const auto& elem, const Value& fn,
                                 std::vector<Value>& call_args, const SourceLocation& loc) {
                                  if (invoke_callable(fn, call_args, loc).is_truthy()) {
                                      out.push_back(elem);
                                  }
                              },
                              loc);
                      })
        .func("each", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.each", loc);
                          return container_each(src->elements->begin(), src->elements->end(),
                                                args[1], loc);
                      })
        .func("find", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.find", loc);
                          return find_with_error_handling(
                              src->elements->begin(), src->elements->end(), args[1],
                              [](const auto& it) { return *it; }, loc);
                      })
        .func("find_index", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.find_index", loc);
                          return find_with_error_handling(
                              src->elements->begin(), src->elements->end(), args[1],
                              [&](auto it) {
                                  return Value{static_cast<std::int64_t>(
                                      std::distance(src->elements->begin(), std::move(it)))};
                              },
                              loc);
                      })
        .func("all", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.all", loc);
                          return iter_all(src->elements->begin(), src->elements->end(), args[1],
                                          loc);
                      })
        .func("any", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.any", loc);
                          return iter_any(src->elements->begin(), src->elements->end(), args[1],
                                          loc);
                      })
        .func("count", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.count", loc);
                          return iter_count(src->elements->begin(), src->elements->end(), args[1],
                                            loc);
                      })
        .func("reduce", 3)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[2], "Array.reduce", loc);
                          return container_reduce(*src->elements, args[1], args[2], loc);
                      })
        .func("take_while", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.take_while", loc);
                          auto arr = std::make_shared<ArrayValue>();

                          return apply_with_error_handling([&]() -> Value {
                              std::vector<Value> call_args(1);
                              for (const auto& elem : *src->elements) {
                                  call_args[0] = elem;
                                  if (!invoke_callable(args[1], call_args, loc).is_truthy()) {
                                      break;
                                  }
                                  arr->elements->push_back(elem);
                              }
                              return Value{std::move(arr)};
                          });
                      })
        .func("drop_while", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.drop_while", loc);
                          auto arr = std::make_shared<ArrayValue>();
                          bool dropping{true};

                          return apply_with_error_handling([&]() -> Value {
                              std::vector<Value> call_args(1);
                              for (const auto& elem : *src->elements) {
                                  if (dropping) {
                                      call_args[0] = elem;
                                      if (invoke_callable(args[1], call_args, loc).is_truthy()) {
                                          continue;
                                      }
                                      dropping = false;
                                  }
                                  arr->elements->push_back(elem);
                              }
                              return Value{std::move(arr)};
                          });
                      })
        .func("flat_map", 2)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                expect_callable(args[1], "Array.flat_map", loc);
                auto result = std::make_shared<ArrayValue>();

                return apply_with_error_handling([&]() -> Value {
                    std::vector<Value> call_args(1);
                    for (const auto& elem : *src->elements) {
                        call_args[0] = elem;
                        const auto mapped = invoke_callable(args[1], call_args, loc);

                        if (mapped.is_array()) {
                            append_bounded(*result->elements, *mapped.as_array()->elements, loc,
                                           "Array.flat_map");
                        } else {
                            append_bounded(*result->elements, std::span<const Value>{&mapped, 1},
                                           loc, "Array.flat_map");
                        }
                    }
                    return Value{std::move(result)};
                });
            })
        .func("partition", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.partition", loc);
                          constexpr auto push_back = [](ArrayValue& a, const Value& v) {
                              a.elements->push_back(v);
                          };
                          return container_partition<ArrayValue>(*src->elements, args[1], push_back,
                                                                 loc);
                      })
        .func("scan", 3)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          auto accumulator = args[1];
                          expect_callable(args[2], "Array.scan", loc);

                          auto result = std::make_shared<ArrayValue>();
                          result->elements->reserve(src->elements->size() + 1);
                          result->elements->push_back(accumulator);

                          return apply_with_error_handling([&]() -> Value {
                              std::vector<Value> call_args(2);
                              for (const auto& elem : *src->elements) {
                                  call_args[0] = accumulator;
                                  call_args[1] = elem;
                                  accumulator = invoke_callable(args[2], call_args, loc);
                                  result->elements->push_back(accumulator);
                              }
                              return Value{std::move(result)};
                          });
                      })
        // Array.group_by(array<T>, fn(T) -> string) -> dictionary<array<T>>
        .func("group_by", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.group_by", loc);

                          auto dict = std::make_shared<DictionaryValue>();

                          return apply_with_error_handling([&]() -> Value {
                              std::vector<Value> fn_args(1);
                              for (const auto& elem : *src->elements) {
                                  fn_args[0] = elem;
                                  const auto key =
                                      invoke_callable(args[1], fn_args, loc).to_string();

                                  auto* existing = dict->find(key);
                                  if (existing && existing->is_array()) {
                                      existing->as_array()->elements->push_back(elem);
                                  } else {
                                      auto group = std::make_shared<ArrayValue>();
                                      group->elements->push_back(elem);
                                      dict->set(key, Value{std::move(group)});
                                  }
                              }
                              return Value{std::move(dict)};
                          });
                      })
        .func("find_last", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.find_last", loc);
                          return find_with_error_handling(
                              src->elements->rbegin(), src->elements->rend(), args[1],
                              [](const auto& it) { return *it; }, loc);
                      })
        .func("find_last_index", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Array.find_last_index", loc);
                          return find_with_error_handling(
                              src->elements->rbegin(), src->elements->rend(), args[1],
                              [&](auto it) {
                                  const auto reverse_pos = static_cast<std::size_t>(
                                      std::distance(src->elements->rbegin(), std::move(it)));
                                  return Value{static_cast<std::int64_t>(src->elements->size() - 1 -
                                                                         reverse_pos)};
                              },
                              loc);
                      });
}

} // namespace luma
