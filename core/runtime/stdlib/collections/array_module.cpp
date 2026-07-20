#include "runtime/stdlib/collections/array_module.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

void register_array_ns(const EnvPtr& env) {
    // --- Access and mutation operations ---

    auto push_impl = [](const auto& src, const Args& args, SourceLocation loc) -> Value {
        validate_container_size(src->elements->size(), ResourceLimits::max_array_size, "Array.push",
                                loc);

        auto arr = clone_array(src, 1);
        arr->elements->push_back(args[1]);
        return Value{std::move(arr)};
    };

    ModuleBuilder{"Array", env}
        .func("length", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          return Value{static_cast<std::int64_t>(src->elements->size())};
                      })
        .func("push", 2)
        .extract_body(expect_array, push_impl)
        // Alias: Array.append is the same operation as Array.push.
        .func("append", 2)
        .extract_body(expect_array, push_impl)
        .func("insert_at", 3)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                const auto index = expect_integer_index(args[1], "Array.insert_at", loc);

                if (index < 0 || index > static_cast<std::int64_t>(src->elements->size())) {
                    return make_failure_value(ErrorMessages::index_out_of_bounds(
                        "Array", "insert_at", index, src->elements->size()));
                }

                validate_container_size(src->elements->size(), ResourceLimits::max_array_size,
                                        "Array.insert_at", loc);

                auto arr = clone_array(src, 1);
                arr->elements->insert(arr->elements->begin() + static_cast<std::ptrdiff_t>(index),
                                      args[2]);
                return make_success_value(Value{std::move(arr)});
            })
        .func("set", 3)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto index = expect_integer_index(args[1], "Array.set", loc);

                          if (auto fail = check_bounds(index, src->elements->size())) {
                              return *std::move(fail);
                          }

                          auto arr = clone_array(src);
                          (*arr->elements)[static_cast<std::size_t>(index)] = args[2];
                          return make_success_value(Value{std::move(arr)});
                      })
        .func("remove_at", 2)
        .extract_body(
            expect_array,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                const auto index = expect_integer_index(args[1], "Array.remove_at", loc);

                if (auto fail = check_bounds(index, src->elements->size())) {
                    return *std::move(fail);
                }

                const auto i = static_cast<std::size_t>(index);
                const auto removed = (*src->elements)[i];

                auto arr = clone_array(src);
                arr->elements->erase(arr->elements->begin() + static_cast<std::ptrdiff_t>(i));

                return make_success_value(make_tuple_pair(Value{std::move(arr)}, removed));
            })
        .func("pop", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          if (auto fail = check_not_empty(*src->elements, "Array.pop")) {
                              return *std::move(fail);
                          }

                          auto arr = std::make_shared<ArrayValue>();
                          arr->elements->reserve(src->elements->size() - 1);
                          arr->elements->assign(src->elements->begin(), src->elements->end() - 1);

                          return make_success_value(
                              make_tuple_pair(Value{std::move(arr)}, src->elements->back()));
                      })
        .func("first", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          if (auto fail = check_not_empty(*src->elements, "Array.first")) {
                              return *std::move(fail);
                          }
                          return make_success_value(src->elements->front());
                      })
        .func("last", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          if (auto fail = check_not_empty(*src->elements, "Array.last")) {
                              return *std::move(fail);
                          }
                          return make_success_value(src->elements->back());
                      })
        .func("get", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto i = expect_integer_index(args[1], "Array.get", loc);

                          if (auto fail = check_bounds(i, src->elements->size(), "Array.get")) {
                              return *std::move(fail);
                          }
                          return make_success_value((*src->elements)[static_cast<std::size_t>(i)]);
                      })
        .func("is_empty", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          return Value{src->elements->empty()};
                      })
        .func("compact", 1)
        .extract_body(expect_array, [](const auto& src, const Args&, SourceLocation) -> Value {
            auto arr = std::make_shared<ArrayValue>();

            std::ranges::copy_if(*src->elements, std::back_inserter(*arr->elements),
                                 [](const Value& elem) { return elem.is_some(); });

            return Value{std::move(arr)};
        });

    // --- Functional operations (map, filter, reduce, etc.) ---
    register_array_functional(env);

    // --- Transform and query operations (sort, zip, slice, etc.) ---
    register_array_transform(env);
}

} // namespace luma
