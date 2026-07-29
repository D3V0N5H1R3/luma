#include "runtime/stdlib/collections/queue_module.hpp"

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/container_module_builder.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/native_function_containers.hpp"

namespace luma {

void register_queue_ns(const EnvPtr& env) {
    // Register new(), from_array(), and common ops (length, is_empty,
    // to_array, map, filter, reduce, each, partition, concat).
    const ContainerModuleBuilder<QueueValue> cmb{"Queue", env, expect_queue,
                                                 ResourceLimits::max_queue_size};
    cmb.register_all_common();

    // Queue-specific operations (FIFO — enqueue at back, dequeue/peek at front).
    cmb.builder()
        .func("enqueue", 2)
        .extract_body(expect_queue,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return push_back_bounded(src, args[1], ResourceLimits::max_queue_size,
                                                   "Queue.enqueue", loc);
                      })
        .func("dequeue", 1)
        .extract_body(expect_queue,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          return pop_from_end(src, ContainerEnd::Front, "Queue.dequeue");
                      })
        .func("peek", 1)
        .extract_body(expect_queue, [](const auto& src, const Args&, SourceLocation) -> Value {
            return peek_at_end(src, ContainerEnd::Front, "Queue.peek");
        })
        // Queue.contains(q, value) -> boolean — value equality, FIFO order.
        .func("contains", 2)
        .extract_body(expect_queue,
                      [](const auto& src, const Args& args, SourceLocation) -> Value {
                          for (const auto& v : src->elements) {
                              if (v.equals(args[1])) {
                                  return Value{true};
                              }
                          }

                          return Value{false};
                      })
        // Queue.find(q, fn) -> result<T> — first FIFO match.
        .func("find", 2)
        .extract_body(expect_queue,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Queue.find", loc);

                          return find_with_error_handling(
                              src->elements.begin(), src->elements.end(), args[1],
                              [](const auto& it) { return *it; }, loc);
                      })
        // Queue.any(q, fn) -> result<boolean>
        .func("any", 2)
        .extract_body(expect_queue,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Queue.any", loc);

                          return iter_any(src->elements.begin(), src->elements.end(), args[1], loc);
                      })
        // Queue.all(q, fn) -> result<boolean>
        .func("all", 2)
        .extract_body(expect_queue,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Queue.all", loc);

                          return iter_all(src->elements.begin(), src->elements.end(), args[1], loc);
                      })
        // Queue.count(q, fn) -> result<integer>
        .func("count", 2)
        .extract_body(expect_queue,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Queue.count", loc);

                          return iter_count(src->elements.begin(), src->elements.end(), args[1],
                                            loc);
                      })
        // Queue.reverse(q) -> queue — a new queue with the element order reversed.
        .func("reverse", 1)
        .extract_body(expect_queue, [](const auto& src, const Args&, SourceLocation) -> Value {
            auto result = std::make_shared<QueueValue>();
            result->elements.assign(src->elements.rbegin(), src->elements.rend());

            return Value{std::move(result)};
        });
}

} // namespace luma
