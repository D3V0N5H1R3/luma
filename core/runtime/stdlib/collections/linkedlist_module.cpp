#include "runtime/stdlib/collections/linkedlist_module.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/overflow.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/container_module_builder.hpp"
#include "runtime/stdlib/collections/value_compare.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

// Whether an index's upper bound is exclusive (idx < count, for access and
// removal) or inclusive (idx <= count, for insertion at the end).
enum class IndexBound {
    Exclusive,
    Inclusive
};

// Internal helper for linked-list node chain operations.
// Groups the low-level pointer manipulation used throughout the module.
struct LinkedListHelper {
    // Deep-copy the node chain, returning both the new head and its tail node.
    // Callers that immediately append or splice reuse the tail instead of
    // walking the freshly cloned chain a second time with tail_of().
    [[nodiscard]] static std::pair<std::shared_ptr<LinkedListNode>, std::shared_ptr<LinkedListNode>>
    clone_chain_with_tail(const std::shared_ptr<LinkedListNode>& head) {
        if (!head) {
            return {nullptr, nullptr};
        }

        auto new_head = std::make_shared<LinkedListNode>(head->value);

        auto dst = new_head;
        auto src = head->next;

        while (src) {
            auto node = std::make_shared<LinkedListNode>(src->value);
            node->prev = dst;

            dst->next = node;

            dst = node;
            src = src->next;
        }

        return {std::move(new_head), std::move(dst)};
    }

    // Deep-copy the node chain.
    [[nodiscard]] static std::shared_ptr<LinkedListNode>
    clone_chain(const std::shared_ptr<LinkedListNode>& head) {
        return std::move(clone_chain_with_tail(head).first);
    }

    // Walk to the tail of a chain.
    [[nodiscard]] static std::shared_ptr<LinkedListNode>
    tail_of(const std::shared_ptr<LinkedListNode>& head) {
        if (!head) {
            return nullptr;
        }

        auto cur = head;

        while (cur->next) {
            cur = cur->next;
        }

        return cur;
    }

    // Traverse the linked list to the node at the given index.
    // Caller must ensure 0 <= idx < list size.
    [[nodiscard]] static std::shared_ptr<LinkedListNode>
    node_at(const std::shared_ptr<LinkedListNode>& head, std::int64_t idx) {
        auto cur = head;
        for (std::int64_t i = 0; i < idx && cur; ++i) {
            cur = cur->next;
        }
        return cur;
    }

    // Clone an entire linked list (deep-copy chain + size), also returning the
    // tail node of the copy so callers appending to the end need not re-walk it.
    [[nodiscard]] static std::pair<std::shared_ptr<LinkedListValue>,
                                   std::shared_ptr<LinkedListNode>>
    clone_list_with_tail(const std::shared_ptr<LinkedListValue>& src) {
        auto result = std::make_shared<LinkedListValue>();
        auto [head, tail] = clone_chain_with_tail(src->head);
        result->head = std::move(head);
        result->count_ = src->count_;

        return {std::move(result), std::move(tail)};
    }

    // Clone an entire linked list (deep-copy chain + size).
    [[nodiscard]] static std::shared_ptr<LinkedListValue>
    clone_list(const std::shared_ptr<LinkedListValue>& src) {
        return std::move(clone_list_with_tail(src).first);
    }

    // Append a node to the tail of a list being built.
    static void append_node(std::shared_ptr<LinkedListValue>& list,
                            std::shared_ptr<LinkedListNode>& tail,
                            std::shared_ptr<LinkedListNode> node) {
        if (!list->head) {
            list->head = node;
        } else {
            node->prev = tail;
            tail->next = node;
        }

        tail = std::move(node);

        ++list->count_;
    }

    // Returns a failure value if the linked list is empty, or std::nullopt if non-empty.
    // `function_name` is the already-qualified "LinkedList.<fn>" name, so the
    // single-argument empty_container overload is used to avoid a doubled prefix.
    [[nodiscard]] static std::optional<Value>
    check_not_empty(const std::shared_ptr<LinkedListValue>& list, std::string_view function_name) {
        if (!list->head) {
            return make_failure_value(ErrorMessages::empty_container(function_name),
                                      std::string{error_codes::empty_container},
                                      std::string{function_name});
        }
        return std::nullopt;
    }

