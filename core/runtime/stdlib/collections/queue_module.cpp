#include "runtime/stdlib/collections/queue_module.hpp"

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/container_module_builder.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

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
        });
}

} // namespace luma
