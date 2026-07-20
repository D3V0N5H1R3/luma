// BinaryTree module — ordered search, set, higher-order, and rebalancing
// operations.  Split from binarytree_module.cpp for readability.  Registered
// by register_binarytree_ns() via register_binarytree_advanced().

#include <algorithm>
#include <cmath>
#include <format>
#include <queue>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/binarytree_internal.hpp"
#include "runtime/stdlib/collections/binarytree_module.hpp"
#include "runtime/stdlib/collections/value_compare.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

using namespace binarytree_detail;

void register_binarytree_advanced(const EnvPtr& env) {
    ModuleBuilder{"BinaryTree", env}
        .func("floor_value", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.floor_value", loc);

            const Value* found = tree_floor(src->root, args[1], loc);

            if (!found) {
                return failure_msg("BinaryTree", "floor_value",
                                   "no value less than or equal to key", error_codes::not_found);
            }

            return make_success_value(*found);
        })
        .func("ceiling_value", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.ceiling_value", loc);

            const Value* found = tree_ceiling(src->root, args[1], loc);

            if (!found) {
                return failure_msg("BinaryTree", "ceiling_value",
                                   "no value greater than or equal to key", error_codes::not_found);
            }

            return make_success_value(*found);
        })
        .func("union", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_tree(args[0], "BinaryTree.union", loc);
            auto b = expect_tree(args[1], "BinaryTree.union", loc);

            // Start with a copy of tree a, insert all elements from b.
            auto result = std::make_shared<BinaryTreeValue>();
            result->root = a->root;
            result->count_ = a->count_;

            std::vector<Value> b_elems;
            inorder(b->root, b_elems);

            for (const auto& elem : b_elems) {
                tree_insert(*result, elem, loc);
            }

            return Value{std::move(result)};
        })
        .func("filter", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.filter", loc);

            std::vector<Value> elems;
            inorder(src->root, elems);

            auto result = std::make_shared<BinaryTreeValue>();

            return apply_with_error_handling([&]() -> Value {
                for (const auto& elem : elems) {
                    std::vector<Value> call_args{elem};

                    const auto val = invoke_callable(args[1], call_args, loc);

                    if (val.is_truthy()) {
                        tree_insert(*result, elem, loc);
                    }
                }

                return Value{std::move(result)};
            });
        })
        .func("reduce", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.reduce", loc);

            std::vector<Value> elems;
            inorder(src->root, elems);

            auto accumulator = args[1];

            return apply_with_error_handling([&]() -> Value {
                for (const auto& elem : elems) {
                    std::vector<Value> call_args{accumulator, elem};

                    accumulator = invoke_callable(args[2], call_args, loc);
                }

                return std::move(accumulator);
            });
        })
        .func("partition", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.partition", loc);

            auto matches = std::make_shared<BinaryTreeValue>();
            auto rest = std::make_shared<BinaryTreeValue>();

            std::vector<Value> elems;
            inorder(src->root, elems);

            return apply_with_error_handling([&]() -> Value {
                for (const auto& elem : elems) {
                    std::vector<Value> call_args{elem};

                    const auto val = invoke_callable(args[1], call_args, loc);

                    if (val.is_truthy()) {
                        tree_insert(*matches, elem, loc);
                    } else {
                        tree_insert(*rest, elem, loc);
                    }
                }

                auto pair = std::make_shared<TupleValue>();
                pair->elements.emplace_back(std::move(matches));
                pair->elements.emplace_back(std::move(rest));

                return Value{std::move(pair)};
            });
        })
        .func("balance", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.balance", loc);

            if (!src->root) {
                return Value{std::make_shared<BinaryTreeValue>()};
            }

            std::vector<Value> sorted;
            inorder(src->root, sorted);

            auto result = std::make_shared<BinaryTreeValue>();
            result->root = build_balanced(sorted, 0, static_cast<std::int64_t>(sorted.size()) - 1);
            result->count_ = src->count_;

            return Value{std::move(result)};
        })
        .func("successor", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.successor", loc);

            if (!src->root) {
                return make_failure_value(ErrorMessages::empty_container("BinaryTree.successor"),
                                          std::string{error_codes::empty_container},
                                          "BinaryTree.successor");
            }

            const Value* successor = tree_successor(src->root, args[1], loc);

            if (!successor) {
                return failure_msg("BinaryTree", "successor", "no successor found",
                                   error_codes::not_found);
            }

            return make_success_value(*successor);
        })
        .func("predecessor", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.predecessor", loc);

            if (!src->root) {
                return make_failure_value(ErrorMessages::empty_container("BinaryTree.predecessor"),
                                          std::string{error_codes::empty_container},
                                          "BinaryTree.predecessor");
            }

            const Value* predecessor = tree_predecessor(src->root, args[1], loc);

            if (!predecessor) {
                return failure_msg("BinaryTree", "predecessor", "no predecessor found",
                                   error_codes::not_found);
            }

            return make_success_value(*predecessor);
        });
}

} // namespace luma
