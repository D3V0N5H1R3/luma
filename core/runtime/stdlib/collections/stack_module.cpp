#include "runtime/stdlib/collections/stack_module.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/container_module_builder.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

void register_stack_ns(const EnvPtr& env) {
    // Register new(), from_array(), and common ops (length, is_empty,
    // to_array, map, filter, reduce, each (reverse), partition, concat).
    const ContainerModuleBuilder<StackValue, true> cmb{"Stack", env, expect_stack,
                                                       ResourceLimits::max_stack_size,
                                                       [](StackValue& c, const Value& v) {
                                                           c.elements.push_back(v);
                                                       }};
    cmb.register_all_common();

    // Stack-specific operations (LIFO — push/pop/peek act on the back).
    cmb.builder()
        .func("push", 2)
        .extract_body(expect_stack,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return push_back_bounded(src, args[1], ResourceLimits::max_stack_size,
                                                   "Stack.push", loc);
                      })
        .func("pop", 1)
        .extract_body(expect_stack,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          return pop_from_end(src, ContainerEnd::Back, "Stack.pop");
                      })
        .func("peek", 1)
        .extract_body(expect_stack,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          return peek_at_end(src, ContainerEnd::Back, "Stack.peek");
                      })
        // Predicate queries mirroring Array.any/all/count/find (see
        // native_function_containers.hpp iter_* / find_with_error_handling).
        .func("any", 2)
        .extract_body(expect_stack,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Stack.any", loc);
                          return iter_any(src->elements.begin(), src->elements.end(), args[1], loc);
                      })
        .func("all", 2)
        .extract_body(expect_stack,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Stack.all", loc);
                          return iter_all(src->elements.begin(), src->elements.end(), args[1], loc);
                      })
        .func("count", 2)
        .extract_body(expect_stack,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Stack.count", loc);
                          return iter_count(src->elements.begin(), src->elements.end(), args[1],
                                            loc);
                      })
        // Membership and search (contains by value equality; find returns the
        // first match from the top, i.e. the back, mirroring Array.find's
        // result<T> "not found" failure).
        .func("contains", 2)
        .extract_body(expect_stack,
                      [](const auto& src, const Args& args, SourceLocation) -> Value {
                          return Value{std::ranges::any_of(
                              src->elements, [&](const Value& v) { return v.equals(args[1]); })};
                      })
        .func("find", 2)
        .extract_body(expect_stack,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Stack.find", loc);
                          return find_with_error_handling(
                              src->elements.rbegin(), src->elements.rend(), args[1],
                              [](const auto& it) { return *it; }, loc);
                      })
        // Pop top elements (from the back) while the predicate holds, returning
        // (popped top-first, remaining stack).  Mirrors Array.take_while and the
        // classic shunting-yard "pop while higher precedence" pattern.
        .func("pop_while", 2)
        .extract_body(expect_stack,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Stack.pop_while", loc);

                          return apply_with_error_handling([&]() -> Value {
                              const auto& elems = src->elements;
                              std::size_t keep = elems.size();
                              auto popped = std::make_shared<ArrayValue>();
                              std::vector<Value> call_args(1);

                              // Walk from the top (back) down while the predicate
                              // holds, collecting popped elements top-first.
                              while (keep > 0) {
                                  call_args[0] = elems[keep - 1];
                                  if (!invoke_callable(args[1], call_args, loc).is_truthy()) {
                                      break;
                                  }
                                  popped->elements->push_back(elems[keep - 1]);
                                  --keep;
                              }

                              auto remaining = std::make_shared<StackValue>();
                              remaining->elements.assign(elems.begin(),
                                                         elems.begin() +
                                                             static_cast<std::ptrdiff_t>(keep));

                              return make_tuple_pair(Value{std::move(popped)},
                                                     Value{std::move(remaining)});
                          });
                      })
        // Reverse the stack's element order (top becomes bottom).
        .func("reverse", 1)
        .extract_body(expect_stack, [](const auto& src, const Args&, SourceLocation) -> Value {
            auto result = std::make_shared<StackValue>();
            result->elements.assign(src->elements.rbegin(), src->elements.rend());
            return Value{std::move(result)};
        });
}

} // namespace luma
