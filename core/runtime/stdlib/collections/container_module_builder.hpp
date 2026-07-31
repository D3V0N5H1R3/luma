#ifndef LUMA_STDLIB_CONTAINER_MODULE_BUILDER_HPP
#define LUMA_STDLIB_CONTAINER_MODULE_BUILDER_HPP

// ═══════════════════════════════════════════════════════════════════
// Container Module Builder — Template Helpers for Stdlib Containers
// ═══════════════════════════════════════════════════════════════════
//
// Provides reusable template functions that encapsulate the common
// registration patterns found across container stdlib modules (Queue,
// Stack, Set).  Each module can use these helpers
// for the boilerplate operations while still defining its own
// container-specific operations (e.g. Queue.enqueue, Stack.push).
//
// The helpers work with the existing ModuleBuilder / FuncBuilder DSL,
// ContainerOps, and NativeFunction infrastructure.
//
// Typical usage:
//
//   ContainerModuleBuilder<QueueValue> cmb{
//       "Queue", env, expect_queue, ResourceLimits::max_queue_size};
//   cmb.register_new();
//   cmb.register_from_array();
//   cmb.register_common_ops();
//
//   // Then register Queue-specific ops via ModuleBuilder as before.

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "runtime/interpreter/environment.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/container_ops.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

// ─── ContainerModuleBuilder ───
//
// Template parameters:
//   Container    — The value type (QueueValue, StackValue, etc.).
//                  Must have a `std::vector<Value> elements` member.
//   ReverseEach  — If true, each() iterates in reverse (default: false).
//
// This class provides registration helpers for the common container
// operations that are nearly identical across modules:
//   - new()       — creates an empty container
//   - from_array() — converts an array to the container type
//   - Common ops via ContainerOps (length, is_empty, to_array, map,
//     filter, reduce, each, partition, concat)
//
// Typical usage:
//
//   void register_mycontainer_ns(const EnvPtr& env) {
//       ContainerModuleBuilder<MyContainerValue> builder{
//           "MyContainer", env, expect_mycontainer, ResourceLimits::max_container_size};
//       builder.register_all_common();
//       // Add container-specific functions here with ModuleBuilder{"MyContainer", env}
//   }
//
template <typename Container, bool ReverseEach = false, bool Unique = false>
class ContainerModuleBuilder {
public:
    using ExtractFn = std::function<std::shared_ptr<Container>(const Value&, std::string_view,
                                                               const SourceLocation&)>;
    using InsertFn = std::function<void(Container&, const Value&)>;

    ContainerModuleBuilder(std::string_view module_name, const EnvPtr& env, ExtractFn extractor,
                           std::size_t max_size, InsertFn inserter = default_inserter)
        : module_name_{module_name},
          env_{env},
          extractor_{std::move(extractor)},
          max_size_{max_size},
          inserter_{std::move(inserter)} {}

    // Register Module.new() — creates an empty container.
    void register_new() const {
        ModuleBuilder{module_name_, env_}.func("new", 0).raw_body(
            []([[maybe_unused]] std::span<const Value> args,
               [[maybe_unused]] SourceLocation loc) -> Value {
                return Value{std::make_shared<Container>()};
            });
    }

    // Register Module.from_array() — creates a container from an array.
    // Elements are copied directly into the container's `elements` vector.
    void register_from_array() const {
        // Copy module_name_ to a local std::string for lambda capture — the
        // lambda outlives this method call and needs its own copy.
        auto mod = std::string{module_name_};
        auto max_size = max_size_;
        ModuleBuilder{module_name_, env_}
            .func("from_array", 1)
            .raw_body([mod, max_size](std::span<const Value> args, SourceLocation loc) -> Value {
                auto func_name = mod + ".from_array";
                const auto& src = *expect_array(args[0], func_name, loc)->elements;

                validate_container_size(src.size(), 0, max_size, func_name, loc);

                auto container = std::make_shared<Container>();
                container->elements = src;
                return Value{std::move(container)};
            });
    }

    // Register all common operations via ContainerOps:
    // length, is_empty, to_array, map, filter, reduce, each, partition, concat.
    void register_common_ops() const {
        ContainerOps<Container, ReverseEach, Unique>{module_name_, extractor_, max_size_, inserter_}
            .register_all(env_);
    }

    // Convenience: register new() + from_array() + common ops all at once.
    void register_all_common() const {
        register_new();
        register_from_array();
        register_common_ops();
    }

    // Return a ModuleBuilder for registering additional module-specific
    // functions using the fluent API.
    [[nodiscard]] ModuleBuilder builder() const {
        return ModuleBuilder{module_name_, env_};
    }

    // Accessors for use in custom registrations.
    [[nodiscard]] std::string_view module_name() const {
        return module_name_;
    }

    [[nodiscard]] const EnvPtr& env() const {
        return env_;
    }

    [[nodiscard]] const ExtractFn& extractor() const {
        return extractor_;
    }

    [[nodiscard]] std::size_t max_size() const {
        return max_size_;
    }

private:
    std::string module_name_;
    EnvPtr env_;
    ExtractFn extractor_;
    std::size_t max_size_;
    InsertFn inserter_;

    static void default_inserter(Container& c, const Value& v) {
        c.elements.push_back(v);
    }
};

} // namespace luma

#endif // LUMA_STDLIB_CONTAINER_MODULE_BUILDER_HPP