    // Returns a failure value if `idx` is out of range for a list of `count`
    // elements, or std::nullopt if it is valid.  With IndexBound::Inclusive the
    // upper bound is inclusive (idx == count is allowed), as insert_at requires;
    // with IndexBound::Exclusive idx must be strictly less than count.
    // `function_name` is the unqualified operation name (e.g. "at"); the
    // "LinkedList." prefix is added.
    [[nodiscard]] static std::optional<Value> check_index(std::int64_t idx, std::size_t count,
                                                          std::string_view function_name,
                                                          IndexBound bound) {
        const auto count_i = static_cast<std::int64_t>(count);
        const bool out_of_range = bound == IndexBound::Inclusive ? (idx < 0 || idx > count_i)
                                                                 : (idx < 0 || idx >= count_i);

        if (out_of_range) {
            return make_failure_value(
                ErrorMessages::index_out_of_bounds("LinkedList", function_name, idx, count),
                std::string{error_codes::index_out_of_bounds},
                std::format("LinkedList.{}", function_name));
        }

        return std::nullopt;
    }

    // Clone a list and append a value to the tail.
    // Used by LinkedList.append and LinkedList.push (which are aliases).
    [[nodiscard]] static std::shared_ptr<LinkedListValue>
    clone_and_append(const std::shared_ptr<LinkedListValue>& src, const Value& value) {
        // clone_list_with_tail hands back the copy's tail, so we append without
        // walking the freshly cloned chain a second time.
        auto [result, t] = clone_list_with_tail(src);

        auto node = std::make_shared<LinkedListNode>(value);

        if (t) {
            t->next = node;
            node->prev = t;
        } else {
            result->head = node;
        }

        ++result->count_;

        return result;
    }

    // Clone a list and prepend a value to the head.
    [[nodiscard]] static std::shared_ptr<LinkedListValue>
    clone_and_prepend(const std::shared_ptr<LinkedListValue>& src, const Value& value) {
        auto result = clone_list(src);

        auto node = std::make_shared<LinkedListNode>(value);
        node->next = result->head;

        if (result->head) {
            result->head->prev = node;
        }

        result->head = node;

        ++result->count_;

        return result;
    }

    // Clone a list and remove the first node.  Caller must ensure the list is non-empty.
    [[nodiscard]] static std::shared_ptr<LinkedListValue>
    clone_and_remove_first(const std::shared_ptr<LinkedListValue>& src) {
        auto ll = clone_list(src);

        ll->head = ll->head->next;

        if (ll->head) {
            ll->head->prev.reset();
        }

        --ll->count_;

        return ll;
    }

    // Clone a list and remove the last node.  Caller must ensure the list is non-empty.
    [[nodiscard]] static std::shared_ptr<LinkedListValue>
    clone_and_remove_last(const std::shared_ptr<LinkedListValue>& src) {
        auto [ll, t] = clone_list_with_tail(src);

        auto prev_node = t->prev.lock();

        if (prev_node) {
            prev_node->next.reset();
        } else {
            ll->head.reset();
        }

        --ll->count_;

        return ll;
    }

    // Build a new linked list from a vector of values.
    [[nodiscard]] static std::shared_ptr<LinkedListValue>
    from_elements(const std::vector<Value>& elems) {
        auto ll = std::make_shared<LinkedListValue>();
        std::shared_ptr<LinkedListNode> tail;

        for (const auto& elem : elems) {
            append_node(ll, tail, std::make_shared<LinkedListNode>(elem));
        }

        return ll;
    }
};

} // namespace

