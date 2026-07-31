#ifndef LUMA_STDLIB_CONTAINER_OPS_HPP
#define LUMA_STDLIB_CONTAINER_OPS_HPP

// ═══════════════════════════════════════════════════════════════════
// Unified Container Operations
// ═══════════════════════════════════════════════════════════════════
//
// Registers a standard set of operations (length, is_empty, to_array,
// map, filter, reduce, each, partition, concat) for any container type
// that stores its elements in a `std::vector<Value>`.  This eliminates
// the near-identical implementations across the Queue, Stack, and Set
// modules.
//
// Usage:
//
//   ContainerOps<QueueValue> ops{"Queue", expect_queue,
//                                ResourceLimits::max_queue_size};
//   ops.register_all(env);
//
// Each container module still defines its own type-specific operations
// (e.g. Queue.enqueue, Stack.push, Set.add) separately.

#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <string_view>

#include "runtime/interpreter/environment.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

// ContainerOps<Container, ReverseEach, Unique> — registers common operations
// for a container type.  Container must have an `elements` member of type
// `std::vector<Value>`.
//
// Template parameters:
//   Container   — The value type (QueueValue, StackValue, etc.).
//   ReverseEach — If true, each() iterates in reverse order (default: false).
//   Unique      — If true, the container maintains a set-uniqueness invariant
//                 (default: false).  This changes the functional ops:
//                   • map    deduplicates its output in first-seen order via a
//                            temporary ValueSet (distinct inputs may collapse
//                            onto equal outputs), instead of using `inserter`.
//                   • filter / partition push results directly (no dedup): the
//                            input is already unique and a subset stays unique,
//                            so the `inserter`'s dedup would be redundant work.
//                 With Unique=false the `inserter` drives all three ops as before.
//
// Constructor parameters:
//   prefix      — Module name prefix (e.g. "Queue", "Stack").
//   extractor   — Function to extract the container from a Value
//                  (e.g. expect_queue, expect_stack).
//   max_size    — Maximum container size from ResourceLimits.
//   inserter    — Functor to insert a value into the container.
//                  Signature: void(Container&, const Value&).
template <typename Container, bool ReverseEach = false, bool Unique = false> class ContainerOps {
public:
    using ExtractFn = std::function<std::shared_ptr<Container>(const Value&, std::string_view,
                                                               const SourceLocation&)>;
    using InsertFn = std::function<void(Container&, const Value&)>;

    ContainerOps(std::string_view prefix, ExtractFn extractor, std::size_t max_size,
                 InsertFn inserter = default_inserter)
        : prefix_{prefix},
          extractor_{std::move(extractor)},
          max_size_{max_size},
          inserter_{std::move(inserter)} {}

    // Register all common operations at once.
    void register_all(const EnvPtr& env) const {
        register_length(env);
        register_is_empty(env);
        register_to_array(env);
        register_map(env);
        register_filter(env);
        register_reduce(env);
        register_each(env);
        register_partition(env);
        register_concat(env);
    }

private:
    std::string prefix_;
    ExtractFn extractor_;
    std::size_t max_size_;
    InsertFn inserter_;

    static void default_inserter(Container& c, const Value& v) {
        c.elements.push_back(v);
    }

    [[nodiscard]] std::string name(std::string_view suffix) const {
        return std::string{prefix_} + "." + std::string{suffix};
    }

    void register_length(const EnvPtr& env) const {
        ModuleBuilder{prefix_, env}
            .func("length", 1)
            .extract_body(extractor_, [](const auto& src, const Args&, SourceLocation) -> Value {
                return Value{static_cast<std::int64_t>(src->elements.size())};
            });
    }

    void register_is_empty(const EnvPtr& env) const {
        ModuleBuilder{prefix_, env}
            .func("is_empty", 1)
            .extract_body(extractor_, [](const auto& src, const Args&, SourceLocation) -> Value {
                return Value{src->elements.empty()};
            });
    }

    void register_to_array(const EnvPtr& env) const {
        ModuleBuilder{prefix_, env}
            .func("to_array", 1)
            .extract_body(extractor_, [](const auto& src, const Args&, SourceLocation) -> Value {
                auto arr = std::make_shared<ArrayValue>();
                *arr->elements = src->elements;
                return Value{std::move(arr)};
            });
    }

    void register_map(const EnvPtr& env) const {
        auto ins = inserter_;
        ModuleBuilder{prefix_, env}.func("map", 2).extract_body(
            extractor_, [ins](const auto& src, const Args& args, SourceLocation loc) -> Value {
                if constexpr (Unique) {
                    return container_map_unique<Container>(src->elements, args[1], loc);
                } else {
                    return container_map<Container>(src->elements, args[1], ins, loc);
                }
            });
    }

    void register_filter(const EnvPtr& env) const {
        auto ins = inserter_;
        ModuleBuilder{prefix_, env}
            .func("filter", 2)
            .extract_body(
                extractor_, [ins](const auto& src, const Args& args, SourceLocation loc) -> Value {
                    if constexpr (Unique) {
                        return container_filter<Container>(src->elements, args[1], default_inserter,
                                                           loc);
                    } else {
                        return container_filter<Container>(src->elements, args[1], ins, loc);
                    }
                });
    }

    void register_reduce(const EnvPtr& env) const {
        ModuleBuilder{prefix_, env}
            .func("reduce", 3)
            .extract_body(extractor_,
                          [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                              return container_reduce(src->elements, args[1], args[2], loc);
                          });
    }

    void register_each(const EnvPtr& env) const {
        ModuleBuilder{prefix_, env}.func("each", 2).extract_body(
            extractor_, [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                if constexpr (ReverseEach) {
                    return container_each(src->elements.rbegin(), src->elements.rend(), args[1],
                                          loc);
                } else {
                    return container_each(src->elements.begin(), src->elements.end(), args[1], loc);
                }
            });
    }

    void register_partition(const EnvPtr& env) const {
        auto ins = inserter_;
        ModuleBuilder{prefix_, env}
            .func("partition", 2)
            .extract_body(
                extractor_, [ins](const auto& src, const Args& args, SourceLocation loc) -> Value {
                    if constexpr (Unique) {
                        return container_partition<Container>(src->elements, args[1],
                                                              default_inserter, loc);
                    } else {
                        return container_partition<Container>(src->elements, args[1], ins, loc);
                    }
                });
    }

    void register_concat(const EnvPtr& env) const {
        auto extractor = extractor_;
        auto max_size = max_size_;
        auto full_name = name("concat");
        ModuleBuilder{prefix_, env}
            .func("concat", 2)
            .extract_body(extractor_,
                          [extractor, max_size, full_name](const auto& a, const Args& args,
                                                           SourceLocation loc) -> Value {
                              auto b = extractor(args[1], full_name, loc);

                              validate_container_size(a->elements.size(), b->elements.size(),
                                                      max_size, full_name, loc);

                              if constexpr (Unique) {
                                  // Set-like containers keep the uniqueness
                                  // invariant: drop elements of `b` already
                                  // present in `a` (documented contract).
                                  return container_concat_unique<Container>(a->elements,
                                                                            b->elements);
                              } else {
                                  auto result = std::make_shared<Container>();
                                  result->elements.reserve(a->elements.size() + b->elements.size());
                                  result->elements.insert(result->elements.end(),
                                                          a->elements.begin(), a->elements.end());
                                  result->elements.insert(result->elements.end(),
                                                          b->elements.begin(), b->elements.end());
                                  return Value{std::move(result)};
                              }
                          });
    }
};

} // namespace luma

#endif // LUMA_STDLIB_CONTAINER_OPS_HPP
