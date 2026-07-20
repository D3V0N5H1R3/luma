// BinaryTree module — core operations: construction, mutation, basic queries,
// and traversals.  Split from binarytree_module.cpp for readability.  Registered
// by register_binarytree_ns() via register_binarytree_core().

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

void register_binarytree_core(const EnvPtr& env) {
    ModuleBuilder{"BinaryTree", env}
        .func("new", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return Value{std::make_shared<BinaryTreeValue>()};
        })
        .func("from_array", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& src = expect_array(args[0], "BinaryTree.from_array", loc);

            auto tree = std::make_shared<BinaryTreeValue>();

            for (const auto& elem : *src->elements) {
                tree_insert(*tree, elem, loc);
            }

            return Value{std::move(tree)};
        })
        .func("insert", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.insert", loc);

            auto result = std::make_shared<BinaryTreeValue>();
            result->root = src->root;
            result->count_ = src->count_;

            tree_insert(*result, args[1], loc);

            return Value{std::move(result)};
        })
        .func("remove", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.remove", loc);
            auto [new_root, removed] = remove_node(src->root, args[1], loc);

            auto result = std::make_shared<BinaryTreeValue>();
            result->root = std::move(new_root);
            result->count_ = src->count_ - (removed ? 1 : 0);

            return Value{std::move(result)};
        })
        .func("contains", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.contains", loc);

            return Value{tree_contains(src->root, args[1], loc)};
        })
        .func("length", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.length", loc);

            return Value{static_cast<std::int64_t>(src->count_)};
        })
        .func("is_empty", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.is_empty", loc);

            return Value{src->count_ == 0};
        })
        .func("height", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.height", loc);

            return Value{tree_height(src->root)};
        })
        .func("min", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.min", loc);

            if (!src->root) {
                return make_failure_value(ErrorMessages::empty_container("BinaryTree.min"),
                                          std::string{error_codes::empty_container},
                                          "BinaryTree.min");
            }

            const auto& min_node = find_min(src->root);

            return make_success_value(min_node->value);
        })
        .func("max", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.max", loc);

            if (!src->root) {
                return make_failure_value(ErrorMessages::empty_container("BinaryTree.max"),
                                          std::string{error_codes::empty_container},
                                          "BinaryTree.max");
            }

            const auto& max_node = find_max(src->root);

            return make_success_value(max_node->value);
        })
        .func("inorder", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.inorder", loc);
            auto arr = std::make_shared<ArrayValue>();

            inorder(src->root, *arr->elements);

            return Value{std::move(arr)};
        })
        .func("preorder", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.preorder", loc);
            auto arr = std::make_shared<ArrayValue>();

            preorder(src->root, *arr->elements);

            return Value{std::move(arr)};
        })
        .func("postorder", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.postorder", loc);
            auto arr = std::make_shared<ArrayValue>();

            postorder(src->root, *arr->elements);

            return Value{std::move(arr)};
        })
        .func("level_order", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.level_order", loc);
            auto arr = std::make_shared<ArrayValue>();

            level_order(src->root, *arr->elements);

            return Value{std::move(arr)};
        })
        .func("to_array", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_tree(args[0], "BinaryTree.to_array", loc);
            auto arr = std::make_shared<ArrayValue>();

            inorder(src->root, *arr->elements);

            return Value{std::move(arr)};
        });
}

} // namespace luma