void register_linkedlist_ns(const EnvPtr& env) {
    // Register new() via builder.  Remaining operations use node-chain
    // traversal so they cannot use ContainerOps (which assumes a
    // contiguous elements vector).
    const ContainerModuleBuilder<LinkedListValue> cmb{"LinkedList", env, expect_list,
                                                      ResourceLimits::max_linked_list_size,
                                                      [](LinkedListValue&, const Value&) {
                                                      }};
    cmb.register_new();

    // LinkedList-specific operations.
    auto append_impl = [](const auto& src, const Args& args, SourceLocation loc) -> Value {
        validate_container_size(src->count_, ResourceLimits::max_linked_list_size,
                                "LinkedList.append", loc);

        return Value{LinkedListHelper::clone_and_append(src, args[1])};
    };

    cmb.builder()
        .func("from_array", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "LinkedList.from_array", loc)->elements;

            validate_container_size(elems.size(), 0, ResourceLimits::max_linked_list_size,
                                    "LinkedList.from_array", loc);

            return Value{LinkedListHelper::from_elements(elems)};
        })
        .func("length", 1)
        .extract_body(expect_list,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          return Value{static_cast<std::int64_t>(src->count_)};
                      })
        .func("is_empty", 1)
        .extract_body(expect_list,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          return Value{src->count_ == 0};
                      })
        .func("prepend", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          validate_container_size(src->count_, ResourceLimits::max_linked_list_size,
                                                  "LinkedList.prepend", loc);

                          return Value{LinkedListHelper::clone_and_prepend(src, args[1])};
                      })
        .func("append", 2)
        .extract_body(expect_list, append_impl)
        // Alias: LinkedList.push is the same operation as LinkedList.append.
        .func("push", 2)
        .extract_body(expect_list, append_impl)
        .func("first", 1)
        .extract_body(expect_list,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          if (auto fail =
                                  LinkedListHelper::check_not_empty(src, "LinkedList.first")) {
                              return *std::move(fail);
                          }

                          return make_success_value(src->head->value);
                      })
        .func("last", 1)
        .extract_body(expect_list,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          if (auto fail =
                                  LinkedListHelper::check_not_empty(src, "LinkedList.last")) {
                              return *std::move(fail);
                          }

                          auto t = LinkedListHelper::tail_of(src->head);

                          return make_success_value(t->value);
                      })
        .func("at", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto idx = expect_integer(args[1], "LinkedList.at", loc);

                          if (auto fail = LinkedListHelper::check_index(idx, src->count_, "at",
                                                                        IndexBound::Exclusive)) {
                              return *std::move(fail);
                          }

                          auto cur = LinkedListHelper::node_at(src->head, idx);

                          return make_success_value(cur->value);
                      })
        .func("remove_first", 1)
        .extract_body(
            expect_list,
            [](const auto& src, const Args&, SourceLocation) -> Value {
                if (auto fail = LinkedListHelper::check_not_empty(src, "LinkedList.remove_first")) {
                    return *std::move(fail);
                }

                return make_success_value(Value{LinkedListHelper::clone_and_remove_first(src)});
            })
        .func("remove_last", 1)
        .extract_body(
            expect_list,
            [](const auto& src, const Args&, SourceLocation) -> Value {
                if (auto fail = LinkedListHelper::check_not_empty(src, "LinkedList.remove_last")) {
                    return *std::move(fail);
                }

                return make_success_value(Value{LinkedListHelper::clone_and_remove_last(src)});
            })
        .func("contains", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation) -> Value {
                          for (const Value& v : std::ranges::subrange{
                                   LinkedListNodeIterator{src->head}, LinkedListNodeIterator{}}) {
                              if (v.equals(args[1])) {
                                  return Value{true};
                              }
                          }

                          return Value{false};
                      })
        .func("reverse", 1)
        .extract_body(expect_list,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          auto result = std::make_shared<LinkedListValue>();
                          result->count_ = src->count_;

                          auto cur = src->head;

                          while (cur) {
                              auto node = std::make_shared<LinkedListNode>(cur->value);
                              node->next = result->head;

                              if (result->head) {
                                  result->head->prev = node;
                              }

                              result->head = node;

                              cur = cur->next;
                          }

                          return Value{std::move(result)};
                      })
        .func("to_array", 1)
        .extract_body(expect_list,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          auto result = std::make_shared<ArrayValue>();

                          for (const Value& v : std::ranges::subrange{
                                   LinkedListNodeIterator{src->head}, LinkedListNodeIterator{}}) {
                              result->elements->push_back(v);
                          }

                          return Value{std::move(result)};
                      })
        .func("concat", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_list(args[0], "LinkedList.concat", loc);
            auto b = expect_list(args[1], "LinkedList.concat", loc);

            validate_container_size(a->count_, b->count_, ResourceLimits::max_linked_list_size,
                                    "LinkedList.concat", loc);

            auto [result, t] = LinkedListHelper::clone_list_with_tail(a);

            auto b_chain = LinkedListHelper::clone_chain(b->head);

            if (t) {
                t->next = b_chain;

                if (b_chain) {
                    b_chain->prev = t;
                }
            } else {
                result->head = b_chain;
            }

            result->count_ += b->count_;

            return Value{std::move(result)};
        })
        .func("insert_at", 3)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto idx = expect_integer(args[1], "LinkedList.insert_at", loc);

                          if (auto fail = LinkedListHelper::check_index(
                                  idx, src->count_, "insert_at", IndexBound::Inclusive)) {
                              return *std::move(fail);
                          }

                          validate_container_size(src->count_, ResourceLimits::max_linked_list_size,
                                                  "LinkedList.insert_at", loc);

                          auto ll = LinkedListHelper::clone_list(src);

                          auto node = std::make_shared<LinkedListNode>(args[2]);

                          if (idx == 0) {
                              node->next = ll->head;

                              if (ll->head) {
                                  ll->head->prev = node;
                              }

                              ll->head = node;
                          } else {
                              auto cur = LinkedListHelper::node_at(ll->head, idx - 1);

                              node->next = cur->next;
                              node->prev = cur;

                              if (cur->next) {
                                  cur->next->prev = node;
                              }

                              cur->next = node;
                          }

                          ++ll->count_;

                          return make_success_value(Value{std::move(ll)});
                      })
        .func("remove_at", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto idx = expect_integer(args[1], "LinkedList.remove_at", loc);

                          if (auto fail = LinkedListHelper::check_index(
                                  idx, src->count_, "remove_at", IndexBound::Exclusive)) {
                              return *std::move(fail);
                          }

                          auto ll = LinkedListHelper::clone_list(src);

                          if (idx == 0) {
                              ll->head = ll->head->next;

                              if (ll->head) {
                                  ll->head->prev.reset();
                              }
                          } else {
                              auto cur = LinkedListHelper::node_at(ll->head, idx);

                              auto prev_node = cur->prev.lock();

                              if (prev_node) {
                                  prev_node->next = cur->next;
                              }

                              if (cur->next) {
                                  cur->next->prev = prev_node;
                              }
                          }

                          --ll->count_;

                          return make_success_value(Value{std::move(ll)});
                      })
        .func("map", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          auto result = std::make_shared<LinkedListValue>();
                          std::shared_ptr<LinkedListNode> tail;

                          return iter_map(
                              LinkedListNodeIterator{src->head}, LinkedListNodeIterator{}, args[1],
                              [&](Value mapped) {
                                  LinkedListHelper::append_node(
                                      result, tail,
                                      std::make_shared<LinkedListNode>(std::move(mapped)));
                              },
                              [&]() -> Value { return Value{std::move(result)}; }, loc);
                      })
        .func("filter", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          auto result = std::make_shared<LinkedListValue>();
                          std::shared_ptr<LinkedListNode> tail;

                          return iter_filter(
                              LinkedListNodeIterator{src->head}, LinkedListNodeIterator{}, args[1],
                              [&](const Value& v) {
                                  LinkedListHelper::append_node(
                                      result, tail, std::make_shared<LinkedListNode>(v));
                              },
                              [&]() -> Value { return Value{std::move(result)}; }, loc);
                      })
        .func("each", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return container_each(LinkedListNodeIterator{src->head},
                                                LinkedListNodeIterator{}, args[1], loc);
                      })
        .func("reduce", 3)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return iter_reduce(LinkedListNodeIterator{src->head},
                                             LinkedListNodeIterator{}, args[1], args[2], loc);
                      })
        .func("partition", 2)
        .extract_body(
            expect_list,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                auto matches = std::make_shared<LinkedListValue>();
                auto rest = std::make_shared<LinkedListValue>();
                std::shared_ptr<LinkedListNode> matches_tail;
                std::shared_ptr<LinkedListNode> rest_tail;

                return iter_partition(
                    LinkedListNodeIterator{src->head}, LinkedListNodeIterator{}, args[1],
                    [&](const Value& v) {
                        LinkedListHelper::append_node(matches, matches_tail,
                                                      std::make_shared<LinkedListNode>(v));
                    },
                    [&](const Value& v) {
                        LinkedListHelper::append_node(rest, rest_tail,
                                                      std::make_shared<LinkedListNode>(v));
                    },
                    [&]() -> Value {
                        return make_tuple_pair(Value{std::move(matches)}, Value{std::move(rest)});
                    },
                    loc);
            })
        // LinkedList.find(list, fn) -> result<T>
        .func("find", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return find_with_error_handling(
                              LinkedListNodeIterator{src->head}, LinkedListNodeIterator{}, args[1],
                              [](const auto& it) { return *it; }, loc);
                      })
        // LinkedList.zip(a, b) -> linked_list
        .func("zip", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_list(args[0], "LinkedList.zip", loc);
            auto b = expect_list(args[1], "LinkedList.zip", loc);

            auto result = std::make_shared<LinkedListValue>();
            std::shared_ptr<LinkedListNode> tail;

            auto ca = a->head;
            auto cb = b->head;

            while (ca && cb) {
                LinkedListHelper::append_node(
                    result, tail,
                    std::make_shared<LinkedListNode>(make_tuple_pair(ca->value, cb->value)));

                ca = ca->next;
                cb = cb->next;
            }

            return Value{std::move(result)};
        })
        // LinkedList.sort(list, fn) -> result<linked_list>
        // Sorts elements by extracting to a vector, sorting with the comparator,
        // and building a new list.
        .func("sort", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return apply_with_error_handling([&]() -> Value {
                              // Collect elements into a vector.
                              std::vector<Value> elems;
                              elems.reserve(src->count_);

                              for (const Value& v :
                                   std::ranges::subrange{LinkedListNodeIterator{src->head},
                                                         LinkedListNodeIterator{}}) {
                                  elems.push_back(v);
                              }

                              // Sort using the comparator.  Reuse one 2-element buffer
                              // across all comparisons instead of allocating a vector per
                              // comparison.  stable_sort, not sort: the comparator is
                              // arbitrary user code that need not form a strict weak
                              // ordering, which is undefined behaviour for
                              // std::ranges::sort (introsort can read/write past the
                              // buffer).  stable_sort's merge path stays in bounds, so a
                              // malformed comparator yields an unspecified-but-safe order
                              // — the same hardening applied to Array.sort.
                              std::vector<Value> call_args(2);
                              std::ranges::stable_sort(
                                  elems, [&](const Value& lhs, const Value& rhs) -> bool {
                                      call_args[0] = lhs;
                                      call_args[1] = rhs;

                                      const auto cmp = invoke_callable(args[1], call_args, loc);

                                      return cmp.is_truthy();
                                  });

                              // Build a new linked list from sorted elements.
                              return Value{LinkedListHelper::from_elements(elems)};
                          });
                      })
        // LinkedList.unique(list) -> linked_list
        .func("unique", 1)
        .extract_body(expect_list,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          auto result = std::make_shared<LinkedListValue>();
                          std::shared_ptr<LinkedListNode> tail;

                          dedup_in_order(std::ranges::subrange{LinkedListNodeIterator{src->head},
                                                               LinkedListNodeIterator{}},
                                         [&](const Value& v) {
                                             LinkedListHelper::append_node(
                                                 result, tail, std::make_shared<LinkedListNode>(v));
                                         });

                          return Value{std::move(result)};
                      })
        // LinkedList.any(list, fn) -> result<boolean>
        .func("any", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "LinkedList.any", loc);

                          return iter_any(LinkedListNodeIterator{src->head},
                                          LinkedListNodeIterator{}, args[1], loc);
                      })
        // LinkedList.all(list, fn) -> result<boolean>
        .func("all", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "LinkedList.all", loc);

                          return iter_all(LinkedListNodeIterator{src->head},
                                          LinkedListNodeIterator{}, args[1], loc);
                      })
        // LinkedList.count(list, fn) -> result<integer>
        .func("count", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "LinkedList.count", loc);

                          return iter_count(LinkedListNodeIterator{src->head},
                                            LinkedListNodeIterator{}, args[1], loc);
                      })
        // LinkedList.take(list, n) -> linked_list — first n elements (n clamped).
        .func("take", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto n = expect_integer(args[1], "LinkedList.take", loc);
                          const auto keep =
                              n < 0 ? std::size_t{0}
                                    : std::min(static_cast<std::size_t>(n), src->count_);

                          auto result = std::make_shared<LinkedListValue>();
                          std::shared_ptr<LinkedListNode> tail;
                          std::size_t taken{0};

                          for (const Value& v : std::ranges::subrange{
                                   LinkedListNodeIterator{src->head}, LinkedListNodeIterator{}}) {
                              if (taken >= keep) {
                                  break;
                              }

                              LinkedListHelper::append_node(result, tail,
                                                            std::make_shared<LinkedListNode>(v));
                              ++taken;
                          }

                          return Value{std::move(result)};
                      })
        // LinkedList.drop(list, n) -> linked_list — all but the first n (n clamped).
        .func("drop", 2)
        .extract_body(expect_list,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto n = expect_integer(args[1], "LinkedList.drop", loc);
                          const auto skip =
                              n < 0 ? std::size_t{0}
                                    : std::min(static_cast<std::size_t>(n), src->count_);

                          auto result = std::make_shared<LinkedListValue>();
                          std::shared_ptr<LinkedListNode> tail;
                          std::size_t seen{0};

                          for (const Value& v : std::ranges::subrange{
                                   LinkedListNodeIterator{src->head}, LinkedListNodeIterator{}}) {
                              if (seen++ < skip) {
                                  continue;
                              }

                              LinkedListHelper::append_node(result, tail,
                                                            std::make_shared<LinkedListNode>(v));
                          }

                          return Value{std::move(result)};
                      })
        // LinkedList.take_while(list, fn) -> result<linked_list>
        .func("take_while", 2)
        .extract_body(
            expect_list,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                expect_callable(args[1], "LinkedList.take_while", loc);

                return apply_with_error_handling([&]() -> Value {
                    auto result = std::make_shared<LinkedListValue>();
                    std::shared_ptr<LinkedListNode> tail;
                    std::vector<Value> call_args(1);

                    for (const Value& v : std::ranges::subrange{LinkedListNodeIterator{src->head},
                                                                LinkedListNodeIterator{}}) {
                        call_args[0] = v;

                        if (!invoke_callable(args[1], call_args, loc).is_truthy()) {
                            break;
                        }

                        LinkedListHelper::append_node(result, tail,
                                                      std::make_shared<LinkedListNode>(v));
                    }

                    return Value{std::move(result)};
                });
            })
        // LinkedList.drop_while(list, fn) -> result<linked_list>
        .func("drop_while", 2)
        .extract_body(
            expect_list,
            [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                expect_callable(args[1], "LinkedList.drop_while", loc);

                return apply_with_error_handling([&]() -> Value {
                    auto result = std::make_shared<LinkedListValue>();
                    std::shared_ptr<LinkedListNode> tail;
                    std::vector<Value> call_args(1);
                    bool dropping{true};

                    for (const Value& v : std::ranges::subrange{LinkedListNodeIterator{src->head},
                                                                LinkedListNodeIterator{}}) {
                        if (dropping) {
                            call_args[0] = v;

                            if (invoke_callable(args[1], call_args, loc).is_truthy()) {
                                continue;
                            }

                            dropping = false;
                        }

                        LinkedListHelper::append_node(result, tail,
                                                      std::make_shared<LinkedListNode>(v));
                    }

                    return Value{std::move(result)};
                });
            })
        // LinkedList.min(list) -> result<T> — fails on an empty list.
        .func("min", 1)
        .extract_body(
            expect_list,
            [](const auto& src, const Args&, SourceLocation loc) -> Value {
                if (src->count_ == 0) {
                    return make_failure_value(error_msg("LinkedList", "min", "empty list"));
                }

                return apply_with_error_handling([&]() -> Value {
                    const Value* best{nullptr};

                    for (const Value& v : std::ranges::subrange{LinkedListNodeIterator{src->head},
                                                                LinkedListNodeIterator{}}) {
                        if (best == nullptr ||
                            compare_values(v, *best, loc, "LinkedList.min") < 0) {
                            best = &v;
                        }
                    }

                    return *best;
                });
            })
        // LinkedList.max(list) -> result<T> — fails on an empty list.
        .func("max", 1)
        .extract_body(
            expect_list,
            [](const auto& src, const Args&, SourceLocation loc) -> Value {
                if (src->count_ == 0) {
                    return make_failure_value(error_msg("LinkedList", "max", "empty list"));
                }

                return apply_with_error_handling([&]() -> Value {
                    const Value* best{nullptr};

                    for (const Value& v : std::ranges::subrange{LinkedListNodeIterator{src->head},
                                                                LinkedListNodeIterator{}}) {
                        if (best == nullptr ||
                            compare_values(v, *best, loc, "LinkedList.max") > 0) {
                            best = &v;
                        }
                    }

                    return *best;
                });
            })
        // LinkedList.sum(list) -> result<integer | number> — fails on non-numeric.
        .func("sum", 1)
        .extract_body(expect_list,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          bool all_int{true};
                          std::int64_t int_sum{0};
                          double dbl_sum{0.0};

                          for (const Value& v : std::ranges::subrange{
                                   LinkedListNodeIterator{src->head}, LinkedListNodeIterator{}}) {
                              if (v.is_integer()) {
                                  if (all_int && would_overflow_add(int_sum, v.as_integer())) {
                                      all_int = false;
                                  }
                                  if (all_int) {
                                      int_sum += v.as_integer();
                                  }
                                  dbl_sum += static_cast<double>(v.as_integer());
                              } else if (v.is_number()) {
                                  all_int = false;
                                  dbl_sum += v.as_number();
                              } else {
                                  return make_failure_value(
                                      error_msg("LinkedList", "sum", "non-numeric element"));
                              }
                          }

                          return make_success_value(all_int ? Value{int_sum} : Value{dbl_sum});
                      })
        // LinkedList.tail(list) -> result<linked_list> — the list without its head.
        .func("tail", 1)
        .extract_body(
            expect_list,
            [](const auto& src, const Args&, SourceLocation) -> Value {
                if (auto fail = LinkedListHelper::check_not_empty(src, "LinkedList.tail")) {
                    return *std::move(fail);
                }

                return make_success_value(Value{LinkedListHelper::clone_and_remove_first(src)});
            })
        // LinkedList.uncons(list) -> result<(T, linked_list)> — head and tail.
        .func("uncons", 1)
        .extract_body(expect_list, [](const auto& src, const Args&, SourceLocation) -> Value {
            if (auto fail = LinkedListHelper::check_not_empty(src, "LinkedList.uncons")) {
                return *std::move(fail);
            }

            auto head_value = src->head->value;
            auto rest = LinkedListHelper::clone_and_remove_first(src);

            return make_success_value(
                make_tuple_pair(std::move(head_value), Value{std::move(rest)}));
        });
}
} // namespace luma
