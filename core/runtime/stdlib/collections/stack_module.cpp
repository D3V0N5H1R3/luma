#include "runtime/stdlib/collections/stack_module.hpp"

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
        .extract_body(expect_stack, [](const auto& src, const Args&, SourceLocation) -> Value {
            return peek_at_end(src, ContainerEnd::Back, "Stack.peek");
        });
}

} // namespace luma
